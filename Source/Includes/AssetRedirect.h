#pragma once
#import <Foundation/Foundation.h>
#import <CoreFoundation/CoreFoundation.h>
#import <fcntl.h>
#import <stdarg.h>
#import <stdio.h>
#import <string.h>
#import <unistd.h>
#import <sys/stat.h>
#import <sys/mman.h>
#import <sys/time.h>
#import <zlib.h>
#include <atomic>
#include <mutex>
#include <cstdlib>
#import <objc/runtime.h>
#import <dlfcn.h>
#import <time.h>
#import <errno.h>
#import <dirent.h>

#import "MemoryUtils.h"
#import "fishhook.h"

// Gói Delta.zip nằm NGAY TRONG App Bundle: FreeFire.app/Delta.zip
#define DELTA_ZIP_BUNDLE_NAME "Delta.zip"
// Tên thư mục đích trong Documents/ - cố tình không phải "Delta" để không lộ ra là thư mục mod
// nếu ai đó duyệt Documents qua Files app (user bật File Sharing để tự xem log/debug).
//
// KHÔNG còn hardcode 1 chuỗi cố định trong code nữa (dù là hash trần hay "com.apple.cache.dat")
// - theo đúng hành vi THẬT của Monite mà user quan sát được trên máy: tên thư mục của họ trông
// như 1 hash, và ĐỔI SANG HASH KHÁC mỗi lần xoá cài lại - tức được SINH NGẪU NHIÊN LÚC RUNTIME rồi
// lưu lại (không phải hardcode compile-time). "com.apple.cache.dat" (giải mã được từ chính code
// Monite, xem MoniteAnalysis/README.md mục 3c) nhiều khả năng KHÔNG PHẢI tên thư mục thật của họ,
// mà là tên KEY trong NSUserDefaults nơi họ lưu lại tên thư mục ngẫu nhiên đã sinh - dùng lại đúng
// ý tưởng đó bên dưới (ar_getOrCreateModdedFolderName). Ưu điểm so với hash cố định trong code: cài
// lại/máy khác sẽ ra tên khác nhau, không có 1 chuỗi tĩnh duy nhất trong dylib để nhận diện.
#define DELTA_FOLDER_NAME_DEFAULTS_KEY @"com.apple.cache.dat"

// Lấy tên thư mục đã sinh từ lần chạy trước (lưu trong NSUserDefaults, KHÔNG nằm trong Documents/
// nên không bị lộ nếu duyệt qua Files app) - nếu chưa có (lần đầu tiên/vừa xoá cài lại) thì sinh
// 1 chuỗi hex 16 byte ngẫu nhiên (arc4random_buf - đủ tốt cho mục đích nguỵ trang, không phải bí
// mật cần chống crack) rồi lưu lại để các lần chạy SAU dùng lại ĐÚNG tên này (không sinh mới mỗi
// lần mở app - nếu không sẽ tạo thư mục mới liên tục và luôn bị coi là "chưa từng giải nén").
inline NSString *ar_getOrCreateModdedFolderName() {
    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
    NSString *existing = [defaults stringForKey:DELTA_FOLDER_NAME_DEFAULTS_KEY];
    if (existing.length > 0) return existing;

    uint8_t randomBytes[16];
    arc4random_buf(randomBytes, sizeof(randomBytes));
    char hex[33];
    for (int i = 0; i < 16; i++) {
        snprintf(hex + i * 2, 3, "%02x", randomBytes[i]);
    }
    NSString *generated = [NSString stringWithUTF8String:hex];
    [defaults setObject:generated forKey:DELTA_FOLDER_NAME_DEFAULTS_KEY];
    return generated;
}
static char g_deltaZipPathC[1152] = {0};

// Quản lý tiền tố đường dẫn gốc của App Bundle và thư mục Delta trong Cache
static char g_bundlePrefixC[1024] = {0};
static size_t g_bundlePrefixLen = 0;

// CẤU TRÚC THƯ MỤC kiểu Monite (xem MoniteAnalysis/decoded_strings_final.txt: "temp_folder",
// "contentcache") thay vì 1 thư mục phẳng duy nhất như bản cũ:
//   g_moddedRootC        = Documents/<tên ngẫu nhiên>/            (thư mục gốc của mình)
//   g_moddedPrefixC      = g_moddedRootC + "contentcache/"        (nội dung đã giải nén, VFS đọc từ đây)
//   g_moddedTempFolderC  = g_moddedRootC + "temp_folder/"         (staging lúc giải nén, xem ar_extractZipEntry)
// Tách riêng temp/content để 1 lần giải nén dở dang (crash giữa chừng) không để lại file rác lẫn
// trong nội dung thật - chỉ cần dọn temp_folder, không đụng gì tới contentcache đang phục vụ.
static char g_moddedRootC[1024] = {0};
static size_t g_moddedRootLen = 0;

static char g_moddedPrefixC[1024] = {0};
static size_t g_moddedPrefixLen = 0;

static char g_moddedTempFolderC[1024] = {0};
static char g_moddedFolderNameC[64] = {0};

// Documents/ THUẦN (không có tên thư mục mod nối thêm) - cần riêng để chặn ghi vào các thư mục
// cache THẬT của game (contentcache/ImageCache/Workshop, xem ar_isUnderGameCacheFolder bên dưới),
// khác hẳn g_moddedPrefixC (chỉ trỏ đúng 1 thư mục con của mình).
static char g_documentsPrefixC[1024] = {0};
static size_t g_documentsPrefixLen = 0;

// Con trỏ gốc của hàm access bắt buộc phải được gán trước
static int (*orig_access)(const char *, int);
// orig_stat khai báo SỚM ở đây (không phải cùng chỗ hooked_stat phía dưới) vì
// ar_extractZipEntry() cần dùng nó sớm hơn nhiều (fix mtime SC_Info/FreeFire.sinf).
static int (*orig_stat)(const char *, struct stat *);

// ============================================================================
//  THỐNG KÊ / LOG
// ============================================================================
static std::atomic<unsigned long long> g_deltaHitCount{0};   
static std::atomic<unsigned long long> g_deltaMissCount{0};  
static std::atomic<unsigned long long> g_deltaTotalCalls{0}; 
static std::atomic<unsigned long long> g_deltaBundleCalls{0};
static std::atomic<unsigned int> g_deltaExtractedFiles{0};   
static std::atomic<unsigned int> g_deltaHooksOK{0};          
static std::atomic<bool> g_deltaExtractRan{false};
static std::atomic<bool> g_deltaZipFound{false};
static std::atomic<bool> g_deltaActive{false};
static char g_deltaLastHitPath[1024] = {0};
static char g_deltaLastAnyPath[1024] = {0};

// GIẢI NÉN HẾT 1 ĐỢT (bulk, giống Monite thật) TRÊN 1 PROCESS RIÊNG DÀNH CHO LẦN ĐẦU. Từng thử
// kiểu lazy (giải nén từng file đúng lúc cần, VFS active ngay trong CÙNG process với game) nhưng
// bỏ vì không ổn định qua nhiều vòng debug. Quan sát Monite.dylib (đối thủ) trên máy thật: nó hiện
// popup "Please Wait", giải nén xong thì CRASH, người dùng tự mở lại app lần 2 mới chơi được - tức
// là KHÔNG có gì cần sống sót qua giai đoạn giải nén cả, vì game chỉ thực sự chạy ở process THỨ
// HAI, lúc mọi thứ đã nằm sẵn trên đĩa. Kiến trúc này copy lại đúng ý đó - xem
// ar_ensureFirstRunChecked/DeltaVFS_runFirstRunExtraction bên dưới và
// installAppDelegateLaunchGuard/showUpdatingPopupThenRelaunch trong Menu.mm.
static char g_deltaStatePlistPathC[1152] = {0};
static struct stat g_deltaZipStat;
static std::atomic<bool> g_deltaNeedsFirstRun{false};

// ============================================================================
//  LOG SỐNG SÓT QUA CRASH - ghi thẳng write() syscall (không qua libc buffer, không cần fflush)
//  vào 1 file cố định trong Documents/<hash>/ (dùng dlsym trực tiếp thay vì orig_open của
//  constructor, vì hàm log này có thể được gọi trước khi constructor cài xong fishhook) - user
//  đã bật File Sharing nên xem trực tiếp qua Files app trên điện thoại được, không cần Mac/Xcode.
//  KHÔNG có Filza/SSH/Mac thì dùng bản trong RAM (DeltaVFS_debugLogSnapshot).
// ============================================================================
static int g_deltaLogFd = -1;
static std::mutex g_deltaLogMutex;

#define DELTA_LOG_RING_LINES 40
static char g_deltaLogRingLines[DELTA_LOG_RING_LINES][160];
static int g_deltaLogRingHead = 0;
static unsigned int g_deltaLogRingTotal = 0;
static std::mutex g_deltaLogRingMutex;

// Ring of relative paths that actually got served from Delta/ (a "hit") - replaces the old
// generic NET LOG panel in the INFO tab per user request: they want to see exactly WHICH files
// got redirected, not raw network traffic (that's what DELTA VFS's hit/miss counters already
// summarize numerically).
#define DELTA_HIT_RING_LINES 120
static char g_deltaHitRingLines[DELTA_HIT_RING_LINES][200];
static int g_deltaHitRingHead = 0;
static unsigned int g_deltaHitRingTotal = 0;
static std::mutex g_deltaHitRingMutex;

inline void deltaHitRingPush(const char *relativePath) {
    std::lock_guard<std::mutex> lock(g_deltaHitRingMutex);
    strncpy(g_deltaHitRingLines[g_deltaHitRingHead], relativePath, sizeof(g_deltaHitRingLines[0]) - 1);
    g_deltaHitRingLines[g_deltaHitRingHead][sizeof(g_deltaHitRingLines[0]) - 1] = '\0';
    g_deltaHitRingHead = (g_deltaHitRingHead + 1) % DELTA_HIT_RING_LINES;
    g_deltaHitRingTotal++;
}

inline NSString *DeltaVFS_hitPathsSnapshot(int maxLines) {
    std::lock_guard<std::mutex> lock(g_deltaHitRingMutex);
    int count = (int)((g_deltaHitRingTotal < DELTA_HIT_RING_LINES) ? g_deltaHitRingTotal : DELTA_HIT_RING_LINES);
    int show = (maxLines < count) ? maxLines : count;
    if (show <= 0) return @"(chưa có file nào được redirect qua Delta)";
    int start = ((g_deltaHitRingHead - show) % DELTA_HIT_RING_LINES + DELTA_HIT_RING_LINES) % DELTA_HIT_RING_LINES;
    NSMutableString *out = [NSMutableString string];
    for (int i = 0; i < show; i++) {
        int idx = (start + i) % DELTA_HIT_RING_LINES;
        [out appendFormat:@"%s\n", g_deltaHitRingLines[idx]];
    }
    return out;
}

// Ring riêng cho các file KHÔNG có trong Delta.zip (miss - đọc bản gốc trong bundle) - tách khỏi
// ring HIT ở trên để không bị lấn át (miss luôn nhiều hơn hit rất nhiều, gộp chung sẽ trôi hết hit
// ra khỏi 120 dòng gần nhất).
#define DELTA_MISS_RING_LINES 120
static char g_deltaMissRingLines[DELTA_MISS_RING_LINES][200];
static int g_deltaMissRingHead = 0;
static unsigned int g_deltaMissRingTotal = 0;
static std::mutex g_deltaMissRingMutex;

inline void deltaMissRingPush(const char *relativePath) {
    std::lock_guard<std::mutex> lock(g_deltaMissRingMutex);
    strncpy(g_deltaMissRingLines[g_deltaMissRingHead], relativePath, sizeof(g_deltaMissRingLines[0]) - 1);
    g_deltaMissRingLines[g_deltaMissRingHead][sizeof(g_deltaMissRingLines[0]) - 1] = '\0';
    g_deltaMissRingHead = (g_deltaMissRingHead + 1) % DELTA_MISS_RING_LINES;
    g_deltaMissRingTotal++;
}

inline NSString *DeltaVFS_missPathsSnapshot(int maxLines) {
    std::lock_guard<std::mutex> lock(g_deltaMissRingMutex);
    int count = (int)((g_deltaMissRingTotal < DELTA_MISS_RING_LINES) ? g_deltaMissRingTotal : DELTA_MISS_RING_LINES);
    int show = (maxLines < count) ? maxLines : count;
    if (show <= 0) return @"(chưa có file nào miss)";
    int start = ((g_deltaMissRingHead - show) % DELTA_MISS_RING_LINES + DELTA_MISS_RING_LINES) % DELTA_MISS_RING_LINES;
    NSMutableString *out = [NSMutableString string];
    for (int i = 0; i < show; i++) {
        int idx = (start + i) % DELTA_MISS_RING_LINES;
        [out appendFormat:@"%s\n", g_deltaMissRingLines[idx]];
    }
    return out;
}

inline void deltaLogEnsureOpen() {
    if (g_deltaLogFd >= 0) return;
    std::lock_guard<std::mutex> lock(g_deltaLogMutex);
    if (g_deltaLogFd >= 0) return;
    // g_moddedPrefixC (Documents/<hash>/), NOT the bundle root - the .app bundle itself turned
    // out to be sandbox-read-only on-device (confirmed: EPERM from mkdir()), so a log file
    // written there would have been silently failing this whole time same as the real extraction.
    if (g_moddedPrefixLen == 0) return;
    int (*rawOpen)(const char *, int, ...) = (int (*)(const char *, int, ...))dlsym(RTLD_DEFAULT, "open");
    if (!rawOpen) return;
    char logPath[1200];
    snprintf(logPath, sizeof(logPath), "%sdebug.log", g_moddedPrefixC);
    g_deltaLogFd = rawOpen(logPath, O_WRONLY | O_CREAT | O_APPEND, 0644);
}

