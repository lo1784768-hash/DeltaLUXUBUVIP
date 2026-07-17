#pragma once
#import <Foundation/Foundation.h>
#import <fcntl.h>
#import <stdarg.h>
#import <stdio.h>
#import <string.h>
#import <unistd.h>

#include <string>

#import "MemoryUtils.h"

// Redirects open()/fopen()/access() calls for files under the game bundle's Data/
// directory to the mirrored replacement directory at Library/Caches/Delta/Data/,
// falling back to the original bundle asset whenever no replacement file exists at the
// mirrored path.
//
// g_bundlePrefix/g_moddedPrefix are computed exactly once, in the constructor, before
// any other code in the process runs (dyld serializes image-load constructors ahead of
// any thread spawning) and are never written again afterward - so concurrent reads from
// the many threads that load game assets need no lock. Adding a mutex here would only
// cost latency on one of the hottest syscall paths in the process for no actual benefit,
// since there is nothing left to race on.
static std::string g_bundlePrefix;  // e.g. ".../FreeFire.app/Data/"
static std::string g_moddedPrefix;  // e.g. ".../Library/Caches/Delta/Data/"

// redirectAssetPath() needs to check whether a modded file exists, but access() itself
// is one of the functions being hooked below - it must call the ORIGINAL access (this
// pointer) for that check, never the bare libc access()/hooked_access(), or every
// lookup would recurse into itself.
static int (*orig_access)(const char *, int);

inline std::string redirectAssetPath(const char *path) {
    if (!path) return std::string();
    if (g_bundlePrefix.empty()) return path; // constructor hasn't run yet (or found no bundle) - passthrough

    const char *found = strstr(path, g_bundlePrefix.c_str());
    if (!found) return path;

    std::string relative(found + g_bundlePrefix.size());
    std::string moddedPath = g_moddedPrefix + relative;

    if (orig_access(moddedPath.c_str(), F_OK) == 0) {
        return moddedPath;
    }
    return path;
}

static int (*orig_open)(const char *, int, ...);

inline int hooked_open(const char *path, int oflag, ...) {
    mode_t mode = 0;
    if (oflag & O_CREAT) {
        va_list args;
        va_start(args, oflag);
        mode = (mode_t)va_arg(args, int);
        va_end(args);
    }
    std::string redirected = redirectAssetPath(path);
    return orig_open(redirected.c_str(), oflag, mode);
}

static FILE *(*orig_fopen)(const char *, const char *);

inline FILE *hooked_fopen(const char *filename, const char *mode) {
    std::string redirected = redirectAssetPath(filename);
    return orig_fopen(redirected.c_str(), mode);
}

inline int hooked_access(const char *path, int mode) {
    std::string redirected = redirectAssetPath(path);
    return orig_access(redirected.c_str(), mode);
}

__attribute__((constructor))
static void initDeltaVirtualFS() {
    @autoreleasepool {
        NSString *bundlePath = [[NSBundle mainBundle] bundlePath];
        if (bundlePath) {
            g_bundlePrefix = std::string([bundlePath UTF8String]) + "/Data/";
        }

        NSArray<NSString *> *cachesPaths = NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask, YES);
        NSString *cachesDir = cachesPaths.firstObject;
        NSString *moddedDataDir = [cachesDir stringByAppendingPathComponent:@"Delta/Data"];
        g_moddedPrefix = std::string([moddedDataDir UTF8String]) + "/";
    }

    // access() must be hooked first: redirectAssetPath() (used by all three hooks)
    // calls orig_access(), so it needs to already be populated before open/fopen can
    // safely be exercised. HOOKSYM sets the orig_* pointer synchronously, so this order
    // just avoids a brief window where orig_access would still be null if open/fopen
    // somehow fired before this line - harmless either way, but cheap to be explicit.
    HOOKSYM("access", hooked_access, orig_access);
    HOOKSYM("open", hooked_open, orig_open);
    HOOKSYM("fopen", hooked_fopen, orig_fopen);
}
