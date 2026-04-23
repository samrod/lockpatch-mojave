#include <CoreFoundation/CoreFoundation.h>
#include <dlfcn.h>
#include <stdio.h>

/*
 * Compile with: 
 * clang -framework CoreFoundation get_grace.c -o get_grace
 */

// Signature matched to your Hopper disassembly:
// Returns CFDictionaryRef in rax, takes 0 (keybag ID) in rdi.
typedef CFDictionaryRef (*MKBDeviceGetGracePeriod_t)(int bag_id);

int main() {
    void *handle = dlopen("/System/Library/PrivateFrameworks/MobileKeyBag.framework/MobileKeyBag", RTLD_LAZY);
    if (!handle) return 1;

    MKBDeviceGetGracePeriod_t get_grace = (MKBDeviceGetGracePeriod_t)dlsym(handle, "MKBDeviceGetGracePeriod");
    CFStringRef *kMKBConfigGracePeriod = (CFStringRef *)dlsym(handle, "kMKBConfigGracePeriod");

    if (!get_grace || !kMKBConfigGracePeriod) {
        dlclose(handle);
        return 1;
    }

    // Call with 0 as seen in: xor edi, edi ; call MKBDeviceGetGracePeriod
    CFDictionaryRef settings = get_grace(0);
    
    if (settings) {
        // Retrieve value using the key: mov rsi, qword [rax] ; mov rdi, rbx ; call CFDictionaryGetValue
        CFNumberRef graceValue = (CFNumberRef)CFDictionaryGetValue(settings, *kMKBConfigGracePeriod);
        
        int32_t seconds = 0;
        if (graceValue && CFNumberGetValue(graceValue, kCFNumberSInt32Type, &seconds)) {
            // "Never" is typically 2147483647 (INT_MAX)
            if (seconds == 2147483647) {
                printf("Value: Never (%d)\n", seconds);
            } else {
                printf("Value: %d seconds\n", seconds);
            }
        } else {
            printf("Value: 0 (Immediate)\n");
        }
        
        // Note: The dictionary returned by MKB is often internally managed, 
        // but your disassembly shows a call to CFRelease
        CFRelease(settings);
    } else {
        printf("Error: Could not retrieve Keybag settings.\n");
    }

    dlclose(handle);
    return 0;
}
