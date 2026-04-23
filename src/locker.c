/*
 * LockPatch-Mojave
 * Copyright (C) 2026  Samrod Shenassa
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free-Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
 
 #include "locker.h"
#include "logger.h"
#include <dlfcn.h>
#include <objc/runtime.h>
#include <objc/message.h>

typedef void (*SACLockScreenImmediate_t)(void);

void attempt_system_lock() {
    log_status("Attempting system lock transition...");

    // Method 1: Private Framework call (SAC = Screen Administration Command)
    void *login_framework = dlopen("/System/Library/PrivateFrameworks/login.framework/Versions/A/login", RTLD_LAZY);
    if (login_framework) {
        SACLockScreenImmediate_t lock = (SACLockScreenImmediate_t)dlsym(login_framework, "SACLockScreenImmediate");
        if (lock) {
            lock();
            log_status("Lock triggered via SACLockScreenImmediate");
            dlclose(login_framework);
            return;
        }
        dlclose(login_framework);
    }

    // Method 2: Fallback to calling loginwindow's internal lock mechanism via Obj-C
    // This is less 'silent' but very reliable.
    id shared_controller = ((id (*)(id, SEL))objc_msgSend)((id)objc_getClass("LoginAppController"), sel_registerName("sharedController"));
    if (shared_controller) {
        ((void (*)(id, SEL))objc_msgSend)(shared_controller, sel_registerName("lockSession"));
        log_status("Lock triggered via sharedController lockSession");
    }
}
