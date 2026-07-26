// DlsymSpoof.h - hook chính dlsym() qua fishhook, nhưng CHỈ trên 2 image cụ thể
// (UnityFramework + binary chính) thay vì toàn bộ process - tránh crash-loop do hook global
// va chạm với ObjC/Swift runtime liên tục gọi dlsym() lúc dyld còn đang init.
//
// MỤC ĐÍCH: nếu Free Fire tự dlsym("_dyld_get_image_name") thay vì gọi qua import tĩnh,
// DylibHide.h (fishhook vá GOT) bị bỏ qua hoàn toàn — dlsym đọc export trie trực tiếp.
// Hook dlsym() trả về bản đã giấu dylib thay vì hàm thật.
//
// PHẠM VI: CHỈ redirect dlsym() resolve cho _dyld_get_image_name, _dyld_get_image_header,
// _dyld_image_count. Các symbol khác passthrough.
//
// LƯU Ý TƯƠNG THÍCH VỚI DylibSpy.h: file đó CŨNG hook "dlsym" nhưng qua
// rebind_symbols_image() (chỉ vá GOT của RIÊNG Monite.dylib) và CHỈ khi user bấm "Bắt đầu
// giám sát" - luôn cài SAU constructor này. Chuỗi gọi vẫn đúng thứ tự.
#pragma once
#import <Foundation/Foundation.h>
#import <dlfcn.h>
#import <string.h>
#import <mach-o/dyld.h>
#include "fishhook.h"
#include "DylibHide.h"  // dùng lại hooked_dyld_get_image_name/header/count (static, cùng TU)

extern void DeltaVFS_debugLog(const char *msg);
extern void DeltaVFS_debugLogf(const char *fmt, ...);

typedef void *(*ORIG_dlsym)(void *, const char *);
static ORIG_dlsym orig_dlsym_real = NULL;

static void *hooked_dlsym(void *handle, const char *symbol) {
    void *real = orig_dlsym_real(handle, symbol);
    if (!symbol) return real;

    // Chỉ thay thế khi dlsym THẬT SỰ trả về đúng địa chỉ hàm thật (real == orig_* đã
    // resolve sẵn trong DylibHide_install()) - không thay thế nhầm symbol trùng tên.
    if (strcmp(symbol, "_dyld_get_image_name") == 0 && real == (void *)orig_dyld_get_image_name) {
        return (void *)hooked_dyld_get_image_name;
    }
    if (strcmp(symbol, "_dyld_get_image_header") == 0 && real == (void *)orig_dyld_get_image_header) {
        return (void *)hooked_dyld_get_image_header;
    }
    if (strcmp(symbol, "_dyld_image_count") == 0 && real == (void *)orig_dyld_image_count) {
        return (void *)hooked_dyld_image_count;
    }
    if (strcmp(symbol, "_dyld_get_image_vmaddr_slide") == 0 && real == (void *)orig_dyld_get_image_vmaddr_slide) {
        return (void *)hooked_dyld_get_image_vmaddr_slide;
    }

    // Quan sát thuần tuý (không sửa gì) - xem Free Fire có thực sự dlsym các hàm nhạy cảm
    if (strcmp(symbol, "task_info") == 0 || strcmp(symbol, "CC_MD5") == 0 ||
        strcmp(symbol, "ptrace") == 0 || strcmp(symbol, "csops") == 0 || strcmp(symbol, "sysctl") == 0) {
        DeltaVFS_debugLogf("DlsymSpoof: quan sat - co ai do dlsym('%s')", symbol);
    }

    return real;
}

// Tìm mach_header của 1 image theo tên (substring, case-insensitive)
static const struct mach_header *DlsymSpoof_findImage(const char *nameSubstring) {
    if (!orig_dyld_image_count || !orig_dyld_get_image_name || !orig_dyld_get_image_header) return NULL;
    uint32_t count = orig_dyld_image_count();
    for (uint32_t i = 0; i < count; i++) {
        const char *name = orig_dyld_get_image_name(i);
        if (name && DylibHide_containsCI(name, nameSubstring)) {
            return orig_dyld_get_image_header(i);
        }
    }
    return NULL;
}

inline void installDlsymSpoof() {
    // rebind_symbols_image() thay vì rebind_symbols() - CHỈ vá GOT của image chỉ định,
    // không đụng framework hệ thống, tránh crash-loop.
    struct rebinding rebindings[1];
    rebindings[0].name = "dlsym";
    rebindings[0].replacement = (void *)hooked_dlsym;
    rebindings[0].replaced = (void **)&orig_dlsym_real;

    int ok = 0, fail = 0;

    // Target 1: UnityFramework (game code chính)
    const struct mach_header *uf = DlsymSpoof_findImage("UnityFramework");
    if (uf) {
        int ret = rebind_symbols_image((void *)uf, orig_dyld_get_image_vmaddr_slide(0), // slide sẽ đúng khi dùng header
                                        rebindings, 1);
        // rebind_symbols_image cần slide đúng - tìm lại
        uint32_t count = orig_dyld_image_count();
        for (uint32_t i = 0; i < count; i++) {
            if (orig_dyld_get_image_header(i) == uf) {
                ret = rebind_symbols_image((void *)uf, orig_dyld_get_image_vmaddr_slide(i),
                                            rebindings, 1);
                break;
            }
        }
        if (ret == 0) { ok++; } else { fail++; }
        DeltaVFS_debugLogf("DlsymSpoof: rebind dlsym trong UnityFramework ret=%d", ret);
    } else {
        DeltaVFS_debugLog("DlsymSpoof: khong tim thay UnityFramework - bo qua");
    }

    // Target 2: binary chính (freefireth / freefiremax)
    const struct mach_header *main_hdr = DlsymSpoof_findImage("freefir");
    if (!main_hdr) {
        // Fallback: image index 0 luôn là binary chính
        main_hdr = orig_dyld_get_image_header(0);
    }
    if (main_hdr && main_hdr != uf) {
        uint32_t count = orig_dyld_image_count();
        for (uint32_t i = 0; i < count; i++) {
            if (orig_dyld_get_image_header(i) == main_hdr) {
                int ret = rebind_symbols_image((void *)main_hdr, orig_dyld_get_image_vmaddr_slide(i),
                                                rebindings, 1);
                if (ret == 0) { ok++; } else { fail++; }
                DeltaVFS_debugLogf("DlsymSpoof: rebind dlsym trong main binary ret=%d", ret);
                break;
            }
        }
    }

    DeltaVFS_debugLogf("DlsymSpoof: xong - %d image ok, %d fail", ok, fail);
}
