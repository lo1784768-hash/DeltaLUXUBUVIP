#pragma once
// TaskDyldInfoSpoof.h - fishhook task_info() để giấu Delta.dylib + TẤT CẢ injection framework
// (substrate/substitute/ellekit) khỏi bất kỳ code nào tự hỏi "process này có nạp dylib nào khả
// nghi không" qua đường task_info(TASK_DYLD_INFO) — tầng THẤP HƠN hẳn 4 hàm _dyld_get_image_*
// mà DylibHide.h đang giấu.
//
// BỐI CẢNH: disassemble Monite.dylib (FUN_000ad944) xác nhận họ hook task_info() và lọc bỏ
// entry của chính mình khỏi dyld_all_image_infos.infoArray. Delta.dylib cũng cần làm tương tự,
// VÀ THÊM lọc cả libsubstrate/substitute/ellekit — DylibHide.h giấu chúng ở tầng _dyld_*, nhưng
// task_info(TASK_DYLD_INFO) đọc TRỰC TIẾP struct dyld_all_image_infos, không qua 4 hàm đó.
#import <Foundation/Foundation.h>
#import <mach/mach.h>
#import <mach-o/dyld.h>
#import <mach-o/dyld_images.h>
#import <dlfcn.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "fishhook.h"

// Dùng lại hàm nhận diện từ DylibHide.h (cùng translation unit)
// DylibHide_shouldHide() và DylibHide_containsCI() đã định nghĩa sẵn.

extern void DeltaVFS_debugLog(const char *msg);
extern void DeltaVFS_debugLogf(const char *fmt, ...);

typedef kern_return_t (*ORIG_task_info)(task_name_t, task_flavor_t, task_info_t, mach_msg_type_number_t *);
static ORIG_task_info orig_task_info_fn = NULL;

// Lấy path của chính Delta.dylib (cache 1 lần)
static const char *taskDyldInfoSpoof_getSelfPath() {
    static const char *cached = NULL;
    static bool tried = false;
    if (tried) return cached;
    tried = true;
    Dl_info info;
    if (dladdr((void *)&taskDyldInfoSpoof_getSelfPath, &info) && info.dli_fname) {
        cached = info.dli_fname;
    }
    return cached;
}

static kern_return_t hooked_task_info(task_name_t target_task, task_flavor_t flavor,
                                       task_info_t task_info_out, mach_msg_type_number_t *task_info_outCnt) {
    kern_return_t kr = orig_task_info_fn(target_task, flavor, task_info_out, task_info_outCnt);
    if (kr != KERN_SUCCESS) return kr;
    if (flavor != TASK_DYLD_INFO) return kr;
    if (!task_info_out) return kr;

    task_dyld_info_t dyldInfo = (task_dyld_info_t)task_info_out;
    struct dyld_all_image_infos *realInfos = (struct dyld_all_image_infos *)(uintptr_t)dyldInfo->all_image_info_addr;
    if (!realInfos || dyldInfo->all_image_info_size < sizeof(struct dyld_all_image_infos)) return kr;

    uint32_t realCount = realInfos->infoArrayCount;
    const struct dyld_image_info *realArray = realInfos->infoArray;
    if (!realArray || realCount == 0) return kr;

    const char *selfPath = taskDyldInfoSpoof_getSelfPath();

    // Đếm trước bao nhiêu entry cần lọc
    uint32_t hideCount = 0;
    for (uint32_t i = 0; i < realCount; i++) {
        const char *name = realArray[i].imageFilePath;
        if (DylibHide_shouldHide(name, selfPath)) {
            hideCount++;
        }
    }
    if (hideCount == 0) return kr;  // không có gì cần lọc

    // Dựng mảng đã lọc — cố ý KHÔNG free() (hiếm khi gọi, sống hết đời process)
    struct dyld_image_info *filteredArray = (struct dyld_image_info *)malloc(sizeof(struct dyld_image_info) * realCount);
    if (!filteredArray) return kr;
    uint32_t filteredCount = 0;
    for (uint32_t i = 0; i < realCount; i++) {
        const char *name = realArray[i].imageFilePath;
        if (DylibHide_shouldHide(name, selfPath)) continue;
        filteredArray[filteredCount++] = realArray[i];
    }

    // Bản sao dyld_all_image_infos — copy nguyên, chỉ ghi đè infoArray/count
    struct dyld_all_image_infos *fakeInfos = (struct dyld_all_image_infos *)malloc((size_t)dyldInfo->all_image_info_size);
    if (!fakeInfos) { free(filteredArray); return kr; }
    memcpy(fakeInfos, realInfos, (size_t)dyldInfo->all_image_info_size);
    fakeInfos->infoArrayCount = filteredCount;
    fakeInfos->infoArray = filteredArray;

    dyldInfo->all_image_info_addr = (mach_vm_address_t)(uintptr_t)fakeInfos;
    DeltaVFS_debugLogf("TaskDyldInfoSpoof: task_info(TASK_DYLD_INFO) loc %u -> %u entries (da giau %u dylib)",
                        realCount, filteredCount, hideCount);
    return kr;
}

inline void installTaskDyldInfoSpoof() {
    struct rebinding rebindings[1];
    rebindings[0].name = "task_info";
    rebindings[0].replacement = (void *)hooked_task_info;
    rebindings[0].replaced = (void **)&orig_task_info_fn;
    int ret = rebind_symbols(rebindings, 1);
    DeltaVFS_debugLogf("TaskDyldInfoSpoof: rebind task_info ret=%d", ret);
}
