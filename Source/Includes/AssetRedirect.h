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

#import "MemoryUtils.h"
#import "fishhook.h"

// Gói Delta.zip nằm NGAY TRONG App Bundle: FreeFire.app/Delta.zip
// (đường dẫn đầy đủ được dựng lúc chạy từ bundlePath vì UUID bundle chỉ biết khi runtime)
#define DELTA_ZIP_BUNDLE_NAME "Delta.zip"
static char g_deltaZipPathC[1152] = {0};   // Đường dẫn tuyệt đối tới Delta.zip, gán trong constructor

// Quản lý tiền tố đường dẫn gốc của App Bundle và thư mục Delta trong Cache
static char g_bundlePrefixC[1024] = {0};
static size_t g_bundlePrefixLen = 0;

static char g_moddedPrefixC[1024] = {0};
static size_t g_moddedPrefixLen = 0;

// Con trỏ gốc của hàm access bắt buộc phải được gán trước
static int (*orig_access)(const char *, int);

// ============================================================================
//  THỐNG KÊ / LOG (để tab INFO trong menu soi được traffic có qua Delta không)
// ============================================================================
static std::atomic<unsigned long long> g_deltaHitCount{0};   // Số lần đọc file THÀNH CÔNG từ Delta
static std::atomic<unsigned long long> g_deltaMissCount{0};  // Số lần file không có trong Delta -> đọc bundle gốc
static std::atomic<unsigned long long> g_deltaTotalCalls{0}; // MỌI lời gọi file qua hook (bất kể path ở đâu)
static std::atomic<unsigned long long> g_deltaBundleCalls{0};// Lời gọi có path nằm trong App Bundle
static std::atomic<unsigned int> g_deltaExtractedFiles{0};   // Số file đã bung ra từ Delta.zip
static std::atomic<unsigned int> g_deltaHooksOK{0};          // Bitmask hook cài đặt thành công: 1=open 2=fopen 4=access 8=stat 16=lstat
static std::atomic<bool> g_deltaExtractRan{false};           // Đã chạy bước giải nén trong phiên này chưa
static std::atomic<bool> g_deltaZipFound{false};             // Có tìm thấy file Delta.zip tại đường dẫn nguồn không
static std::atomic<bool> g_deltaActive{false};              // Thư mục Delta/ có tồn tại -> BẬT chế độ ép đọc toàn bộ trong Delta
static char g_deltaLastHitPath[1024] = {0};                  // Đường dẫn (tương đối) được phục vụ từ Delta gần nhất
static char g_deltaLastAnyPath[1024] = {0};                  // Path bất kỳ gần nhất game mở (debug: xem game đọc ở đâu)

// API cho Menu.mm đọc trạng thái (khai báo inline trong header, Menu.mm đã #import file này)
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

// ============================================================================
//  KIỂM TRA CHỮ KÝ: Delta có nằm trong code signature của app không?
// ----------------------------------------------------------------------------
// Chữ ký app liệt kê hash MỌI file được ký trong FreeFire.app/_CodeSignature/
// CodeResources (một plist). Ở đây ta đọc plist đó rồi đếm xem có entry nào của
// Delta.zip và của thư mục Delta/... được ký vào không.
//   - "Delta.zip trong chữ ký"  = bạn đã nhét Delta.zip vào .app TRƯỚC khi re-sign.
//   - "file Delta/ trong chữ ký" = bạn bung sẵn folder Delta/ rồi mới ký (folder
//     được ký thẳng vào app, không phải bung lúc chạy).
// LƯU Ý: folder Delta/ do dylib TỰ BUNG lúc chạy thì KHÔNG bao giờ nằm trong chữ ký
// (nó sinh ra sau khi app khởi động) -> phần "file Delta/ trong chữ ký" sẽ = 0.
// ============================================================================
inline NSString *DeltaVFS_signatureSummary() {
    if (g_bundlePrefixLen == 0) return @"(chưa xác định bundle)";
    char crPath[1200];
    snprintf(crPath, sizeof(crPath), "%s_CodeSignature/CodeResources", g_bundlePrefixC);
    NSString *p = [NSString stringWithUTF8String:crPath];
    // dictionaryWithContentsOfFile đọc được cả plist nhị phân lẫn XML.
    NSDictionary *cr = [NSDictionary dictionaryWithContentsOfFile:p];
    if (!cr) return @"CodeResources: KHÔNG đọc được (app chưa ký / thiếu file?)";

    BOOL zipSigned = NO;
    unsigned deltaDirFiles = 0;
    // Chữ ký format v2 dùng section "files2", format cũ dùng "files".
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
//  PHẦN 1: GIẢI NÉN DELTA.ZIP (Bung gói mod vào thẳng thư mục Caches/Delta/)
//  Dùng zlib (libz có sẵn trên iOS) để đọc trực tiếp cấu trúc ZIP, không cần
//  thêm thư viện bên thứ 3. Chạy 1 lần duy nhất, có marker để bỏ qua lần sau.
// ============================================================================

// Đọc số little-endian trực tiếp từ buffer (iOS arm64 là little-endian)
static inline uint16_t ar_rd16(const uint8_t *p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}
static inline uint32_t ar_rd32(const uint8_t *p) {
    return (uint32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24));
}
static inline uint64_t ar_rd64(const uint8_t *p) {
    return (uint64_t)ar_rd32(p) | ((uint64_t)ar_rd32(p + 4) << 32);
}

