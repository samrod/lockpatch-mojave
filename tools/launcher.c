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
 
 #include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <libproc.h>
#include "../src/logger.h"

// Finds the PID of loginwindow using the process list
pid_t get_loginwindow_pid() {
    int proc_count = proc_listpids(PROC_ALL_PIDS, 0, NULL, 0);
    pid_t pids[proc_count];
    proc_listpids(PROC_ALL_PIDS, 0, pids, sizeof(pids));
    
    for (int i = 0; i < proc_count; i++) {
        char name[PROC_PIDPATHINFO_MAXSIZE];
        if (proc_name(pids[i], name, sizeof(name)) > 0) {
            if (strcmp(name, "loginwindow") == 0) {
                return pids[i];
            }
        }
    }
    return -1;
}

void trigger_lldb_injection(const char* pid_str) {
    pid_t child = fork();
    if (child == 0) {
        setsid();
        // Redirect to a log we can actually read
        freopen("/tmp/lockpatch_lldb.log", "w", stdout);
        freopen("/tmp/lockpatch_lldb.log", "a", stderr);
        
        sleep(15); 
        log_status("Grandchild: Attempting lldb attach...");
        
        // THE FIX: Escaped quotes for the path
        execl("/usr/bin/lldb", "lldb", "--attach-pid", pid_str, "--batch", 
              "--one-line", "p (void*)dlopen((char*)\"/Library/Application Support/LockPatch/lockpatch.dylib\", 1)", 
              "--one-line", "detach", NULL);
        exit(0);
    }
}

int main() {
    log_status("--- LOCKPATCH LAUNCHER STARTUP ---");

    // We wait a few seconds to ensure the process list is populated
    sleep(5); 

    pid_t pid = get_loginwindow_pid();
    if (pid == -1) {
        log_status("ERROR: Could not find loginwindow PID.");
        return 1;
    }

    char pid_str[10];
    snprintf(pid_str, sizeof(pid_str), "%d", pid);
    
    char msg[64];
    snprintf(msg, sizeof(msg), "Targeting loginwindow PID: %s", pid_str);
    log_status(msg);

    // Trigger the background injection
    trigger_lldb_injection(pid_str);

    log_status("Launcher detached. Boot sequence handed back to OS.");
    
    return 0;
}