inline void DeltaVFS_debugLog(const char *msg) {
    deltaLogEnsureOpen();
    char buf[512];
    // pid in every line: if constructor lines and Menu +load lines ever show DIFFERENT pids,
    // that proves 2 separate process runs got concatenated (e.g. a crash+relaunch the user
    // never noticed), not a same-process ordering bug - decides which theory to chase next.
    // No trailing \n here - DeltaVFS_debugLogSnapshot() adds one per line when rendering, and
    // the file write below appends its own, so embedding one here would double up blank lines.
    int n = snprintf(buf, sizeof(buf), "[t=%.0f pid=%d] %s", (double)time(NULL), (int)getpid(), msg);
    if (n <= 0) return;
    size_t writeLen = ((size_t)n < sizeof(buf)) ? (size_t)n : sizeof(buf) - 1;

    if (g_deltaLogFd >= 0) {
        write(g_deltaLogFd, buf, writeLen);
        write(g_deltaLogFd, "\n", 1);
    }

    // In-RAM ring, no filesystem access needed to read it back - Menu.mm polls
    // DeltaVFS_debugLogSnapshot() straight onto the blocking "please wait" screen. Store `buf`
    // (has the pid/timestamp prefix), not the bare `msg`, so the on-screen panel shows pid too.
    std::lock_guard<std::mutex> lock(g_deltaLogRingMutex);
    strncpy(g_deltaLogRingLines[g_deltaLogRingHead], buf, sizeof(g_deltaLogRingLines[0]) - 1);
    g_deltaLogRingLines[g_deltaLogRingHead][sizeof(g_deltaLogRingLines[0]) - 1] = '\0';
    g_deltaLogRingHead = (g_deltaLogRingHead + 1) % DELTA_LOG_RING_LINES;
    g_deltaLogRingTotal++;
}

inline NSString *DeltaVFS_debugLogSnapshot(int maxLines) {
    std::lock_guard<std::mutex> lock(g_deltaLogRingMutex);
    int count = (int)((g_deltaLogRingTotal < DELTA_LOG_RING_LINES) ? g_deltaLogRingTotal : DELTA_LOG_RING_LINES);
    int show = (maxLines < count) ? maxLines : count;
    if (show <= 0) return @"(chưa có log)";
    int start = ((g_deltaLogRingHead - show) % DELTA_LOG_RING_LINES + DELTA_LOG_RING_LINES) % DELTA_LOG_RING_LINES;
    NSMutableString *out = [NSMutableString string];
    for (int i = 0; i < show; i++) {
        int idx = (start + i) % DELTA_LOG_RING_LINES;
        [out appendFormat:@"%s\n", g_deltaLogRingLines[idx]];
    }
    return out;
}

inline void DeltaVFS_debugLogf(const char *fmt, ...) {
    char msg[400];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);
    DeltaVFS_debugLog(msg);
}

inline NSString *DeltaVFS_debugLogPath() {
    if (g_moddedPrefixLen == 0) return @"(chưa xác định)";
    return [NSString stringWithFormat:@"%sdebug.log", g_moddedPrefixC];
}

// API cho Menu.mm đọc trạng thái
inline unsigned long long DeltaVFS_hits()          { return g_deltaHitCount.load(std::memory_order_relaxed); }
inline unsigned long long DeltaVFS_misses()        { return g_deltaMissCount.load(std::memory_order_relaxed); }
inline unsigned long long DeltaVFS_totalCalls()    { return g_deltaTotalCalls.load(std::memory_order_relaxed); }
inline unsigned long long DeltaVFS_bundleCalls()   { return g_deltaBundleCalls.load(std::memory_order_relaxed); }
inline unsigned int       DeltaVFS_extractedFiles(){ return g_deltaExtractedFiles.load(std::memory_order_relaxed); }
inline unsigned int       DeltaVFS_hooksOK()        { return g_deltaHooksOK.load(std::memory_order_relaxed); }
inline bool               DeltaVFS_extractRan()     { return g_deltaExtractRan.load(std::memory_order_relaxed); }
inline bool               DeltaVFS_zipFound()        { return g_deltaZipFound.load(std::memory_order_relaxed); }
inline bool               DeltaVFS_active()           { return g_deltaActive.load(std::memory_order_relaxed); }
inline const char*        DeltaVFS_lastHitPath()    { return g_deltaLastHitPath; }
inline const char*        DeltaVFS_lastAnyPath()    { return g_deltaLastAnyPath; }
inline const char*        DeltaVFS_deltaDir()       { return g_moddedPrefixC; }
inline const char*        DeltaVFS_zipPath()         { return g_deltaZipPathC; }

inline NSString *DeltaVFS_signatureSummary() {
    if (g_bundlePrefixLen == 0) return @"(chưa xác định bundle)";
    char crPath[1200];
    snprintf(crPath, sizeof(crPath), "%s_CodeSignature/CodeResources", g_bundlePrefixC);
    NSString *p = [NSString stringWithUTF8String:crPath];
    NSDictionary *cr = [NSDictionary dictionaryWithContentsOfFile:p];
    if (!cr) return @"CodeResources: KHÔNG đọc được (app chưa ký / thiếu file?)";

    // Chỉ còn kiểm tra Delta.zip (nguồn, vẫn nằm trong bundle) có được ký không - thư mục đích
    // (g_moddedPrefixC) giờ nằm trong Documents/, được tạo ra lúc chạy, KHÔNG thuộc payload IPA
    // nên không bao giờ có mặt trong CodeResources - không còn gì để kiểm tra ở đó nữa.
    BOOL zipSigned = NO;
    NSArray *sections = @[@"files2", @"files"];
    for (NSString *sect in sections) {
        NSDictionary *files = cr[sect];
        if (![files isKindOfClass:[NSDictionary class]]) continue;
        if ([files objectForKey:@DELTA_ZIP_BUNDLE_NAME]) { zipSigned = YES; break; }
    }
    return [NSString stringWithFormat:@"Delta.zip trong chữ ký: %@", zipSigned ? @"CÓ ✓" : @"KHÔNG ✗"];
}

// ============================================================================
//  PHẦN 1: GIẢI NÉN DELTA.ZIP (BULK, 1 process riêng cho lần đầu - xem giải thích ở
//  g_deltaStatePlistPathC phía trên)
// ============================================================================
static inline uint16_t ar_rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static inline uint32_t ar_rd32(const uint8_t *p) { return (uint32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24)); }
static inline uint64_t ar_rd64(const uint8_t *p) { return (uint64_t)ar_rd32(p) | ((uint64_t)ar_rd32(p + 4) << 32); }

static std::atomic<unsigned int> g_arMkpathFailLogged{0};

static void ar_mkpath(const char *dir) {
    char tmp[2048];
    snprintf(tmp, sizeof(tmp), "%s", dir);
    size_t len = strlen(tmp);
    if (len == 0) return;
    if (tmp[len - 1] == '/') tmp[len - 1] = '\0';
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    int ret = mkdir(tmp, 0755);
    int mkErrno = errno;
    if (ret != 0 && mkErrno != EEXIST) {
        // Rate-limited: every file's parent dir goes through here, and if the bundle is
        // read-only every single one fails the same way - only need to see it a few times.
        if (g_arMkpathFailLogged.fetch_add(1, std::memory_order_relaxed) < 5) {
            struct stat st;
            bool existsAfter = (stat(tmp, &st) == 0 && S_ISDIR(st.st_mode));
            DeltaVFS_debugLogf("ar_mkpath: mkdir FAILED errno=%d (%s) existsAfter=%d path=%s",
                mkErrno, strerror(mkErrno), existsAfter, tmp);
        }
    }
}

// Xoá đệ quy - dùng khi Delta.zip đã đổi so với lần chạy trước, cần dọn sạch cache cũ trong
// Documents/hash trước khi lazy-extraction bổ sung lại từ đầu (tránh phục vụ nhầm file cũ).
static void ar_rmrf(const char *path) {
    DIR *d = opendir(path);
    if (!d) { unlink(path); return; }
    struct dirent *ent;
    char child[2048];
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        snprintf(child, sizeof(child), "%s/%s", path, ent->d_name);
        struct stat st;
        if (lstat(child, &st) == 0 && S_ISDIR(st.st_mode)) {
            ar_rmrf(child);
        } else {
            unlink(child);
        }
    }
    closedir(d);
    rmdir(path);
}

static bool ar_inflateToFd(const uint8_t *src, size_t srcLen, int outFd) {
    z_stream strm;
    memset(&strm, 0, sizeof(strm));
    if (inflateInit2(&strm, -MAX_WBITS) != Z_OK) return false;

    strm.next_in = (Bytef *)src;
    strm.avail_in = (uInt)srcLen;

    static unsigned char outBuf[65536];
    int ret;
    do {
        strm.next_out = outBuf;
        strm.avail_out = sizeof(outBuf);
        ret = inflate(&strm, Z_NO_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END && ret != Z_BUF_ERROR) {
            inflateEnd(&strm);
            return false;
        }
        size_t have = sizeof(outBuf) - strm.avail_out;
        if (have > 0 && write(outFd, outBuf, have) != (ssize_t)have) {
            inflateEnd(&strm);
            return false;
        }
        if (ret == Z_BUF_ERROR && strm.avail_in == 0) break;
    } while (ret != Z_STREAM_END);

    inflateEnd(&strm);
    return true;
}

// ============================================================================
//  INDEX TRONG RAM CỦA DELTA.ZIP - build 1 LẦN lúc khởi động bằng cách đọc central directory
//  (nhanh, không giải nén file nào cả), giữ nguyên mmap của cả file zip SỐNG SUỐT VÒNG ĐỜI app
//  (không munmap) để ar_extractZipEntry() đọc lại bất kỳ lúc nào 1 file cần giải nén thật.
// ============================================================================
#define AR_ZIP_MAX_ENTRIES 4096
struct ArZipEntry {
    char name[512];        // đường dẫn tương đối bên trong zip, khớp với "relative"
                            // mà redirectAllTrafficPath() tính ra (KHÔNG có tiền tố FreeFire.app/)
    uint16_t method;        // 0 = lưu thẳng, 8 = deflate
    uint32_t compSize;
    uint32_t uncompSize;
    uint64_t localOff;
};
static ArZipEntry g_arZipEntries[AR_ZIP_MAX_ENTRIES];
static int g_arZipEntryCount = 0;
static uint8_t *g_arZipBase = NULL;
static size_t g_arZipSize = 0;
static std::atomic<unsigned long> g_arTmpCounter{0};

static bool ar_buildZipIndex(const char *zipPath) {
    int fd = open(zipPath, O_RDONLY);
    if (fd < 0) { DeltaVFS_debugLogf("ar_buildZipIndex: ABORT open(zip) failed errno=%d", errno); return false; }

    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size < 22) {
        DeltaVFS_debugLogf("ar_buildZipIndex: ABORT fstat failed or size<22 (size=%lld)", (long long)st.st_size);
        close(fd);
        return false;
    }
    size_t fileSize = (size_t)st.st_size;

    uint8_t *base = (uint8_t *)mmap(NULL, fileSize, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (base == MAP_FAILED) { DeltaVFS_debugLogf("ar_buildZipIndex: ABORT mmap failed errno=%d", errno); return false; }

    const uint8_t *eocd = NULL;
    size_t maxBack = (fileSize < (22 + 65535)) ? fileSize : (22 + 65535);
    for (size_t i = 22; i <= maxBack; i++) {
        const uint8_t *p = base + fileSize - i;
        if (ar_rd32(p) == 0x06054b50) { eocd = p; break; }
    }
    if (!eocd) { DeltaVFS_debugLog("ar_buildZipIndex: ABORT no EOCD signature found"); munmap(base, fileSize); return false; }

    uint64_t totalEntries = ar_rd16(eocd + 10);
    uint64_t cdOffset     = ar_rd32(eocd + 16);

    if ((size_t)(eocd - base) >= 20) {
        const uint8_t *loc = eocd - 20;
        if (ar_rd32(loc) == 0x07064b50) {
            uint64_t z64Off = ar_rd64(loc + 8);
            if (z64Off + 56 <= fileSize && ar_rd32(base + z64Off) == 0x06064b50) {
                const uint8_t *z64 = base + z64Off;
                totalEntries = ar_rd64(z64 + 32);
                cdOffset     = ar_rd64(z64 + 48);
            }
        }
    }
    DeltaVFS_debugLogf("ar_buildZipIndex: totalEntries=%llu cdOffset=%llu", (unsigned long long)totalEntries, (unsigned long long)cdOffset);

    if (cdOffset >= fileSize) { DeltaVFS_debugLog("ar_buildZipIndex: ABORT cdOffset >= fileSize"); munmap(base, fileSize); return false; }

    const uint8_t *cd = base + cdOffset;
    int count = 0;
    unsigned int skipped = 0;

    for (uint64_t e = 0; e < totalEntries && count < AR_ZIP_MAX_ENTRIES; e++) {
        if ((size_t)(cd - base) + 46 > fileSize) { DeltaVFS_debugLogf("ar_buildZipIndex: STOP cd+46>fileSize at entry=%llu", (unsigned long long)e); break; }
        if (ar_rd32(cd) != 0x02014b50) { DeltaVFS_debugLogf("ar_buildZipIndex: STOP bad CD signature at entry=%llu", (unsigned long long)e); break; }

        uint16_t method     = ar_rd16(cd + 10);
        uint32_t uncompSize = ar_rd32(cd + 24);
        uint64_t compSize   = ar_rd32(cd + 20);
        uint16_t nameLen    = ar_rd16(cd + 28);
        uint16_t extraLen   = ar_rd16(cd + 30);
        uint16_t commentLen = ar_rd16(cd + 32);
        uint64_t localOff   = ar_rd32(cd + 42);
        const uint8_t *nameP = cd + 46;

        {
            const uint8_t *ex = nameP + nameLen;
            const uint8_t *exEnd = ex + extraLen;
            while (ex + 4 <= exEnd) {
                uint16_t exId  = ar_rd16(ex);
                uint16_t exSz  = ar_rd16(ex + 2);
                const uint8_t *fld = ex + 4;
                if (ex + 4 + exSz > exEnd) break;
                if (exId == 0x0001) {
                    if (uncompSize == 0xFFFFFFFF && fld + 8 <= ex + 4 + exSz) fld += 8;
                    if (compSize   == 0xFFFFFFFF && fld + 8 <= ex + 4 + exSz) { compSize = ar_rd64(fld); fld += 8; }
                    if (localOff   == 0xFFFFFFFF && fld + 8 <= ex + 4 + exSz) { localOff = ar_rd64(fld); fld += 8; }
                    break;
                }
                ex += 4 + exSz;
            }
        }

        const uint8_t *nextCd = nameP + nameLen + extraLen + commentLen;

        if (nameLen > 0 && nameLen < sizeof(g_arZipEntries[0].name)) {
            char nameBuf[512];
            memcpy(nameBuf, nameP, nameLen);
            nameBuf[nameLen] = '\0';
            size_t nl = strlen(nameBuf);
            bool isDir = (nl > 0 && nameBuf[nl - 1] == '/');
            bool traversal = (nameBuf[0] == '/' || strstr(nameBuf, "..") != NULL);
            if (!isDir && !traversal) {
                ArZipEntry &ent = g_arZipEntries[count];
                strncpy(ent.name, nameBuf, sizeof(ent.name) - 1);
                ent.name[sizeof(ent.name) - 1] = '\0';
                ent.method = method;
                ent.compSize = (uint32_t)compSize;
                ent.uncompSize = uncompSize;
                ent.localOff = localOff;
                count++;
            } else {
                skipped++;
            }
        } else {
            skipped++;
        }
        cd = nextCd;
    }

    g_arZipBase = base;
    g_arZipSize = fileSize;
    g_arZipEntryCount = count;
    // CỐ Ý KHÔNG munmap(base, fileSize) - giữ sống suốt vòng đời app để đọc lại lúc giải nén
    // từng file theo yêu cầu (xem ar_extractZipEntry).
    DeltaVFS_debugLogf("ar_buildZipIndex: OK indexed=%d skipped=%u (zip totalEntries=%llu)", count, skipped, (unsigned long long)totalEntries);
    return count > 0;
}

