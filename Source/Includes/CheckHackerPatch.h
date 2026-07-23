// CheckHackerPatch.h - vá thẳng 8 byte đầu COW.GameConfig.get_CheckHacker() trong
// UnityFramework để hàm này luôn trả về false, thay vì hook qua trampoline
// (Dobby/MSHookFunction) như AntiReportSpoof.h đã thử và bị crash Firebase
// Crashlytics do chính cơ chế cài trampoline, không phải do giá trị field bị đổi
// (xem AntiReportSpoof.h - bản passthrough không đổi field gì vẫn crash 2/3 lần mở).
//
// Vì sao nhắm vào get_CheckHacker() thay vì spoof field trong GetMatchClientInfo():
// disassemble trực tiếp binary that (lief+capstone, RVA lấy từ dump.cs bản ob54 hiện
// tại - KHÔNG phải offset cũ) cho thấy get_CheckHacker() (RVA 0x4DDCD8C) là điều kiện
// cbnz DUY NHẤT gác toàn bộ 1 hàm kiểm tra tại RVA ~0x6D48120 (2 nơi gọi trực tiếp
// bl 0x4DDCD8C, cả 2 đều trong hàm này) - false thì hàm return ngay, true thì mới
// chạy tiếp phần so sánh match_mode/config kiểu HackerPoolCdt. Patch tại NGUỒN
// (get_CheckHacker) chặn được cả 2 điểm gọi cùng lúc, sạch hơn vá rải rác từng nơi.
//
// Kỹ thuật: ghi đè 8 byte đầu hàm (2 lệnh prologue "stp x20,x19,[sp,#-0x20]!" +
// "stp x29,x30,[sp,#0x10]") thành "mov w0,#0 ; ret" (00008052 C0035FD6) - hàm trả
// về false ngay, không đụng stack/register nào khác. KHÔNG dùng trampoline/hook nên
// không có rủi ro ARM64 adrp-relocation như GetMatchClientInfo() từng gặp.
//
// CHƯA kiểm chứng trên thiết bị thật - bật thử, nếu game crash/không vào được trận thì
// comment lại dòng gọi installCheckHackerPatch() trong Menu.mm và báo lại để phân tích tiếp.
#pragma once
#import <Foundation/Foundation.h>
#include <mach/mach.h>
#include <libkern/OSCacheControl.h>
#include "MemoryUtils.h"
#include "AssetRedirect.h"

#define CHECKHACKER_PATCH_RVA 0x4DDCD8CULL

static inline bool CheckHackerPatch_writeBytes(uintptr_t address, const uint8_t *bytes, size_t len) {
    mach_port_t port = mach_task_self();
    kern_return_t err = vm_protect(port, (mach_vm_address_t)address, len, false,
                                    VM_PROT_READ | VM_PROT_WRITE | VM_PROT_COPY);
    if (err != KERN_SUCCESS) {
        DeltaVFS_debugLogf("CheckHackerPatch: vm_protect(RW) that bai err=%d tai 0x%llx", err, (unsigned long long)address);
        return false;
    }
    memcpy((void *)address, bytes, len);
    err = vm_protect(port, (mach_vm_address_t)address, len, false, VM_PROT_READ | VM_PROT_EXECUTE);
    if (err != KERN_SUCCESS) {
        DeltaVFS_debugLogf("CheckHackerPatch: vm_protect(RX) that bai err=%d tai 0x%llx", err, (unsigned long long)address);
        return false;
    }
    sys_icache_invalidate((void *)address, len);
    return true;
}

inline void installCheckHackerPatch() {
    uintptr_t target = (uintptr_t)getRealOffset(CHECKHACKER_PATCH_RVA);
    if (!target) {
        DeltaVFS_debugLogf("CheckHackerPatch: khong tim thay UnityFramework, bo qua");
        return;
    }

    static const uint8_t kPatchBytes[8] = {
        0x00, 0x00, 0x80, 0x52,  // mov w0, #0
        0xC0, 0x03, 0x5F, 0xD6   // ret
    };
    static const uint8_t kExpectedOriginal[8] = {
        0xF4, 0x4F, 0xBE, 0xA9,  // stp x20, x19, [sp, #-0x20]!
        0xFD, 0x7B, 0x01, 0xA9   // stp x29, x30, [sp, #0x10]
    };

    if (memcmp((void *)target, kExpectedOriginal, sizeof(kExpectedOriginal)) != 0) {
        DeltaVFS_debugLogf("CheckHackerPatch: byte goc tai 0x%lx KHONG khop du lieu ob54 da phan tich "
                            "(game da update / offset lech) - HUY patch de an toan", (unsigned long)target);
        return;
    }

    if (CheckHackerPatch_writeBytes(target, kPatchBytes, sizeof(kPatchBytes))) {
        DeltaVFS_debugLogf("CheckHackerPatch: da vo hieu hoa get_CheckHacker() tai 0x%lx (RVA 0x%llx)",
                            (unsigned long)target, CHECKHACKER_PATCH_RVA);
    } else {
        DeltaVFS_debugLogf("CheckHackerPatch: ghi patch that bai tai 0x%lx", (unsigned long)target);
    }
}
