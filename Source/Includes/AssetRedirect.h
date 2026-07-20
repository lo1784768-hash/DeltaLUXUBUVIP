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
#import <zlib.h>
#include <atomic>
#include <mutex>
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
#define DELTA_DEST_DIR_NAME "a3f8c91e2b47d6089d2a71c5f8e93b06"
static char g_deltaZipPathC[1152] = {0};

// Quản lý tiền tố đường dẫn gốc của App Bundle và thư mục Delta trong Cache
static char g_bundlePrefixC[1024] = {0};
static size_t g_bundlePrefixLen = 0;

static char g_moddedPrefixC[1024] = {0};
static size_t g_moddedPrefixLen = 0;

// Con trỏ gốc của hàm access bắt buộc phải được gán trước
static int (*orig_access)(const char *, int);

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
// bỏ: nó buộc HWBreakHook (hardware breakpoint cho open(), xem HWBreakHook.h) phải sống sót qua
// đúng lúc I/O dồn dập nhất của quá trình giải nén, và không bao giờ ổn định được sau nhiều vòng
// debug. Quan sát Monite.dylib (đối thủ, dylib không chống bằng biện pháp tương tự) trên máy thật:
// nó hiện popup "Please Wait", giải nén xong thì CRASH, người dùng tự mở lại app lần 2 mới chơi
// được - tức là KHÔNG có custom hook nào cần sống sót qua giai đoạn giải nén cả, vì game (và
// HWBreakHook) chỉ thực sự chạy ở process THỨ HAI, lúc mọi thứ đã nằm sẵn trên đĩa. Kiến trúc này
// copy lại đúng ý đó - xem ar_ensureFirstRunChecked/DeltaVFS_runFirstRunExtraction bên dưới và
// installAppDelegateLaunchGuard/showUpdatingPopupThenRelaunch trong Menu.mm.
static char g_deltaMarkerPathC[1152] = {0};
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
//  PHẦN 1: GIẢI NÉN DELTA.ZIP (LAZY - từng file một, đúng lúc cần, xem giải thích ở
//  g_deltaMarkerPathC phía trên)
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
    char name[512];        // đường dẫn tương đối bên trong zip, khớp với "relative"/"destRelative"
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

