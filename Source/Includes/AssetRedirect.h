#pragma once
#import <Foundation/Foundation.h>
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

#import "MemoryUtils.h"
#import "fishhook.h"

// Gói Delta.zip nằm NGAY TRONG App Bundle: FreeFire.app/Delta.zip
#define DELTA_ZIP_BUNDLE_NAME "Delta.zip"
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

// First-run extraction (Delta.zip present but not yet unzipped, or updated since last unzip):
// UIKit isn't up yet inside the constructor, so we can't show an "updating..." popup this early.
// The constructor just raises this flag; Menu.mm's +load (once keyWindow exists) shows the popup,
// runs DeltaVFS_runFirstRunExtraction(), then deliberately crashes so the NEXT launch starts clean
// with Delta/ already fully populated instead of racing the game against a mid-flight unzip.
static std::atomic<bool> g_deltaNeedsFirstRun{false};
static char g_deltaMarkerPathC[1152] = {0};
static struct stat g_deltaZipStat;

// ============================================================================
//  LOG GIẢI NÉN SỐNG SÓT QUA CRASH - ar_extractZip mmap nguyên Delta.zip (có thể
//  vài trăm MB) và chạy trên background queue ngay lúc app mới mở, đúng lúc bộ
//  nhớ căng nhất; nếu bị iOS kill (Jetsam) hay bất kỳ điểm return sớm nào bên
//  trong, mọi state đang nằm trong atomic RAM sẽ mất sạch không cách nào soi lại.
//  Ghi thẳng write() syscall (không qua libc buffer, không cần fflush) vào 1 file
//  cố định ở GỐC bundle (dùng dlsym trực tiếp thay vì orig_open của constructor,
//  vì hàm log này có thể được gọi trước khi constructor cài xong fishhook) - lấy
//  ra bằng Xcode > Devices and Simulators > (chọn app) > "Download Container..."
//  nếu có Mac (không cần jailbreak). KHÔNG có Filza/SSH/Mac thì dùng bản trong RAM
//  (DeltaVFS_debugLogSnapshot) mà Menu.mm hiện thẳng lên màn hình lúc chặn/giải nén.
// ============================================================================
static int g_deltaLogFd = -1;
static std::mutex g_deltaLogMutex;

#define DELTA_LOG_RING_LINES 40
static char g_deltaLogRingLines[DELTA_LOG_RING_LINES][160];
static int g_deltaLogRingHead = 0;
static unsigned int g_deltaLogRingTotal = 0;
static std::mutex g_deltaLogRingMutex;

inline void deltaLogEnsureOpen() {
    if (g_deltaLogFd >= 0) return;
    std::lock_guard<std::mutex> lock(g_deltaLogMutex);
    if (g_deltaLogFd >= 0) return;
    if (g_bundlePrefixLen == 0) return;
    int (*rawOpen)(const char *, int, ...) = (int (*)(const char *, int, ...))dlsym(RTLD_DEFAULT, "open");
    if (!rawOpen) return;
    char logPath[1200];
    snprintf(logPath, sizeof(logPath), "%sDeltaExtractLog.txt", g_bundlePrefixC);
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
    if (g_bundlePrefixLen == 0) return @"(chưa xác định bundle)";
    return [NSString stringWithFormat:@"%sDeltaExtractLog.txt", g_bundlePrefixC];
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
// DeltaVFS_needsFirstRunExtraction() is defined further down, after ar_needExtract()/ar_mkpath()
// - it actively re-checks rather than trusting a flag the constructor may not have set yet
// (confirmed on-device: with a dylib patched into the IPA post-build rather than linked in at
// build time, the constructor is not guaranteed to run before Menu.mm's +load).

inline NSString *DeltaVFS_signatureSummary() {
    if (g_bundlePrefixLen == 0) return @"(chưa xác định bundle)";
    char crPath[1200];
    snprintf(crPath, sizeof(crPath), "%s_CodeSignature/CodeResources", g_bundlePrefixC);
    NSString *p = [NSString stringWithUTF8String:crPath];
    NSDictionary *cr = [NSDictionary dictionaryWithContentsOfFile:p];
    if (!cr) return @"CodeResources: KHÔNG đọc được (app chưa ký / thiếu file?)";

    BOOL zipSigned = NO;
    unsigned deltaDirFiles = 0;
    NSArray *sections = @[@"files2", @"files"];
    for (NSString *sect in sections) {
        NSDictionary *files = cr[sect];
        if (![files isKindOfClass:[NSDictionary class]]) continue;
        for (NSString *k in files) {
            if ([k isEqualToString:@DELTA_ZIP_BUNDLE_NAME]) zipSigned = YES;
            if ([k hasPrefix:@"Delta/"]) deltaDirFiles++;
        }
    }
    return [NSString stringWithFormat:
            @"Delta.zip trong chữ ký: %@\nFile Delta/ được ký: %u",
            zipSigned ? @"CÓ ✓" : @"KHÔNG ✗", deltaDirFiles];
}

// ============================================================================
//  PHẦN 1: GIẢI NÉN DELTA.ZIP (Bung gói mod vào thẳng thư mục Delta/)
// ============================================================================
static inline uint16_t ar_rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static inline uint32_t ar_rd32(const uint8_t *p) { return (uint32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24)); }
static inline uint64_t ar_rd64(const uint8_t *p) { return (uint64_t)ar_rd32(p) | ((uint64_t)ar_rd32(p + 4) << 32); }

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
    mkdir(tmp, 0755);
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