inline bool ar_endsWith(const char *s, const char *suffix) {
    if (!s || !suffix) return false;
    size_t sLen = strlen(s), sufLen = strlen(suffix);
    if (sufLen > sLen) return false;
    return strcmp(s + (sLen - sufLen), suffix) == 0;
}

// Giải nén ĐÚNG 1 entry (theo index trong g_arZipEntries) ra destPath. Ghi vào file tạm trong
// tempDirC (kiểu Monite: "temp_folder/" riêng, TÁCH khỏi contentcache/ đang phục vụ - xem
// g_moddedTempFolderC) rồi rename() sang tên thật trong destPath - tránh trường hợp 1 thread khác
// đọc phải file đang ghi dở, và đảm bảo 1 lần giải nén dở dang (crash giữa chừng) chỉ để lại rác
// trong temp_folder/, không lẫn vào contentcache/ thật.
static bool ar_extractZipEntry(int idx, const char *destPath, const char *tempDirC) {
    if (idx < 0 || idx >= g_arZipEntryCount || !g_arZipBase) return false;
    ArZipEntry &ent = g_arZipEntries[idx];

    if ((size_t)ent.localOff + 30 > g_arZipSize) {
        DeltaVFS_debugLogf("ar_extractZipEntry: local header OOB name=%s", ent.name);
        return false;
    }
    const uint8_t *lh = g_arZipBase + ent.localOff;
    if (ar_rd32(lh) != 0x04034b50) {
        DeltaVFS_debugLogf("ar_extractZipEntry: bad local header sig name=%s", ent.name);
        return false;
    }
    uint16_t lhNameLen  = ar_rd16(lh + 26);
    uint16_t lhExtraLen = ar_rd16(lh + 28);
    const uint8_t *data = lh + 30 + lhNameLen + lhExtraLen;
    if ((size_t)(data - g_arZipBase) + ent.compSize > g_arZipSize) {
        DeltaVFS_debugLogf("ar_extractZipEntry: data OOB name=%s", ent.name);
        return false;
    }

    char parent[2048];
    snprintf(parent, sizeof(parent), "%s", destPath);
    char *slash = strrchr(parent, '/');
    if (slash) { *slash = '\0'; ar_mkpath(parent); }

    char tmpPath[2200];
    snprintf(tmpPath, sizeof(tmpPath), "%s.delta_tmp_%d_%lu", tempDirC, (int)getpid(),
              g_arTmpCounter.fetch_add(1, std::memory_order_relaxed));

    int outFd = open(tmpPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (outFd < 0) {
        DeltaVFS_debugLogf("ar_extractZipEntry: open(write tmp) FAILED errno=%d name=%s", errno, ent.name);
        return false;
    }

    bool ok;
    if (ent.method == 0) {
        ok = (ent.compSize == 0) || (write(outFd, data, ent.compSize) == (ssize_t)ent.compSize);
    } else if (ent.method == 8) {
        ok = ar_inflateToFd(data, ent.compSize, outFd);
    } else {
        DeltaVFS_debugLogf("ar_extractZipEntry: unsupported method=%u name=%s", ent.method, ent.name);
        ok = false;
    }
    close(outFd);
    if (!ok) { unlink(tmpPath); return false; }

    if (rename(tmpPath, destPath) != 0) {
        DeltaVFS_debugLogf("ar_extractZipEntry: rename FAILED errno=%d name=%s", errno, ent.name);
        unlink(tmpPath);
        return false;
    }
    g_deltaExtractedFiles.fetch_add(1, std::memory_order_relaxed);

    // SC_Info/FreeFire.sinf - đây là file chữ ký FairPlay DRM Apple tạo ra khi app được tải THẬT
    // từ App Store, thường bị mất/không hợp lệ sau khi IPA bị ký lại để cài ngoài App Store (như
    // ở đây). MoniteAnalysis/README.md mục 3e (đã phân tích Monite.dylib trước đó) tìm thấy Monite
    // ĐẶC CÁCH file này: mtime của bản extract KHÔNG lấy từ zip mà lấy mtime THẬT của chính
    // FreeFire.app đang cài trên máy - suy luận hợp lý: để tránh lộ dấu vết "app vừa bị giải nén/
    // ký lại gần đây" (mtime = lúc extract sẽ mới hơn nhiều so với mtime cài đặt thật) nếu có gì đó
    // đọc mtime file này để kiểm tra tính nhất quán. Áp dụng lại đúng kỹ thuật đó ở đây - CHỈ set
    // lại mtime (utimes), không đụng nội dung file, không patch/hook gì game - an toàn tương đương
    // phần VFS redirect còn lại của file này.
    if (ar_endsWith(ent.name, "SC_Info/FreeFire.sinf")) {
        struct stat bundleSt;
        if (orig_stat && orig_stat(g_bundlePrefixC, &bundleSt) == 0) {
            struct timeval times[2];
            times[0].tv_sec = bundleSt.st_atimespec.tv_sec;  times[0].tv_usec = 0;
            times[1].tv_sec = bundleSt.st_mtimespec.tv_sec;  times[1].tv_usec = 0;
            if (utimes(destPath, times) == 0) {
                DeltaVFS_debugLogf("ar_extractZipEntry: da chinh mtime %s theo bundle that (mtime=%ld)",
                                    ent.name, (long)bundleSt.st_mtimespec.tv_sec);
            } else {
                DeltaVFS_debugLogf("ar_extractZipEntry: utimes() that bai errno=%d cho %s", errno, ent.name);
            }
        }
    }
    return true;
}

// 3 khả năng khi 1 file được yêu cầu qua redirectAllTrafficPath():
//  - NOT_MODDED: file không có trong Delta.zip - bình thường, không phải file bị mod.
//  - PRESENT: file có trong Delta.zip VÀ đang có mặt trên đĩa - hit bình thường.
//  - TAMPERED: file có trong Delta.zip nhưng KHÔNG còn trên đĩa dù VFS đã active (nghĩa là
//    ar_verifyAllFilesPresent() từng xác nhận đủ file lúc khởi động - bị xoá/đổi tên SAU đó, giữa
//    session đang chạy, VD: xoá thư mục Documents/<hash> ngay tại màn login rồi tiếp tục vào game,
//    đúng kiểu test đã làm với Monite.dylib).
enum ArEntryStatus { AR_ENTRY_NOT_MODDED = 0, AR_ENTRY_PRESENT = 1, AR_ENTRY_TAMPERED = 2 };

// Đảm bảo destPath tồn tại trên đĩa - đường nhanh (đã giải nén từ trước) chỉ tốn 1 access().
//
// KHÔNG tự vá (re-extract) khi TAMPERED - CỐ Ý bỏ, theo yêu cầu user: muốn thấy ĐÚNG hành vi như
// Monite thật (xoá folder giữa session -> game đọc thiếu file -> tự hiện lỗi/texture đen, KHÔNG
// crash, KHÔNG âm thầm vá lại) để tự xác nhận VFS có thật sự đang được game đọc qua hay không.
static ArEntryStatus ar_extractOneEntryIfNeeded(const char *destPath, const char *relativePath) {
    if (!destPath || !relativePath) return AR_ENTRY_NOT_MODDED;
    if (orig_access && orig_access(destPath, F_OK) == 0) return AR_ENTRY_PRESENT;
    if (g_arZipEntryCount == 0) return AR_ENTRY_NOT_MODDED;

    for (int i = 0; i < g_arZipEntryCount; i++) {
        if (strcmp(g_arZipEntries[i].name, relativePath) == 0) {
            DeltaVFS_debugLogf("ar_extractOneEntryIfNeeded: TAMPERED - %s có trong Delta.zip nhưng đã biến mất khỏi đĩa (destPath=%s) giữa session dù VFS đã active - để miss thật, không tự vá", relativePath, destPath);
            return AR_ENTRY_TAMPERED;
        }
    }
    return AR_ENTRY_NOT_MODDED; // không có trong Delta.zip - không phải lỗi, chỉ là file này không được mod
}

// TRẠNG THÁI kiểu Monite: plist với "status": "extracting"/"complete" (xem
// MoniteAnalysis/decoded_strings_final.txt) thay vì 1 marker text phẳng. Lợi thế so với marker cũ
// (chỉ có mtime:size) - phân biệt được 2 tình huống hoàn toàn khác nhau:
//   - status vẫn còn "extracting" lúc app mở lại -> lần TRƯỚC đã CRASH GIỮA CHỪNG lúc đang giải
//     nén (đúng hành vi Monite thật quan sát được: unzip xong rồi crash, mở lại app lần 2 mới vào
//     được) -> ar_stateNeedsExtract() vẫn trả true đúng như marker cũ, NHƯNG giờ còn phân biệt
//     được để set thêm cờ dọn dẹp (xem DELTA_PENDING_CLEANUP_DEFAULTS_KEY) thay vì chỉ im lặng
//     giải nén lại.
//   - status "complete" nhưng zip_marker không khớp Delta.zip hiện tại -> Delta.zip đã đổi, cần
//     giải nén lại bình thường (giống hệt marker cũ).
#define DELTA_STATE_KEY_STATUS   @"status"
#define DELTA_STATE_KEY_FOLDER   @"folder"
#define DELTA_STATE_KEY_ZIPMARK  @"zip_marker"
#define DELTA_STATE_STATUS_EXTRACTING @"extracting"
#define DELTA_STATE_STATUS_COMPLETE   @"complete"

// Cờ "cần dọn dẹp lại từ đầu" kiểu Monite (tên key giả trang tương tự
// DELTA_FOLDER_NAME_DEFAULTS_KEY ở trên) - lưu trong NSUserDefaults (không nằm trong Documents/,
// không lộ qua Files app), được set khi ar_ensureFirstRunChecked() phát hiện status còn dang dở
// "extracting" từ lần chạy trước, và được tiêu thụ/xoá ngay ở đầu DeltaVFS_runFirstRunExtraction().
#define DELTA_PENDING_CLEANUP_DEFAULTS_KEY @"com.apple.cache.mrk"

// Notification post lúc giải nén xong (kiểu MoniteUnzipCompletedNotification) - hiện KHÔNG có ai
// observe (Menu.mm dùng completion block ở DeltaVFS_runFirstRunExtraction để relaunch, không cần
// notification), chỉ post thêm cho khớp kiến trúc Monite/dễ gắn thêm observer sau này nếu cần.
#define DELTA_EXTRACTION_COMPLETE_NOTIFICATION @"com.apple.assetcache.completed"

// nil nếu chưa từng chạy (chưa có file) hoặc plist hỏng/đọc không ra dict.
inline NSDictionary *ar_stateLoad(const char *plistPathC) {
    @autoreleasepool {
        NSString *path = [NSString stringWithUTF8String:plistPathC];
        return [NSDictionary dictionaryWithContentsOfFile:path];
    }
}

// folderNameC là tên thư mục ngẫu nhiên hiện tại (ar_getOrCreateModdedFolderName) - lưu lại trong
// state chỉ để dễ đối chiếu lúc đọc log/debug qua Files app, VFS không dựa vào field này để hoạt động.
inline bool ar_stateSave(const char *plistPathC, NSString *status, const char *folderNameC, const struct stat *zipSt) {
    @autoreleasepool {
        NSString *zipMarker = [NSString stringWithFormat:@"%lld:%lld", (long long)zipSt->st_mtime, (long long)zipSt->st_size];
        NSDictionary *dict = @{
            DELTA_STATE_KEY_STATUS:  status,
            DELTA_STATE_KEY_FOLDER:  folderNameC ? [NSString stringWithUTF8String:folderNameC] : @"",
            DELTA_STATE_KEY_ZIPMARK: zipMarker,
        };
        NSString *path = [NSString stringWithUTF8String:plistPathC];
        BOOL ok = [dict writeToFile:path atomically:YES];
        if (!ok) {
            DeltaVFS_debugLogf("ar_stateSave: FAILED write plist path=%s status=%s", plistPathC, status.UTF8String);
        } else {
            DeltaVFS_debugLogf("ar_stateSave: OK path=%s status=%s", plistPathC, status.UTF8String);
        }
        return ok == YES;
    }
}

// true nếu chưa từng giải nén, dở dang, hoặc Delta.zip đã đổi so với lần "complete" gần nhất.
inline bool ar_stateNeedsExtract(NSDictionary *state, const struct stat *zipSt) {
    if (!state) return true;
    NSString *status = state[DELTA_STATE_KEY_STATUS];
    if (![status isEqualToString:DELTA_STATE_STATUS_COMPLETE]) return true;
    NSString *expected = [NSString stringWithFormat:@"%lld:%lld", (long long)zipSt->st_mtime, (long long)zipSt->st_size];
    return ![state[DELTA_STATE_KEY_ZIPMARK] isEqualToString:expected];
}

// Kiểm tra TỪNG file trong Delta.zip có thực sự tồn tại trên đĩa (Documents/<hash>/contentcache/...)
// hay không - KHÔNG chỉ tin zip_marker (mtime:size) trong state.plist như ar_stateNeedsExtract() ở
// trên. Lý do thêm hàm này: đối chiếu hành vi THẬT của Monite trên máy (xoá/đổi tên 1 file/folder
// ĐÃ giải nén, KHÔNG đụng gì tới file zip nguồn) - app của họ vẫn phát hiện ra và tự hiện lại popup
// "Please Wait" để giải nén lại, chứng tỏ họ kiểm tra kỹ hơn 1 marker đơn thuần. zip_marker chỉ so
// mtime/size của CHÍNH Delta.zip - đổi tên 1 file lẻ bên trong contentcache/ không đụng gì tới
// Delta.zip nên zip_marker vẫn khớp, ar_stateNeedsExtract() vẫn trả false (tưởng đã ổn) - không tự
// phát hiện/sửa được kiểu tampering đó. Hàm này lấp đúng lỗ hổng này.
//
// Chi phí: build index (đọc central directory, KHÔNG giải nén gì) rồi access() từng entry - vài
// nghìn syscall access() rất nhanh (micro-giây/lần), chấp nhận được vì chỉ chạy ĐÚNG 1 LẦN lúc
// khởi động (ar_ensureFirstRunChecked, gọi 1 lần nhờ g_deltaFirstRunCheckDone), không lặp lại
// trong lúc chơi - đúng mô hình "kiểm tra kỹ 1 lần lúc đầu, tin tưởng tuyệt đối sau đó" đã xác
// nhận qua hành vi thật của Monite.
static bool ar_verifyAllFilesPresent(const char *zipPath) {
    if (!ar_buildZipIndex(zipPath)) {
        DeltaVFS_debugLog("ar_verifyAllFilesPresent: build index thất bại - coi như cần giải nén lại");
        return false;
    }
    static thread_local char destPath[2048];
    for (int i = 0; i < g_arZipEntryCount; i++) {
        const char *relative = g_arZipEntries[i].name;
        int written = snprintf(destPath, sizeof(destPath), "%s%s", g_moddedPrefixC, relative);
        if (written < 0 || written >= (int)sizeof(destPath)) continue;
        if (access(destPath, F_OK) != 0) {
            DeltaVFS_debugLogf("ar_verifyAllFilesPresent: THIẾU %s (đường dẫn %s) - cần giải nén lại", relative, destPath);
            return false;
        }
    }
    DeltaVFS_debugLogf("ar_verifyAllFilesPresent: OK, đủ cả %d file", g_arZipEntryCount);
    return true;
}

// ============================================================================
//  KIỂM TRA/KHỞI TẠO - IDEMPOTENT, gọi từ bất kỳ đâu, bất kỳ lúc nào (constructor lẫn Menu.mm's
//  +load có thể gọi theo bất kỳ thứ tự nào - xem lý do ở memory delta-asset-redirect-design).
//  Build xong index là VFS active NGAY - không còn phải chờ 1 đợt giải nén lớn hoàn tất nữa.
// ============================================================================
static std::atomic<bool> g_deltaFirstRunCheckDone{false};
static std::mutex g_deltaFirstRunCheckMutex;

inline void ar_ensureFirstRunChecked() {
    if (g_deltaFirstRunCheckDone.load(std::memory_order_acquire)) return;
    std::lock_guard<std::mutex> lock(g_deltaFirstRunCheckMutex);
    if (g_deltaFirstRunCheckDone.load(std::memory_order_relaxed)) return;

    if (g_bundlePrefixLen == 0) {
        @autoreleasepool {
            NSString *bundlePath = [[NSBundle mainBundle] bundlePath];
            if (bundlePath) {
                NSString *bundleRoot = [bundlePath stringByAppendingString:@"/"];
                strncpy(g_bundlePrefixC, [bundleRoot UTF8String], sizeof(g_bundlePrefixC) - 1);
                g_bundlePrefixLen = strlen(g_bundlePrefixC);

                // Delta/ must NOT live inside the .app bundle - confirmed on-device that the
                // sandbox denies mkdir() there with EPERM (the bundle is read-only/immutable for
                // a properly code-signed app, jailbreak or not). Use Documents/ instead - this
                // build's Info.plist has UIFileSharingEnabled on, so the user can browse it via
                // the Files app without Filza/a Mac. Folder is named a hash rather than "Delta" so
                // it doesn't stick out among the app's real Documents files.
                NSArray<NSString *> *documentsDirs = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES);
                NSString *documentsDir = documentsDirs.firstObject;
                strncpy(g_documentsPrefixC, [[documentsDir stringByAppendingString:@"/"] UTF8String], sizeof(g_documentsPrefixC) - 1);
                g_documentsPrefixLen = strlen(g_documentsPrefixC);
                NSString *folderName = ar_getOrCreateModdedFolderName();
                strncpy(g_moddedFolderNameC, [folderName UTF8String], sizeof(g_moddedFolderNameC) - 1);

                // 3 tầng thư mục kiểu Monite - xem giải thích ở khai báo g_moddedRootC phía trên.
                NSString *moddedRootDir = [[documentsDir stringByAppendingPathComponent:folderName] stringByAppendingString:@"/"];
                strncpy(g_moddedRootC, [moddedRootDir UTF8String], sizeof(g_moddedRootC) - 1);
                g_moddedRootLen = strlen(g_moddedRootC);

                NSString *contentCacheDir = [moddedRootDir stringByAppendingString:@"contentcache/"];
                strncpy(g_moddedPrefixC, [contentCacheDir UTF8String], sizeof(g_moddedPrefixC) - 1);
                g_moddedPrefixLen = strlen(g_moddedPrefixC);

                NSString *tempFolderDir = [moddedRootDir stringByAppendingString:@"temp_folder/"];
                strncpy(g_moddedTempFolderC, [tempFolderDir UTF8String], sizeof(g_moddedTempFolderC) - 1);

                NSString *zipPath = [bundlePath stringByAppendingString:@"/" DELTA_ZIP_BUNDLE_NAME];
                strncpy(g_deltaZipPathC, [zipPath UTF8String], sizeof(g_deltaZipPathC) - 1);
            }
        }
    }

    DeltaVFS_debugLogf("ar_ensureFirstRunChecked: bundle=%s zip=%s", g_bundlePrefixC, g_deltaZipPathC);

    if (g_moddedPrefixLen > 0 && g_deltaZipPathC[0]) {
        if (stat(g_deltaZipPathC, &g_deltaZipStat) == 0) {
            g_deltaZipFound.store(true, std::memory_order_relaxed);
            DeltaVFS_debugLogf("ar_ensureFirstRunChecked: Delta.zip found, size=%lld mtime=%lld", (long long)g_deltaZipStat.st_size, (long long)g_deltaZipStat.st_mtime);
            ar_mkpath(g_moddedRootC);
            ar_mkpath(g_moddedPrefixC);
            {
                // Does Documents/hash actually let us create/write? Keep this canary check -
                // cheap, and confirms the fix instead of assuming it.
                struct stat checkSt;
                bool dirExists = (stat(g_moddedPrefixC, &checkSt) == 0 && S_ISDIR(checkSt.st_mode));
                char canaryPath[1200];
                snprintf(canaryPath, sizeof(canaryPath), "%s.write_test", g_moddedPrefixC);
                int canaryFd = open(canaryPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                bool canaryWritable = (canaryFd >= 0);
                int canaryErrno = errno;
                if (canaryFd >= 0) { close(canaryFd); unlink(canaryPath); }
                DeltaVFS_debugLogf("ar_ensureFirstRunChecked: Delta/ dirExists=%d writable=%d (errno=%d %s)",
                    dirExists, canaryWritable, canaryWritable ? 0 : canaryErrno, canaryWritable ? "" : strerror(canaryErrno));

                // Exclude from iCloud/iTunes backup - regenerable data (re-extracted from
                // Delta.zip on demand), no reason to bloat the user's device backup. Loại trừ
                // CẢ THƯ MỤC GỐC (root), không chỉ contentcache/, vì temp_folder/ cũng là dữ liệu
                // tạm không cần backup.
                if (dirExists) {
                    @autoreleasepool {
                        NSURL *rootURL = [NSURL fileURLWithPath:[NSString stringWithUTF8String:g_moddedRootC] isDirectory:YES];
                        NSError *excludeErr = nil;
                        [rootURL setResourceValue:@YES forKey:NSURLIsExcludedFromBackupKey error:&excludeErr];
                    }
                }
            }

            snprintf(g_deltaStatePlistPathC, sizeof(g_deltaStatePlistPathC), "%s.state.plist", g_moddedRootC);
            NSDictionary *priorState = ar_stateLoad(g_deltaStatePlistPathC);

            if ([priorState[DELTA_STATE_KEY_STATUS] isEqualToString:DELTA_STATE_STATUS_EXTRACTING]) {
                // status vẫn "extracting" từ lần chạy trước -> lần đó đã CRASH GIỮA CHỪNG lúc đang
                // giải nén (đúng hành vi Monite thật: unzip xong crash, mở lại app lần 2 mới vào
                // được). Set cờ dọn dẹp kiểu Monite - DeltaVFS_runFirstRunExtraction() sẽ đọc và
                // xoá cờ này lúc dọn temp_folder/contentcache trước khi giải nén lại từ đầu.
                DeltaVFS_debugLog("ar_ensureFirstRunChecked: state 'extracting' dở dang từ lần trước (crash giữa chừng) - set cờ dọn dẹp");
                [[NSUserDefaults standardUserDefaults] setBool:YES forKey:DELTA_PENDING_CLEANUP_DEFAULTS_KEY];
            }

            if (ar_stateNeedsExtract(priorState, &g_deltaZipStat)) {
                // Delta.zip đổi (hoặc chưa từng chạy) so với lần trước - đây là process "lần đầu".
                // KHÔNG giải nén gì ở đây (hàm này phải nhanh - gọi từ constructor, chặn dyld nếu
                // chậm). Chỉ báo hiệu qua g_deltaNeedsFirstRun; Menu.mm sẽ hiện popup rồi gọi
                // DeltaVFS_runFirstRunExtraction() trên background thread. g_deltaActive CỐ Ý
                // không bật - game (process này) sẽ không bao giờ thực sự chạy tới lúc đọc file.
                DeltaVFS_debugLog("ar_ensureFirstRunChecked: Delta.zip thay đổi/chưa từng chạy - cần giải nén lần đầu (process này sẽ popup rồi thoát)");
                g_deltaExtractRan.store(true, std::memory_order_relaxed);
                g_deltaNeedsFirstRun.store(true, std::memory_order_relaxed);
            } else if (ar_verifyAllFilesPresent(g_deltaZipPathC)) {
                // state "complete" khớp VÀ từng file thực sự còn nguyên trên đĩa (xem
                // ar_verifyAllFilesPresent - đối chiếu hành vi thật của Monite: chỉ tin state thôi
                // không đủ, ai đó xoá/đổi tên 1 file lẻ trong contentcache/ thì state vẫn "complete"
                // nhưng thực tế đã thiếu). Index vừa build được GIỮ LẠI (không tốn công build lại) -
                // ar_extractOneEntryIfNeeded() từ giờ có thêm khả năng TỰ VÁ nếu 1 file lẻ nào đó
                // biến mất giữa lúc đang chơi, không chỉ existence-check đơn thuần như trước.
                g_deltaActive.store(true, std::memory_order_relaxed);
                DeltaVFS_debugLog("ar_ensureFirstRunChecked: state complete + đủ file - VFS active ngay");
            } else {
                // state "complete" NHƯNG thiếu ít nhất 1 file thật (bị xoá/đổi tên...) - coi như cần
                // giải nén lại từ đầu, ĐÚNG hành vi Monite thể hiện qua popup "Please Wait" khi
                // user cố tình đổi tên/xoá file trong Delta/ (không đụng gì tới Delta.zip nguồn).
                DeltaVFS_debugLog("ar_ensureFirstRunChecked: state complete nhưng THIẾU file thật - coi như cần giải nén lần đầu (process này sẽ popup rồi thoát)");
                g_deltaExtractRan.store(true, std::memory_order_relaxed);
                g_deltaNeedsFirstRun.store(true, std::memory_order_relaxed);
            }
        } else {
            DeltaVFS_debugLogf("ar_ensureFirstRunChecked: Delta.zip NOT FOUND at %s (stat errno=%d) - VFS stays inactive, nothing redirected", g_deltaZipPathC, errno);
        }
    }

    g_deltaFirstRunCheckDone.store(true, std::memory_order_release);
}

inline bool DeltaVFS_needsFirstRunExtraction() {
    ar_ensureFirstRunChecked();
    return g_deltaNeedsFirstRun.load(std::memory_order_relaxed);
}

// Chạy giải nén HẾT Delta.zip (bulk) trên 1 background queue, rồi nhảy về main queue gọi
// completion - Menu.mm dùng completion để giữ popup trên màn hình rồi abort() cho relaunch sạch.
// Trong lúc này redirectAllTrafficPath() vẫn để nguyên path gốc (g_deltaActive còn false) - dù
// thực tế game sẽ không bao giờ chạy tới đó trong process này (bị installAppDelegateLaunchGuard
// chặn từ trước khi didFinishLaunching thật sự chạy).
// completion(success) - success=false nếu không file nào được ghi hoặc state "complete" không ghi
// được. Caller (Menu.mm) TUYỆT ĐỐI KHÔNG được abort()/crash khi failure: state chưa ghi "complete"
// thì relaunch nào cũng lại detect "cần giải nén" rồi lặp vô hạn không cách nào biết vì sao (RAM
// log mất, log trên đĩa cần Filza/Mac không có).
inline void DeltaVFS_runFirstRunExtraction(void (^completion)(BOOL success)) {
    DeltaVFS_debugLog("runFirstRunExtraction: dispatching to background queue");
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        // Ghi status="extracting" NGAY LẬP TỨC, trước khi đụng tới bất kỳ file nào - nếu process
        // này crash giữa chừng (đúng hành vi Monite thật quan sát được), lần chạy sau đọc state ra
        // vẫn thấy "extracting" (không phải "complete" cũ hay hoàn toàn không có) nên biết chắc
        // phải giải nén lại từ đầu (xem ar_stateNeedsExtract) thay vì tưởng nhầm đã xong.
        ar_stateSave(g_deltaStatePlistPathC, DELTA_STATE_STATUS_EXTRACTING, g_moddedFolderNameC, &g_deltaZipStat);

        bool pendingCleanup = [[NSUserDefaults standardUserDefaults] boolForKey:DELTA_PENDING_CLEANUP_DEFAULTS_KEY];
        if (pendingCleanup) {
            DeltaVFS_debugLog("runFirstRunExtraction: tiêu thụ cờ dọn dẹp (lần trước crash giữa chừng) - xoá cả temp_folder lẫn contentcache trước khi giải nén lại");
            [[NSUserDefaults standardUserDefaults] removeObjectForKey:DELTA_PENDING_CLEANUP_DEFAULTS_KEY];
        }

        // Delta.zip đổi so với lần trước (hoặc cài mới, hoặc đang dọn dẹp sau crash) - dọn sạch cả
        // temp_folder/ (rác giải nén dở dang) lẫn contentcache/ (nội dung cũ) rồi tạo lại từ đầu,
        // tránh lẫn file cũ/thiếu đồng bộ với bản Delta.zip hiện tại.
        DeltaVFS_debugLog("runFirstRunExtraction: xoá cache cũ (nếu có) rồi tạo lại temp_folder/ + contentcache/");
        ar_rmrf(g_moddedTempFolderC);
        ar_rmrf(g_moddedPrefixC);
        ar_mkpath(g_moddedTempFolderC);
        ar_mkpath(g_moddedPrefixC);

        DeltaVFS_debugLog("runFirstRunExtraction: build zip index");
        bool indexed = ar_buildZipIndex(g_deltaZipPathC);
        unsigned int filesWritten = 0;
        bool allOK = indexed;
        if (indexed) {
            DeltaVFS_debugLogf("runFirstRunExtraction: bắt đầu giải nén %d entries", g_arZipEntryCount);
            static thread_local char destPath[2048];
            for (int i = 0; i < g_arZipEntryCount; i++) {
                const char *relative = g_arZipEntries[i].name;
                int written = snprintf(destPath, sizeof(destPath), "%s%s", g_moddedPrefixC, relative);
                if (written < 0 || written >= (int)sizeof(destPath)) { allOK = false; continue; }
                if (ar_extractZipEntry(i, destPath, g_moddedTempFolderC)) {
                    filesWritten++;
                } else {
                    allOK = false;
                }
                if ((i % 500) == 0) {
                    DeltaVFS_debugLogf("runFirstRunExtraction: tiến độ %d/%d", i, g_arZipEntryCount);
                }
            }
        }

        // temp_folder/ chỉ dùng để staging lúc giải nén (xem ar_extractZipEntry) - dọn sạch ngay
        // sau khi xong, không để rác (dù rename() đã chuyển hết file hợp lệ sang contentcache/,
        // 1 entry lỗi giữa chừng vẫn có thể để lại .delta_tmp_* mồ côi).
        ar_rmrf(g_moddedTempFolderC);

        bool extractedOK = allOK && filesWritten > 0;
        NSString *finalStatus = extractedOK ? DELTA_STATE_STATUS_COMPLETE : DELTA_STATE_STATUS_EXTRACTING;
        bool stateOK = ar_stateSave(g_deltaStatePlistPathC, finalStatus, g_moddedFolderNameC, &g_deltaZipStat);
        bool success = extractedOK && stateOK;
        DeltaVFS_debugLogf("runFirstRunExtraction: filesWritten=%u allOK=%d stateOK=%d success=%d", filesWritten, allOK, stateOK, success);
        if (success) {
            g_deltaActive.store(true, std::memory_order_relaxed);
            g_deltaNeedsFirstRun.store(false, std::memory_order_relaxed);
            @autoreleasepool {
                [[NSNotificationCenter defaultCenter] postNotificationName:DELTA_EXTRACTION_COMPLETE_NOTIFICATION object:nil];
            }
        }
        if (completion) dispatch_async(dispatch_get_main_queue(), ^{ completion(success); });
    });
}

