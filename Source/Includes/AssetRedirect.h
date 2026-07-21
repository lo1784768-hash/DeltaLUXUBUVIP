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
#include <set>
#include <string>
#import <objc/runtime.h>
#import <dlfcn.h>
#import <time.h>
#import <errno.h>
#import <dirent.h>

#import "MemoryUtils.h"

// Gói Delta.zip nằm NGAY TRONG App Bundle: FreeFire.app/Delta.zip
#define DELTA_ZIP_BUNDLE_NAME "Delta.zip"
// Tên thư mục đích trong Documents/ - cố tình không phải "Delta" để không lộ ra là thư mục mod
// nếu ai đó duyệt Documents qua Files app (user bật File Sharing để tự xem log/debug).
#define DELTA_DEST_DIR_NAME "a3f8c91e2b47d6089d2a71c5f8e93b06"
static char g_deltaZipPathC[1152] = {0};

// Danh sách file được PHÉP redirect - khai báo CHỦ ĐỘNG, không còn kiểu "redirect hết mọi thứ nằm
// trong Delta.zip trừ 1 blocklist thủ công" như trước. Delta.zip đóng gói NGUYÊN app bundle (không
// phải diff), nên "có mặt trong zip" không đủ để coi là an toàn - từng có 1 bản _CodeSignature/
// CodeResources cũ vô tình nằm sẵn trong zip gây lỗi "hotfix: SaveFailed" trên máy thật. File này
// (1 dòng = 1 relative path, cùng định dạng destRelative mà redirectAllTrafficPath() dùng để khớp
// entry trong Delta.zip, VD "FreeFire2" không phải "FreeFire") phải được đóng gói SẴN bên trong
// chính Delta.zip dưới tên dưới đây - đây là nơi người đóng gói Delta.zip khai báo rõ ràng "tôi cố
// ý patch đúng những file này", VFS sẽ không bao giờ redirect bất kỳ path nào khác, kể cả khi nó
// tình cờ có mặt trong zip.
#define DELTA_MANIFEST_ENTRY_NAME "patchlist.txt"
static std::set<std::string> g_deltaAllowedPaths;

// Quản lý tiền tố đường dẫn gốc của App Bundle và thư mục Delta trong Cache
static char g_bundlePrefixC[1024] = {0};
static size_t g_bundlePrefixLen = 0;

static char g_moddedPrefixC[1024] = {0};
static size_t g_moddedPrefixLen = 0;

// ============================================================================
//  THỐNG KÊ / LOG
// ============================================================================
static std::atomic<unsigned long long> g_deltaHitCount{0};   
static std::atomic<unsigned long long> g_deltaMissCount{0};  
static std::atomic<unsigned long long> g_deltaTotalCalls{0}; 
static std::atomic<unsigned long long> g_deltaBundleCalls{0};
static std::atomic<unsigned int> g_deltaExtractedFiles{0};
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
//  vào 1 file cố định trong Documents/<hash>/ (dùng dlsym trực tiếp thay vì gọi open() bình
//  thường, vì hàm log này có thể được gọi trước khi constructor chạy xong) - user đã bật File
//  Sharing nên xem trực tiếp qua Files app trên điện thoại được, không cần Mac/Xcode.
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
// v5: KHÔNG còn hook libc/NSBundle nào cả (đổi sang kiểu Monite - xem PHẦN 3 phía dưới), nên
// luôn trả 0. Giữ hàm này lại (thay vì xoá) chỉ để Menu.mm còn build được - dòng "Fishhook: ..."
// trong INFO tab giờ SAI Ý NGHĨA (không phải "thất bại", chỉ là "chủ động không dùng nữa") - cần
// sửa lại chữ hiển thị trong Menu.mm, chưa làm ở đây vì ngoài phạm vi AssetRedirect.h.
inline unsigned int       DeltaVFS_hooksOK()        { return 0; }
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

// Tìm entry trong index theo tên - dùng chung bởi ar_extractOneEntryIfNeeded (asset thường) và
// DeltaVFS_runFirstRunExtraction (extract riêng file manifest patchlist.txt).
static int ar_findZipEntryIndex(const char *name) {
    for (int i = 0; i < g_arZipEntryCount; i++) {
        if (strcmp(g_arZipEntries[i].name, name) == 0) return i;
    }
    return -1;
}