// Tạo cây thư mục đệ quy (giống mkdir -p)
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

// Bung 1 luồng deflate (raw, windowBits = -15) từ bộ nhớ ra file
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
        if (ret == Z_BUF_ERROR && strm.avail_in == 0) break; // Hết dữ liệu vào
    } while (ret != Z_STREAM_END);

    inflateEnd(&strm);
    return true;
}

// Giải nén toàn bộ archive ZIP tại zipPath ra thư mục destDir (destDir kết thúc bằng '/')
static void ar_extractZip(const char *zipPath, const char *destDir) {
    int fd = open(zipPath, O_RDONLY);
    if (fd < 0) return;

    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size < 22) { close(fd); return; }
    size_t fileSize = (size_t)st.st_size;

    uint8_t *base = (uint8_t *)mmap(NULL, fileSize, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (base == MAP_FAILED) return;

    // Tìm End Of Central Directory (chữ ký 0x06054b50) quét ngược từ cuối file
    const uint8_t *eocd = NULL;
    size_t maxBack = (fileSize < (22 + 65535)) ? fileSize : (22 + 65535);
    for (size_t i = 22; i <= maxBack; i++) {
        const uint8_t *p = base + fileSize - i;
        if (ar_rd32(p) == 0x06054b50) { eocd = p; break; }
    }
    if (!eocd) { munmap(base, fileSize); return; }

    uint64_t totalEntries = ar_rd16(eocd + 10);
    uint64_t cdOffset     = ar_rd32(eocd + 16);

    // ===== ZIP64: nếu EOCD thường dùng giá trị sentinel thì lấy số thật từ bản ghi ZIP64 =====
    // Ngay TRƯỚC EOCD là "ZIP64 EOCD Locator" (chữ ký 0x07064b50, dài 20 byte). Locator trỏ tới
    // "ZIP64 EOCD Record" (chữ ký 0x06064b50) chứa số entry và offset CD ở dạng 64-bit.
    if ((size_t)(eocd - base) >= 20) {
        const uint8_t *loc = eocd - 20;
        if (ar_rd32(loc) == 0x07064b50) {
            uint64_t z64Off = ar_rd64(loc + 8);
            if (z64Off + 56 <= fileSize && ar_rd32(base + z64Off) == 0x06064b50) {
                const uint8_t *z64 = base + z64Off;
                totalEntries = ar_rd64(z64 + 32); // Tổng số entry (64-bit)
                cdOffset     = ar_rd64(z64 + 48); // Offset Central Directory (64-bit)
            }
        }
    }

    if (cdOffset >= fileSize) { munmap(base, fileSize); return; }

    const uint8_t *cd = base + cdOffset;
    char pathBuf[2048];
    char nameBuf[1024];

    for (uint64_t e = 0; e < totalEntries; e++) {
        if ((size_t)(cd - base) + 46 > fileSize) break;
        if (ar_rd32(cd) != 0x02014b50) break; // Không còn Central Directory Header hợp lệ

        uint16_t method     = ar_rd16(cd + 10);
        uint32_t uncompSize = ar_rd32(cd + 24);
        uint64_t compSize   = ar_rd32(cd + 20);
        uint16_t nameLen    = ar_rd16(cd + 28);
        uint16_t extraLen   = ar_rd16(cd + 30);
        uint16_t commentLen = ar_rd16(cd + 32);
        uint64_t localOff   = ar_rd32(cd + 42);
        const uint8_t *nameP = cd + 46;

        // ===== ZIP64: các trường 32-bit bị 0xFFFFFFFF -> giá trị thật nằm trong extra field 0x0001 =====
        // Trong extra, các trường 64-bit xuất hiện THEO THỨ TỰ cố định và CHỈ khi trường 32-bit
        // tương ứng bị sentinel: uncompressed(8) -> compressed(8) -> localHeaderOffset(8) -> disk(4).
        {
            const uint8_t *ex = nameP + nameLen;
            const uint8_t *exEnd = ex + extraLen;
            while (ex + 4 <= exEnd) {
                uint16_t exId  = ar_rd16(ex);
                uint16_t exSz  = ar_rd16(ex + 2);
                const uint8_t *fld = ex + 4;
                if (ex + 4 + exSz > exEnd) break;
                if (exId == 0x0001) {
                    if (uncompSize == 0xFFFFFFFF && fld + 8 <= ex + 4 + exSz) fld += 8; // bỏ qua uncompressed
                    if (compSize   == 0xFFFFFFFF && fld + 8 <= ex + 4 + exSz) { compSize = ar_rd64(fld); fld += 8; }
                    if (localOff   == 0xFFFFFFFF && fld + 8 <= ex + 4 + exSz) { localOff = ar_rd64(fld); fld += 8; }
                    break;
                }
                ex += 4 + exSz;
            }
        }

        // Nhảy sang entry kế tiếp trước khi xử lý (tránh quên cập nhật)
        const uint8_t *nextCd = nameP + nameLen + extraLen + commentLen;

        if (nameLen == 0 || nameLen >= sizeof(nameBuf)) { cd = nextCd; continue; }
        memcpy(nameBuf, nameP, nameLen);
        nameBuf[nameLen] = '\0';
        cd = nextCd;

        // Chặn path traversal: bỏ qua đường dẫn tuyệt đối hoặc chứa ".."
        if (nameBuf[0] == '/' || strstr(nameBuf, "..") != NULL) continue;

        int w = snprintf(pathBuf, sizeof(pathBuf), "%s%s", destDir, nameBuf);
        if (w < 0 || w >= (int)sizeof(pathBuf)) continue;

        // Entry là thư mục
        size_t nl = strlen(nameBuf);
        if (nameBuf[nl - 1] == '/') { ar_mkpath(pathBuf); continue; }

        // Đảm bảo thư mục cha tồn tại
        char parent[2048];
        snprintf(parent, sizeof(parent), "%s", pathBuf);
        char *slash = strrchr(parent, '/');
        if (slash) { *slash = '\0'; ar_mkpath(parent); }

        // Định vị dữ liệu thật qua Local File Header
        if ((size_t)localOff + 30 > fileSize) continue;
        const uint8_t *lh = base + localOff;
        if (ar_rd32(lh) != 0x04034b50) continue;
        uint16_t lhNameLen  = ar_rd16(lh + 26);
        uint16_t lhExtraLen = ar_rd16(lh + 28);
        const uint8_t *data = lh + 30 + lhNameLen + lhExtraLen;
        if ((size_t)(data - base) + compSize > fileSize) continue;

        int outFd = open(pathBuf, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (outFd < 0) continue;
        if (method == 0) {              // Store (không nén)
            write(outFd, data, compSize);
        } else if (method == 8) {       // Deflate
            ar_inflateToFd(data, compSize, outFd);
        }
        close(outFd);
        g_deltaExtractedFiles.fetch_add(1, std::memory_order_relaxed);
    }

    munmap(base, fileSize);
}

// Marker chứa "mtime:size" của Delta.zip -> chỉ giải nén lại khi gói thay đổi
static bool ar_needExtract(const char *markerPath, const struct stat *zipSt) {
    int fd = open(markerPath, O_RDONLY);
    if (fd < 0) return true;
    char buf[128];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return true;
    buf[n] = '\0';
    char expected[128];
    snprintf(expected, sizeof(expected), "%lld:%lld",
             (long long)zipSt->st_mtime, (long long)zipSt->st_size);
    return strcmp(buf, expected) != 0;
}
static void ar_writeMarker(const char *markerPath, const struct stat *zipSt) {
    int fd = open(markerPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return;
    char buf[128];
    int w = snprintf(buf, sizeof(buf), "%lld:%lld",
                     (long long)zipSt->st_mtime, (long long)zipSt->st_size);
    write(fd, buf, w);
    close(fd);
}

// ============================================================================
//  PHẦN 2: ĐỊNH TUYẾN TRAFFIC (Bẻ hướng mọi lời gọi file của game sang Delta/)
// ============================================================================

inline const char* redirectAllTrafficPath(const char *path) {
    if (!path) return path;

    // LOG CHẨN ĐOÁN: đếm MỌI lời gọi file qua hook (kể cả path ngoài bundle) + lưu path gần nhất.
    // Nếu con số này = 0 nghĩa là hook KHÔNG bắt được -> lỗi ở tầng MSHookFunction/dlsym.
    // Nếu > 0 mà "Trong bundle" = 0 -> game không đọc file trong .app (đọc ở Documents/Caches).
    g_deltaTotalCalls.fetch_add(1, std::memory_order_relaxed);
    strncpy(g_deltaLastAnyPath, path, sizeof(g_deltaLastAnyPath) - 1);
    g_deltaLastAnyPath[sizeof(g_deltaLastAnyPath) - 1] = '\0';

    if (g_bundlePrefixLen == 0 || g_moddedPrefixLen == 0) return path;

    // BƯỚC 1: Kiểm tra xem file yêu cầu có nằm trong App Bundle hay không
    if (strncmp(path, g_bundlePrefixC, g_bundlePrefixLen) != 0) {
        return path;
    }
    g_deltaBundleCalls.fetch_add(1, std::memory_order_relaxed);

    // BƯỚC 1.5: CHỐNG BẺ HƯỚNG LẶP
    // Thư mục Delta/ nằm ngay trong bundle (FreeFire.app/Delta/), nên bản thân nó cũng khớp
    // tiền tố bundle. Nếu path đã trỏ vào Delta/ rồi thì để nguyên, tránh thành Delta/Delta/...
    if (strncmp(path, g_moddedPrefixC, g_moddedPrefixLen) == 0) {
        return path;
    }

    // BƯỚC 2: Tạo bộ đệm thread_local an toàn đa luồng, triệt tiêu việc cấp phát RAM (Zero Allocation)
    static thread_local char redirectedBuffer[2048];

    // Lấy phần đường dẫn tương đối (bỏ phần tiền tố App Bundle đi)
    const char *relative = path + g_bundlePrefixLen;

    // Ghép đường dẫn mới hướng vào thư mục Delta trong bundle
    int written = snprintf(redirectedBuffer, sizeof(redirectedBuffer), "%s%s", g_moddedPrefixC, relative);
    if (written < 0 || written >= (int)sizeof(redirectedBuffer)) {
        return path; // Nếu đường dẫn quá dài vượt bộ đệm, fallback về file gốc để an toàn
    }

    // BƯỚC 3: ÉP ĐỌC TRONG DELTA - LUÔN LUÔN, KHÔNG CÓ NGOẠI LỆ.
    // Kiểm tra tồn tại chỉ để đếm hit/miss cho tab INFO chẩn đoán - KHÔNG dùng kết quả này để
    // quyết định fallback nữa (bản trước còn overlay "không có thì đọc gốc" khi g_deltaActive
    // chưa kịp bật, đúng lúc đó lại là lỗ hổng: file thiếu trong Delta bị lặng lẽ đọc bundle
    // gốc). Giờ: match tiền tố bundle là redirect thẳng vào Delta, file nào Delta không có thì
    // trả path Delta (không tồn tại) -> game báo thiếu file, tuyệt đối không đọc bản gốc.
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

// stat/lstat: Foundation & engine thường kiểm tra file tồn tại + kích thước TRƯỚC khi open.
// Phải bẻ hướng luôn thì các file CHỈ có trong Delta (không có trong bundle gốc) mới được game thấy.
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

            // Định tuyến toàn bộ traffic bundle vào thẳng thư mục Delta/ NẰM TRONG bundle
            // -> /var/containers/Bundle/Application/.../FreeFire.app/Delta/
            NSString *moddedDataDir = [bundlePath stringByAppendingString:@"/Delta/"];
            strncpy(g_moddedPrefixC, [moddedDataDir UTF8String], sizeof(g_moddedPrefixC) - 1);
            g_moddedPrefixLen = strlen(g_moddedPrefixC);

            // Dựng đường dẫn tới gói Delta.zip đặt NGAY TRONG bundle: FreeFire.app/Delta.zip
            NSString *zipPath = [bundlePath stringByAppendingString:@"/" DELTA_ZIP_BUNDLE_NAME];
            strncpy(g_deltaZipPathC, [zipPath UTF8String], sizeof(g_deltaZipPathC) - 1);
        }

        // ============ GIẢI NÉN DELTA.ZIP KHI MỞ GAME (chạy TRƯỚC khi cài hook) ============
        // Phải bung xong toàn bộ file mod ra FreeFire.app/Delta/ trước khi game bắt đầu đọc asset,
        // nếu không redirect sẽ trỏ vào thư mục rỗng và game vẫn đọc bundle gốc.
        if (g_moddedPrefixLen > 0 && g_deltaZipPathC[0]) {
            struct stat zipSt;
            if (stat(g_deltaZipPathC, &zipSt) == 0) {
                g_deltaZipFound.store(true, std::memory_order_relaxed);
                // Đảm bảo thư mục đích tồn tại
                ar_mkpath(g_moddedPrefixC);

                char markerPath[1152];
                snprintf(markerPath, sizeof(markerPath), "%s.delta_extracted", g_moddedPrefixC);

                // Chỉ giải nén khi chưa bung hoặc gói Delta.zip đã được cập nhật (mtime/size đổi)
                if (ar_needExtract(markerPath, &zipSt)) {
                    g_deltaExtractRan.store(true, std::memory_order_relaxed);
                    ar_extractZip(g_deltaZipPathC, g_moddedPrefixC);
                    ar_writeMarker(markerPath, &zipSt);
                }

                // BẬT chế độ ép đọc toàn bộ trong Delta: chỉ bật khi thư mục Delta/ thực sự
                // tồn tại (giải nén xong). Nếu vì lý do gì Delta không có, để nguyên overlay
                // an toàn -> game vẫn đọc bundle gốc, không tự brick lúc mở.
                struct stat deltaDirSt;
                if (stat(g_moddedPrefixC, &deltaDirSt) == 0 && S_ISDIR(deltaDirSt.st_mode)) {
                    g_deltaActive.store(true, std::memory_order_relaxed);
                }
            }
        }
    }

    // ============ CÀI HOOK I/O BẰNG FISHHOOK ============
    // MSHookFunction KHÔNG hook được open/stat/... vì chúng nằm trong dyld shared cache
    // (vùng code read-only Apple bảo vệ). Fishhook không patch code mà chỉ tráo con trỏ
    // trong bảng import (__DATA) của từng image -> chạy được cả khi KHÔNG jailbreak, và
    // bắt được luôn các hàm trong shared cache. Đây là cách chuẩn để chặn I/O của game.

    // Lấy sẵn con trỏ hàm gốc qua dlsym làm mạng an toàn (phòng khi fishhook không bắt
    // được binding cho 1 image nào đó -> orig_* vẫn trỏ vào libc thật, không bị NULL).
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

    // rebind_symbols trả 0 nếu chạy trót lọt. Con số thật để soi hook sống hay không vẫn
    // là "Tổng lời gọi file" bên tab INFO (fishhook ăn thì nó tăng ngay lập tức).
    g_deltaHooksOK.store(rebindRet == 0 ? 0x1F : 0, std::memory_order_relaxed);
}