// ============================================================================
//  PHẦN 2: ĐỊNH TUYẾN TRAFFIC (VFS HOOK CẤP THẤP)
// ============================================================================
#define ABHOTUPDATES_MARKER "/ABHotUpdates/"

static std::atomic<unsigned long long> g_abHotUpdatesHitCount{0};
static std::atomic<unsigned long long> g_abHotUpdatesMissCount{0};
inline unsigned long long DeltaVFS_abHotUpdatesHits()   { return g_abHotUpdatesHitCount.load(std::memory_order_relaxed); }
inline unsigned long long DeltaVFS_abHotUpdatesMisses() { return g_abHotUpdatesMissCount.load(std::memory_order_relaxed); }

inline const char *redirectABHotUpdatesPath(const char *path) {
    if (!path || g_moddedPrefixLen == 0) return NULL;
    const char *marker = strstr(path, ABHOTUPDATES_MARKER);
    if (!marker) return NULL;
    const char *relative = marker + 1;
    static thread_local char abBuffer[2048];
    int written = snprintf(abBuffer, sizeof(abBuffer), "%s%s", g_moddedPrefixC, relative);
    if (written < 0 || written >= (int)sizeof(abBuffer)) return NULL;
    return abBuffer;
}

// ============================================================================
//  CHẶN TUYỆT ĐỐI: watermark do dịch vụ ký lại IPA (esign.yyyue.xyz) tự nhét file vào GỐC bundle
//  (ngang hàng Info.plist) lúc ký - đã xác nhận có "SignedByEsign" (nội dung "Signed By Esign\n
//  ...\nhttps://esign.yyyue.xyz"), user báo thêm biến thể tên "SignByEsign" (khác tool/khác lần
//  ký, chưa lấy được nội dung file để xác nhận qua IDE - RAR temp folder đã dọn trước khi đọc
//  được). Đây là dấu vết lộ app không được ký bởi cert gốc Garena - SDK bên thứ 3 trong app (VD
//  DataDomeSDK.framework, chuyên chống gian lận/bot) có thể quét bundle root và phát hiện. Chặn
//  ĐỘC LẬP với DeltaVFS (không phụ thuộc g_deltaActive, g_bundlePrefixC đã sẵn sàng ngay trong
//  ar_ensureFirstRunChecked() TRƯỚC KHI hook cài xong) - luôn có hiệu lực ngay từ constructor, kể
//  cả ở process "lần đầu" popup rồi thoát. Danh sách tên bị chặn - thêm tên mới vào đây nếu phát
//  hiện thêm biến thể khác.
// ============================================================================
static const char *AR_ESIGN_MARKER_NAMES[] = {
    "SignedByEsign",
    "SignByEsign",
};
#define AR_ESIGN_MARKER_NAME_COUNT (sizeof(AR_ESIGN_MARKER_NAMES) / sizeof(AR_ESIGN_MARKER_NAMES[0]))

