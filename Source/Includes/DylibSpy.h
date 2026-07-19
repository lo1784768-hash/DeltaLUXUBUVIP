#pragma once
// ============================================================================
// DylibSpy - soi dylib B (mặc định: Monite.dylib, nằm trong Frameworks/) đang
// làm gì bên trong tiến trình game: gọi hàm nào, sửa vùng nhớ nào.
// ----------------------------------------------------------------------------
// 3 mảng độc lập, mỗi mảng có giới hạn riêng cần biết trước khi đọc số liệu:
//
//   1) TARGET INFO   - định vị image qua dyld (base/slide/đường dẫn). Chỉ có
//      khi Monite.dylib đã load - gọi lại DylibSpy_tick() mỗi frame (từ
//      updateMenu) để tự dò lại cho tới khi thấy, vì thứ tự nạp dylib giữa
//      tweak này và Monite.dylib không đảm bảo trước.
//
//   2) IMPORT/EXPORT - đọc thẳng LC_SYMTAB (không phụ thuộc classic bind hay
//      chained fixups - bảng symbol tự nó không đổi giữa 2 cơ chế) để liệt kê
//      symbol Monite.dylib import (N_UNDF, tức tên hàm nó gọi ra ngoài) và
//      export (N_SECT + N_EXT, hàm/biến nó cung cấp ra ngoài).
//
//   3) CALL TRACE    - dùng fishhook rebind_symbols_image() để patch CHỈ các
//      con trỏ lazy/non-lazy symbol pointer NẰM TRONG image của Monite.dylib
//      (không đụng game hay dylib khác) cho 1 danh sách hàm cố định, chọn vì
//      an toàn (chữ ký C thuần, không phải hàm biến đối/objc_msgSend - hook
//      generic objc_msgSend cần trampoline assembly giữ nguyên convention gọi
//      hàm, làm sai 1 ly là crash thẳng tiến trình game, nên KHÔNG làm ở bản
//      này) và có tín hiệu cao cho đúng câu hỏi "gọi hàm nào / sửa bộ nhớ đâu":
//      dlopen, dlsym, mmap, mprotect, vm_protect, vm_write.
//      CHỦ ĐỘNG, KHÔNG TỰ CHẠY: patch GOT là can thiệp trực tiếp vào runtime
//      của Monite.dylib, nếu đúng lúc nó đang tự gọi 1 trong 6 hàm trên cho
//      việc khởi tạo riêng (rất dễ xảy ra trong vài giây đầu sau khi app mở)
//      thì vá giữa chừng là rủi ro crash thật (từng làm app văng ngay lúc mở,
//      không sinh crash report vì đây là can thiệp runtime dylib khác chứ
//      không phải lỗi trong code của tweak này). Vì vậy chỉ chạy khi người
//      dùng chủ động bấm "Bắt đầu giám sát" ở tab SPY (DylibSpy_startMonitoring)
//      - xem thêm ghi chú ở g_spyMonitoringRequested. TARGET INFO (mục 1) vẫn
//      tự động vì chỉ đọc dyld image list, không đụng bộ nhớ Monite.dylib.
//      GIỚI HẠN: fishhook (bản Facebook gốc, xem Source/fishhook.c) chỉ hiểu
//      classic bind qua indirect symbol table - dylib build bằng Xcode mới
//      (min target iOS 15+, mặc định hiện nay) dùng chained fixups
//      (LC_DYLD_CHAINED_FIXUPS) thì các slot import sẽ KHÔNG hook được qua
//      đường này (không phải bug ở đây - tự vá GOT slot của chained fixups
//      đúng cách cần parse thêm cấu trúc dyld_chained_* và ghi sai 1 offset
//      là crash ngay, nên cố tình KHÔNG làm mù trong bản này). Tab SPY tự
//      phát hiện + báo rõ trường hợp này (DylibSpy_callTraceSummary) thay vì
//      hook im lặng rồi không hiểu sao 0 lời gọi nào được ghi.
//
//   4) MEM WATCH     - checksum (FNV-1a) theo khối 4KB vùng __TEXT của
//      UnityFramework + GameAssembly.dylib (giới hạn DYLIBSPY_MEMWATCH_CAP
//      byte đầu mỗi binary, tránh quét hết binary to hàng chục MB mỗi vòng),
//      lấy baseline 1 lần rồi diff định kỳ - khối nào đổi checksum thì log
//      lại rồi cập nhật baseline (không log lặp lại mỗi vòng cho cùng 1 khối
//      đã biết đổi). KHÔNG khẳng định CHÍNH XÁC Monite.dylib là thủ phạm
//      (không bẫy trang/page-fault để biết chắc ai ghi - làm vậy trong 1
//      tiến trình game đang chạy dễ crash nếu bẫy sai), chỉ là "có gì đó vừa
//      ghi đè code ở đây" - đủ để khoanh vùng rồi đối chiếu với CALL TRACE
//      (vd thấy mprotect/vm_protect ngay trước 1 dòng MEM-DIFF là dấu hiệu
//      khá chắc chắn chính Monite.dylib vừa patch chỗ đó).
// ============================================================================
#import <Foundation/Foundation.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <mach/mach.h>
#include <mach/vm_map.h>
#include <sys/mman.h>
#include <dlfcn.h>
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <utility>
#include <cstdint>
#include <cstring>
#include <cstdarg>
#include <cstdio>
#include <ctime>