// Giải nén ĐÚNG 1 entry (theo index trong g_arZipEntries) ra destPath. Ghi vào file tạm rồi
// rename() sang tên thật - tránh trường hợp 1 thread khác đọc phải file đang ghi dở nếu 2 thread
// cùng lúc yêu cầu file này lần đầu (rename() là atomic trên cùng 1 filesystem).
static bool ar_extractZipEntry(int idx, const char *destPath) {
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
    snprintf(tmpPath, sizeof(tmpPath), "%s.part%d.%lu", destPath, (int)getpid(),
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
    return true;
}

// Đảm bảo destPath tồn tại trên đĩa - đường nhanh (đã giải nén từ trước) chỉ tốn 1 access();
// đường chậm (lần đầu file này được yêu cầu) tìm trong index rồi giải nén ngay, đồng bộ, trên
// CHÍNH thread đang gọi (an toàn - xem chỗ gọi trong redirectAllTrafficPath()).
static bool ar_extractOneEntryIfNeeded(const char *destPath, const char *relativePath) {
    if (!destPath || !relativePath || g_arZipEntryCount == 0) return false;
    if (orig_access && orig_access(destPath, F_OK) == 0) return true;

    for (int i = 0; i < g_arZipEntryCount; i++) {
        if (strcmp(g_arZipEntries[i].name, relativePath) == 0) {
            return ar_extractZipEntry(i, destPath);
        }
    }
    return false; // không có trong Delta.zip - không phải lỗi, chỉ là file này không được mod
}

static bool ar_needExtract(const char *markerPath, const struct stat *zipSt) {
    int fd = open(markerPath, O_RDONLY);
    if (fd < 0) return true;
    char buf[128];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return true;
    buf[n] = '\0';
    char expected[128];
    snprintf(expected, sizeof(expected), "%lld:%lld", (long long)zipSt->st_mtime, (long long)zipSt->st_size);
    return strcmp(buf, expected) != 0;
}
// Returns false if the marker could not be written (e.g. bundle dir not writable).
static bool ar_writeMarker(const char *markerPath, const struct stat *zipSt) {
    int fd = open(markerPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        DeltaVFS_debugLogf("ar_writeMarker: ABORT open(marker) failed path=%s errno=%d", markerPath, errno);
        return false;
    }
    char buf[128];
    int w = snprintf(buf, sizeof(buf), "%lld:%lld", (long long)zipSt->st_mtime, (long long)zipSt->st_size);
    ssize_t written = write(fd, buf, w);
    close(fd);
    if (written != w) {
        DeltaVFS_debugLogf("ar_writeMarker: ABORT write() incomplete wrote=%zd expected=%d errno=%d", written, w, errno);
        return false;
    }
    DeltaVFS_debugLogf("ar_writeMarker: OK wrote marker to %s", markerPath);
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
                NSString *moddedDataDir = [documentsDir stringByAppendingString:@"/" DELTA_DEST_DIR_NAME "/"];
                strncpy(g_moddedPrefixC, [moddedDataDir UTF8String], sizeof(g_moddedPrefixC) - 1);
                g_moddedPrefixLen = strlen(g_moddedPrefixC);

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
                // Delta.zip on demand), no reason to bloat the user's device backup.
                if (dirExists) {
                    @autoreleasepool {
                        NSURL *deltaURL = [NSURL fileURLWithPath:[NSString stringWithUTF8String:g_moddedPrefixC] isDirectory:YES];
                        NSError *excludeErr = nil;
                        [deltaURL setResourceValue:@YES forKey:NSURLIsExcludedFromBackupKey error:&excludeErr];
                    }
                }
            }

            snprintf(g_deltaMarkerPathC, sizeof(g_deltaMarkerPathC), "%s.state", g_moddedPrefixC);

            if (ar_needExtract(g_deltaMarkerPathC, &g_deltaZipStat)) {
                // Delta.zip đổi (hoặc chưa từng chạy) so với lần trước - đây là process "lần đầu".
                // KHÔNG giải nén gì ở đây (hàm này phải nhanh - gọi từ constructor, chặn dyld nếu
                // chậm). Chỉ báo hiệu qua g_deltaNeedsFirstRun; Menu.mm sẽ hiện popup rồi gọi
                // DeltaVFS_runFirstRunExtraction() trên background thread. g_deltaActive CỐ Ý
                // không bật - game (process này) sẽ không bao giờ thực sự chạy tới lúc đọc file.
                DeltaVFS_debugLog("ar_ensureFirstRunChecked: Delta.zip thay đổi/chưa từng chạy - cần giải nén lần đầu (process này sẽ popup rồi thoát)");
                g_deltaExtractRan.store(true, std::memory_order_relaxed);
                g_deltaNeedsFirstRun.store(true, std::memory_order_relaxed);
            } else {
                // Đã giải nén đầy đủ từ lần trước (marker khớp) - process "bình thường". Không
                // cần build index/mmap cả Delta.zip nữa, chỉ cần existence-check thẳng trên đĩa
                // (xem ar_extractOneEntryIfNeeded) - vừa nhanh vừa bớt 1 thứ đụng vào mmap ngay
                // lúc HWBreakHook đang kích hoạt.
                g_deltaActive.store(true, std::memory_order_relaxed);
                DeltaVFS_debugLog("ar_ensureFirstRunChecked: marker khớp - đã giải nén sẵn, VFS active ngay");
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
// completion(success) - success=false nếu không file nào được ghi hoặc marker không ghi được.
// Caller (Menu.mm) TUYỆT ĐỐI KHÔNG được abort()/crash khi failure: marker chưa ghi thì relaunch
// nào cũng lại detect "cần giải nén" rồi lặp vô hạn không cách nào biết vì sao (RAM log mất, log
// trên đĩa cần Filza/Mac không có).
inline void DeltaVFS_runFirstRunExtraction(void (^completion)(BOOL success)) {
    DeltaVFS_debugLog("runFirstRunExtraction: dispatching to background queue");
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        // Delta.zip đổi so với lần trước (hoặc cài mới) - dọn sạch cache cũ 1 lần trước khi giải
        // nén lại từ đầu, tránh lẫn file cũ/thiếu đồng bộ với bản Delta.zip hiện tại.
        DeltaVFS_debugLog("runFirstRunExtraction: xoá cache cũ (nếu có) rồi tạo lại thư mục đích");
        ar_rmrf(g_moddedPrefixC);
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
                // Cùng quy tắc đổi tên đặc biệt cho binary chính như redirectAllTrafficPath().
                const char *destRelative = (strcmp(relative, "FreeFire") == 0) ? "FreeFire2" : relative;
                int written = snprintf(destPath, sizeof(destPath), "%s%s", g_moddedPrefixC, destRelative);
                if (written < 0 || written >= (int)sizeof(destPath)) { allOK = false; continue; }
                if (ar_extractZipEntry(i, destPath)) {
                    filesWritten++;
                } else {
                    allOK = false;
                }
                if ((i % 500) == 0) {
                    DeltaVFS_debugLogf("runFirstRunExtraction: tiến độ %d/%d", i, g_arZipEntryCount);
                }
            }
        }

        bool markerOK = allOK && ar_writeMarker(g_deltaMarkerPathC, &g_deltaZipStat);
        bool success = markerOK && filesWritten > 0;
        DeltaVFS_debugLogf("runFirstRunExtraction: filesWritten=%u allOK=%d markerOK=%d success=%d", filesWritten, allOK, markerOK, success);
        if (success) {
            g_deltaActive.store(true, std::memory_order_relaxed);
            g_deltaNeedsFirstRun.store(false, std::memory_order_relaxed);
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

inline const char* redirectAllTrafficPath(const char *path) {
    if (!path) return path;

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

    // The game's own main executable ("FreeFire.app/FreeFire") is special-cased to a
    // differently-named destination file ("FreeFire2") instead of the generic same-name mapping
    // - a clean, unpatched copy ships under that name in Delta.zip, so if the game reads its own
    // binary back off disk (e.g. a self-integrity/tamper check), it lands on the clean copy
    // rather than a same-named file that could collide with something else's expectations.
    const char *destRelative = (strcmp(relative, "FreeFire") == 0) ? "FreeFire2" : relative;

    int written = snprintf(redirectedBuffer, sizeof(redirectedBuffer), "%s%s", g_moddedPrefixC, destRelative);
    if (written < 0 || written >= (int)sizeof(redirectedBuffer)) {
        return path;
    }

    // LAZY: nếu file này chưa từng được giải nén, ar_extractOneEntryIfNeeded tự giải nén NGAY
    // (đồng bộ, trên chính thread đang gọi hàm này) trước khi trả về true/false - thay cho việc
    // chỉ kiểm tra tồn tại như trước (khi mọi thứ đã được giải nén sẵn từ 1 đợt lúc mở app).
    bool existsInDelta = ar_extractOneEntryIfNeeded(redirectedBuffer, destRelative);
    if (existsInDelta) {
        g_deltaHitCount.fetch_add(1, std::memory_order_relaxed);
        // Log "requested -> actual destination" when they differ (e.g. the FreeFire -> FreeFire2
        // rename above) so the INFO tab is actually usable to VERIFY the rename happened, instead
        // of always showing the originally-requested name and hiding whether a special case fired.
        char hitLabel[210];
        if (strcmp(relative, destRelative) != 0) {
            snprintf(hitLabel, sizeof(hitLabel), "%s -> %s", relative, destRelative);
        } else {
            snprintf(hitLabel, sizeof(hitLabel), "%s", relative);
        }
        strncpy(g_deltaLastHitPath, hitLabel, sizeof(g_deltaLastHitPath) - 1);
        g_deltaLastHitPath[sizeof(g_deltaLastHitPath) - 1] = '\0';
        deltaHitRingPush(hitLabel);
    } else {
        g_deltaMissCount.fetch_add(1, std::memory_order_relaxed);
    }
    return redirectedBuffer;
}

// Cơ chế hook open() thay thế bằng hardware breakpoint (né dấu vết fishhook để lại trên GOT) -
// THỬ NGHIỆM. Đặt include ở đây (không phải đầu file) vì HWBreakHook.h gọi thẳng
// redirectAllTrafficPath() và DeltaVFS_debugLog*() vừa định nghĩa ở trên - xem chi tiết/rủi ro
// trong chính file đó. Constructor bên dưới gọi HWBreakHook_tryInstallForOpen(); nếu trả về
// true thì "open" bị loại khỏi danh sách fishhook (không hook 2 lần cho cùng 1 hàm).
#import "HWBreakHook.h"

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
    const char *redirected = (path && path[0] == '/') ? redirectAllTrafficPath(path) : path;
    return orig_openat(dirfd, redirected, oflag, mode);
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

static int (*orig_stat)(const char *, struct stat *);
inline int hooked_stat(const char *path, struct stat *buf) {
    const char *redirected = redirectAllTrafficPath(path);
    return orig_stat(redirected, buf);
}

static int (*orig_lstat)(const char *, struct stat *);
inline int hooked_lstat(const char *path, struct stat *buf) {
    const char *redirected = redirectAllTrafficPath(path);
    return orig_lstat(redirected, buf);
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
//  CONSTRUCTOR KÍCH HOẠT HỆ THỐNG
// ============================================================================
__attribute__((constructor))
static void initDeltaAllTrafficVFS() {
    @autoreleasepool {
        // 1. KIỂM TRA CÓ CẦN GIẢI NÉN LẦN ĐẦU KHÔNG (bulk, xem PHẦN 1) - nhanh, không giải nén
        // gì ở đây cả. Idempotent, an toàn gọi từ nhiều nơi/nhiều thứ tự (constructor lẫn
        // Menu.mm's +load).
        ar_ensureFirstRunChecked();

        // 2. KÍCH HOẠT HOOK CẤP CAO TRÊN RAM (NSBundle Swizzling)
        initNSBundleMethodSwizzling();
    }

    bool needsFirstRun = g_deltaNeedsFirstRun.load(std::memory_order_relaxed);

    // 3. KÀI ĐẶT HOOK CẤP THẤP (VFS I/O via Fishhook)
    orig_open   = (int   (*)(const char *, int, ...))     dlsym((void *)RTLD_DEFAULT, "open");
    orig_openat = (int   (*)(int, const char *, int, ...))dlsym((void *)RTLD_DEFAULT, "openat");
    orig_fopen  = (FILE *(*)(const char *, const char *)) dlsym((void *)RTLD_DEFAULT, "fopen");
    orig_access = (int   (*)(const char *, int))          dlsym((void *)RTLD_DEFAULT, "access");
    orig_stat   = (int   (*)(const char *, struct stat *))dlsym((void *)RTLD_DEFAULT, "stat");
    orig_lstat  = (int   (*)(const char *, struct stat *))dlsym((void *)RTLD_DEFAULT, "lstat");
    orig_CFBundleGetInfoDictionary          = (ORIG_CFBundleGetInfoDictionary)dlsym((void *)RTLD_DEFAULT, "CFBundleGetInfoDictionary");
    orig_CFBundleGetValueForInfoDictionaryKey = (ORIG_CFBundleGetValueForInfoDictionaryKey)dlsym((void *)RTLD_DEFAULT, "CFBundleGetValueForInfoDictionaryKey");

    // THỬ NGHIỆM: cố dùng hardware breakpoint (né dấu vết fishhook để lại trên GOT) cho riêng
    // open() trước - xem HWBreakHook.h. Có tự kiểm tra + fallback an toàn: nếu KHÔNG hoạt
    // động đúng trong 500ms, hàm trả false và "open" vẫn được thêm vào fishhook như bình
    // thường bên dưới (không có khoảng trống không hook được).
    // CHỈ thử trên process "bình thường" (đã giải nén sẵn từ trước) - process "lần đầu" đang
    // chuẩn bị popup + giải nén bulk trên background thread rồi tự thoát (xem Menu.mm), không có
    // lý do gì để mạo hiểm kích hoạt breakpoint đúng lúc I/O nặng nhất, và process này sẽ không
    // bao giờ chạy tới lúc game thật sự đọc file cả.
    bool hwBreakOpenActive = needsFirstRun ? false : HWBreakHook_tryInstallForOpen();

    struct rebinding rebindings[8];
    int n = 0;
    if (!hwBreakOpenActive) {
        rebindings[n].name = "open";   rebindings[n].replacement = (void *)hooked_open;   rebindings[n].replaced = (void **)&orig_open;   n++;
    }
    rebindings[n].name = "openat"; rebindings[n].replacement = (void *)hooked_openat; rebindings[n].replaced = (void **)&orig_openat; n++;
    rebindings[n].name = "fopen";  rebindings[n].replacement = (void *)hooked_fopen;  rebindings[n].replaced = (void **)&orig_fopen;  n++;
    rebindings[n].name = "access"; rebindings[n].replacement = (void *)hooked_access; rebindings[n].replaced = (void **)&orig_access; n++;
    rebindings[n].name = "stat";   rebindings[n].replacement = (void *)hooked_stat;   rebindings[n].replaced = (void **)&orig_stat;   n++;
    rebindings[n].name = "lstat";  rebindings[n].replacement = (void *)hooked_lstat;  rebindings[n].replaced = (void **)&orig_lstat;  n++;
    rebindings[n].name = "CFBundleGetInfoDictionary";           rebindings[n].replacement = (void *)hooked_CFBundleGetInfoDictionary;           rebindings[n].replaced = (void **)&orig_CFBundleGetInfoDictionary;           n++;
    rebindings[n].name = "CFBundleGetValueForInfoDictionaryKey"; rebindings[n].replacement = (void *)hooked_CFBundleGetValueForInfoDictionaryKey; rebindings[n].replaced = (void **)&orig_CFBundleGetValueForInfoDictionaryKey; n++;

    int rebindRet = rebind_symbols(rebindings, n);
    g_deltaHooksOK.store(rebindRet == 0 ? 0x3F : 0, std::memory_order_relaxed);
}