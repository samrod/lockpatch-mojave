#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IOMessage.h>
#include <IOKit/pwr_mgt/IOPMLib.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <dispatch/dispatch.h>
#include <libproc.h>

/*
 * Compile with: 
 * clang -framework CoreFoundation -framework IOKit monitor.c -o monitor
 */

int is_patched = 0; 
int lock_handled = 0; // New flag to prevent duplicate logging during a single lock session

void apply_patch() {
    if (is_patched) return;
    printf("[ACTION] Patching loginwindow (Suppressing immediate lock bug)...\n");
    // [Insert your task_for_pid / mach_vm_write code here]
    is_patched = 1;
}

void revert_patch() {
    if (!is_patched) return;
    printf("[ACTION] Reverting loginwindow (Handing off to native grace timer)...\n");
    // [Insert your task_for_pid / mach_vm_write code here]
    is_patched = 0;
}

typedef CFDictionaryRef (*MKBDeviceGetGracePeriod_t)(int bag_id);

int32_t get_current_grace_period() {
    void *handle = dlopen("/System/Library/PrivateFrameworks/MobileKeyBag.framework/MobileKeyBag", RTLD_LAZY);
    if (!handle) return 0;
    MKBDeviceGetGracePeriod_t get_grace = (MKBDeviceGetGracePeriod_t)dlsym(handle, "MKBDeviceGetGracePeriod");
    CFStringRef *kMKBConfigGracePeriod = (CFStringRef *)dlsym(handle, "kMKBConfigGracePeriod");
    int32_t seconds = 0;
    if (get_grace && kMKBConfigGracePeriod) {
        CFDictionaryRef settings = get_grace(0);
        if (settings) {
            CFNumberRef val = (CFNumberRef)CFDictionaryGetValue(settings, *kMKBConfigGracePeriod);
            if (val) CFNumberGetValue(val, kCFNumberSInt32Type, &seconds);
            CFRelease(settings);
        }
    }
    dlclose(handle);
    return seconds;
}

void handle_lock_intent(const char* source) {
    // If we already handled the start of this lock/saver session, ignore duplicate triggers
    if (lock_handled) return;
    
    int32_t grace = get_current_grace_period();
    printf("[LOG] Trigger: %s | Grace: %d\n", source, grace);

    lock_handled = 1; // Mark as handled until next unlock

    if (grace == 2147483647) return; // "Never" - keep patch active

    if (grace == 0) {
        revert_patch();
    } else {
        // Wait 1s to clear the buggy "immediate" window, then revert to allow native timer
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 1 * NSEC_PER_SEC), dispatch_get_main_queue(), ^{
            revert_patch();
        });
    }
}

// Process Poller (Will Start precursor)
void check_process_list(CFRunLoopTimerRef timer, void *info) {
    if (lock_handled) return; // Don't bother checking if we've already reacted

    int proc_count = proc_listpids(PROC_ALL_PIDS, 0, NULL, 0);
    pid_t pids[proc_count];
    proc_listpids(PROC_ALL_PIDS, 0, pids, sizeof(pids));
    
    for (int i = 0; i < proc_count; i++) {
        char name[PROC_PIDPATHINFO_MAXSIZE];
        if (proc_name(pids[i], name, sizeof(name)) > 0) {
            if (strcmp(name, "ScreenSaverEngine") == 0) {
                handle_lock_intent("ScreenSaverEngine Process Detected");
                break;
            }
        }
    }
}

void system_power_callback(void *refCon, io_service_t service, uint32_t messageType, void *messageArgument) {
    if (messageType == kIOMessageSystemWillSleep) {
        handle_lock_intent("System Will Sleep");
        IOAllowPowerChange((io_connect_t)refCon, (long)messageArgument);
    } else if (messageType == kIOMessageCanSystemSleep) {
        IOAllowPowerChange((io_connect_t)refCon, (long)messageArgument);
    }
}

void display_callback(void *refCon, io_service_t service, uint32_t messageType, void *messageArgument) {
    if (messageType == kIOMessageDeviceWillPowerOff) {
        handle_lock_intent("Display Will Sleep");
    }
}

void notification_callback(CFNotificationCenterRef center, void *observer, CFStringRef name, const void *object, CFDictionaryRef userInfo) {
    char buffer[128];
    if (name && CFStringGetCString(name, buffer, sizeof(buffer), kCFStringEncodingUTF8)) {
        if (strcmp(buffer, "com.apple.screensaver.didstart") == 0) {
            handle_lock_intent("Screen Saver Notification (didstart)");
        } 
        else if (strcmp(buffer, "com.apple.screenIsUnlocked") == 0 || 
                 strcmp(buffer, "com.apple.screensaver.didstop") == 0) {
            if (lock_handled) {
                printf("[EVENT] Unlock Detected. Resetting state.\n");
                lock_handled = 0; // Reset for the next lock event
                apply_patch();
            }
        }
    }
}

int main() {
    CFRunLoopRef runLoop = CFRunLoopGetCurrent();
    CFNotificationCenterRef distCenter = CFNotificationCenterGetDistributedCenter();

    const char* notifications[] = { "com.apple.screensaver.didstart", "com.apple.screensaver.didstop", "com.apple.screenIsUnlocked" };
    for(int i = 0; i < 3; i++) {
        CFStringRef noteName = CFStringCreateWithCString(NULL, notifications[i], kCFStringEncodingUTF8);
        CFNotificationCenterAddObserver(distCenter, NULL, notification_callback, noteName, NULL, CFNotificationSuspensionBehaviorDeliverImmediately);
        CFRelease(noteName);
    }

    IONotificationPortRef notifyPortRef = IONotificationPortCreate(kIOMasterPortDefault);
    io_object_t notifier;
    io_connect_t root_port = IORegisterForSystemPower(&root_port, &notifyPortRef, system_power_callback, &notifier);
    CFRunLoopAddSource(runLoop, IONotificationPortGetRunLoopSource(notifyPortRef), kCFRunLoopDefaultMode);

    io_service_t displayWrangler = IOServiceGetMatchingService(kIOMasterPortDefault, IOServiceMatching("IODisplayWrangler"));
    if (displayWrangler) {
        IOServiceAddInterestNotification(notifyPortRef, displayWrangler, kIOGeneralInterest, display_callback, NULL, &notifier);
        IOObjectRelease(displayWrangler);
    }

    // Poller runs every 0.25 seconds for higher precision
    CFRunLoopTimerContext ctx = {0, NULL, NULL, NULL, NULL};
    CFRunLoopTimerRef timer = CFRunLoopTimerCreate(NULL, CFAbsoluteTimeGetCurrent(), 0.25, 0, 0, check_process_list, &ctx);
    CFRunLoopAddTimer(runLoop, timer, kCFRunLoopDefaultMode);

    apply_patch();
    printf("Monitoring Mojave... (Ctrl+C to stop)\n");
    CFRunLoopRun();
    return 0;
}