// Đảm bảo destPath tồn tại trên đĩa - đường nhanh (đã giải nén từ trước) chỉ tốn 1 access();
// đường chậm (lần đầu file này được yêu cầu) tìm trong index rồi giải nén ngay, đồng bộ, trên
// CHÍNH thread đang gọi (an toàn - xem chỗ gọi trong redirectAllTrafficPath()).
static bool ar_extractOneEntryIfNeeded(const char *destPath, const char *relativePath) {
    if (!destPath || !relativePath) return false;
    // Đường nhanh PHẢI đứng trước check g_arZipEntryCount==0: ở process bình thường (marker
    // khớp), ar_ensureFirstRunChecked() CỐ Ý không build index nữa (đã bung sẵn từ trước, xem
    // PHẦN 1) nên g_arZipEntryCount luôn = 0 - nếu check đó đứng trước thì hàm này LUÔN return
    // false ngay cả khi file đã thật sự nằm sẵn trên đĩa, gây miss 100% dù Delta/ đầy đủ file
    // (xác nhận qua ảnh chụp máy thật: folder có file nhưng INFO tab báo miss hết).
    if (access(destPath, F_OK) == 0) return true;
    if (g_arZipEntryCount == 0) return false;

    int idx = ar_findZipEntryIndex(relativePath);
    if (idx < 0) return false; // không có trong Delta.zip - không phải lỗi, chỉ là file này không được mod
    return ar_extractZipEntry(idx, destPath);
}

// "bulk1:" prefix CỐ Ý khác định dạng marker cũ ("%lld:%lld" trần, từng dùng cho cả bản lazy lẫn
// bulk trước đây) - máy đã test qua bản lazy-extraction (session trước khi revert lại bulk) vẫn
// còn marker cũ khớp mtime:size hiện tại nhưng Delta/ thực tế gần như trống (lazy chỉ giải nén
// đúng vài file trước khi crash). Đổi định dạng để marker cũ LUÔN bị coi là stale, buộc giải nén
// bulk đầy đủ lại từ đầu đúng 1 lần, thay vì tin nhầm "đã xong" và để redirect miss 100%.
static bool ar_needExtract(const char *markerPath, const struct stat *zipSt) {
    int fd = open(markerPath, O_RDONLY);
    if (fd < 0) return true;
    char buf[128];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return true;
    buf[n] = '\0';
    char expected[128];
    snprintf(expected, sizeof(expected), "bulk1:%lld:%lld", (long long)zipSt->st_mtime, (long long)zipSt->st_size);
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
    int w = snprintf(buf, sizeof(buf), "bulk1:%lld:%lld", (long long)zipSt->st_mtime, (long long)zipSt->st_size);
    ssize_t written = write(fd, buf, w);
    close(fd);
    if (written != w) {
        DeltaVFS_debugLogf("ar_writeMarker: ABORT write() incomplete wrote=%zd expected=%d errno=%d", written, w, errno);
        return false;
    }
    DeltaVFS_debugLogf("ar_writeMarker: OK wrote marker to %s", markerPath);
    return true;
}

