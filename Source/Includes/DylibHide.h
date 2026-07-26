#pragma once
// ============================================================================
//  DylibHide.h - giấu NHIỀU dylib khỏi 4 API liệt kê dyld image chuẩn:
//  _dyld_image_count / _dyld_get_image_name / _dyld_get_image_header /
//  _dyld_get_image_vmaddr_slide.
//
//  GIẤU: chính Delta.dylib + TẤT CẢ injection framework (libsubstrate, CydiaSubstrate,
//  SubstrateLoader, SubstrateInserter, libsubstitute, libellekit, ...) - bất kỳ image nào
//  có tên chứa "substrate", "substitute", hoặc "ellekit" (case-insensitive).
//
//  DÙNG FISHHOOK (rebind_symbols), KHÔNG DÙNG MSHookFunction: 4 hàm _dyld_image_count/
//  _dyld_get_image_name/_dyld_get_image_header/_dyld_get_image_vmaddr_slide nằm trong
//  libdyld.dylib - 1 phần của dyld SHARED CACHE. MSHookFunction KHÔNG hook được hàm nằm
//  trong shared cache.
// ============================================================================
#import <mach-o/dyld.h>
#import <mach-o/loader.h>
#import <dlfcn.h>
#import <string.h>
#include <atomic>
#include <algorithm>
#import "fishhook.h"

// Tối đa 16 dylib cần giấu (Delta + substrate + substitute + ellekit + dự phòng) - trên thực
// tế hiếm khi quá 5-6, nhưng để dư cho an toàn.
#define DYLIB_HIDE_MAX 16

static uint32_t g_hideIndices[DYLIB_HIDE_MAX];  // sorted ASC
static std::atomic<int> g_hideCount{0};          // 0 = chưa tìm / không có gì để giấu

typedef uint32_t (*ORIG_dyld_image_count)(void);
static ORIG_dyld_image_count orig_dyld_image_count = NULL;
static uint32_t hooked_dyld_image_count(void) {
    uint32_t real = orig_dyld_image_count();
    int hidden = g_hideCount.load(std::memory_order_relaxed);
    return (hidden > 0 && (uint32_t)hidden <= real) ? real - (uint32_t)hidden : real;
}

// Ánh xạ index "đã giấu" -> index THẬT: với N image bị giấu, caller thấy danh sách có
// (real_count - N) entry liên tục. Cần dịch index của caller thành index thật bằng cách
// đếm bao nhiêu hidden index <= current adjusted index, rồi nhảy qua chúng.
static inline uint32_t DylibHide_mapIndex(uint32_t visibleIndex) {
    int count = g_hideCount.load(std::memory_order_relaxed);
    if (count <= 0) return visibleIndex;

    uint32_t realIndex = visibleIndex;
    for (int i = 0; i < count; i++) {
        if (g_hideIndices[i] <= realIndex) {
            realIndex++;  // nhảy qua slot bị giấu
        } else {
            break;  // sorted - không có hidden index nào nhỏ hơn nữa
        }
    }
    return realIndex;
}

typedef const char *(*ORIG_dyld_get_image_name)(uint32_t);
static ORIG_dyld_get_image_name orig_dyld_get_image_name = NULL;
static const char *hooked_dyld_get_image_name(uint32_t index) {
    return orig_dyld_get_image_name(DylibHide_mapIndex(index));
}

typedef const struct mach_header *(*ORIG_dyld_get_image_header)(uint32_t);
static ORIG_dyld_get_image_header orig_dyld_get_image_header = NULL;
static const struct mach_header *hooked_dyld_get_image_header(uint32_t index) {
    return orig_dyld_get_image_header(DylibHide_mapIndex(index));
}

typedef intptr_t (*ORIG_dyld_get_image_vmaddr_slide)(uint32_t);
static ORIG_dyld_get_image_vmaddr_slide orig_dyld_get_image_vmaddr_slide = NULL;
static intptr_t hooked_dyld_get_image_vmaddr_slide(uint32_t index) {
    return orig_dyld_get_image_vmaddr_slide(DylibHide_mapIndex(index));
}