#import "fishhook.h"

#ifndef DYLIBSPY_TARGET_NAME
#define DYLIBSPY_TARGET_NAME "Monite.dylib"
#endif

#ifndef LC_DYLD_CHAINED_FIXUPS
#define LC_DYLD_CHAINED_FIXUPS 0x80000034
#endif

#define DYLIBSPY_RING_LINES 80
#define DYLIBSPY_MEMWATCH_CHUNK 4096
// Chỉ soi 16MB đầu __TEXT mỗi binary - đa số điểm patch thực tế nằm ở hàm cụ
// thể (offset đã biết từ dump.cs), không cần quét hết binary to hàng chục MB.
#define DYLIBSPY_MEMWATCH_CAP (16 * 1024 * 1024)

// ============================================================================
//  RING BUFFER LOG (giống NetLogRing bên NetLog.h, tách riêng cho DylibSpy để
//  không lẫn traffic mạng với traffic "hàm nội bộ" vào chung 1 khung).
// ============================================================================
struct DylibSpyRing {
    char lines[DYLIBSPY_RING_LINES][160];
    int head = 0;
    unsigned int total = 0;
    std::mutex mutex;
};
static DylibSpyRing g_spyCallRing;  // log CALL TRACE (dlopen/dlsym/mmap/mprotect/vm_protect/vm_write)
static DylibSpyRing g_spyMemRing;   // log MEM WATCH (checksum đổi)

inline void dylibSpyLogTo(DylibSpyRing &ring, const char *fmt, ...) {
    char buf[136];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    time_t t = time(NULL);
    struct tm tmv;
    localtime_r(&t, &tmv);

    std::lock_guard<std::mutex> lock(ring.mutex);
    snprintf(ring.lines[ring.head], sizeof(ring.lines[0]), "%02d:%02d:%02d %s",
             tmv.tm_hour, tmv.tm_min, tmv.tm_sec, buf);
    ring.head = (ring.head + 1) % DYLIBSPY_RING_LINES;
    ring.total++;
}

inline NSString *dylibSpyRingSnapshot(DylibSpyRing &ring) {
    std::lock_guard<std::mutex> lock(ring.mutex);
    if (ring.total == 0) return @"(chưa ghi được gì)";
    unsigned int n = std::min(ring.total, (unsigned int)DYLIBSPY_RING_LINES);
    NSMutableString *s = [NSMutableString string];
    int last = (ring.head - 1 + DYLIBSPY_RING_LINES) % DYLIBSPY_RING_LINES;
    for (unsigned int i = 0; i < n; i++) {
        int idx = (last - (int)i + DYLIBSPY_RING_LINES * 2) % DYLIBSPY_RING_LINES;
        [s appendFormat:@"%s\n", ring.lines[idx]];
    }
    return s;
}

