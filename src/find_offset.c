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
 
 #include "find_offset.h"
#include <stdlib.h>
#include <string.h>
#include <mach/mach_vm.h>

unsigned char signature[] = { 
    0x55, 0x48, 0x89, 0xE5, 0x41, 0x57, 0x41, 0x56, 
    0x41, 0x55, 0x41, 0x54, 0x53, 0x48, 0x81, 0xEC, 
    0xB8, 0x00, 0x00, 0x00, 0x41, 0x89, 0xD6, 0x49,
    0x89, 0xFD, 0x48, 0x8B, 0x05, 0xB0, 0x66, 0x0C 
};

uint64_t find_live_offset(task_t task, uint64_t *base_addr) {
    mach_vm_address_t address = 0x100000000; // Skip to main executable space
    mach_vm_size_t size = 0;
    vm_region_basic_info_data_64_t info;
    mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
    mach_port_t object_name;

    while (mach_vm_region(task, &address, &size, VM_REGION_BASIC_INFO_64, (vm_region_info_t)&info, &count, &object_name) == KERN_SUCCESS) {
        if ((info.protection & VM_PROT_READ) && (info.protection & VM_PROT_EXECUTE)) {
            unsigned char *buf = malloc(size);
            if (!buf) goto next;

            mach_vm_size_t bytes_read;
            if (mach_vm_read_overwrite(task, address, size, (mach_vm_address_t)buf, &bytes_read) == KERN_SUCCESS) {
                for (mach_vm_size_t i = 0; i < bytes_read - sizeof(signature); i++) {
                    if (memcmp(&buf[i + 1], &signature[1], sizeof(signature) - 1) == 0) {
                        if (buf[i] == 0x55 || buf[i] == 0xC3) {
                            uint64_t found = address + i;
                            free(buf);
                            return found;
                        }
                    }
                }
            }
            free(buf);
        }
    next:
        address += size;
        if (address > 0x7fffffffffff) break;
    }
    return 0;
}