// Nạp lại allowlist từ bản patchlist.txt đã được extract sẵn ra đĩa (g_moddedPrefixC + ".patchlist")
// - gọi mỗi lần app khởi động (ar_ensureFirstRunChecked), KHÔNG cần đụng lại Delta.zip, nên vẫn rẻ
// ở process "bình thường" (marker khớp, không build zip index). Nếu file chưa tồn tại (chưa từng
// extract lần nào, hoặc Delta.zip thiếu DELTA_MANIFEST_ENTRY_NAME) thì allowlist rỗng - VFS sẽ
// KHÔNG redirect file nào cả thay vì đoán mò, an toàn hơn là lỡ redirect nhầm.
static void ar_loadManifestFromDisk() {
    g_deltaAllowedPaths.clear();
    if (g_moddedPrefixLen == 0) return;
    char manifestPath[1200];
    snprintf(manifestPath, sizeof(manifestPath), "%s.patchlist", g_moddedPrefixC);
    FILE *f = fopen(manifestPath, "rb");
    if (!f) {
        DeltaVFS_debugLogf("ar_loadManifestFromDisk: không mở được %s (errno=%d) - allowlist rỗng, chưa redirect gì cả", manifestPath, errno);
        return;
    }
    char line[512];
    unsigned int count = 0;
    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = '\0';
        if (len == 0) continue;
        g_deltaAllowedPaths.insert(std::string(line));
        count++;
    }
    fclose(f);
    DeltaVFS_debugLogf("ar_loadManifestFromDisk: nạp %u path được phép redirect từ %s", count, manifestPath);
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

    // Nạp allowlist ngay khi có g_moddedPrefixC - kể cả process "bình thường" (marker khớp, không
    // đụng lại Delta.zip) cũng cần allowlist mới mỗi lần khởi động vì nó chỉ sống trong RAM.
    ar_loadManifestFromDisk();

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
                // (xem ar_extractOneEntryIfNeeded) - vừa nhanh vừa bớt 1 thứ đụng vào mmap.
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
            // Trích riêng patchlist.txt ra đĩa TRƯỚC vòng lặp giải nén chính bên dưới - đây là
            // allowlist quyết định file nào thật sự được redirect (xem ar_loadManifestFromDisk),
            // phải luôn có mặt bất kể vòng lặp bên dưới có ghi thiếu file asset nào đó hay không.
            int manifestIdx = ar_findZipEntryIndex(DELTA_MANIFEST_ENTRY_NAME);
            if (manifestIdx >= 0) {
                char manifestDest[1200];
                snprintf(manifestDest, sizeof(manifestDest), "%s.patchlist", g_moddedPrefixC);
                bool manifestOK = ar_extractZipEntry(manifestIdx, manifestDest);
                DeltaVFS_debugLogf("runFirstRunExtraction: extract '%s' -> %s ok=%d", DELTA_MANIFEST_ENTRY_NAME, manifestDest, manifestOK);
                ar_loadManifestFromDisk();
            } else {
                DeltaVFS_debugLogf("runFirstRunExtraction: Delta.zip KHÔNG có '%s' - allowlist sẽ rỗng, VFS sẽ không redirect file nào cả", DELTA_MANIFEST_ENTRY_NAME);
            }

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
//  PHẦN 2: TÍNH TOÁN PATH REDIRECT (không còn tự động chặn gì - xem PHẦN 3 phía dưới để dùng)
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
        bool abExists = (access(abPath, F_OK) == 0);
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

    // Data/Raw/ios/versioninfo + fileinfo là manifest của hệ thống hotfix/asset-streaming (version
    // bản vá hiện tại + checksum từng file) - PHẢI luôn phản ánh đúng trạng thái THẬT của máy đang
    // chạy, không phải ảnh chụp cũ đóng gói sẵn trong Delta.zip. Xác nhận trên máy thật: sau khi
    // loại _CodeSignature/ vẫn còn lỗi "hotfix: SaveFailed", REDIRECTED FILES cho thấy đúng 2 file
    // này bị đọc lặp đi lặp lại từ Delta ngay lúc lỗi hiện ra - game đọc nhầm version/checksum cũ
    // nên lưu hotfix mới đè lên bị fail. Cùng bản chất với CodeResources ở trên - loại khỏi redirect.
    if (strcmp(relative, "Data/Raw/ios/versioninfo") == 0 || strcmp(relative, "Data/Raw/ios/fileinfo") == 0) {
        return path;
    }

    // Frameworks/ (VD: UnityFramework.framework/UnityFramework) TUYỆT ĐỐI không được redirect -
    // đây là BINARY THỰC THI (code máy, không phải asset/config), đã được dyld load thẳng từ bản
    // gốc TRƯỚC KHI hook của mình kịp cài xong. Delta.zip chỉ là ảnh chụp đóng gói sẵn từ 1 thời
    // điểm khác, không đảm bảo khớp byte-for-byte với bản đang thực sự chạy trong bộ nhớ - nếu có
    // bước tự kiểm tra tính toàn vẹn engine nào đó đọc lại file này để verify (giống lý do
    // CodeResources gây lỗi ở trên) thì phát hiện sai lệch dễ dẫn tới hậu quả nặng hơn (khoá tài
    // khoản) chứ không chỉ 1 popup lỗi thường. Xác nhận trên máy thật: REDIRECTED FILES cho thấy
    // UnityFramework bị đọc lặp lại từ Delta ngay lúc màn hình báo tài khoản bị khoá hiện ra.
    if (strncmp(relative, "Frameworks/", 11) == 0) {
        return path;
    }

    // The game's own main executable ("FreeFire.app/FreeFire") is special-cased to a
    // differently-named destination file ("FreeFire2") instead of the generic same-name mapping
    // - a clean, unpatched copy ships under that name in Delta.zip, so if the game reads its own
    // binary back off disk (e.g. a self-integrity/tamper check), it lands on the clean copy
    // rather than a same-named file that could collide with something else's expectations.
    const char *destRelative = (strcmp(relative, "FreeFire") == 0) ? "FreeFire2" : relative;

    // Chỉ redirect nếu path này nằm trong allowlist CHỦ ĐỘNG khai báo (patchlist.txt đóng gói sẵn
    // trong Delta.zip, xem ar_loadManifestFromDisk) - không còn suy ra qua "có mặt trong Delta.zip"
    // như trước nữa, vì Delta.zip đóng gói NGUYÊN app bundle (không phải diff) nên gần như mọi path
    // đều "có mặt". 2 blocklist thủ công phía trên (versioninfo/fileinfo, Frameworks/) vẫn giữ lại
    // làm lớp phòng thủ thứ 2 phòng khi patchlist.txt lỡ khai nhầm - không phải lớp chính nữa.
    if (g_deltaAllowedPaths.find(destRelative) == g_deltaAllowedPaths.end()) {
        return path;
    }

    int written = snprintf(redirectedBuffer, sizeof(redirectedBuffer), "%s%s", g_moddedPrefixC, destRelative);
    if (written < 0 || written >= (int)sizeof(redirectedBuffer)) {
        return path;
    }

    // existsInDelta=false nghĩa là file này KHÔNG thuộc bộ mod (Delta.zip không có nó) - vẫn
    // hoàn toàn bình thường, ví dụ FreeFire.app/_CodeSignature/CodeResources. TRƯỚC ĐÂY hàm này
    // luôn return redirectedBuffer bất kể hit/miss - trên miss, redirectedBuffer trỏ tới 1 đường
    // dẫn KHÔNG TỒN TẠI trong Delta/, nên open()/stat()/... sau đó luôn lỗi ENOENT thay vì đọc
    // được bản gốc trong bundle - xác nhận qua ảnh chụp máy thật (95/95 lời gọi trong bundle đều
    // miss, game báo lỗi mạng vì đọc thiếu file). Miss giờ phải trả về path GỐC để game đọc thẳng
    // bundle như chưa hề bị redirect.
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
        return redirectedBuffer;
    }

    g_deltaMissCount.fetch_add(1, std::memory_order_relaxed);
    return path;
}