// ============================================================================
//  PHẦN 1: ĐỊNH VỊ TARGET (Monite.dylib) QUA DYLD
// ============================================================================
struct DylibSpyTarget {
    std::atomic<bool> found{false};
    const struct mach_header_64 *header = nullptr;
    intptr_t slide = 0;
    std::string path;
    bool hasChainedFixups = false;
};
static DylibSpyTarget g_spyTarget;
static std::mutex g_spyTargetMutex;

inline bool DylibSpy_targetFound() { return g_spyTarget.found.load(std::memory_order_relaxed); }

inline bool dylibSpyFindTarget() {
    if (DylibSpy_targetFound()) return true;
    std::lock_guard<std::mutex> lock(g_spyTargetMutex);
    if (DylibSpy_targetFound()) return true;

    uint32_t count = _dyld_image_count();
    for (uint32_t i = 0; i < count; i++) {
        const char *name = _dyld_get_image_name(i);
        if (!name || !strstr(name, DYLIBSPY_TARGET_NAME)) continue;

        g_spyTarget.header = (const struct mach_header_64 *)_dyld_get_image_header(i);
        g_spyTarget.slide  = _dyld_get_image_vmaddr_slide(i);
        g_spyTarget.path   = name;

        // Quét load command 1 lần để biết Monite.dylib dùng chained fixups hay
        // không - quyết định CALL TRACE có khả thi hay không (xem ghi chú đầu file).
        const uint8_t *cmdPtr = (const uint8_t *)g_spyTarget.header + sizeof(struct mach_header_64);
        for (uint32_t c = 0; c < g_spyTarget.header->ncmds; c++) {
            const struct load_command *lc = (const struct load_command *)cmdPtr;
            if (lc->cmd == LC_DYLD_CHAINED_FIXUPS) { g_spyTarget.hasChainedFixups = true; break; }
            cmdPtr += lc->cmdsize;
        }

        g_spyTarget.found.store(true, std::memory_order_release);
        return true;
    }
    return false;
}

inline NSString *DylibSpy_targetInfo() {
    if (!DylibSpy_targetFound()) {
        return [NSString stringWithFormat:@"(chưa thấy %s load trong tiến trình)", DYLIBSPY_TARGET_NAME];
    }
    return [NSString stringWithFormat:@"%s\nbase=0x%llx  slide=0x%llx\nchained fixups: %s\n%s",
            DYLIBSPY_TARGET_NAME,
            (unsigned long long)(uintptr_t)g_spyTarget.header,
            (unsigned long long)g_spyTarget.slide,
            g_spyTarget.hasChainedFixups ? "CÓ (call trace có thể thiếu 1 số hàm)" : "không (call trace hoạt động đầy đủ)",
            g_spyTarget.path.c_str()];
}

// ============================================================================
//  PHẦN 2: LIỆT KÊ IMPORT/EXPORT QUA LC_SYMTAB
// ============================================================================
struct DylibSpySymbols {
    std::vector<std::string> imports;
    std::vector<std::string> exports;
    bool parsed = false;
};
static DylibSpySymbols g_spySymbols;
static std::mutex g_spySymbolsMutex;

