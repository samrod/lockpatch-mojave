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
 
 #include "lockpatch.h"
#include "find_offset.h"
#include "logger.h"
#include <string.h>
#include <stdio.h>
#include <mach/mach_vm.h>

uint64_t cached_target_addr = 0;
int current_patch_state = -1; // Initialize to unknown

void perform_memory_op(task_t task, int mode) {
    // mode 0 = Patch, mode 1 = Revert
    int requested_state = (mode == 0) ? 1 : 0;

    if (current_patch_state == requested_state) {
        return; 
    }

    task_t target_task = (task == 0) ? mach_task_self() : task;

    if (cached_target_addr == 0) {
        uint64_t base_addr = 0;
        cached_target_addr = find_live_offset(target_task, &base_addr);
        if (cached_target_addr == 0) return;
    }   

    uint8_t patch[] = {0xC3, 0x90}, revert[] = {0x55, 0x48};
    uint8_t *data = (mode == 0) ? patch : revert;

    if (vm_protect(target_task, cached_target_addr, 2, FALSE, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY) == KERN_SUCCESS) {
        memcpy((void*)cached_target_addr, data, 2);
        vm_protect(target_task, cached_target_addr, 2, FALSE, VM_PROT_READ | VM_PROT_EXECUTE);
        
        // Update the state and log ONLY when a change actually happens
        current_patch_state = requested_state;
        log_status(mode == 0 ? "PATCH APPLIED" : "PATCH REVERTED");
    }
}
