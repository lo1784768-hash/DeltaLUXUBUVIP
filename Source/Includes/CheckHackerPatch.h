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
//
// SỬA LẦN 2 (sau khi test thật thấy "vừa hiện logo là văng"): debug.log cho thấy
// vm_protect(RW) trên chính trang code THÀNH CÔNG, memcpy đã ghi đè byte thật, nhưng
// vm_protect(RX) để khôi phục lại quyền thực thi SAU ĐÓ THẤT BẠI (err=2) - trang bị bỏ
// lại ở trạng thái không thực thi được, game gọi tới hàm đó (rất sớm, gần lúc logo) là
// crash ngay. Đây là giới hạn W^X (Write XOR Execute) của iOS hiện đại - không thể đổi
// thẳng 1 trang code RX -> RW -> RX bằng vm_protect. Cách đúng: vm_remap tạo ra 1 ánH
// XẠ RIÊNG (cùng trang vật lý, khác virtual address) rồi ghi qua ánh xạ đó - KHÔNG BAO
// GIỜ đụng tới quyền của trang thực thi gốc nên không có nguy cơ bỏ lại nó ở trạng thái
// hỏng. Nếu vm_remap/vm_protect trên bản remap thất bại thì return false TRƯỚC KHI
// memcpy - không đụng gì tới code gốc, an toàn 100% dù patch có tác dụng hay không.
#pragma once
#import <Foundation/Foundation.h>
#include <mach/mach.h>
#include <mach/vm_map.h>
#include <mach/vm_param.h>
#include <libkern/OSCacheControl.h>
#include "MemoryUtils.h"
#include "AssetRedirect.h"

#define CHECKHACKER_PATCH_RVA 0x4DDCD8CULL

static inline bool CheckHackerPatch_writeBytes(uintptr_t address, const uint8_t *bytes, size_t len) {
    mach_port_t task = mach_task_self();
    vm_size_t pageSize = vm_page_size ? vm_page_size : 4096;

    uintptr_t pageStart = address & ~(uintptr_t)(pageSize - 1);
    uintptr_t pageEnd = (address + len + pageSize - 1) & ~(uintptr_t)(pageSize - 1);
    vm_size_t mapSize = (vm_size_t)(pageEnd - pageStart);

    vm_address_t remapAddr = 0;
    vm_prot_t curProt = 0, maxProt = 0;
    kern_return_t err = vm_remap(task, &remapAddr, mapSize, 0, VM_FLAGS_ANYWHERE,
                                  task, (vm_address_t)pageStart, FALSE,
                                  &curProt, &maxProt, VM_INHERIT_NONE);
    if (err != KERN_SUCCESS) {
        DeltaVFS_debugLogf("CheckHackerPatch: vm_remap that bai err=%d tai 0x%lx", err, (unsigned long)address);
        return false;
    }

    err = vm_protect(task, remapAddr, mapSize, FALSE, VM_PROT_READ | VM_PROT_WRITE);
    if (err != KERN_SUCCESS) {
        DeltaVFS_debugLogf("CheckHackerPatch: vm_protect(remap RW) that bai err=%d tai 0x%lx", err, (unsigned long)address);
        vm_deallocate(task, remapAddr, mapSize);
        return false;
    }

    uintptr_t writeAt = (uintptr_t)remapAddr + (address - pageStart);
    memcpy((void *)writeAt, bytes, len);

    vm_deallocate(task, remapAddr, mapSize);
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