// Case-insensitive substring search (không dùng strcasestr vì không portable trên mọi toolchain)
static inline bool DylibHide_containsCI(const char *haystack, const char *needle) {
    if (!haystack || !needle) return false;
    size_t hLen = strlen(haystack), nLen = strlen(needle);
    if (nLen > hLen) return false;
    for (size_t i = 0; i <= hLen - nLen; i++) {
        bool match = true;
        for (size_t j = 0; j < nLen; j++) {
            char a = haystack[i + j], b = needle[j];
            if (a >= 'A' && a <= 'Z') a += 32;
            if (b >= 'A' && b <= 'Z') b += 32;
            if (a != b) { match = false; break; }
        }
        if (match) return true;
    }
    return false;
}

// Kiểm tra 1 image name có phải là dylib cần giấu hay không
static inline bool DylibHide_shouldHide(const char *imageName, const char *selfPath) {
    if (!imageName) return false;
    // Chính Delta.dylib (so khớp path chính xác)
    if (selfPath && strcmp(imageName, selfPath) == 0) return true;
    // Injection frameworks (substring, case-insensitive)
    if (DylibHide_containsCI(imageName, "substrate")) return true;
    if (DylibHide_containsCI(imageName, "substitute")) return true;
    if (DylibHide_containsCI(imageName, "ellekit")) return true;
    if (DylibHide_containsCI(imageName, "pogo")) return true;  // TrollStore loader
    return false;
}

// Quét toàn bộ danh sách image THẬT, đánh dấu tất cả image cần giấu.
// Luôn gọi orig_* (KHÔNG gọi qua bản đã hook) để thấy danh sách THẬT.
static void DylibHide_findHiddenIndices() {
    // Tìm path của chính mình qua dladdr
    const char *selfPath = NULL;
    Dl_info info;
    memset(&info, 0, sizeof(info));
    if (dladdr((void *)&DylibHide_findHiddenIndices, &info) && info.dli_fname) {
        selfPath = info.dli_fname;
    }

    uint32_t count = orig_dyld_image_count();
    int found = 0;
    for (uint32_t i = 0; i < count && found < DYLIB_HIDE_MAX; i++) {
        const char *name = orig_dyld_get_image_name(i);
        if (DylibHide_shouldHide(name, selfPath)) {
            g_hideIndices[found++] = i;
            DeltaVFS_debugLogf("DylibHide: se giau index %u (%s)", i, name ? name : "(null)");
        }
    }

    // Sort ascending (hầu như đã sorted vì quét tuần tự, nhưng để chắc chắn)
    if (found > 1) {
        std::sort(g_hideIndices, g_hideIndices + found);
    }

    g_hideCount.store(found, std::memory_order_relaxed);
    DeltaVFS_debugLogf("DylibHide: tim thay %d image can giau trong %u image tong cong", found, count);
}

// Gọi CÀNG SỚM CÀNG TỐT (constructor, trước khi game/Unity có cơ hội tự quét danh sách image
// lần nào). Rebind qua fishhook vì 4 hàm này nằm trong shared cache.
inline void DylibHide_install() {
    struct rebinding rebindings[4];
    int n = 0;
    rebindings[n].name = "_dyld_image_count";            rebindings[n].replacement = (void *)hooked_dyld_image_count;            rebindings[n].replaced = (void **)&orig_dyld_image_count;            n++;
    rebindings[n].name = "_dyld_get_image_name";         rebindings[n].replacement = (void *)hooked_dyld_get_image_name;         rebindings[n].replaced = (void **)&orig_dyld_get_image_name;         n++;
    rebindings[n].name = "_dyld_get_image_header";       rebindings[n].replacement = (void *)hooked_dyld_get_image_header;       rebindings[n].replaced = (void **)&orig_dyld_get_image_header;       n++;
    rebindings[n].name = "_dyld_get_image_vmaddr_slide"; rebindings[n].replacement = (void *)hooked_dyld_get_image_vmaddr_slide; rebindings[n].replaced = (void **)&orig_dyld_get_image_vmaddr_slide; n++;

    int ret = rebind_symbols(rebindings, n);
    if (ret != 0 || !orig_dyld_image_count || !orig_dyld_get_image_name) {
        DeltaVFS_debugLogf("DylibHide: rebind_symbols that bai (ret=%d) cho 1 hoac nhieu ham dyld_* - huy, khong giau gi ca", ret);
        return;
    }
    DylibHide_findHiddenIndices();
}