inline void dylibSpyParseSymbols() {
    if (g_spySymbols.parsed || !DylibSpy_targetFound()) return;
    if (!g_spyMonitoringRequested.load(std::memory_order_relaxed)) return;
    std::lock_guard<std::mutex> lock(g_spySymbolsMutex);
    if (g_spySymbols.parsed) return;

    const struct mach_header_64 *header = g_spyTarget.header;
    intptr_t slide = g_spyTarget.slide;

    const struct segment_command_64 *linkeditSeg = nullptr;
    const struct symtab_command *symtabCmd = nullptr;

    const uint8_t *cmdPtr = (const uint8_t *)header + sizeof(struct mach_header_64);
    for (uint32_t c = 0; c < header->ncmds; c++) {
        const struct load_command *lc = (const struct load_command *)cmdPtr;
        if (lc->cmd == LC_SEGMENT_64) {
            const struct segment_command_64 *seg = (const struct segment_command_64 *)lc;
            if (strcmp(seg->segname, SEG_LINKEDIT) == 0) linkeditSeg = seg;
        } else if (lc->cmd == LC_SYMTAB) {
            symtabCmd = (const struct symtab_command *)lc;
        }
        cmdPtr += lc->cmdsize;
    }
    if (!linkeditSeg || !symtabCmd) { g_spySymbols.parsed = true; return; }

    // Công thức chuẩn (giống fishhook.c's rebind_symbols_for_image): __LINKEDIT
    // không nhất thiết nằm ngay sau header trong bộ nhớ nạp, phải quy đổi qua
    // (vmaddr - fileoff) của chính segment __LINKEDIT rồi mới cộng offset trong
    // LC_SYMTAB (vốn là offset TRONG FILE, không phải địa chỉ chạy).
    uintptr_t linkeditBase = (uintptr_t)slide + linkeditSeg->vmaddr - linkeditSeg->fileoff;
    const struct nlist_64 *symtab = (const struct nlist_64 *)(linkeditBase + symtabCmd->symoff);
    const char *strtab = (const char *)(linkeditBase + symtabCmd->stroff);

    for (uint32_t i = 0; i < symtabCmd->nsyms; i++) {
        const struct nlist_64 &sym = symtab[i];
        if ((sym.n_type & N_STAB) != 0) continue; // bỏ debug symbol
        if (!(sym.n_type & N_EXT)) continue;       // chỉ quan tâm symbol external
        if (sym.n_un.n_strx == 0) continue;
        const char *nameC = strtab + sym.n_un.n_strx;
        if (!nameC[0]) continue;
        std::string name = nameC;

        uint8_t type = sym.n_type & N_TYPE;
        if (type == N_UNDF) {
            if (g_spySymbols.imports.size() < 500) g_spySymbols.imports.push_back(name);
        } else if (type == N_SECT) {
            if (g_spySymbols.exports.size() < 500) g_spySymbols.exports.push_back(name);
        }
    }
    g_spySymbols.parsed = true;
}

inline NSString *DylibSpy_symbolSummary() {
    if (!DylibSpy_targetFound()) return @"(chưa có target)";
    if (!g_spyMonitoringRequested.load(std::memory_order_relaxed)) return @"(chưa bật - bấm \"Bắt đầu giám sát\")";
    std::lock_guard<std::mutex> lock(g_spySymbolsMutex);
    if (!g_spySymbols.parsed) return @"(đang phân tích...)";

    NSMutableString *s = [NSMutableString string];
    [s appendFormat:@"IMPORT (%zu hàm gọi ra ngoài, tối đa 40 dòng đầu):\n", g_spySymbols.imports.size()];
    size_t importN = std::min(g_spySymbols.imports.size(), (size_t)40);
    for (size_t i = 0; i < importN; i++) [s appendFormat:@"  %s\n", g_spySymbols.imports[i].c_str()];

    [s appendFormat:@"\nEXPORT (%zu hàm/biến nó cung cấp, tối đa 20 dòng đầu):\n", g_spySymbols.exports.size()];
    size_t exportN = std::min(g_spySymbols.exports.size(), (size_t)20);
    for (size_t i = 0; i < exportN; i++) [s appendFormat:@"  %s\n", g_spySymbols.exports[i].c_str()];
    return s;
}

// ============================================================================
//  PHẦN 3: CALL TRACE - hook qua fishhook, CHỈ trong image Monite.dylib
// ============================================================================
static std::atomic<unsigned long long> g_spyCount_dlopen{0};
static std::atomic<unsigned long long> g_spyCount_dlsym{0};
static std::atomic<unsigned long long> g_spyCount_mmap{0};
static std::atomic<unsigned long long> g_spyCount_mprotect{0};
static std::atomic<unsigned long long> g_spyCount_vmprotect{0};
static std::atomic<unsigned long long> g_spyCount_vmwrite{0};

typedef void *(*dylibspy_dlopen_t)(const char *, int);
static dylibspy_dlopen_t orig_spy_dlopen = nullptr;
inline void *hooked_spy_dlopen(const char *path, int mode) {
    g_spyCount_dlopen.fetch_add(1, std::memory_order_relaxed);
    dylibSpyLogTo(g_spyCallRing, "dlopen(\"%s\")", path ? path : "(null)");
    return orig_spy_dlopen ? orig_spy_dlopen(path, mode) : NULL;
}