static void ar_extractZip(const char *zipPath, const char *destDir) {
    DeltaVFS_debugLogf("ar_extractZip: start zip=%s dest=%s", zipPath, destDir);

    int fd = open(zipPath, O_RDONLY);
    if (fd < 0) { DeltaVFS_debugLogf("ar_extractZip: ABORT open(zip) failed errno=%d", errno); return; }

    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size < 22) {
        DeltaVFS_debugLogf("ar_extractZip: ABORT fstat failed or size<22 (size=%lld)", (long long)st.st_size);
        close(fd);
        return;
    }
    size_t fileSize = (size_t)st.st_size;
    DeltaVFS_debugLogf("ar_extractZip: zip size=%zu", fileSize);

    uint8_t *base = (uint8_t *)mmap(NULL, fileSize, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (base == MAP_FAILED) { DeltaVFS_debugLogf("ar_extractZip: ABORT mmap failed errno=%d", errno); return; }

    const uint8_t *eocd = NULL;
    size_t maxBack = (fileSize < (22 + 65535)) ? fileSize : (22 + 65535);
    for (size_t i = 22; i <= maxBack; i++) {
        const uint8_t *p = base + fileSize - i;
        if (ar_rd32(p) == 0x06054b50) { eocd = p; break; }
    }
    if (!eocd) { DeltaVFS_debugLog("ar_extractZip: ABORT no EOCD signature found"); munmap(base, fileSize); return; }

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
    DeltaVFS_debugLogf("ar_extractZip: totalEntries=%llu cdOffset=%llu", (unsigned long long)totalEntries, (unsigned long long)cdOffset);

    if (cdOffset >= fileSize) { DeltaVFS_debugLog("ar_extractZip: ABORT cdOffset >= fileSize"); munmap(base, fileSize); return; }

    const uint8_t *cd = base + cdOffset;
    char pathBuf[2048];
    char nameBuf[1024];

    for (uint64_t e = 0; e < totalEntries; e++) {
        if (e % 500 == 0) DeltaVFS_debugLogf("ar_extractZip: progress entry=%llu/%llu written=%u", (unsigned long long)e, (unsigned long long)totalEntries, g_deltaExtractedFiles.load(std::memory_order_relaxed));
        if ((size_t)(cd - base) + 46 > fileSize) { DeltaVFS_debugLogf("ar_extractZip: STOP cd+46>fileSize at entry=%llu", (unsigned long long)e); break; }
        if (ar_rd32(cd) != 0x02014b50) { DeltaVFS_debugLogf("ar_extractZip: STOP bad CD signature at entry=%llu", (unsigned long long)e); break; }

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

        if (nameLen == 0 || nameLen >= sizeof(nameBuf)) { cd = nextCd; continue; }
        memcpy(nameBuf, nameP, nameLen);
        nameBuf[nameLen] = '\0';
        cd = nextCd;

        if (nameBuf[0] == '/' || strstr(nameBuf, "..") != NULL) continue;

        int w = snprintf(pathBuf, sizeof(pathBuf), "%s%s", destDir, nameBuf);
        if (w < 0 || w >= (int)sizeof(pathBuf)) continue;

        size_t nl = strlen(nameBuf);
        if (nameBuf[nl - 1] == '/') { ar_mkpath(pathBuf); continue; }

        char parent[2048];
        snprintf(parent, sizeof(parent), "%s", pathBuf);
        char *slash = strrchr(parent, '/');
        if (slash) { *slash = '\0'; ar_mkpath(parent); }

        if ((size_t)localOff + 30 > fileSize) continue;
        const uint8_t *lh = base + localOff;
        if (ar_rd32(lh) != 0x04034b50) continue;
        uint16_t lhNameLen  = ar_rd16(lh + 26);
        uint16_t lhExtraLen = ar_rd16(lh + 28);
        const uint8_t *data = lh + 30 + lhNameLen + lhExtraLen;
        if ((size_t)(data - base) + compSize > fileSize) continue;

        int outFd = open(pathBuf, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (outFd < 0) continue;
        if (method == 0) {              
            write(outFd, data, compSize);
        } else if (method == 8) {       
            ar_inflateToFd(data, compSize, outFd);
        }
        close(outFd);
        g_deltaExtractedFiles.fetch_add(1, std::memory_order_relaxed);
    }

    DeltaVFS_debugLogf("ar_extractZip: DONE written=%u", g_deltaExtractedFiles.load(std::memory_order_relaxed));
    munmap(base, fileSize);
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
// Returns false if the marker could not be written (e.g. bundle dir not writable) - caller
// must NOT treat extraction as durably complete in that case, or every relaunch will look
// like a fresh install again forever with no way to tell why.
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
//  KIỂM TRA "CẦN GIẢI NÉN LẦN ĐẦU?" - IDEMPOTENT, gọi từ bất kỳ đâu, bất kỳ lúc nào.
//  Bằng chứng trên máy thật (cùng pid, cùng giây, 3 lần cài mới độc lập): Menu.mm's +load
//  ĐÔI KHI đọc g_deltaNeedsFirstRun TRƯỚC KHI constructor kịp set nó - dylib này được một
//  tool patch thẳng vào IPA sau khi build (không phải linker chèn từ lúc build gốc), nên
//  thứ tự "constructor luôn chạy trước +load" không được đảm bảo như với dylib link bình
//  thường. Thay vì dựa vào ai chạy trước, hàm này tự làm luôn việc kiểm tra (tính path nếu
//  constructor chưa kịp tính, stat() zip, so marker) và chỉ làm ĐÚNG 1 LẦN dù bị gọi từ
//  nhiều nơi (constructor + Menu +load) theo bất kỳ thứ tự nào.
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

                NSString *moddedDataDir = [bundlePath stringByAppendingString:@"/Delta/"];
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

            snprintf(g_deltaMarkerPathC, sizeof(g_deltaMarkerPathC), "%s.delta_extracted", g_moddedPrefixC);

            if (ar_needExtract(g_deltaMarkerPathC, &g_deltaZipStat)) {
                DeltaVFS_debugLog("ar_ensureFirstRunChecked: marker missing/stale -> needs first-run extraction");
                g_deltaExtractRan.store(true, std::memory_order_relaxed);
                g_deltaNeedsFirstRun.store(true, std::memory_order_relaxed);
            } else {
                struct stat deltaDirSt;
                if (stat(g_moddedPrefixC, &deltaDirSt) == 0 && S_ISDIR(deltaDirSt.st_mode)) {
                    g_deltaActive.store(true, std::memory_order_relaxed);
                    DeltaVFS_debugLog("ar_ensureFirstRunChecked: marker matches -> already extracted, Delta VFS active immediately");
                } else {
                    DeltaVFS_debugLog("ar_ensureFirstRunChecked: marker matches but Delta/ dir missing/not a dir - VFS stays inactive");
                }
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

// Runs the (potentially slow) unzip on a background queue, then hops back to the main queue
// for `completion` — Menu.mm uses that to hold the popup on screen and then trigger the crash.
// While this is in flight, redirectAllTrafficPath() passes bundle paths through untouched
// (g_deltaActive is still false), so the game keeps reading the original, unmodded bundle.
// `completion(success)` - success is false if nothing actually got extracted, or the marker
// couldn't be written. Caller (Menu.mm) MUST NOT crash-to-relaunch on failure: since the marker
// never got written, every relaunch would re-detect "needs extraction" and loop forever with no
// way to see why (RAM log gone, on-disk log needs Filza/Mac neither of which the user has here).
inline void DeltaVFS_runFirstRunExtraction(void (^completion)(BOOL success)) {
    DeltaVFS_debugLog("runFirstRunExtraction: dispatching to background queue");
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        DeltaVFS_debugLog("runFirstRunExtraction: background block entered, calling ar_extractZip");
        ar_extractZip(g_deltaZipPathC, g_moddedPrefixC);
        unsigned int filesWritten = g_deltaExtractedFiles.load(std::memory_order_relaxed);
        bool markerOK = ar_writeMarker(g_deltaMarkerPathC, &g_deltaZipStat);
        bool success = markerOK && filesWritten > 0;
        DeltaVFS_debugLogf("runFirstRunExtraction: filesWritten=%u markerOK=%d success=%d", filesWritten, markerOK, success);
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

    // First-run extraction hasn't finished yet (see g_deltaNeedsFirstRun) — Delta/ may be empty
    // or stale, so don't hard-redirect bundle paths into it. Let the game boot off the original
    // bundle for this one launch; Menu.mm crashes the app right after extraction finishes so the
    // NEXT launch starts with Delta/ fully populated and the normal no-fallback policy resumes.
    if (!g_deltaActive.load(std::memory_order_relaxed)) return path;

    if (strncmp(path, g_bundlePrefixC, g_bundlePrefixLen) != 0) {
        return path;
    }
    g_deltaBundleCalls.fetch_add(1, std::memory_order_relaxed);

    if (strncmp(path, g_moddedPrefixC, g_moddedPrefixLen) == 0) {
        return path;
    }

    static thread_local char redirectedBuffer[2048];
    const char *relative = path + g_bundlePrefixLen;

    int written = snprintf(redirectedBuffer, sizeof(redirectedBuffer), "%s%s", g_moddedPrefixC, relative);
    if (written < 0 || written >= (int)sizeof(redirectedBuffer)) {
        return path; 
    }

    bool existsInDelta = (orig_access && orig_access(redirectedBuffer, F_OK) == 0);
    if (existsInDelta) {
        g_deltaHitCount.fetch_add(1, std::memory_order_relaxed);
        strncpy(g_deltaLastHitPath, relative, sizeof(g_deltaLastHitPath) - 1);
        g_deltaLastHitPath[sizeof(g_deltaLastHitPath) - 1] = '\0';
    } else {
        g_deltaMissCount.fetch_add(1, std::memory_order_relaxed);
    }
    return redirectedBuffer;
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

static NSDictionary* hooked_nsbundle_infoDictionary(id self, SEL _cmd) {
    if (self == [NSBundle mainBundle]) {
        static NSDictionary *fakeDict = nil;
        static dispatch_once_t onceToken;
        dispatch_once(&onceToken, ^{
            // Dựng đường dẫn tuyệt đối tới file Info.plist sạch nằm bên trong Delta/
            NSString *bundlePath = [[NSBundle mainBundle] bundlePath];
            NSString *deltaPlistPath = [bundlePath stringByAppendingString:@"/Delta/Info.plist"];
            
            // Ép nạp cấu trúc XML/Binary Plist từ Delta/
            fakeDict = [[NSDictionary alloc] initWithContentsOfFile:deltaPlistPath];
            
            // Nếu Delta không có Info.plist (hoặc file lỗi), fallback an toàn về dictionary gốc
            if (!fakeDict) {
                fakeDict = orig_nsbundle_infoDictionary(self, _cmd);
            }
        });
        return fakeDict;
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
        // 1. KIỂM TRA XEM CÓ CẦN GIẢI NÉN KHÔNG (chưa từng giải nén, hoặc Delta.zip đã đổi).
        // Dùng chung hàm idempotent với DeltaVFS_needsFirstRunExtraction() - xem ghi chú ở đó
        // về việc thứ tự constructor-trước-+load KHÔNG được đảm bảo với dylib patch hậu-build.
        ar_ensureFirstRunChecked();

        // 2. KÍCH HOẠT HOOK CẤP CAO TRÊN RAM (NSBundle Swizzling) VỪA BUNG XONG FILE
        initNSBundleMethodSwizzling();
    }

    // 3. KÀI ĐẶT HOOK CẤP THẤP (VFS I/O via Fishhook)
    orig_open   = (int   (*)(const char *, int, ...))     dlsym((void *)RTLD_DEFAULT, "open");
    orig_fopen  = (FILE *(*)(const char *, const char *)) dlsym((void *)RTLD_DEFAULT, "fopen");
    orig_access = (int   (*)(const char *, int))          dlsym((void *)RTLD_DEFAULT, "access");
    orig_stat   = (int   (*)(const char *, struct stat *))dlsym((void *)RTLD_DEFAULT, "stat");
    orig_lstat  = (int   (*)(const char *, struct stat *))dlsym((void *)RTLD_DEFAULT, "lstat");

    struct rebinding rebindings[] = {
        {"open",   (void *)hooked_open,   (void **)&orig_open},
        {"fopen",  (void *)hooked_fopen,  (void **)&orig_fopen},
        {"access", (void *)hooked_access, (void **)&orig_access},
        {"stat",   (void *)hooked_stat,   (void **)&orig_stat},
        {"lstat",  (void *)hooked_lstat,  (void **)&orig_lstat},
    };
    int rebindRet = rebind_symbols(rebindings, sizeof(rebindings) / sizeof(rebindings[0]));
    g_deltaHooksOK.store(rebindRet == 0 ? 0x1F : 0, std::memory_order_relaxed);
}