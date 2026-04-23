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
 
 #include <CoreFoundation/CoreFoundation.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include "logger.h"
#include "lockpatch.h"
#include "grace_period.h"
#include "locker.h"
#include "monitor.h"

static double last_session_start = 0;
static int patch_is_active = 0; // State tracking

void update_shield_state(int mode) {
    if (patch_is_active == (mode == 0)) return;

    perform_memory_op(0, mode);
    patch_is_active = (mode == 0);

    if (mode == 0) {
        int32_t grace = get_current_grace_period();
        char msg[128];
        if (grace >= MKB_NEVER_LOCK || grace < 0) {
            snprintf(msg, sizeof(msg), "Shield Active (Grace: NEVER)");
        } else {
            snprintf(msg, sizeof(msg), "Shield Active (Grace: %d seconds)", grace);
        }
        log_status(msg);
    } else {
        log_status("Shield Lifted (System Locking)");
    }
}

void* timer_thread(void* vargp) {
    double session_time = *(double*)vargp;
    free(vargp);
    int32_t grace = get_current_grace_period();

    if (grace >= MKB_NEVER_LOCK || grace < 0) return NULL;

    if (grace > 0) sleep(grace);

    if (last_session_start == session_time) {
        update_shield_state(1); // Lift shield (Revert patch)
        usleep(10000);
        attempt_system_lock();
    }
    return NULL;
}

void handle_did_start() {
    double now = CFAbsoluteTimeGetCurrent();
    if (now - last_session_start < 1.0) return;
    last_session_start = now;

    update_shield_state(0); // Ensure shield is active

    double* thread_session = malloc(sizeof(double));
    if (thread_session) {
        *thread_session = last_session_start;
        pthread_t tid;
        pthread_create(&tid, NULL, timer_thread, thread_session);
        pthread_detach(tid);
    }
}

void handle_did_stop() {
    last_session_start = 0;
    
    int32_t grace = get_current_grace_period();
    char msg[128];
    if (grace >= MKB_NEVER_LOCK || grace < 0) {
        snprintf(msg, sizeof(msg), "Shield Active (Grace: NEVER)");
    } else {
        snprintf(msg, sizeof(msg), "Shield Active (Grace: %d seconds)", grace);
    }
    log_status(msg);
    
    perform_memory_op(0, 0); 
}

void notification_callback(CFNotificationCenterRef center, void *observer, CFStringRef name, const void *object, CFDictionaryRef userInfo) {
    char buffer[128];
    if (name && CFStringGetCString(name, buffer, sizeof(buffer), kCFStringEncodingUTF8)) {
        if (strcmp(buffer, "com.apple.screensaver.didstart") == 0) {
            handle_did_start();
        } else {
            handle_did_stop();
        }
    }
}

__attribute__((constructor))
static void initialize(void) {
    // Initial silent patch on load
    perform_memory_op(0, 0);
    patch_is_active = 1;
    log_status("LockPatch Service Started.");

    start_monitoring_module();
    
    CFNotificationCenterRef center = CFNotificationCenterGetDistributedCenter();
    CFNotificationCenterAddObserver(center, NULL, notification_callback, CFSTR("com.apple.screensaver.didstart"), NULL, CFNotificationSuspensionBehaviorDeliverImmediately);
    CFNotificationCenterAddObserver(center, NULL, notification_callback, CFSTR("com.apple.screensaver.didstop"), NULL, CFNotificationSuspensionBehaviorDeliverImmediately);
    CFNotificationCenterAddObserver(center, NULL, notification_callback, CFSTR("com.apple.screenIsUnlocked"), NULL, CFNotificationSuspensionBehaviorDeliverImmediately);
}