typedef void *(*dylibspy_dlsym_t)(void *, const char *);
static dylibspy_dlsym_t orig_spy_dlsym = nullptr;
inline void *hooked_spy_dlsym(void *handle, const char *symbol) {
    g_spyCount_dlsym.fetch_add(1, std::memory_order_relaxed);
    dylibSpyLogTo(g_spyCallRing, "dlsym(\"%s\")", symbol ? symbol : "(null)");
    return orig_spy_dlsym ? orig_spy_dlsym(handle, symbol) : NULL;
}

typedef void *(*dylibspy_mmap_t)(void *, size_t, int, int, int, off_t);
static dylibspy_mmap_t orig_spy_mmap = nullptr;
inline void *hooked_spy_mmap(void *addr, size_t len, int prot, int flags, int fd, off_t offset) {
    g_spyCount_mmap.fetch_add(1, std::memory_order_relaxed);
    dylibSpyLogTo(g_spyCallRing, "mmap(len=%zu prot=%d flags=%d fd=%d)", len, prot, flags, fd);
    return orig_spy_mmap ? orig_spy_mmap(addr, len, prot, flags, fd, offset) : MAP_FAILED;
}

typedef int (*dylibspy_mprotect_t)(void *, size_t, int);
static dylibspy_mprotect_t orig_spy_mprotect = nullptr;
inline int hooked_spy_mprotect(void *addr, size_t len, int prot) {
    g_spyCount_mprotect.fetch_add(1, std::memory_order_relaxed);
    dylibSpyLogTo(g_spyCallRing, "mprotect(addr=%p len=%zu prot=%d) <- thường đi trước 1 lần patch code", addr, len, prot);
    return orig_spy_mprotect ? orig_spy_mprotect(addr, len, prot) : -1;
}

typedef kern_return_t (*dylibspy_vmprotect_t)(vm_map_t, vm_address_t, vm_size_t, boolean_t, vm_prot_t);
static dylibspy_vmprotect_t orig_spy_vm_protect = nullptr;
inline kern_return_t hooked_spy_vm_protect(vm_map_t task, vm_address_t address, vm_size_t size, boolean_t setMax, vm_prot_t newProt) {
    g_spyCount_vmprotect.fetch_add(1, std::memory_order_relaxed);
    dylibSpyLogTo(g_spyCallRing, "vm_protect(addr=0x%llx size=%llu prot=%d)", (unsigned long long)address, (unsigned long long)size, newProt);
    return orig_spy_vm_protect ? orig_spy_vm_protect(task, address, size, setMax, newProt) : KERN_FAILURE;
}

typedef kern_return_t (*dylibspy_vmwrite_t)(vm_map_t, vm_address_t, vm_offset_t, mach_msg_type_number_t);
static dylibspy_vmwrite_t orig_spy_vm_write = nullptr;
inline kern_return_t hooked_spy_vm_write(vm_map_t task, vm_address_t address, vm_offset_t data, mach_msg_type_number_t size) {
    g_spyCount_vmwrite.fetch_add(1, std::memory_order_relaxed);
    dylibSpyLogTo(g_spyCallRing, "vm_write(addr=0x%llx size=%u) <- GHI ĐÈ BỘ NHỚ TRỰC TIẾP", (unsigned long long)address, size);
    return orig_spy_vm_write ? orig_spy_vm_write(task, address, data, size) : KERN_FAILURE;
}

static std::atomic<bool> g_spyHooksInstalled{false};
static std::atomic<unsigned int> g_spyHookedSymbolCount{0};
// Cờ CHỦ ĐỘNG - phải người dùng bấm nút "Bắt đầu giám sát" ở tab SPY thì mới
// bật, KHÔNG tự chạy ngay lúc thấy Monite.dylib load nữa. Lý do: cài call
// trace nghĩa là VÁ TRỰC TIẾP GOT của 1 dylib người khác (dlopen/dlsym/mmap/
// mprotect/vm_protect/vm_write) - nếu đúng lúc đó chính Monite.dylib đang tự
// gọi 1 trong 6 hàm này cho việc khởi tạo riêng của nó (rất có thể xảy ra
// trong vài giây đầu sau khi app mở), vá GOT giữa chừng là rủi ro crash thật
// sự, từng khiến app văng ngay lúc mở (không sinh crash report vì đây không
// phải lỗi trong code CỦA MÌNH mà là can thiệp vào runtime của dylib khác).
// Việc ĐỊNH VỊ (DylibSpy_tick -> dylibSpyFindTarget) vẫn tự động vì chỉ ĐỌC
// dyld image list, không đụng bộ nhớ Monite.dylib nên vô hại.
static std::atomic<bool> g_spyMonitoringRequested{false};

