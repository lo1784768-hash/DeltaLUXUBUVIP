// MatchClientInfoPatch.h - vá thẳng 2 lệnh đọc field bên trong
// COW.UIModelCustomRoom.GetMatchClientInfo() (RVA 0x3D82760) để "lib_result" và
// "exception_count" LUÔN báo cáo 0 lên server, thay vì hook toàn hàm qua Dobby như
// AntiReportSpoof.h đã thử (crash Firebase Crashlytics do cơ chế trampoline, xem
// AntiReportSpoof.h - đã tắt, không gọi installAntiReportSpoof() nữa).
//
// Vì sao patch ở ĐÂY thay vì chặn OnMsgMatchMakingCdtHackerNtf (CheckHackerPatch.h):
// CheckHackerPatch.h chỉ chặn CLIENT xử lý thông báo "phát hiện hacker" mà SERVER
// gửi XUỐNG sau khi server đã tự đánh giá xong dữ liệu client gửi LÊN - nếu server tự
// hành động độc lập (không chờ client phản hồi Ntf) thì patch đó vô tác dụng. Patch ở
// đây chặn NGAY TỪ GỐC: sửa để server không bao giờ nhận được số liệu "khả nghi" để
// đánh giá ngay từ đầu. Nên dùng CẢ HAI (bổ trợ nhau, không xung đột).
//
// Disassemble trực tiếp GetMatchClientInfo() (lief+capstone, RVA từ dump.cs bản ob54
// hiện tại) cho thấy lib_result (field +0x30) và exception_count (field +0x34) được
// COPY THẲNG (ldr, không tính toán gì) từ 2 field int của 1 object "trạng thái phát
// hiện" tĩnh - không đụng gì tới string/pointer field khác (tpsdk_str, file_exception,
// native_result, gin_check_data) CHỦ Ý giữ nguyên, vì set rỗng/null các field đó từng
// bị nghi tự nó là dấu hiệu (HackerCdtID_CLIENT_INFO_EMPTY=101 trong enum
// EHacker.HackerCdtID) - chỉ đổi 2 SỐ NGUYÊN về 0 là giá trị "sạch" bình thường, không
// phải giá trị rỗng/bất thường.
//
// Kỹ thuật: 2 lệnh "ldr w8,[..]" (4 byte, đọc field vào w8) đổi thành "mov w8,#0" (4
// byte) - CÙNG KÍCH THƯỚC, không đụng control flow/stack/register nào khác ngoài w8,
// nên không có rủi ro kiểu trampoline/adrp-relocation.
//
// CHƯA kiểm chứng trên thiết bị thật.
#pragma once
#import <Foundation/Foundation.h>
#include <mach/mach.h>
#include <libkern/OSCacheControl.h>
#include "MemoryUtils.h"
#include "AssetRedirect.h"
#import "CheckHackerPatch.h"  // dùng lại CheckHackerPatch_writeBytes() - #import (không #include)
                                // để tránh include lại 2 lần trong cùng translation unit khi
                                // Menu.mm cũng #import trực tiếp file này

#define GMCI_LIB_RESULT_RVA      0x3D82990ULL
#define GMCI_EXCEPTION_COUNT_RVA 0x3D829A4ULL

inline void installMatchClientInfoPatch() {
    static const uint8_t kPatchBytes[4] = {0x08, 0x00, 0x80, 0x52};  // mov w8, #0

    struct PatchSite { uint64_t rva; const uint8_t original[4]; const char *label; };
    static const PatchSite sites[] = {
        {GMCI_LIB_RESULT_RVA,      {0x08, 0x19, 0x40, 0xB9}, "lib_result"},       // ldr w8, [x8, #0x18]
        {GMCI_EXCEPTION_COUNT_RVA, {0x08, 0x5C, 0x40, 0xB9}, "exception_count"},  // ldr w8, [x0, #0x5c]
    };

    for (const auto &site : sites) {
        uintptr_t target = (uintptr_t)getRealOffset(site.rva);
        if (!target) {
            DeltaVFS_debugLogf("MatchClientInfoPatch: khong tim thay UnityFramework, bo qua %s", site.label);
            continue;
        }
        if (memcmp((void *)target, site.original, 4) != 0) {
            DeltaVFS_debugLogf("MatchClientInfoPatch: byte goc tai %s (0x%lx) KHONG khop du lieu ob54 "
                                "da phan tich (game da update / offset lech) - HUY patch nay de an toan",
                                site.label, (unsigned long)target);
            continue;
        }
        if (CheckHackerPatch_writeBytes(target, kPatchBytes, 4)) {
            DeltaVFS_debugLogf("MatchClientInfoPatch: da ep %s ve 0 tai 0x%lx (RVA 0x%llx)",
                                site.label, (unsigned long)target, (unsigned long long)site.rva);
        } else {
            DeltaVFS_debugLogf("MatchClientInfoPatch: ghi patch %s that bai tai 0x%lx", site.label, (unsigned long)target);
        }
    }
}