// ============================================================================
//  PHẦN 3: API DÙNG TRỰC TIẾP (kiểu Monite - KHÔNG hook gì cả)
//
//  Đối chiếu Monite.dylib (xem MoniteAnalysis/, đã decompile qua Ghidra xác nhận) cho thấy họ
//  KHÔNG hook open()/fopen()/NSBundle gì cả - code của họ tự tính path Documents rồi gọi thẳng
//  open() ở đúng chỗ cần đọc file đã patch. Đổi theo đúng kiến trúc đó, theo yêu cầu: bỏ hẳn
//  fishhook + NSBundle method swizzling + HWBreakHook (3 lớp hook cũ, xem lịch sử git) - giữ lại
//  TOÀN BỘ phần zip/giải nén/allowlist ở trên (PHẦN 1) và redirectAllTrafficPath() (PHẦN 2, logic
//  tính toán path không đổi) làm nền, chỉ đổi CÁCH GỌI: không còn tự động chặn mọi open() trong
//  process nữa - chỗ nào trong dylib cần đọc 1 file đã patch phải TỰ gọi 1 trong các hàm dưới đây
//  thay vì gọi thẳng open()/fopen().
//
//  ĐÁNH ĐỔI ĐÃ XÁC NHẬN VỚI USER TRƯỚC KHI ĐỔI: game/Unity KHÔNG tự biết gọi các hàm này, nên
//  bất kỳ file nào UNITY TỰ ĐỌC (texture, Data/Raw/..., asset bundle...) sẽ KHÔNG còn được
//  redirect tự động nữa - chỉ những chỗ CHÍNH dylib này tự viết code gọi (Menu.mm, feature riêng)
//  mới dùng được. Nếu sau này cần lại khả năng redirect cho Unity, phải quay lại kiểu hook (xem
//  git history commit trước bản này).
//
//  deltaComputeMonitePath() TÍNH PATH THẲNG, KHÔNG QUA g_deltaAllowedPaths/ar_extractOneEntryIfNeeded
//  - khác hẳn redirectAllTrafficPath() (vẫn giữ nguyên phía trên, PHẦN 2, cho ABHotUpdates dùng).
//  ĐÂY LÀ CHỦ Ý CỦA USER, không phải thiếu sót: đối chiếu Ghidra decompile Monite.dylib
//  (FUN_000c14c8/_patch, xem MoniteAnalysis/), code họ KHÔNG kiểm tra allowlist lẫn "file có tồn
//  tại trong Documents không" trước khi gọi open() - cứ giả định file luôn có sẵn. User xác nhận
//  muốn giống Y HỆT hành vi đó, kể cả phần rủi ro: đã quan sát trên máy thật (xoá monite.zip ->
//  game văng sau ~30s-1p) rằng Monite cũng crash nếu thiếu file, và muốn code này khi thiếu
//  Delta.zip/file chưa giải nén CŨNG crash giống vậy thay vì tự âm thầm rơi về path gốc - lý do:
//  hành vi "im lặng dùng bản gốc" (redirectAllTrafficPath()) che mất lỗi thiếu file, khó phát
//  hiện lúc dev/test hơn là crash ngay và rõ ràng. KHÔNG có allowlist ở đây cũng an toàn tương tự
//  Monite: hàm này chỉ được gọi ở NHỮNG CHỖ dylib tự chủ động viết ra (đã biết chắc muốn đọc file
//  nào), không phải 1 hook chặn path bất kỳ từ đâu tới - không có nguy cơ vô tình redirect nhầm
//  _CodeSignature/SC_Info như hồi còn hook toàn cục.
inline const char *deltaComputeMonitePath(const char *path) {
    if (!path || g_bundlePrefixLen == 0 || g_moddedPrefixLen == 0) return path;
    if (strncmp(path, g_bundlePrefixC, g_bundlePrefixLen) != 0) return path;
    const char *relative = path + g_bundlePrefixLen;
    // Giữ đúng quy tắc đổi tên đặc biệt cho binary chính như redirectAllTrafficPath() - xem
    // giải thích ở đó (FreeFire -> FreeFire2, bản sạch chống tự-kiểm-tra).
    const char *destRelative = (strcmp(relative, "FreeFire") == 0) ? "FreeFire2" : relative;
    static thread_local char buf[2048];
    int written = snprintf(buf, sizeof(buf), "%s%s", g_moddedPrefixC, destRelative);
    if (written < 0 || written >= (int)sizeof(buf)) return path;
    return buf;
}

