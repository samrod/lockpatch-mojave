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
#include <libproc.h>
#include "lockpatch.h"
#include "logger.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: lockpatch_cli [--patch | --revert]\n");
        return 1;
    }

    int mode = (strcmp(argv[1], "--revert") == 0) ? 1 : 0;
    pid_t pid = -1;
    int proc_count = proc_listpids(PROC_ALL_PIDS, 0, NULL, 0);
    pid_t pids[proc_count];
    proc_listpids(PROC_ALL_PIDS, 0, pids, sizeof(pids));

    for (int i = 0; i < proc_count; i++) {
        char name[1024];
        if (proc_name(pids[i], name, sizeof(name)) > 0 && strcmp(name, "loginwindow") == 0) {
            pid = pids[i];
            break;
        }
    }

    if (pid == -1) {
        printf("Error: loginwindow process not found.\n");
        return 1;
    }

    task_t task;
    if (task_for_pid(mach_task_self(), pid, &task) != KERN_SUCCESS) {
        printf("Error: Could not get task port for loginwindow (run with sudo).\n");
        return 1;
    }

    perform_memory_op(task, mode);
    printf("Operation successful. Check /var/log/samrod.lockpatch.log for details.\n");

    return 0;
}