// Path chắc chắn không tồn tại trên đĩa - dùng làm "kết quả redirect" khi phát hiện path trỏ tới
// 1 trong các marker Esign, để CHÍNH orig_open/orig_stat/orig_access/orig_fopen thật tự báo lỗi
// ENOENT như file chưa từng có mặt. Không cần sửa riêng từng call site (hooked_open/openat/fopen/
// access/stat/lstat VÀ đường HWBreakHook.h cùng đi qua redirectAllTrafficPath() bên dưới) - 1 chỗ
// chặn duy nhất, không có nguy cơ quên 1 call site nào.
static const char *AR_ESIGN_BLOCKED_PATH = "/private/var/.ar_no_such_9f3ca1e0b6";

inline bool ar_isEsignMarkerPath(const char *path) {
    if (!path || g_bundlePrefixLen == 0) return false;
    char normalizedPathBuf[2048];
    const char *cmpPath = path;
    if (strncmp(path, "/var/", 5) == 0) {
        int n = snprintf(normalizedPathBuf, sizeof(normalizedPathBuf), "/private%s", path);
        if (n > 0 && n < (int)sizeof(normalizedPathBuf)) cmpPath = normalizedPathBuf;
    }
    if (strncmp(cmpPath, g_bundlePrefixC, g_bundlePrefixLen) != 0) return false;
    const char *relative = cmpPath + g_bundlePrefixLen;
    for (size_t i = 0; i < AR_ESIGN_MARKER_NAME_COUNT; i++) {
        if (strcmp(relative, AR_ESIGN_MARKER_NAMES[i]) == 0) return true;
    }
    return false;
}

// Có phải path này TRỎ ĐÚNG thư mục gốc bundle không (dùng để giới hạn hook readdir chỉ lọc tên
// file trong đúng thư mục chứa marker, không đụng tới readdir() của bất kỳ thư mục nào khác trong
// toàn app - tránh overhead/rủi ro không cần thiết). g_bundlePrefixC luôn có dấu "/" ở cuối (xem
// ar_ensureFirstRunChecked) nên chấp nhận cả 2 dạng "<bundle>" và "<bundle>/".
inline bool ar_pathIsBundleRoot(const char *path) {
    if (!path || g_bundlePrefixLen == 0) return false;
    char normalizedPathBuf[2048];
    const char *cmpPath = path;
    if (strncmp(path, "/var/", 5) == 0) {
        int n = snprintf(normalizedPathBuf, sizeof(normalizedPathBuf), "/private%s", path);
        if (n > 0 && n < (int)sizeof(normalizedPathBuf)) cmpPath = normalizedPathBuf;
    }
    size_t cmpLen = strlen(cmpPath);
    if (cmpLen == g_bundlePrefixLen) {
        return strncmp(cmpPath, g_bundlePrefixC, g_bundlePrefixLen) == 0;
    }
    if (cmpLen == g_bundlePrefixLen - 1) {
        // path không có dấu "/" cuối - so với prefix bỏ dấu "/" cuối
        return strncmp(cmpPath, g_bundlePrefixC, g_bundlePrefixLen - 1) == 0;
    }
    return false;
}

// DIR* đang mở TRÊN ĐÚNG thư mục gốc bundle - readdir() hook chỉ lọc entry "SignedByEsign" khi
// stream nằm trong danh sách này. 8 slot là quá đủ (bundle root hiếm khi có >1-2 lượt opendir()
// đang mở đồng thời).
#define AR_ESIGN_DIR_TRACK_MAX 8
static DIR *g_esignTrackedDirs[AR_ESIGN_DIR_TRACK_MAX] = {NULL};
static std::mutex g_esignTrackedDirsMutex;

