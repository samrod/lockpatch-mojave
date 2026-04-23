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
 
 #include "grace_period.h"
#include <dlfcn.h>
#include <CoreFoundation/CoreFoundation.h>

typedef CFDictionaryRef (*MKBDeviceGetGracePeriod_t)(int bag_id);

int32_t get_current_grace_period() {
    int32_t seconds = 0;
    void *handle = dlopen("/System/Library/PrivateFrameworks/MobileKeyBag.framework/MobileKeyBag", RTLD_LAZY);
    if (!handle) return 0;

    MKBDeviceGetGracePeriod_t get_grace = (MKBDeviceGetGracePeriod_t)dlsym(handle, "MKBDeviceGetGracePeriod");
    CFStringRef *kMKBConfigGracePeriod = (CFStringRef *)dlsym(handle, "kMKBConfigGracePeriod");

    if (get_grace && kMKBConfigGracePeriod) {
        CFDictionaryRef settings = get_grace(0);
        if (settings) {
            CFNumberRef val = (CFNumberRef)CFDictionaryGetValue(settings, *kMKBConfigGracePeriod);
            if (val) CFNumberGetValue(val, kCFNumberSInt32Type, &seconds);
            CFRelease(settings);
        }
    }
    dlclose(handle);
    return seconds;
}
