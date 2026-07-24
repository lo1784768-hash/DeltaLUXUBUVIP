#pragma once
// TaskDyldInfoSpoof.h - fishhook task_info() để giấu CHÍNH Delta.dylib khỏi bất kỳ code nào tự hỏi
// "process này có nạp dylib nào khả nghi không" qua đường task_info(TASK_DYLD_INFO), một tầng THẤP
// HƠN hẳn 4 hàm _dyld_get_image_name/_dyld_get_image_header mà DylibHide.h đang giấu.
//
// BỐI CẢNH: disassemble trực tiếp bảng thay thế hàm của Monite.dylib (FUN_000add00, xem
// MoniteAnalysis/ghidra_decompiled_all.txt) cho thấy họ hook CẢ task_info, không chỉ 4 hàm dyld_*
// - hàm thay thế của họ (FUN_000ad944 trong Monite.dylib) gọi task_info() THẬT trước, rồi CHỈ khi
// flavor == 0x11 (TASK_DYLD_INFO) mới can thiệp: duyệt qua struct dyld_all_image_infos.infoArray
// (danh sách dylib đã nạp thật), lọc bỏ 1 entry khớp CHÍNH mình, dựng lại mảng đã lọc, rồi trả về
// con trỏ tới bản dựng lại đó thay vì bản thật. Khác với CC_MD5 (hàm thay thế của Monite nhảy
// THẲNG vào vùng ảo hoá `.vlizer`, không đọc được gì - xem memory unityframework-syscall-patch),
// task_info ĐỌC ĐƯỢC bình thường, đủ để dịch lại đúng kỹ thuật bằng fishhook chuẩn (không cần đụng
// UnityFramework/raw syscall gì cả).
//
// CHƯA KIỂM CHỨNG TRÊN THIẾT BỊ THẬT.
#import <Foundation/Foundation.h>
#import <mach/mach.h>
#import <mach-o/dyld.h>
#import <mach-o/dyld_images.h>
#import <dlfcn.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "fishhook.h"

extern void DeltaVFS_debugLog(const char *msg);
extern void DeltaVFS_debugLogf(const char *fmt, ...);

typedef kern_return_t (*ORIG_task_info)(task_name_t, task_flavor_t, task_info_t, mach_msg_type_number_t *);
static ORIG_task_info orig_task_info_fn = NULL;

// dladdr() trên 1 hàm nằm THẲNG trong Delta.dylib (chính hàm hook này) để lấy dli_fbase - con trỏ
// mach_header gốc của CHÍNH Delta.dylib lúc chạy, dùng để nhận diện đúng entry nào trong
// dyld_all_image_infos.infoArray cần lọc bỏ - không hardcode offset/index, luôn đúng bất kể ASLR.
static uintptr_t taskDyldInfoSpoof_getSelfImageBase();

static kern_return_t hooked_task_info(task_name_t target_task, task_flavor_t flavor,
                                       task_info_t task_info_out, mach_msg_type_number_t *task_info_outCnt) {
    kern_return_t kr = orig_task_info_fn(target_task, flavor, task_info_out, task_info_outCnt);
    if (kr != KERN_SUCCESS) return kr;
    if (flavor != TASK_DYLD_INFO) return kr;
    if (!task_info_out) return kr;

    task_dyld_info_t dyldInfo = (task_dyld_info_t)task_info_out;
    struct dyld_all_image_infos *realInfos = (struct dyld_all_image_infos *)(uintptr_t)dyldInfo->all_image_info_addr;
    if (!realInfos || dyldInfo->all_image_info_size < sizeof(struct dyld_all_image_infos)) return kr;

    uintptr_t selfBase = taskDyldInfoSpoof_getSelfImageBase();
    if (!selfBase) return kr;

    uint32_t realCount = realInfos->infoArrayCount;
    const struct dyld_image_info *realArray = realInfos->infoArray;
    if (!realArray || realCount == 0) return kr;

    bool foundSelf = false;
    for (uint32_t i = 0; i < realCount; i++) {
        if ((uintptr_t)realArray[i].imageLoadAddress == selfBase) { foundSelf = true; break; }
    }
    if (!foundSelf) return kr;  // không thấy mình trong danh sách - không cần lọc gì cả

    // Dựng mảng infoArray đã lọc bỏ CHÍNH Delta.dylib - cố ý KHÔNG free() (chỉ gọi hiếm, sống hết
    // đời process cũng không đáng kể, tránh rủi ro lifetime nếu caller giữ con trỏ dùng sau).
    struct dyld_image_info *filteredArray = (struct dyld_image_info *)malloc(sizeof(struct dyld_image_info) * realCount);
    if (!filteredArray) return kr;
    uint32_t filteredCount = 0;
    for (uint32_t i = 0; i < realCount; i++) {
        if ((uintptr_t)realArray[i].imageLoadAddress == selfBase) continue;
        filteredArray[filteredCount++] = realArray[i];
    }

    // Bản sao dyld_all_image_infos - copy nguyên các field khác (version, notification callback,
    // libSystemInitialized...) y hệt bản thật, chỉ ghi đè infoArrayCount/infoArray - dùng đúng
    // all_image_info_size THẬT (khác nhau giữa các bản iOS) thay vì hardcode sizeof structure.
    struct dyld_all_image_infos *fakeInfos = (struct dyld_all_image_infos *)malloc((size_t)dyldInfo->all_image_info_size);
    if (!fakeInfos) { free(filteredArray); return kr; }
    memcpy(fakeInfos, realInfos, (size_t)dyldInfo->all_image_info_size);
    fakeInfos->infoArrayCount = filteredCount;
    fakeInfos->infoArray = filteredArray;

    dyldInfo->all_image_info_addr = (mach_vm_address_t)(uintptr_t)fakeInfos;
    DeltaVFS_debugLogf("TaskDyldInfoSpoof: task_info(TASK_DYLD_INFO) loc %u -> %u entries (da giau Delta.dylib)",
                        realCount, filteredCount);
    return kr;
}

static uintptr_t taskDyldInfoSpoof_getSelfImageBase() {
    static uintptr_t cached = 0;
    static bool tried = false;
    if (tried) return cached;
    tried = true;
    Dl_info info;
    if (dladdr((void *)&hooked_task_info, &info) && info.dli_fbase) {
        cached = (uintptr_t)info.dli_fbase;
    }
    return cached;
}

inline void installTaskDyldInfoSpoof() {
    struct rebinding rebindings[1];
    rebindings[0].name = "task_info";
    rebindings[0].replacement = (void *)hooked_task_info;
    rebindings[0].replaced = (void **)&orig_task_info_fn;
    int ret = rebind_symbols(rebindings, 1);
    DeltaVFS_debugLogf("TaskDyldInfoSpoof: rebind task_info ret=%d", ret);
}