inline const char* redirectAllTrafficPath(const char *path) {
    if (!path) return path;

    // CHẶN TUYỆT ĐỐI marker Esign - kiểm tra TRƯỚC TIÊN, không đếm vào thống kê DeltaVFS bình
    // thường (đây không phải hit/miss của bộ mod, là 1 lớp chặn riêng biệt).
    if (ar_isEsignMarkerPath(path)) return AR_ESIGN_BLOCKED_PATH;

    g_deltaTotalCalls.fetch_add(1, std::memory_order_relaxed);
    strncpy(g_deltaLastAnyPath, path, sizeof(g_deltaLastAnyPath) - 1);
    g_deltaLastAnyPath[sizeof(g_deltaLastAnyPath) - 1] = '\0';

    const char *abPath = redirectABHotUpdatesPath(path);
    if (abPath) {
        bool abExists = (orig_access && orig_access(abPath, F_OK) == 0);
        if (abExists) {
            g_abHotUpdatesHitCount.fetch_add(1, std::memory_order_relaxed);
            return abPath;
        }
        g_abHotUpdatesMissCount.fetch_add(1, std::memory_order_relaxed);
        return path; 
    }

    // ==== CỜ TEST TẠM THỜI - đặt 1 để tắt HẲN việc thay nội dung file qua Delta.zip. ĐÃ TEST
    // XONG (đặt 1, vào trận thật): VẪN bị đá giữa trận dù tắt hẳn redirect nội dung - loại trừ
    // luôn giả thuyết "nội dung file bị đổi" (xem AntiReportSpoof.h cho hướng đang thử tiếp
    // theo). Trả về 0 - khôi phục VFS hoạt động bình thường. ====
    #define AR_FORCE_DISABLE_VFS_FOR_TEST 0
    #if AR_FORCE_DISABLE_VFS_FOR_TEST
    return path;
    #endif
    #undef AR_FORCE_DISABLE_VFS_FOR_TEST

    if (g_bundlePrefixLen == 0 || g_moddedPrefixLen == 0) return path;

    // VFS chưa active (Delta.zip không có / build index thất bại) - không redirect gì cả, để
    // game đọc thẳng bundle gốc.
    if (!g_deltaActive.load(std::memory_order_relaxed)) return path;

    // Normalize "/var/..." to "/private/var/..." for comparison ONLY - iOS symlinks /var to
    // /private/var, and g_bundlePrefixC was captured from bundlePath which always returns the
    // /private/var form, but not every caller is consistent about which form it opens files
    // with. A bare strncmp against the /private/var-anchored prefix silently misses (and thus
    // never redirects) any request that happens to use the plain /var/... form - a real leak,
    // e.g. a self-integrity check reading its own binary via a differently-normalized path.
    char normalizedPathBuf[2048];
    const char *cmpPath = path;
    if (strncmp(path, "/var/", 5) == 0) {
        int n = snprintf(normalizedPathBuf, sizeof(normalizedPathBuf), "/private%s", path);
        if (n > 0 && n < (int)sizeof(normalizedPathBuf)) cmpPath = normalizedPathBuf;
    }

    if (strncmp(cmpPath, g_bundlePrefixC, g_bundlePrefixLen) != 0) {
        return path;
    }
    g_deltaBundleCalls.fetch_add(1, std::memory_order_relaxed);

    if (strncmp(cmpPath, g_moddedPrefixC, g_moddedPrefixLen) == 0) {
        return path;
    }

    static thread_local char redirectedBuffer[2048];
    const char *relative = cmpPath + g_bundlePrefixLen;

    // KHÔNG còn blocklist nào ở đây nữa (versioninfo/fileinfo, Frameworks/ đã bỏ theo yêu cầu
    // CHỦ Ý của user - CHẤP NHẬN RỦI RO đã biết trước, không phải quên): Frameworks/ (VD:
    // UnityFramework.framework/UnityFramework) từng gây màn hình "tài khoản bị khoá" trên máy
    // thật khi redirect (REDIRECTED FILES xác nhận UnityFramework bị đọc lặp lại từ Delta đúng
    // lúc lỗi đó hiện ra), và Data/Raw/ios/versioninfo+fileinfo từng gây "hotfix: SaveFailed".
    // Redirect TOÀN BỘ mọi thứ có mặt trong Delta.zip, không ngoại lệ.

    int written = snprintf(redirectedBuffer, sizeof(redirectedBuffer), "%s%s", g_moddedPrefixC, relative);
    if (written < 0 || written >= (int)sizeof(redirectedBuffer)) {
        return path;
    }

    // Luôn kiểm tra Delta TRƯỚC: CÓ thì redirect (hit); KHÔNG có thì trả về bản gốc trong bundle
    // (miss - vẫn hoàn toàn bình thường, file này chỉ đơn giản không thuộc bộ mod); bị xoá/đổi tên
    // giữa session dù trước đó VFS đã xác nhận đủ file (TAMPERED) thì vẫn chặn hẳn (không tự vá,
    // không fallback bản gốc) - xem comment ở ar_extractOneEntryIfNeeded.
    ArEntryStatus st = ar_extractOneEntryIfNeeded(redirectedBuffer, relative);
    if (st == AR_ENTRY_PRESENT) {
        g_deltaHitCount.fetch_add(1, std::memory_order_relaxed);
        strncpy(g_deltaLastHitPath, relative, sizeof(g_deltaLastHitPath) - 1);
        g_deltaLastHitPath[sizeof(g_deltaLastHitPath) - 1] = '\0';
        deltaHitRingPush(relative);
        return redirectedBuffer;
    }

    g_deltaMissCount.fetch_add(1, std::memory_order_relaxed);
    if (st == AR_ENTRY_TAMPERED) {
        char tamperedLabel[210];
        snprintf(tamperedLabel, sizeof(tamperedLabel), "[TAMPERED-MISS] %s", relative);
        deltaHitRingPush(tamperedLabel);
        return redirectedBuffer;
    }

    // NOT_MODDED: file này không có trong Delta.zip - ghi vào ring MISS riêng để tab INFO thấy
    // được đúng những file nào đang bị bỏ sót (không thuộc bộ mod), rồi trả về path gốc để game đọc
    // thẳng bundle như chưa hề bị redirect.
    deltaMissRingPush(relative);
    return path;
}

// HWBreakHook.h TỪNG là thử nghiệm hook open() bằng hardware breakpoint - ĐÃ BỎ HẲN (đo được
// chậm hơn fishhook ~100-300 lần, gây lỗi "hotfix: SaveFailed" thật, và xác nhận qua Ghidra rằng
// chính Monite - nguồn cảm hứng ban đầu - cũng KHÔNG dùng kỹ thuật này cho open(), xem đầu file
// đó). Giờ chỉ còn CrashLogger_install() - dùng lại đúng hạ tầng Mach exception-port cho 1 việc
// hợp lý hơn: tự dump thanh ghi lúc crash thật (hiếm, không có vấn đề tần suất/độ trễ). Đặt include
// ở đây (không phải đầu file) vì file đó gọi thẳng DeltaVFS_debugLog*() vừa định nghĩa ở trên.
#import "HWBreakHook.h"
#import "DylibHide.h"

// ============================================================================
//  CHẶN GHI vào thư mục cache THẬT của game (contentcache/ImageCache/Workshop trong Documents/,
//  quan sát thấy qua Files app) - đây là cache do CHÍNH Unity/FreeFire tự tạo để tải asset/ảnh về
//  từ server thật của họ, KHÔNG liên quan gì tới Delta VFS/Monite. Chỉ chặn GHI (open với cờ tạo
//  mới/ghi/truncate/append, fopen mode có "w"/"a"/"+") - ĐỌC vẫn cho qua bình thường, để file đã
//  cache từ trước (avatar/icon/asset đã tải) vẫn dùng được, tránh vỡ tính năng thật đang phụ thuộc
//  vào nội dung đã có sẵn. Trả lỗi EROFS (giống ổ đĩa chỉ đọc) thay vì thật sự gọi open()/fopen().
// ============================================================================
static const char *AR_GAME_CACHE_BLOCKED_NAMES[] = {
    "contentcache",
    "ImageCache",
    "Workshop",
};
#define AR_GAME_CACHE_BLOCKED_NAME_COUNT (sizeof(AR_GAME_CACHE_BLOCKED_NAMES) / sizeof(AR_GAME_CACHE_BLOCKED_NAMES[0]))

inline bool ar_isUnderGameCacheFolder(const char *path) {
    if (!path || g_documentsPrefixLen == 0) return false;
    char normalizedPathBuf[2048];
    const char *cmpPath = path;
    if (strncmp(path, "/var/", 5) == 0) {
        int n = snprintf(normalizedPathBuf, sizeof(normalizedPathBuf), "/private%s", path);
        if (n > 0 && n < (int)sizeof(normalizedPathBuf)) cmpPath = normalizedPathBuf;
    }
    if (strncmp(cmpPath, g_documentsPrefixC, g_documentsPrefixLen) != 0) return false;
    const char *relative = cmpPath + g_documentsPrefixLen;
    for (size_t i = 0; i < AR_GAME_CACHE_BLOCKED_NAME_COUNT; i++) {
        size_t nameLen = strlen(AR_GAME_CACHE_BLOCKED_NAMES[i]);
        if (strncmp(relative, AR_GAME_CACHE_BLOCKED_NAMES[i], nameLen) == 0 &&
            (relative[nameLen] == '/' || relative[nameLen] == '\0')) {
            return true;
        }
    }
    return false;
}

inline bool ar_isWriteIntentOpenFlags(int oflag) {
    return (oflag & (O_WRONLY | O_RDWR | O_CREAT | O_TRUNC | O_APPEND)) != 0;
}

inline bool ar_isWriteIntentFopenMode(const char *mode) {
    if (!mode) return false;
    for (const char *p = mode; *p; p++) {
        if (*p == 'w' || *p == 'a' || *p == '+') return true;
    }
    return false;
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
    if (ar_isWriteIntentOpenFlags(oflag) && ar_isUnderGameCacheFolder(path)) {
        errno = EROFS;
        return -1;
    }
    const char *redirected = redirectAllTrafficPath(path);
    return orig_open(redirected, oflag, mode);
}

// openat() is a separate libc entry point from open() (dirfd + possibly-relative path) - some
// libraries/newer code use it instead of open() for the exact same effect, and it was NOT being
// hooked at all, so a bundle-relative path opened this way slipped straight through to the real
// file. Only redirect when path is absolute (POSIX: openat() ignores dirfd for absolute paths,
// same as open() - our redirect logic only ever matches absolute bundle paths anyway); for a
// relative path we'd need to resolve dirfd's own path first (fcntl F_GETPATH) to know what it's
// relative to, which isn't worth the complexity/risk for paths we don't expect to care about.
static int (*orig_openat)(int, const char *, int, ...);
inline int hooked_openat(int dirfd, const char *path, int oflag, ...) {
    mode_t mode = 0;
    if (oflag & O_CREAT) {
        va_list args;
        va_start(args, oflag);
        mode = (mode_t)va_arg(args, int);
        va_end(args);
    }
    if (path && path[0] == '/' && ar_isWriteIntentOpenFlags(oflag) && ar_isUnderGameCacheFolder(path)) {
        errno = EROFS;
        return -1;
    }
    const char *redirected = (path && path[0] == '/') ? redirectAllTrafficPath(path) : path;
    return orig_openat(dirfd, redirected, oflag, mode);
}

static FILE *(*orig_fopen)(const char *, const char *);
inline FILE *hooked_fopen(const char *filename, const char *mode) {
    if (ar_isWriteIntentFopenMode(mode) && ar_isUnderGameCacheFolder(filename)) {
        errno = EROFS;
        return NULL;
    }
    const char *redirected = redirectAllTrafficPath(filename);
    return orig_fopen(redirected, mode);
}

inline int hooked_access(const char *path, int mode) {
    const char *redirected = redirectAllTrafficPath(path);
    return orig_access(redirected, mode);
}

// orig_stat đã khai báo sớm hơn ở đầu file (xem comment cạnh orig_access) - dùng lại ở đây.
inline int hooked_stat(const char *path, struct stat *buf) {
    const char *redirected = redirectAllTrafficPath(path);
    return orig_stat(redirected, buf);
}

static int (*orig_lstat)(const char *, struct stat *);
inline int hooked_lstat(const char *path, struct stat *buf) {
    const char *redirected = redirectAllTrafficPath(path);
    return orig_lstat(redirected, buf);
}

// opendir/readdir/closedir - lớp chặn thứ 2 cho marker Esign: redirectAllTrafficPath() ở trên chỉ
// che được các API ĐỌC/KIỂM TRA TRỰC TIẾP đường dẫn (open/stat/access/...), không che được việc
// -[NSFileManager contentsOfDirectoryAtPath:error:]/-enumeratorAtPath: (và bất kỳ code C nào tự
// opendir()+readdir() gốc bundle) LIỆT KÊ thư mục rồi thấy tên "SignedByEsign" xuất hiện trong
// kết quả dù chưa hề mở file đó. Chỉ theo dõi/lọc đúng thư mục gốc bundle (ar_pathIsBundleRoot) -
// mọi opendir() khác trong toàn app đi qua orig_readdir nguyên bản, không tốn thêm chi phí.
static DIR *(*orig_opendir)(const char *);
inline DIR *hooked_opendir(const char *path) {
    DIR *d = orig_opendir(path);
    if (d && ar_pathIsBundleRoot(path)) {
        std::lock_guard<std::mutex> lock(g_esignTrackedDirsMutex);
        for (int i = 0; i < AR_ESIGN_DIR_TRACK_MAX; i++) {
            if (!g_esignTrackedDirs[i]) { g_esignTrackedDirs[i] = d; break; }
        }
    }
    return d;
}

static int (*orig_closedir)(DIR *);
inline int hooked_closedir(DIR *dir) {
    {
        std::lock_guard<std::mutex> lock(g_esignTrackedDirsMutex);
        for (int i = 0; i < AR_ESIGN_DIR_TRACK_MAX; i++) {
            if (g_esignTrackedDirs[i] == dir) { g_esignTrackedDirs[i] = NULL; break; }
        }
    }
    return orig_closedir(dir);
}

static struct dirent *(*orig_readdir)(DIR *);
inline struct dirent *hooked_readdir(DIR *dir) {
    bool tracked = false;
    {
        std::lock_guard<std::mutex> lock(g_esignTrackedDirsMutex);
        for (int i = 0; i < AR_ESIGN_DIR_TRACK_MAX; i++) {
            if (g_esignTrackedDirs[i] == dir) { tracked = true; break; }
        }
    }
    struct dirent *ent;
    while ((ent = orig_readdir(dir)) != NULL) {
        if (tracked) {
            bool isMarker = false;
            for (size_t i = 0; i < AR_ESIGN_MARKER_NAME_COUNT; i++) {
                if (strcmp(ent->d_name, AR_ESIGN_MARKER_NAMES[i]) == 0) { isMarker = true; break; }
            }
            if (isMarker) continue;
        }
        break;
    }
    return ent;
}


// ============================================================================
//  PHẦN 3: METHOD SWIZZLING FOR NSBUNDLE (BẺ HƯỚNG BỘ NHỚ RAM TRÊN API CAO CẤP)
// ============================================================================
typedef NSDictionary* (*ORIG_infoDictionary)(id, SEL);
static ORIG_infoDictionary orig_nsbundle_infoDictionary = NULL;

// Shared by the -[NSBundle infoDictionary] swizzle below AND the CFBundleGet...() C-API hooks
// further down - CFBundleGetValueForInfoDictionaryKey/CFBundleGetInfoDictionary read Info.plist
// through CoreFoundation's OWN internal bundle cache, a COMPLETELY SEPARATE path from the
// Objective-C method we swizzle. Native/C++ code (common in Unity plugins) calling the CF API
// directly would silently read the real, unmodified Info.plist otherwise - confirmed as a real
// gap, not hypothetical, same class of bug as the openat() one above.
inline NSDictionary *deltaFakeInfoPlistDictionary() {
    static NSDictionary *fakeDict = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        // Dựng đường dẫn tuyệt đối tới file Info.plist sạch nằm trong g_moddedPrefixC (Delta/
        // ở Documents/<hash> - KHÔNG còn nằm trong bundle nữa, xem ar_ensureFirstRunChecked).
        NSString *deltaPlistPath = g_moddedPrefixLen > 0
            ? [[NSString stringWithUTF8String:g_moddedPrefixC] stringByAppendingString:@"Info.plist"]
            : nil;
        fakeDict = [[NSDictionary alloc] initWithContentsOfFile:deltaPlistPath];
    });
    return fakeDict; // nil if Delta/Info.plist is missing/unreadable - callers fall back to orig
}

static NSDictionary* hooked_nsbundle_infoDictionary(id self, SEL _cmd) {
    if (self == [NSBundle mainBundle]) {
        NSDictionary *fakeDict = deltaFakeInfoPlistDictionary();
        return fakeDict ?: orig_nsbundle_infoDictionary(self, _cmd);
    }
    return orig_nsbundle_infoDictionary(self, _cmd);
}

