#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <libproc.h>
#include <sys/time.h>
#include <time.h>

// Extended 32-byte signature to distinguish from the "Twin" at 0x2b24b
unsigned char signature[] = { 
    0x55, 0x48, 0x89, 0xE5, 0x41, 0x57, 0x41, 0x56, 
    0x41, 0x55, 0x41, 0x54, 0x53, 0x48, 0x81, 0xEC, 
    0xB8, 0x00, 0x00, 0x00, 0x41, 0x89, 0xD6, 0x49,
    0x89, 0xFD, 0x48, 0x8B, 0x05, 0xB0, 0x66, 0x0C 
};

void log_status(const char *msg, uint64_t offset) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm* tm_info = localtime(&tv.tv_sec);
    char time_buf[64];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);
    
    if (offset > 0) {
        printf("[%s.%03d] %s (Offset: 0x%llx)\n", time_buf, (int)(tv.tv_usec / 1000), msg, offset);
    } else {
        printf("[%s.%03d] %s\n", time_buf, (int)(tv.tv_usec / 1000), msg);
    }
}

uint64_t find_offset(task_t task, uint64_t *base_addr) {
    mach_vm_address_t address = 0;
    mach_vm_size_t size = 0;
    vm_region_basic_info_data_64_t info;
    mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
    mach_port_t object_name;

    *base_addr = 0;
    
    // Iterate through all memory regions of the process
    while (mach_vm_region(task, &address, &size, VM_REGION_BASIC_INFO_64, (vm_region_info_t)&info, &count, &object_name) == KERN_SUCCESS) {
        
        // The first region we encounter is the base of the executable image (due to ASLR)
        if (*base_addr == 0) *base_addr = (uint64_t)address;

        // We only care about Readable + Executable regions (the __TEXT segment)
        if ((info.protection & VM_PROT_READ) && (info.protection & VM_PROT_EXECUTE)) {
            unsigned char *buf = malloc(size);
            mach_vm_size_t bytes_read;
            
            if (mach_vm_read_overwrite(task, address, size, (mach_vm_address_t)buf, &bytes_read) == KERN_SUCCESS) {
                // Search the buffer for our 32-byte signature
                for (mach_vm_size_t i = 0; i < bytes_read - sizeof(signature); i++) {
                    
                    /* WILDCARD LOGIC:
                       We compare starting from the second byte (index 1).
                       This allows us to find the function regardless of whether 
                       the first byte is currently 0x55 (Original) or 0xC3 (Patched).
                    */
                    if (memcmp(&buf[i + 1], &signature[1], sizeof(signature) - 1) == 0) {
                        
                        // Final Safety Check: Verify the head byte is one of our two known states
                        if (buf[i] == 0x55 || buf[i] == 0xC3) {
                            uint64_t found_addr = address + i;
                            free(buf);
                            return found_addr; // Target confirmed
                        }
                    }
                }
            }
            free(buf);
        }
        // Move to the next memory region
        address += size;
    }
    
    return 0; // Signature not found in any executable region
}

int main(int argc, char **argv) {
    int revert = (argc > 1 && strcmp(argv[1], "--revert") == 0);
    
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
        log_status("ERROR: loginwindow not found.", 0);
        return 1;
    }

    task_t task;
    if (task_for_pid(mach_task_self(), pid, &task) != KERN_SUCCESS) {
        log_status("ERROR: Task port failed.", 0);
        return 1;
    }

    uint64_t base_addr;
    uint64_t target_addr = find_offset(task, &base_addr);

    if (target_addr == 0) {
        log_status("ERROR: Signature not found.", 0);
        return 1;
    }

    uint64_t offset = target_addr - base_addr;
    uint8_t byte = revert ? 0x55 : 0xC3;
    
    mach_vm_protect(task, target_addr, 1, FALSE, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY);
    if (mach_vm_write(task, target_addr, (pointer_t)&byte, 1) == KERN_SUCCESS) {
        char log_msg[64];
        snprintf(log_msg, sizeof(log_msg), "SUCCESS: %s applied", revert ? "Revert" : "Patch");
        log_status(log_msg, offset);
    }
    mach_vm_protect(task, target_addr, 1, FALSE, VM_PROT_READ | VM_PROT_EXECUTE);

    return 0;
}
