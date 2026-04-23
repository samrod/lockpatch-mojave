# LockPatch-Mojave

**LockPatch** is a surgical system patch to prevent immediate screen locking and restore macOS Mojave's normal behavior on T2 Intel Macs.

## 1. The Problem
On certain hardware configurations, macOS Mojave ignores the "Require password [x] after sleep or screen saver begins" setting and locks the session **immediately** upon sleep or screen saver activation. This has an additionall bug whereby the session locks invisibly, disabling mouse clicks and keyboard entry while still on the desktop. Hitting Esc to show the password unlock screen, or just blindly typing the password releases the hidden lock state.

## 2. Forensic Analysis
This project is based on the following observations and technical hypotheses:

* **Limited Scope:** This bug has only been observed on **macOS Mojave (10.14)** running on a **2018 Intel Mac mini**. It has not been reproduced on non-T2 Intel Macs running the same OS version.
* **Firmware Regression:** The bug is likely caused by a post-Mojave firmware update. Because the 2018 Mac mini originally shipped with Mojave, a bug this significant would have been caught by Apple's QA.
* **Support Lifecycle Gap:** Since Apple typically supports the current macOS plus two previous versions, any errant firmware update introduced during the **Monterey** cycle (or later) would not have been regression-tested against Mojave.
* **T2 Conflict:** Evidence suggests this issue specifically affects **T2-equipped Macs** that have been downgraded to Mojave, where modern firmware expectations conflict with legacy power-management logic.

## 3. Technical Implementation
LockPatch Mojjave operates via **Dynamic Library Injection** into the `loginwindow` process.

### Module Breakdown
| File | Responsibility |
| :--- | :--- |
| **`cli_main.c`** | A command-line interface for manual patching/reverting via terminal. |
| **`find_offset.c`** | Performs a live memory scan starting at `0x100000000` to find the lock function signature. |
| **`grace_period.c`** | Interacts with `MobileKeyBag.framework` to read the user's actual grace period settings. |
| **`locker.c`** | Provides fallbacks to trigger a system lock via `login.framework` or Obj-C `lockSession`. |
| **`lockpatch.c`** | The core memory manipulator; applies the `0xC3` (RET) patch to the target function. |
| **`logger.c`** | A state-aware logger of clean status history updates. |
| **`main_dylib.c`** | The library entry point managing all business logic and the timer thread logic. |
| **`monitor.c`** | A secondary thread using IOKit to catch power events: display sleep & screen saver events. |

## 4. ⚠️ WARNING
**USE AT YOUR OWN RISK.**
* **SIP:** This utility requires partially disabling **System Integrity Protection (SIP)** to allow code injection into a core system process (`loginwindow`).
* **System Integrity:** While the only file this patch modifies on disk is its own log (`/var/log/samrod.lockpatch.log`), you are modifying the in-memory behavior of the primary security gatekeeper of macOS.

## 5. Build & Installation
Run the provided build script to compile the launcher, the CLI tool, and the dylib:

### Partially Disable SIP
1. Boot into recovery mode (cmd-R)
2. Launch Terminal and enter
```bash
csrutil disable --without debug --without fs
```
3. Reboot back into Mojave. To confirm that SIP is the right state, run
```bash
> csrutil status
```
Expect to see something like this:
```bash
System Integrity Protection status: enabled (Custom Configuration).

Configuration:
	Apple Internal: disabled
	Kext Signing: enabled
	Filesystem Protections: disabled
	Debugging Restrictions: disabled
	DTrace Restrictions: enabled
	NVRAM Protections: enabled
	BaseSystem Verification: enabled

This is an unsupported configuration, likely to break in the future and leave your machine in an unknown state.
```

```bash
chmod +x make.sh
./make.sh
```

The script compiles binaries into:

`/Library/Application Support/LockPatch/lockpatch.dylib`
`/Library/LaunchDaemons/com.samrod.lockpatch.plist`
`/usr/local/bin/lockpatch`
`/usr/local/bin/lockpatch_launcher`

And attempt an initial injection via `lldb`.