inline int DeltaVFS_open(const char *path, int flags, int mode) {
    return open(deltaComputeMonitePath(path), flags, mode);
}

inline FILE *DeltaVFS_fopen(const char *path, const char *mode) {
    return fopen(deltaComputeMonitePath(path), mode);
}

inline int DeltaVFS_access(const char *path, int mode) {
    return access(deltaComputeMonitePath(path), mode);
}

// Quét ĐỆ QUY toàn bộ thư mục Delta trong Documents/<hash>/, gọi DeltaVFS_open() cho MỌI file
// tìm được ("đọc hết trong folder Delta ở Documents" theo yêu cầu) - vừa là cách "dùng thử" thật
// sự cho DeltaVFS_open (hết cảnh báo "chưa có code nào gọi" trong INFO tab), vừa để tự kiểm tra
// mọi file trong Delta/ thật sự ĐỌC ĐƯỢC (không chỉ tồn tại). Bỏ qua file nội bộ bắt đầu bằng "."
// (.state, .patchlist, .write_test) - không phải asset thật.
//
// Dùng lại NGUYÊN các counter/ring có sẵn (g_deltaTotalCalls, g_deltaBundleCalls, g_deltaHitCount/
// MissCount, deltaHitRingPush) - vì DeltaVFS_open() giờ đi qua deltaComputeMonitePath() (không
// còn tự đếm như redirectAllTrafficPath() nữa, xem giải thích ở đó), nên phải tự đếm ở ĐÂY để
// INFO tab (Menu.mm, không sửa gì) vẫn hiển thị đúng - không có counter riêng nào khác.
static void ar_readAllDeltaFilesRecursive(const char *dirPath) {
    DIR *d = opendir(dirPath);
    if (!d) return;
    struct dirent *ent;
    char childPath[2048];
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue; // "." "..", và mọi file nội bộ (.state, .patchlist...)
        int written = snprintf(childPath, sizeof(childPath), "%s%s", dirPath, ent->d_name);
        if (written < 0 || written >= (int)sizeof(childPath)) continue;

        struct stat st;
        if (lstat(childPath, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            char subDir[2048];
            int w2 = snprintf(subDir, sizeof(subDir), "%s/", childPath);
            if (w2 > 0 && w2 < (int)sizeof(subDir)) ar_readAllDeltaFilesRecursive(subDir);
            continue;
        }
        if (!S_ISREG(st.st_mode)) continue;

        // childPath đang là path THẬT trong Documents/<hash>/... - dựng lại "path kiểu bundle"
        // (g_bundlePrefixC + phần relative) để đưa vào DeltaVFS_open(), đúng input mà
        // deltaComputeMonitePath() mong đợi (nó tự trừ g_bundlePrefixC rồi cộng lại
        // g_moddedPrefixC) - vòng tính lại này CHỦ Ý dùng chính DeltaVFS_open(), không mở thẳng
        // childPath, để thật sự "dùng thử" API công khai chứ không phải đường tắt.
        const char *relative = childPath + g_moddedPrefixLen;
        char bundleStylePath[2048];
        int w3 = snprintf(bundleStylePath, sizeof(bundleStylePath), "%s%s", g_bundlePrefixC, relative);
        if (w3 < 0 || w3 >= (int)sizeof(bundleStylePath)) continue;

        g_deltaTotalCalls.fetch_add(1, std::memory_order_relaxed);
        g_deltaBundleCalls.fetch_add(1, std::memory_order_relaxed);

        int fd = DeltaVFS_open(bundleStylePath, O_RDONLY, 0);
        if (fd >= 0) {
            close(fd);
            g_deltaHitCount.fetch_add(1, std::memory_order_relaxed);
            strncpy(g_deltaLastHitPath, relative, sizeof(g_deltaLastHitPath) - 1);
            g_deltaLastHitPath[sizeof(g_deltaLastHitPath) - 1] = '\0';
            deltaHitRingPush(relative);
        } else {
            g_deltaMissCount.fetch_add(1, std::memory_order_relaxed);
            DeltaVFS_debugLogf("ar_readAllDeltaFilesRecursive: DeltaVFS_open FAILED errno=%d path=%s", errno, relative);
        }
    }
    closedir(d);
}

