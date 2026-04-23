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
 
 #include "monitor.h"
#include "logger.h"
#include <IOKit/IOKitLib.h>
#include <IOKit/pwr_mgt/IOPMLib.h>
#include <IOKit/pwr_mgt/IOPM.h>
#include <libproc.h>
#include <pthread.h>
#include <string.h>
#include <stdio.h>

extern void handle_did_start();
extern void handle_did_stop();

static int lock_handled = 0;

void trigger_start(const char* source) {
    if (lock_handled) return;
    char msg[128];
    snprintf(msg, sizeof(msg), "EVENT: %s", source);
    //log_status(msg);
    lock_handled = 1;
    handle_did_start();
}

void trigger_stop(const char* source) {
    if (!lock_handled) return;
    //og_status("EVENT: Unlock/Resume Detected.");
    lock_handled = 0;
    handle_did_stop();
}

void check_process_list(CFRunLoopTimerRef timer, void *info) {
    int proc_count = proc_listpids(PROC_ALL_PIDS, 0, NULL, 0);
    pid_t pids[proc_count];
    proc_listpids(PROC_ALL_PIDS, 0, pids, sizeof(pids));
    
    int found = 0;
    for (int i = 0; i < proc_count; i++) {
        char name[PROC_PIDPATHINFO_MAXSIZE];
        if (proc_name(pids[i], name, sizeof(name)) > 0) {
            if (strstr(name, "ScreenSaverEngine")) {
                found = 1;
                break;
            }
        }
    }

    if (found) {
        trigger_start("ScreenSaverEngine Process Detected");
    } else if (!found && lock_handled) {
        log_status("Monitor: ScreenSaverEngine process disappeared. Resetting state.");
        trigger_stop("Process Poller Reset");
    }
}

void system_power_callback(void *refCon, io_service_t service, uint32_t messageType, void *messageArgument) {
    if (messageType == kIOMessageSystemWillSleep) {
        trigger_start("System Power: Will Sleep");
    }
    IOAllowPowerChange((io_connect_t)refCon, (long)messageArgument);
}

void* monitor_thread_entry(void* arg) {
    CFRunLoopRef runLoop = CFRunLoopGetCurrent();
    
    IONotificationPortRef notifyPortRef = IONotificationPortCreate(kIOMasterPortDefault);
    io_object_t notifier;
    io_connect_t root_port = IORegisterForSystemPower(NULL, &notifyPortRef, system_power_callback, &notifier);
    
    if (root_port) {
        CFRunLoopAddSource(runLoop, IONotificationPortGetRunLoopSource(notifyPortRef), kCFRunLoopDefaultMode);
    }

    CFRunLoopTimerContext ctx = {0, NULL, NULL, NULL, NULL};
    CFRunLoopTimerRef timer = CFRunLoopTimerCreate(NULL, CFAbsoluteTimeGetCurrent(), 0.5, 0, 0, check_process_list, &ctx);
    CFRunLoopAddTimer(runLoop, timer, kCFRunLoopDefaultMode);

    log_status("Monitor Thread: Poller + Power monitoring active.");
    CFRunLoopRun(); 
    return NULL;
}

void start_monitoring_module() {
    pthread_t tid;
    pthread_create(&tid, NULL, monitor_thread_entry, NULL);
    pthread_detach(tid);
}