typedef id (*ORIG_objectForKey)(id, SEL, NSString*);
static ORIG_objectForKey orig_nsbundle_objectForKey = NULL;

static id hooked_nsbundle_objectForInfoDictionaryKey(id self, SEL _cmd, NSString *key) {
    if (self == [NSBundle mainBundle]) {
        // Đồng bộ trả về dữ liệu lấy trực tiếp từ Hàm Hook infoDictionary ở trên
        NSDictionary *fakeDict = hooked_nsbundle_infoDictionary(self, @selector(infoDictionary));
        return [fakeDict objectForKey:key];
    }
    return orig_nsbundle_objectForKey(self, _cmd, key);
}

// ===== CFBundle C-API - separate entry points from the ObjC swizzle above, see comment on
// deltaFakeInfoPlistDictionary(). Hooked via fishhook (like open/openat/etc), not method swizzling,
// since these are plain C functions exported by CoreFoundation. =====
typedef CFDictionaryRef (*ORIG_CFBundleGetInfoDictionary)(CFBundleRef);
static ORIG_CFBundleGetInfoDictionary orig_CFBundleGetInfoDictionary = NULL;
inline CFDictionaryRef hooked_CFBundleGetInfoDictionary(CFBundleRef bundle) {
    if (bundle == CFBundleGetMainBundle()) {
        NSDictionary *fakeDict = deltaFakeInfoPlistDictionary();
        if (fakeDict) return (__bridge CFDictionaryRef)fakeDict;
    }
    return orig_CFBundleGetInfoDictionary(bundle);
}

typedef CFTypeRef (*ORIG_CFBundleGetValueForInfoDictionaryKey)(CFBundleRef, CFStringRef);
static ORIG_CFBundleGetValueForInfoDictionaryKey orig_CFBundleGetValueForInfoDictionaryKey = NULL;
inline CFTypeRef hooked_CFBundleGetValueForInfoDictionaryKey(CFBundleRef bundle, CFStringRef key) {
    if (bundle == CFBundleGetMainBundle()) {
        NSDictionary *fakeDict = deltaFakeInfoPlistDictionary();
        id value = fakeDict ? [fakeDict objectForKey:(__bridge NSString *)key] : nil;
        if (value) return (__bridge CFTypeRef)value;
    }
    return orig_CFBundleGetValueForInfoDictionaryKey(bundle, key);
}

static void initNSBundleMethodSwizzling() {
    Class nsBundleClass = [NSBundle class];
    
    // 1. Swizzle -[NSBundle infoDictionary]
    Method origMethod1 = class_getInstanceMethod(nsBundleClass, @selector(infoDictionary));
    if (origMethod1) {
        orig_nsbundle_infoDictionary = (ORIG_infoDictionary)method_getImplementation(origMethod1);
        method_setImplementation(origMethod1, (IMP)hooked_nsbundle_infoDictionary);
    }
    
    // 2. Swizzle -[NSBundle objectForInfoDictionaryKey:]
    Method origMethod2 = class_getInstanceMethod(nsBundleClass, @selector(objectForInfoDictionaryKey:));
    if (origMethod2) {
        orig_nsbundle_objectForKey = (ORIG_objectForKey)method_getImplementation(origMethod2);
        method_setImplementation(origMethod2, (IMP)hooked_nsbundle_objectForInfoDictionaryKey);
    }
}

// ============================================================================
//  PHẦN 4: COCOA-SWIZZLE VFS REDIRECT - thay cho fishhook trên open/openat/fopen/access/stat/
//  lstat/opendir/closedir/readdir (đã bỏ khỏi rebind_symbols, xem constructor bên dưới).
//
//  LÝ DO: soi Monite.dylib (MoniteAnalysis/README.md mục 3) + đối chiếu với enum thật
//  EHacker.HackerPoolCdt (dump.cs, "PMS_HOOK" = 7) cho thấy Free Fire CÓ cơ chế dò hook tiến
//  trình - fishhook rebind GOT của open/fopen/stat/access (bị gọi hàng nghìn lần/giây) là bề mặt
//  dễ bị soi hơn hẳn 2 rebind CFBundle* (hiếm khi gọi, vẫn giữ fishhook cho 2 cái đó vì KHÔNG có
//  cách swizzle Cocoa cho C-API thuần). Monite tự thấy dùng đúng kỹ thuật này (swizzle tầng
//  NSBundle/NSFileManager) thay vì hook libc.
//
//  RỦI RO ĐÃ BIẾT TRƯỚC, CHẤP NHẬN (không phải quên): swizzle Cocoa chỉ chặn được code đi qua
//  Objective-C runtime. Nếu UnityFramework (code C/C++ biên dịch sẵn) tự gọi fopen()/open() thẳng
//  với 1 đường dẫn tự dựng - KHÔNG qua bất kỳ API Cocoa nào - request đó lọt qua hoàn toàn, không
//  bị redirect. Đây chính là lý do Monite tự crash sau khi giải nén (mục 3 README) - áp dụng kỹ
//  thuật của họ nghĩa là chấp nhận rủi ro y hệt cho phần asset chính, đổi lấy dấu vết hook cấp
//  thấp nhỏ hơn. Quyết định đã được xác nhận rõ với user trước khi code phần này.
// ============================================================================

// Dùng lại redirectAllTrafficPath() (đã viết cho fishhook, không đổi logic bên trong) cho path
// dạng NSString - convert 1 chiều C-string -> NSString ngay sau khi gọi, không giữ con trỏ
// redirectedBuffer (thread_local, bị ghi đè ở lần gọi kế tiếp) qua khỏi câu lệnh này.
inline NSString *ar_redirectNSString(NSString *path) {
    if (!path) return path;
    const char *redirected = redirectAllTrafficPath(path.fileSystemRepresentation);
    if (!redirected) return path;
    return [NSString stringWithUTF8String:redirected];
}

// ---- 1. NSBundle pathForResource:ofType:[inDirectory:] - đường lookup asset chính của Unity khi
// CÓ đi qua Cocoa. Redirect kết quả gốc nếu tìm thấy; nếu NSBundle không tìm thấy trong bundle
// thật (file CHỈ có trong Delta.zip, chưa từng có trong IPA gốc), tự dựng lại path ứng viên rồi
// thử redirect - khớp đúng ý "redirect TOÀN BỘ nội dung Delta.zip, không ngoại lệ" đã ghi ở
// redirectAllTrafficPath(), không chỉ replace file đã có sẵn.
typedef NSString* (*ORIG_pathForResourceOfType)(id, SEL, NSString*, NSString*);
static ORIG_pathForResourceOfType orig_nsbundle_pathForResourceOfType = NULL;
static NSString *hooked_nsbundle_pathForResourceOfType(id self, SEL _cmd, NSString *name, NSString *ext) {
    NSString *orig = orig_nsbundle_pathForResourceOfType(self, _cmd, name, ext);
    if (orig) return ar_redirectNSString(orig);
    if (![self isKindOfClass:[NSBundle class]] || name.length == 0) return orig;
    NSString *resourcePath = [(NSBundle *)self resourcePath];
    if (!resourcePath) return orig;
    NSString *fileName = (ext.length > 0) ? [name stringByAppendingPathExtension:ext] : name;
    NSString *candidate = [resourcePath stringByAppendingPathComponent:fileName];
    const char *redirected = redirectAllTrafficPath(candidate.fileSystemRepresentation);
    if (redirected && orig_access && orig_access(redirected, F_OK) == 0) {
        return [NSString stringWithUTF8String:redirected];
    }
    return orig;
}

typedef NSString* (*ORIG_pathForResourceOfTypeInDirectory)(id, SEL, NSString*, NSString*, NSString*);
static ORIG_pathForResourceOfTypeInDirectory orig_nsbundle_pathForResourceOfTypeInDirectory = NULL;
static NSString *hooked_nsbundle_pathForResourceOfTypeInDirectory(id self, SEL _cmd, NSString *name, NSString *ext, NSString *subpath) {
    NSString *orig = orig_nsbundle_pathForResourceOfTypeInDirectory(self, _cmd, name, ext, subpath);
    if (orig) return ar_redirectNSString(orig);
    if (![self isKindOfClass:[NSBundle class]] || name.length == 0) return orig;
    NSString *resourcePath = [(NSBundle *)self resourcePath];
    if (!resourcePath) return orig;
    NSString *fileName = (ext.length > 0) ? [name stringByAppendingPathExtension:ext] : name;
    NSString *dir = subpath.length > 0 ? [resourcePath stringByAppendingPathComponent:subpath] : resourcePath;
    NSString *candidate = [dir stringByAppendingPathComponent:fileName];
    const char *redirected = redirectAllTrafficPath(candidate.fileSystemRepresentation);
    if (redirected && orig_access && orig_access(redirected, F_OK) == 0) {
        return [NSString stringWithUTF8String:redirected];
    }
    return orig;
}

// ---- 2. NSFileManager fileExistsAtPath:[isDirectory:] - redirect path truyền vào TRƯỚC KHI hỏi
// hệ thống thật (esign marker cũng tự bị chặn ở đây vì redirectAllTrafficPath() check nó trước
// tiên, trả path không tồn tại).
typedef BOOL (*ORIG_fileExistsAtPath)(id, SEL, NSString*);
static ORIG_fileExistsAtPath orig_nsfm_fileExistsAtPath = NULL;
static BOOL hooked_nsfm_fileExistsAtPath(id self, SEL _cmd, NSString *path) {
    return orig_nsfm_fileExistsAtPath(self, _cmd, ar_redirectNSString(path));
}

typedef BOOL (*ORIG_fileExistsAtPathIsDirectory)(id, SEL, NSString*, BOOL*);
static ORIG_fileExistsAtPathIsDirectory orig_nsfm_fileExistsAtPathIsDirectory = NULL;
static BOOL hooked_nsfm_fileExistsAtPathIsDirectory(id self, SEL _cmd, NSString *path, BOOL *isDirectory) {
    return orig_nsfm_fileExistsAtPathIsDirectory(self, _cmd, ar_redirectNSString(path), isDirectory);
}

// ---- 3. NSFileManager contentsOfDirectoryAtPath:error: - redirect thư mục được liệt kê, VÀ lọc
// tên marker Esign khỏi kết quả khi liệt kê đúng thư mục gốc bundle - đây là bản Cocoa của lớp
// chặn thứ 2 mà hooked_opendir/hooked_readdir/hooked_closedir từng làm ở tầng libc (xem comment
// gốc ở đó): redirectAllTrafficPath() chỉ che được request ĐỌC/KIỂM TRA trực tiếp 1 path, không
// che được việc liệt kê thư mục rồi thấy tên marker xuất hiện trong danh sách.
typedef NSArray* (*ORIG_contentsOfDirectoryAtPath)(id, SEL, NSString*, NSError**);
static ORIG_contentsOfDirectoryAtPath orig_nsfm_contentsOfDirectoryAtPath = NULL;
static NSArray *hooked_nsfm_contentsOfDirectoryAtPath(id self, SEL _cmd, NSString *path, NSError **error) {
    bool isBundleRoot = path && ar_pathIsBundleRoot(path.fileSystemRepresentation);
    NSArray *result = orig_nsfm_contentsOfDirectoryAtPath(self, _cmd, ar_redirectNSString(path), error);
    if (!isBundleRoot || !result) return result;
    NSMutableArray *filtered = [NSMutableArray arrayWithCapacity:result.count];
    for (NSString *entry in result) {
        bool isMarker = false;
        for (size_t i = 0; i < AR_ESIGN_MARKER_NAME_COUNT; i++) {
            if ([entry isEqualToString:[NSString stringWithUTF8String:AR_ESIGN_MARKER_NAMES[i]]]) { isMarker = true; break; }
        }
        if (!isMarker) [filtered addObject:entry];
    }
    return filtered;
}

// ---- 4. NSData dataWithContentsOfFile:[options:error:] - Unity's C# side (khác UnityFramework
// native code) hay dùng đường này để đọc asset qua managed code, đi qua Cocoa runtime bình
// thường nên swizzle được.
typedef NSData* (*ORIG_dataWithContentsOfFile)(id, SEL, NSString*);
static ORIG_dataWithContentsOfFile orig_nsdata_dataWithContentsOfFile = NULL;
static NSData *hooked_nsdata_dataWithContentsOfFile(id self, SEL _cmd, NSString *path) {
    return orig_nsdata_dataWithContentsOfFile(self, _cmd, ar_redirectNSString(path));
}

typedef NSData* (*ORIG_dataWithContentsOfFileOptionsError)(id, SEL, NSString*, NSUInteger, NSError**);
static ORIG_dataWithContentsOfFileOptionsError orig_nsdata_dataWithContentsOfFileOptionsError = NULL;
static NSData *hooked_nsdata_dataWithContentsOfFileOptionsError(id self, SEL _cmd, NSString *path, NSUInteger options, NSError **error) {
    return orig_nsdata_dataWithContentsOfFileOptionsError(self, _cmd, ar_redirectNSString(path), options, error);
}

// ---- 5. Bản Cocoa của "chặn ghi vào cache thật của game" (xem ar_isUnderGameCacheFolder ở trên,
// trước đây nằm trong hooked_open/hooked_openat/hooked_fopen) - NSData writeToFile:.../
// NSFileManager createFileAtPath:... là 2 đường ghi file phổ biến nhất từ code Objective-C/Unity
// managed, chặn tại đây để không mất tính năng khi bỏ fishhook.
typedef BOOL (*ORIG_writeToFileAtomically)(id, SEL, NSString*, BOOL);
static ORIG_writeToFileAtomically orig_nsdata_writeToFileAtomically = NULL;
static BOOL hooked_nsdata_writeToFileAtomically(id self, SEL _cmd, NSString *path, BOOL atomically) {
    if (path && ar_isUnderGameCacheFolder(path.fileSystemRepresentation)) return NO;
    return orig_nsdata_writeToFileAtomically(self, _cmd, path, atomically);
}

