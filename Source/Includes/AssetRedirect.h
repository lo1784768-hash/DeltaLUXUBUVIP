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
#import <objc/runtime.h>
#import <dlfcn.h>

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
    int fd = open(zipPath, O_RDONLY);
    if (fd < 0) return;

    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size < 22) { close(fd); return; }
    size_t fileSize = (size_t)st.st_size;

    uint8_t *base = (uint8_t *)mmap(NULL, fileSize, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (base == MAP_FAILED) return;

    const uint8_t *eocd = NULL;
    size_t maxBack = (fileSize < (22 + 65535)) ? fileSize : (22 + 65535);
    for (size_t i = 22; i <= maxBack; i++) {
        const uint8_t *p = base + fileSize - i;
        if (ar_rd32(p) == 0x06054b50) { eocd = p; break; }
    }
    if (!eocd) { munmap(base, fileSize); return; }

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

    if (cdOffset >= fileSize) { munmap(base, fileSize); return; }

    const uint8_t *cd = base + cdOffset;
    char pathBuf[2048];
    char nameBuf[1024];

    for (uint64_t e = 0; e < totalEntries; e++) {
        if ((size_t)(cd - base) + 46 > fileSize) break;
        if (ar_rd32(cd) != 0x02014b50) break;

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
static void ar_writeMarker(const char *markerPath, const struct stat *zipSt) {
    int fd = open(markerPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return;
    char buf[128];
    int w = snprintf(buf, sizeof(buf), "%lld:%lld", (long long)zipSt->st_mtime, (long long)zipSt->st_size);
    write(fd, buf, w);
    close(fd);
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

        // 1. GIẢI NÉN PHẢI CHẠY TRƯỚC HẾT
        if (g_moddedPrefixLen > 0 && g_deltaZipPathC[0]) {
            struct stat zipSt;
            if (stat(g_deltaZipPathC, &zipSt) == 0) {
                g_deltaZipFound.store(true, std::memory_order_relaxed);
                ar_mkpath(g_moddedPrefixC);

                char markerPath[1152];
                snprintf(markerPath, sizeof(markerPath), "%s.delta_extracted", g_moddedPrefixC);

                if (ar_needExtract(markerPath, &zipSt)) {
                    g_deltaExtractRan.store(true, std::memory_order_relaxed);
                    ar_extractZip(g_deltaZipPathC, g_moddedPrefixC);
                    ar_writeMarker(markerPath, &zipSt);
                }

                struct stat deltaDirSt;
                if (stat(g_moddedPrefixC, &deltaDirSt) == 0 && S_ISDIR(deltaDirSt.st_mode)) {
                    g_deltaActive.store(true, std::memory_order_relaxed);
                }
            }
        }
        
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