inline bool DylibSpy_monitoringStarted() { return g_spyHooksInstalled.load(std::memory_order_relaxed); }
// Đã XIN bật hay chưa (có thể true trước khi hooksInstalled true, nếu bấm bật
// lúc Monite.dylib chưa kịp load - DylibSpy_tick sẽ tự cài nốt khi thấy nó).
// UI dùng cờ này (không phải monitoringStarted) để đồng bộ vị trí switch,
// tránh switch tự nhảy về OFF trong lúc đang chờ target load.
inline bool DylibSpy_monitoringRequested() { return g_spyMonitoringRequested.load(std::memory_order_relaxed); }

inline void dylibSpyInstallCallTrace() {
    if (g_spyHooksInstalled.load(std::memory_order_relaxed)) return;
    if (!g_spyMonitoringRequested.load(std::memory_order_relaxed)) return;
    if (!DylibSpy_targetFound()) return;

    struct rebinding rebindings[] = {
        {"dlopen",     (void *)hooked_spy_dlopen,     (void **)&orig_spy_dlopen},
        {"dlsym",      (void *)hooked_spy_dlsym,      (void **)&orig_spy_dlsym},
        {"mmap",       (void *)hooked_spy_mmap,       (void **)&orig_spy_mmap},
        {"mprotect",   (void *)hooked_spy_mprotect,   (void **)&orig_spy_mprotect},
        {"vm_protect", (void *)hooked_spy_vm_protect, (void **)&orig_spy_vm_protect},
        {"vm_write",   (void *)hooked_spy_vm_write,   (void **)&orig_spy_vm_write},
    };
    rebind_symbols_image((void *)g_spyTarget.header, g_spyTarget.slide, rebindings,
                          sizeof(rebindings) / sizeof(rebindings[0]));

    unsigned int n = 0;
    if (orig_spy_dlopen) n++;
    if (orig_spy_dlsym) n++;
    if (orig_spy_mmap) n++;
    if (orig_spy_mprotect) n++;
    if (orig_spy_vm_protect) n++;
    if (orig_spy_vm_write) n++;
    g_spyHookedSymbolCount.store(n, std::memory_order_relaxed);
    g_spyHooksInstalled.store(true, std::memory_order_relaxed);
}

inline NSString *DylibSpy_callTraceSummary() {
    if (!DylibSpy_targetFound()) return [NSString stringWithFormat:@"(chưa thấy %s)", DYLIBSPY_TARGET_NAME];
    if (!g_spyMonitoringRequested.load(std::memory_order_relaxed)) {
        return @"(chưa bật - bấm \"Bắt đầu giám sát\" để vá GOT của Monite.dylib, xem ghi chú an toàn ở nút đó trước khi bật)";
    }
    unsigned int hooked = g_spyHookedSymbolCount.load(std::memory_order_relaxed);
    NSMutableString *s = [NSMutableString string];
    [s appendFormat:@"Đã hook %u/6 hàm theo dõi (dlopen/dlsym/mmap/mprotect/vm_protect/vm_write)\n", hooked];
    if (g_spyTarget.hasChainedFixups && hooked < 6) {
        [s appendString:@"⚠️ Monite.dylib dùng chained fixups - fishhook không patch được hết mọi hàm qua đường classic bind (xem ghi chú đầu DylibSpy.h)\n"];
    }
    [s appendFormat:@"dlopen=%llu  dlsym=%llu  mmap=%llu  mprotect=%llu  vm_protect=%llu  vm_write=%llu",
        g_spyCount_dlopen.load(std::memory_order_relaxed), g_spyCount_dlsym.load(std::memory_order_relaxed),
        g_spyCount_mmap.load(std::memory_order_relaxed), g_spyCount_mprotect.load(std::memory_order_relaxed),
        g_spyCount_vmprotect.load(std::memory_order_relaxed), g_spyCount_vmwrite.load(std::memory_order_relaxed)];
    return s;
}