typedef BOOL (*ORIG_writeToFileOptionsError)(id, SEL, NSString*, NSUInteger, NSError**);
static ORIG_writeToFileOptionsError orig_nsdata_writeToFileOptionsError = NULL;
static BOOL hooked_nsdata_writeToFileOptionsError(id self, SEL _cmd, NSString *path, NSUInteger options, NSError **error) {
    if (path && ar_isUnderGameCacheFolder(path.fileSystemRepresentation)) {
        if (error) *error = [NSError errorWithDomain:NSPOSIXErrorDomain code:EROFS userInfo:nil];
        return NO;
    }
    return orig_nsdata_writeToFileOptionsError(self, _cmd, path, options, error);
}

typedef BOOL (*ORIG_createFileAtPath)(id, SEL, NSString*, NSData*, NSDictionary*);
static ORIG_createFileAtPath orig_nsfm_createFileAtPath = NULL;
static BOOL hooked_nsfm_createFileAtPath(id self, SEL _cmd, NSString *path, NSData *contents, NSDictionary *attributes) {
    if (path && ar_isUnderGameCacheFolder(path.fileSystemRepresentation)) return NO;
    return orig_nsfm_createFileAtPath(self, _cmd, path, contents, attributes);
}

// __attribute__((unused)) - không còn được gọi (đã quay lại fishhook, xem constructor bên dưới)
// nhưng giữ nguyên định nghĩa để dễ khôi phục - -Werror,-Wunused-function sẽ chặn build nếu
// không đánh dấu rõ đây là cố ý giữ lại, không phải code chết quên xoá.
static void __attribute__((unused)) initCocoaVFSRedirectSwizzling() {
    Class nsBundleClass = [NSBundle class];
    Class nsFileManagerClass = [NSFileManager class];
    Class nsDataClass = [NSData class];

    Method m1 = class_getInstanceMethod(nsBundleClass, @selector(pathForResource:ofType:));
    if (m1) {
        orig_nsbundle_pathForResourceOfType = (ORIG_pathForResourceOfType)method_getImplementation(m1);
        method_setImplementation(m1, (IMP)hooked_nsbundle_pathForResourceOfType);
    }
    Method m2 = class_getInstanceMethod(nsBundleClass, @selector(pathForResource:ofType:inDirectory:));
    if (m2) {
        orig_nsbundle_pathForResourceOfTypeInDirectory = (ORIG_pathForResourceOfTypeInDirectory)method_getImplementation(m2);
        method_setImplementation(m2, (IMP)hooked_nsbundle_pathForResourceOfTypeInDirectory);
    }
    Method m3 = class_getInstanceMethod(nsFileManagerClass, @selector(fileExistsAtPath:));
    if (m3) {
        orig_nsfm_fileExistsAtPath = (ORIG_fileExistsAtPath)method_getImplementation(m3);
        method_setImplementation(m3, (IMP)hooked_nsfm_fileExistsAtPath);
    }
    Method m4 = class_getInstanceMethod(nsFileManagerClass, @selector(fileExistsAtPath:isDirectory:));
    if (m4) {
        orig_nsfm_fileExistsAtPathIsDirectory = (ORIG_fileExistsAtPathIsDirectory)method_getImplementation(m4);
        method_setImplementation(m4, (IMP)hooked_nsfm_fileExistsAtPathIsDirectory);
    }
    Method m5 = class_getInstanceMethod(nsFileManagerClass, @selector(contentsOfDirectoryAtPath:error:));
    if (m5) {
        orig_nsfm_contentsOfDirectoryAtPath = (ORIG_contentsOfDirectoryAtPath)method_getImplementation(m5);
        method_setImplementation(m5, (IMP)hooked_nsfm_contentsOfDirectoryAtPath);
    }
    Method m6 = class_getClassMethod(nsDataClass, @selector(dataWithContentsOfFile:));
    if (m6) {
        orig_nsdata_dataWithContentsOfFile = (ORIG_dataWithContentsOfFile)method_getImplementation(m6);
        method_setImplementation(m6, (IMP)hooked_nsdata_dataWithContentsOfFile);
    }
    Method m7 = class_getClassMethod(nsDataClass, @selector(dataWithContentsOfFile:options:error:));
    if (m7) {
        orig_nsdata_dataWithContentsOfFileOptionsError = (ORIG_dataWithContentsOfFileOptionsError)method_getImplementation(m7);
        method_setImplementation(m7, (IMP)hooked_nsdata_dataWithContentsOfFileOptionsError);
    }
    Method m8 = class_getInstanceMethod(nsDataClass, @selector(writeToFile:atomically:));
    if (m8) {
        orig_nsdata_writeToFileAtomically = (ORIG_writeToFileAtomically)method_getImplementation(m8);
        method_setImplementation(m8, (IMP)hooked_nsdata_writeToFileAtomically);
    }
    Method m9 = class_getInstanceMethod(nsDataClass, @selector(writeToFile:options:error:));
    if (m9) {
        orig_nsdata_writeToFileOptionsError = (ORIG_writeToFileOptionsError)method_getImplementation(m9);
        method_setImplementation(m9, (IMP)hooked_nsdata_writeToFileOptionsError);
    }
    Method m10 = class_getInstanceMethod(nsFileManagerClass, @selector(createFileAtPath:contents:attributes:));
    if (m10) {
        orig_nsfm_createFileAtPath = (ORIG_createFileAtPath)method_getImplementation(m10);
        method_setImplementation(m10, (IMP)hooked_nsfm_createFileAtPath);
    }
    DeltaVFS_debugLog("initCocoaVFSRedirectSwizzling: da swizzle xong NSBundle/NSFileManager/NSData - thay cho fishhook open/fopen/stat/access/opendir ho (xem comment PHAN 4)");
}


// ============================================================================
//  CONSTRUCTOR KÍCH HOẠT HỆ THỐNG
// ============================================================================
__attribute__((constructor))
static void initDeltaAllTrafficVFS() {
    // 0. GIẤU DYLIB khỏi 4 API liệt kê dyld image (xem DylibHide.h) - làm TRƯỚC TIÊN, trước cả
    // VFS/Esign/crash-logger, để có cơ hội tốt nhất né sớm bất kỳ lượt quét image nào của game
    // (kể cả từ +load của chính SDK bên thứ 3 khác chạy trước constructor này).
    DylibHide_install();

    @autoreleasepool {
        // 1. KIỂM TRA CÓ CẦN GIẢI NÉN LẦN ĐẦU KHÔNG (bulk, xem PHẦN 1) - nhanh, không giải nén
        // gì ở đây cả. Idempotent, an toàn gọi từ nhiều nơi/nhiều thứ tự (constructor lẫn
        // Menu.mm's +load).
        ar_ensureFirstRunChecked();

        // 2. KÍCH HOẠT HOOK CẤP CAO TRÊN RAM (NSBundle Swizzling - Info.plist spoof)
        initNSBundleMethodSwizzling();

        // 2b. initCocoaVFSRedirectSwizzling() ĐÃ BỎ GỌI - quay lại fishhook cho VFS redirect
        // (rebindings[] bên dưới, mục 3) theo yêu cầu test lại - Cocoa-swizzle redirect được ít
        // file hơn hẳn fishhook (xác nhận qua tab INFO: chỉ thấy "FreeFire" thay vì cả danh
        // sách) và KHÔNG giải quyết được vụ bị đá giữa trận dù đã thử (xem AntiReportSpoof.h/
        // PacketCapture.h). Giữ nguyên định nghĩa initCocoaVFSRedirectSwizzling()/PHẦN 4 để dễ
        // quay lại nếu cần, chỉ đơn giản không gọi nữa.
    }

    bool needsFirstRun = g_deltaNeedsFirstRun.load(std::memory_order_relaxed);

    // 3. orig_open/orig_openat/orig_fopen/orig_access/orig_stat/orig_lstat/orig_opendir/
    // orig_closedir/orig_readdir vẫn resolve qua dlsym (KHÔNG rebind_symbols nữa, xem rebindings[]
    // bên dưới chỉ còn 2 CFBundle) - đơn giản là con trỏ TỚI hàm libc thật, không ai patch chúng
    // nữa. Vẫn cần giữ: hooked_open/hooked_openat/hooked_fopen/... còn định nghĩa (dead code, xem
    // PHẦN 4's comment) tham chiếu tới orig_*, và orig_access CÒN ĐƯỢC DÙNG TRỰC TIẾP làm hàm
    // access() thật trong 2 hàm hooked_nsbundle_pathForResourceOfType[InDirectory] ở PHẦN 4.
    orig_open   = (int   (*)(const char *, int, ...))     dlsym((void *)RTLD_DEFAULT, "open");
    orig_openat = (int   (*)(int, const char *, int, ...))dlsym((void *)RTLD_DEFAULT, "openat");
    orig_fopen  = (FILE *(*)(const char *, const char *)) dlsym((void *)RTLD_DEFAULT, "fopen");
    orig_access = (int   (*)(const char *, int))          dlsym((void *)RTLD_DEFAULT, "access");
    orig_stat   = (int   (*)(const char *, struct stat *))dlsym((void *)RTLD_DEFAULT, "stat");
    orig_lstat  = (int   (*)(const char *, struct stat *))dlsym((void *)RTLD_DEFAULT, "lstat");
    orig_opendir  = (DIR *(*)(const char *))              dlsym((void *)RTLD_DEFAULT, "opendir");
    orig_closedir = (int  (*)(DIR *))                     dlsym((void *)RTLD_DEFAULT, "closedir");
    orig_readdir  = (struct dirent *(*)(DIR *))           dlsym((void *)RTLD_DEFAULT, "readdir");
    orig_CFBundleGetInfoDictionary          = (ORIG_CFBundleGetInfoDictionary)dlsym((void *)RTLD_DEFAULT, "CFBundleGetInfoDictionary");
    orig_CFBundleGetValueForInfoDictionaryKey = (ORIG_CFBundleGetValueForInfoDictionaryKey)dlsym((void *)RTLD_DEFAULT, "CFBundleGetValueForInfoDictionaryKey");

    // Crash logger (dùng Mach exception-port, xem HWBreakHook.h) - độc lập hoàn toàn với fishhook/
    // VFS, an toàn gọi vô điều kiện kể cả ở process "lần đầu" (chỉ log lúc crash thật, không đụng
    // gì tới open()/redirect).
    CrashLogger_install();

    // Cờ "Crash=true"/"Crash=false" (xem HWBreakHook.h) - bổ sung cho CrashLogger_install() ở
    // trên, bắt thêm cả những kiểu chết mà Mach exception port không thấy được (SIGKILL do
    // jetsam/watchdog/force-quit). Cũng an toàn gọi vô điều kiện, không đụng gì tới redirect.
    CrashFlag_checkPreviousSessionAndArm();

    // ĐÃ QUAY LẠI fishhook cho toàn bộ họ open/openat/fopen/access/stat/lstat/opendir/closedir/
    // readdir (11 rebind, như bản gốc trước khi thử Cocoa-swizzle) - test thật xác nhận Cocoa-
    // swizzle không giải quyết được vụ bị đá giữa trận và redirect được ít file hơn hẳn, nên
    // không còn lý do đánh đổi coverage để né 1 rủi ro (PMS_HOOK) chưa chắc là nguyên nhân thật.
    struct rebinding rebindings[11];
    int n = 0;
    rebindings[n].name = "open";   rebindings[n].replacement = (void *)hooked_open;   rebindings[n].replaced = (void **)&orig_open;   n++;
    rebindings[n].name = "openat"; rebindings[n].replacement = (void *)hooked_openat; rebindings[n].replaced = (void **)&orig_openat; n++;
    rebindings[n].name = "fopen";  rebindings[n].replacement = (void *)hooked_fopen;  rebindings[n].replaced = (void **)&orig_fopen;  n++;
    rebindings[n].name = "access"; rebindings[n].replacement = (void *)hooked_access; rebindings[n].replaced = (void **)&orig_access; n++;
    rebindings[n].name = "stat";   rebindings[n].replacement = (void *)hooked_stat;   rebindings[n].replaced = (void **)&orig_stat;   n++;
    rebindings[n].name = "lstat";  rebindings[n].replacement = (void *)hooked_lstat;  rebindings[n].replaced = (void **)&orig_lstat;  n++;
    rebindings[n].name = "opendir";  rebindings[n].replacement = (void *)hooked_opendir;  rebindings[n].replaced = (void **)&orig_opendir;  n++;
    rebindings[n].name = "closedir"; rebindings[n].replacement = (void *)hooked_closedir; rebindings[n].replaced = (void **)&orig_closedir; n++;
    rebindings[n].name = "readdir";  rebindings[n].replacement = (void *)hooked_readdir;  rebindings[n].replaced = (void **)&orig_readdir;  n++;
    rebindings[n].name = "CFBundleGetInfoDictionary";           rebindings[n].replacement = (void *)hooked_CFBundleGetInfoDictionary;           rebindings[n].replaced = (void **)&orig_CFBundleGetInfoDictionary;           n++;
    rebindings[n].name = "CFBundleGetValueForInfoDictionaryKey"; rebindings[n].replacement = (void *)hooked_CFBundleGetValueForInfoDictionaryKey; rebindings[n].replaced = (void **)&orig_CFBundleGetValueForInfoDictionaryKey; n++;

    int rebindRet = rebind_symbols(rebindings, n);
    g_deltaHooksOK.store(rebindRet == 0 ? 0x3F : 0, std::memory_order_relaxed);

    // Checkpoint CUỐI CÙNG của constructor - dòng log này LUÔN LÀ 1 TRONG NHỮNG DÒNG ĐẦU TIÊN của
    // debug.log mỗi lần app mở, vì __attribute__((constructor)) chạy trước main()/UIApplicationMain,
    // trước khi bất kỳ code nào của game (Unity, AppDelegate...) có cơ hội chạy. rebindRet ở đây
    // lại đại diện cho toàn bộ 11 rebind fishhook (VFS + 2 CFBundle*) như bản gốc.
    DeltaVFS_debugLogf("initDeltaAllTrafficVFS: HOÀN TẤT - rebindRet=%d hooksOK=%u active=%d zipFound=%d needsFirstRun=%d",
        rebindRet, g_deltaHooksOK.load(std::memory_order_relaxed),
        g_deltaActive.load(std::memory_order_relaxed),
        g_deltaZipFound.load(std::memory_order_relaxed), needsFirstRun);
}