#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dlfcn.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <mach-o/dyld.h>
#include <CoreFoundation/CoreFoundation.h>
#include <notify.h>
#include <pthread.h>
#include <objc/runtime.h>
#include <objc/message.h>
#include <dispatch/dispatch.h>

#define MKB_NEVER_LOCK 2147483647

static double last_session_start = 0;
static int is_never_mode = 0; 
static uint64_t cached_target_addr = 0;

// The 32-byte signature from lockpatch-GM.c
unsigned char signature[] = { 
    0x55, 0x48, 0x89, 0xE5, 0x41, 0x57, 0x41, 0x56, 
    0x41, 0x55, 0x41, 0x54, 0x53, 0x48, 0x81, 0xEC, 
    0xB8, 0x00, 0x00, 0x00, 0x41, 0x89, 0xD6, 0x49,
    0x89, 0xFD, 0x48, 0x8B, 0x05, 0xB0, 0x66, 0x0C 
};

typedef CFDictionaryRef (*MKBDeviceGetGracePeriod_t)(int bag_id);

void log_status(const char* message) {
    FILE *f = fopen("/var/log/samrod.lockpatch.log", "a");
    if (f) {
        time_t now; time(&now);
        char *date_str = ctime(&now);
        if (date_str) {
            size_t len = strlen(date_str);
            if (len > 0) date_str[len - 1] = '\0';
            fprintf(f, "[LockPatch][%s] %s\n", date_str, message);
        }
        fclose(f);
    }
}

uint64_t find_live_offset() {
    task_t task = mach_task_self();
    mach_vm_address_t address = 0;
    mach_vm_size_t size = 0;
    vm_region_basic_info_data_64_t info;
    mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
    mach_port_t object_name;

    while (mach_vm_region(task, &address, &size, VM_REGION_BASIC_INFO_64, (vm_region_info_t)&info, &count, &object_name) == KERN_SUCCESS) {
        if ((info.protection & VM_PROT_READ) && (info.protection & VM_PROT_EXECUTE)) {
            unsigned char *buf = malloc(size);
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
        address += size;
    }
    return 0;
}

void perform_memory_op(int mode) {
    if (is_never_mode && mode == 1) {
        log_status("Shield removal BLOCKED: Never-Mode is active.");
        return;
    }

    if (cached_target_addr == 0) {
        cached_target_addr = find_live_offset();
        if (cached_target_addr == 0) {
            log_status("CRITICAL: Could not find signature in memory.");
            return;
        }
    }

    task_t port = mach_task_self();
    uint8_t patch[] = {0xC3, 0x90}, revert[] = {0x55, 0x48};
    uint8_t *data = (mode == 0) ? patch : revert;

    kern_return_t kr = vm_protect(port, cached_target_addr, 2, FALSE, VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY);
    if (kr != KERN_SUCCESS) return;
    
    memcpy((void*)cached_target_addr, data, 2);
    vm_protect(port, cached_target_addr, 2, FALSE, VM_PROT_READ | VM_PROT_EXECUTE);
    log_status(mode == 0 ? "PATCH APPLIED (Dynamic)" : "PATCH REMOVED (Dynamic)");
}

void* lock_timer_thread(void* vargp) {
    double session_time = *(double*)vargp;
    free(vargp);

    int32_t seconds = 0;
    void *handle = dlopen("/System/Library/PrivateFrameworks/MobileKeyBag.framework/MobileKeyBag", RTLD_LAZY);
    if (handle) {
        MKBDeviceGetGracePeriod_t get_grace = (MKBDeviceGetGracePeriod_t)dlsym(handle, "MKBDeviceGetGracePeriod");
        CFStringRef *kMKBConfigGracePeriod = (CFStringRef *)dlsym(handle, "kMKBConfigGracePeriod");
        if (get_grace && kMKBConfigGracePeriod) {
            CFDictionaryRef settings = get_grace(0);
            if (settings) {
                CFNumberRef graceValue = (CFNumberRef)CFDictionaryGetValue(settings, *kMKBConfigGracePeriod);
                if (graceValue) CFNumberGetValue(graceValue, kCFNumberSInt32Type, &seconds);
                CFRelease(settings);
            }
        }
        dlclose(handle);
    }

    if (seconds >= MKB_NEVER_LOCK || seconds < 0) {
        is_never_mode = 1;
        log_status("Mode: NEVER. Patch is permanent.");
        return NULL; 
    }

    if (seconds > 0) sleep(seconds);

    if (last_session_start == session_time) {
        perform_memory_op(1); 
        // Future: Silent lock logic here
    }
    return NULL;
}

void handle_lock_event() {
    last_session_start = CFAbsoluteTimeGetCurrent();
    double* thread_session = malloc(sizeof(double));
    if (thread_session) {
        *thread_session = last_session_start;
        pthread_t tid;
        pthread_create(&tid, NULL, lock_timer_thread, thread_session);
        pthread_detach(tid);
    }
}

void handle_stop_event() {
    last_session_start = 0;
    perform_memory_op(1); 
}

__attribute__((constructor))
void initialize() {
    perform_memory_op(0); 
    log_status("LockPatch Active. Target found via signature scan.");
    
    CFNotificationCenterRef center = CFNotificationCenterGetDistributedCenter();
    CFNotificationCenterAddObserver(center, NULL, (void*)handle_lock_event, CFSTR("com.apple.screensaver.didstart"), NULL, CFNotificationSuspensionBehaviorDeliverImmediately);
    CFNotificationCenterAddObserver(center, NULL, (void*)handle_stop_event, CFSTR("com.apple.screensaver.didstop"), NULL, CFNotificationSuspensionBehaviorDeliverImmediately);
}
