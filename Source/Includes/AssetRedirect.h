#pragma once
#import <Foundation/Foundation.h>
#import <fcntl.h>
#import <stdarg.h>
#import <stdio.h>
#import <string.h>
#import <unistd.h>

#import "MemoryUtils.h"

// Quản lý tiền tố đường dẫn gốc của App Bundle và thư mục Delta trong Cache
static char g_bundlePrefixC[1024] = {0};
static size_t g_bundlePrefixLen = 0;

static char g_moddedPrefixC[1024] = {0};
static size_t g_moddedPrefixLen = 0;

// Con trỏ gốc của hàm access bắt buộc phải được gán trước
static int (*orig_access)(const char *, int);

inline const char* redirectAllTrafficPath(const char *path) {
    if (!path || g_bundlePrefixLen == 0) return path;

    // BƯỚC 1: Kiểm tra xem file yêu cầu có nằm trong App Bundle hay không
    if (strncmp(path, g_bundlePrefixC, g_bundlePrefixLen) != 0) {
        return path;
    }

    // BƯỚC 2: WHITELIST CHÍ MẠNG - Tuyệt đối không bẻ hướng các file chữ ký và cấu hình hệ thống của Apple
    // Nếu can thiệp vào các file này, iOS Sandbox sẽ giết tiến trình (Crash/Văng app ngay khi mở)
    if (strstr(path, "_CodeSignature") != NULL || 
        strstr(path, "embedded.mobileprovision") != NULL || 
        strstr(path, "Info.plist") != NULL) {
        return path;
    }

    // BƯỚC 3: Tạo bộ đệm thread_local an toàn đa luồng, triệt tiêu việc cấp phát RAM (Zero Allocation)
    static thread_local char redirectedBuffer[2048];

    // Lấy phần đường dẫn tương đối (bỏ phần tiền tố App Bundle đi)
    const char *relative = path + g_bundlePrefixLen;

    // Ghép đường dẫn mới hướng vào thư mục Delta trong Caches
    int written = snprintf(redirectedBuffer, sizeof(redirectedBuffer), "%s%s", g_moddedPrefixC, relative);
    if (written < 0 || written >= (int)sizeof(redirectedBuffer)) {
        return path; // Nếu đường dẫn quá dài vượt bộ đệm, fallback về file gốc để an toàn
    }

    // BƯỚC 4: KIỂM TRA TÍNH TOÀN VẸN (FALLBACK)
    // Gọi hàm access gốc xem file này có tồn tại trong gói Delta 450MB hay không
    if (orig_access(redirectedBuffer, F_OK) == 0) {
        return redirectedBuffer; // Tìm thấy file mod/file cấu hình cấu trúc mới -> Chuyển hướng thành công
    }
    
    return path; // Nếu trong Delta.zip không có file này, trả về đường dẫn gốc ngoài App Bundle để game chạy bình thường
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
    const char *redirected = redirectAllTrafficPath(path);
    return orig_open(redirected, oflag, mode);
}

static FILE *(*orig_fopen)(const char *, const char *);

inline FILE *hooked_fopen(const char *filename, const char *mode) {
    const char *redirected = redirectAllTrafficPath(filename);
    return orig_fopen(redirected, mode);
}

inline int hooked_access(const char *path, int mode) {
    const char *redirected = redirectAllTrafficPath(path);
    return orig_access(redirected, mode);
}

__attribute__((constructor))
static void initDeltaAllTrafficVFS() {
    @autoreleasepool {
        // Lấy đường dẫn gốc của toàn bộ App Bundle (Ví dụ: /var/containers/Bundle/Application/.../FreeFire.app/)
        NSString *bundlePath = [[NSBundle mainBundle] bundlePath];
        if (bundlePath) {
            // Thêm dấu "/" ở cuối để nhận diện chính xác lớp thư mục gốc của App
            NSString *bundleRoot = [bundlePath stringByAppendingString:@"/"];
            strncpy(g_bundlePrefixC, [bundleRoot UTF8String], sizeof(g_bundlePrefixC) - 1);
            g_bundlePrefixLen = strlen(g_bundlePrefixC);
        }

        // Lấy đường dẫn thư mục Caches của ứng dụng
        NSArray<NSString *> *cachesPaths = NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask, YES);
        NSString *cachesDir = cachesPaths.firstObject;
        if (cachesDir) {
            // Định tuyến toàn bộ traffic bundle vào thẳng thư mục Delta/ trong Caches
            NSString *moddedDataDir = [cachesDir stringByAppendingString:@"/Delta/"];
            strncpy(g_moddedPrefixC, [moddedDataDir UTF8String], sizeof(g_moddedPrefixC) - 1);
            g_moddedPrefixLen = strlen(g_moddedPrefixC);
        }
    }

    // Khởi tạo các mắt xích Hook hệ thống thông qua Macro HOOKSYM của bạn
    HOOKSYM("access", hooked_access, orig_access);
    HOOKSYM("open", hooked_open, orig_open);
    HOOKSYM("fopen", hooked_fopen, orig_fopen);
}