inline NSString *DylibSpy_callTraceLog() { return dylibSpyRingSnapshot(g_spyCallRing); }

// ============================================================================
//  PHẦN 4: MEM WATCH - checksum định kỳ, phát hiện vùng __TEXT bị ghi đè
// ============================================================================
inline uint32_t dylibSpyFnv1a(const uint8_t *data, size_t len) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < len; i++) { h ^= data[i]; h *= 16777619u; }
    return h;
}

// Tìm địa chỉ chạy thật + kích thước segment __TEXT của 1 image theo tên (match
// con - giống getMemoryFileInfo bên MemoryUtils.h, nhưng tự viết riêng để không
// dính kiểu MemoryFileInfo trả về struct CHƯA khởi tạo khi không tìm thấy).
inline bool dylibSpyFindImageTextSegment(const char *imageNameSubstr, uintptr_t &outStart, size_t &outSize) {
    uint32_t count = _dyld_image_count();
    for (uint32_t i = 0; i < count; i++) {
        const char *name = _dyld_get_image_name(i);
        if (!name || !strstr(name, imageNameSubstr)) continue;

        const struct mach_header_64 *header = (const struct mach_header_64 *)_dyld_get_image_header(i);
        const uint8_t *cmdPtr = (const uint8_t *)header + sizeof(struct mach_header_64);
        for (uint32_t c = 0; c < header->ncmds; c++) {
            const struct load_command *lc = (const struct load_command *)cmdPtr;
            if (lc->cmd == LC_SEGMENT_64) {
                const struct segment_command_64 *seg = (const struct segment_command_64 *)lc;
                if (strcmp(seg->segname, SEG_TEXT) == 0) {
                    // __TEXT.vmaddr luôn = 0 trước slide cho dylib/framework (không có
                    // __PAGEZERO như executable chính) -> header (đã sau slide) CỘNG
                    // thẳng vmaddr ra đúng địa chỉ chạy, không cần cộng slide riêng.
                    outStart = (uintptr_t)header + seg->vmaddr;
                    outSize = (size_t)seg->vmsize;
                    return true;
                }
            }
            cmdPtr += lc->cmdsize;
        }
        return false;
    }
    return false;
}

struct DylibSpyWatchRegion {
    std::string label;
    uintptr_t start = 0;
    size_t size = 0; // đã cap theo DYLIBSPY_MEMWATCH_CAP
    std::vector<uint32_t> baseline; // checksum mỗi khối DYLIBSPY_MEMWATCH_CHUNK byte
};
static std::vector<DylibSpyWatchRegion> g_spyWatchRegions;
static std::mutex g_spyWatchMutex;
static std::atomic<bool> g_spyMemWatchEnabled{false};
static std::atomic<bool> g_spyBaselineBuilt{false};

inline void DylibSpy_setMemWatchEnabled(bool enabled) { g_spyMemWatchEnabled.store(enabled, std::memory_order_relaxed); }
inline bool DylibSpy_memWatchEnabled() { return g_spyMemWatchEnabled.load(std::memory_order_relaxed); }

inline void dylibSpyBuildBaseline() {
    if (g_spyBaselineBuilt.load(std::memory_order_relaxed)) return;
    std::lock_guard<std::mutex> lock(g_spyWatchMutex);
    if (g_spyBaselineBuilt.load(std::memory_order_relaxed)) return;

    // UnityFramework = binary engine gốc (nơi mọi offset game_sdk trong ESP.h
    // trỏ tới), GameAssembly.dylib = IL2CPP C# đã biên dịch (xem SystemInfoSpoof.h)
    // - 1 dylib B muốn patch logic game gần như chắc chắn nhắm vào 1 trong 2.
    static const char *kTargets[] = {"UnityFramework", "GameAssembly.dylib"};
    for (const char *t : kTargets) {
        uintptr_t start = 0; size_t size = 0;
        if (!dylibSpyFindImageTextSegment(t, start, size)) continue;
        if (size > DYLIBSPY_MEMWATCH_CAP) size = DYLIBSPY_MEMWATCH_CAP;
        if (size == 0) continue;

        DylibSpyWatchRegion region;
        region.label = t;
        region.start = start;
        region.size = size;

        size_t chunkCount = (size + DYLIBSPY_MEMWATCH_CHUNK - 1) / DYLIBSPY_MEMWATCH_CHUNK;
        region.baseline.resize(chunkCount, 0);
        for (size_t i = 0; i < chunkCount; i++) {
            size_t off = i * DYLIBSPY_MEMWATCH_CHUNK;
            size_t len = std::min((size_t)DYLIBSPY_MEMWATCH_CHUNK, size - off);
            region.baseline[i] = dylibSpyFnv1a((const uint8_t *)(start + off), len);
        }
        g_spyWatchRegions.push_back(std::move(region));
    }
    g_spyBaselineBuilt.store(true, std::memory_order_relaxed);
}