inline void DeltaVFS_readAllDeltaFiles() {
    if (g_moddedPrefixLen == 0) return;
    DeltaVFS_debugLog("DeltaVFS_readAllDeltaFiles: bắt đầu quét + đọc hết file trong Delta/");
    ar_readAllDeltaFilesRecursive(g_moddedPrefixC);
    DeltaVFS_debugLogf("DeltaVFS_readAllDeltaFiles: xong, hits=%llu misses=%llu",
                        g_deltaHitCount.load(std::memory_order_relaxed),
                        g_deltaMissCount.load(std::memory_order_relaxed));
}

// ============================================================================
//  CONSTRUCTOR KÍCH HOẠT HỆ THỐNG
// ============================================================================
__attribute__((constructor))
static void initDeltaAllTrafficVFS() {
    @autoreleasepool {
        // Chỉ còn kiểm tra/giải nén Delta.zip (PHẦN 1) - không còn hook nào để cài nữa (xem
        // PHẦN 3 phía trên). Idempotent, an toàn gọi từ nhiều nơi/nhiều thứ tự (constructor lẫn
        // Menu.mm's +load).
        ar_ensureFirstRunChecked();

        // Đọc hết mọi file trong Delta/ ngay lúc khởi động - CHỈ khi đã giải nén sẵn từ trước
        // (g_deltaActive), KHÔNG chạy ở process "lần đầu" (đang bận giải nén bulk trên background
        // thread rồi tự thoát, xem DeltaVFS_runFirstRunExtraction) để tránh vừa giải nén vừa đọc.
        if (g_deltaActive.load(std::memory_order_relaxed)) {
            DeltaVFS_readAllDeltaFiles();
        }
    }
}