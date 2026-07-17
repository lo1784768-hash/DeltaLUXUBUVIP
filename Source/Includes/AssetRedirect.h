#pragma once
#import <Foundation/Foundation.h>
#import <fcntl.h>
#import <stdarg.h>
#import <stdio.h>
#import <string.h>
#import <unistd.h>

#import "MemoryUtils.h"

// Quản lý tiền tố đường dẫn dạng C-string thô và độ dài của chúng để tối ưu hóa so sánh tốc độ cao
static char g_bundlePrefixC[1024] = {0};
static size_t g_bundlePrefixLen = 0;

static char g_moddedPrefixC[1024] = {0};
static size_t g_moddedPrefixLen = 0;

// Con trỏ gốc của hàm access bắt buộc phải được gán trước
static int (*orig_access)(const char *, int);

inline const char* redirectAssetPath(const char *path) {
    if (!path || g_bundlePrefixLen == 0) return path;

    // TỐI ƯU 1: Kiểm tra xem đường dẫn đầu vào có bắt đầu bằng g_bundlePrefix hay không.
    // Thay vì dùng strstr chậm chạp, ta dùng strncmp để so sánh trực tiếp số ký túc định trước.
    if (strncmp(path, g_bundlePrefixC, g_bundlePrefixLen) != 0) {
        return path;
    }

    // TỐI ƯU 2: Sử dụng thread_local buffer thay vì std::string để triệt tiêu việc cấp phát/giải phóng bộ nhớ trên RAM.
    // thread_local giúp an toàn tuyệt đối khi game tải dữ liệu đa luồng (multi-threading).
    static thread_local char redirectedBuffer[2048];

    // Lấy phần đường dẫn tương đối (bỏ phần tiền tố bundle đi)
    const char *relative = path + g_bundlePrefixLen;

    // Ghép đường dẫn mới: g_moddedPrefixC + relative
    // Dùng snprintf an toàn chống tràn bộ đệm (Buffer Overflow)
    int written = snprintf(redirectedBuffer, sizeof(redirectedBuffer), "%s%s", g_moddedPrefixC, relative);
    if (written < 0 || written >= (int)sizeof(redirectedBuffer)) {
        return path; // Nếu đường dẫn quá dài vượt bộ đệm, fallback về file gốc để an toàn
    }

    // KIỂM TRA DỰ PHÒNG CHÍNH XÁC: Gọi hàm access gốc
    if (orig_access(redirectedBuffer, F_OK) == 0) {
        return redirectedBuffer; // Tìm thấy file mod trong Delta, chuyển hướng thành công
    }
    
    return path; // Không tìm thấy file mod, đọc file gốc trong App Bundle
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
    const char *redirected = redirectAssetPath(path);
    return orig_open(redirected, oflag, mode);
}

static FILE *(*orig_fopen)(const char *, const char *);

inline FILE *hooked_fopen(const char *filename, const char *mode) {
    const char *redirected = redirectAssetPath(filename);
    return orig_fopen(redirected, mode);
}

inline int hooked_access(const char *path, int mode) {
    const char *redirected = redirectAssetPath(path);
    return orig_access(redirected, mode);
}

__attribute__((constructor))
static void initDeltaVirtualFS() {
    @autoreleasepool {
        NSString *bundlePath = [[NSBundle mainBundle] bundlePath];
        if (bundlePath) {
            NSString *bundleData = [bundlePath stringByAppendingString:@"/Data/"];
            strncpy(g_bundlePrefixC, [bundleData UTF8String], sizeof(g_bundlePrefixC) - 1);
            g_bundlePrefixLen = strlen(g_bundlePrefixC);
        }

        NSArray<NSString *> *cachesPaths = NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask, YES);
        NSString *cachesDir = cachesPaths.firstObject;
        if (cachesDir) {
            // stringByAppendingString: (not stringByAppendingPathComponent:, which
            // strips the trailing "/" from its argument) so the "/" between Data and
            // the relative asset path is guaranteed present - without it, every
            // redirected path is missing a separator (".../DataTextures/foo.png"),
            // access() always fails, and the whole redirect silently never fires.
            NSString *moddedDataDir = [cachesDir stringByAppendingString:@"/Delta/Data/"];
            strncpy(g_moddedPrefixC, [moddedDataDir UTF8String], sizeof(g_moddedPrefixC) - 1);
            g_moddedPrefixLen = strlen(g_moddedPrefixC);
        }
    }

    // Thiết lập Hook đồng bộ qua thư viện MemoryUtils của bạn
    HOOKSYM("access", hooked_access, orig_access);
    HOOKSYM("open", hooked_open, orig_open);
    HOOKSYM("fopen", hooked_fopen, orig_fopen);
}