// Gọi định kỳ (không phải mỗi frame - việc này đọc/checksum hàng MB bộ nhớ) từ
// 1 hàng đợi nền, CHỈ khi công tắc bật (Vars-side toggle, xem Menu.mm). So mỗi
// khối với baseline; khối nào đổi thì log 1 dòng rồi cập nhật lại baseline của
// chính khối đó - tránh log lặp lại mỗi vòng cho cùng 1 chỗ đã biết là đổi.
// Giới hạn 5 dòng log mỗi vùng mỗi lượt quét để không tràn ring buffer nếu 1
// đợt cập nhật/codegen hợp lệ của game đổi hàng loạt khối cùng lúc.
inline void dylibSpyScanForChanges() {
    if (!DylibSpy_memWatchEnabled()) return;
    dylibSpyBuildBaseline();

    std::lock_guard<std::mutex> lock(g_spyWatchMutex);
    for (auto &region : g_spyWatchRegions) {
        size_t chunkCount = region.baseline.size();
        int loggedThisPass = 0;
        for (size_t i = 0; i < chunkCount && loggedThisPass < 5; i++) {
            size_t off = i * DYLIBSPY_MEMWATCH_CHUNK;
            size_t len = std::min((size_t)DYLIBSPY_MEMWATCH_CHUNK, region.size - off);
            uint32_t now = dylibSpyFnv1a((const uint8_t *)(region.start + off), len);
            if (now != region.baseline[i]) {
                dylibSpyLogTo(g_spyMemRing, "MEM-DIFF %s +0x%zx (%zu byte đổi checksum kể từ baseline)",
                              region.label.c_str(), off, len);
                region.baseline[i] = now;
                loggedThisPass++;
            }
        }
    }
}

inline NSString *DylibSpy_memWatchLog() { return dylibSpyRingSnapshot(g_spyMemRing); }

// Gọi từ nút/switch "Bắt đầu giám sát" trên tab SPY (hành động CHỦ ĐỘNG của
// người dùng, không tự chạy) - bật cờ rồi cài hook + parse symbol NGAY nếu
// target đã thấy sẵn; nếu Monite.dylib chưa load thì DylibSpy_tick sẽ tự làm
// nốt phần này ngay khi thấy nó xuất hiện ở lần tick kế tiếp.
inline void DylibSpy_startMonitoring() {
    g_spyMonitoringRequested.store(true, std::memory_order_relaxed);
    if (DylibSpy_targetFound()) {
        dylibSpyInstallCallTrace();
        dylibSpyParseSymbols();
    }
}

// ============================================================================
//  ENTRY POINT - gọi mỗi frame từ Menu.mm's updateMenu (rẻ). CHỈ tự động dò
//  lại Monite.dylib (đọc dyld image list, vô hại) - KHÔNG tự cài call trace/
//  parse symbol nữa, phải đợi DylibSpy_startMonitoring() từ tab SPY (xem ghi
//  chú ở g_spyMonitoringRequested).
// ============================================================================
inline void DylibSpy_tick() {
    if (!DylibSpy_targetFound()) {
        dylibSpyFindTarget();
        return;
    }
    if (!g_spyMonitoringRequested.load(std::memory_order_relaxed)) return;
    if (!g_spyHooksInstalled.load(std::memory_order_relaxed)) dylibSpyInstallCallTrace();
    if (!g_spySymbols.parsed) dylibSpyParseSymbols();
}
