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
//
// MỞ RỘNG (sau khi user chỉ ra: thời gian bị đá KHÔNG quan trọng - vào trận rồi thoát ngay vẫn bị
// đá y hệt - tức đây là 1 lần đánh giá DUY NHẤT ngay lúc vào trận, không phải quét định kỳ. 2 field
// int đã vá (lib_result/exception_count) rõ ràng CHƯA đủ). Vá thêm 3 field pointer còn lại mà trước
// đây CỐ Ý chừa lại: tpsdk_str@0x10, file_exception@0x20, native_result@0x28 và @0x38 - kỹ thuật
// giống hệt (đổi thanh ghi nguồn của lệnh "str" thành XZR/thanh ghi luôn = 0, cùng kích thước, không
// đổi control flow). RIÊNG gin_check_data@0x50 VẪN GIỮ NGUYÊN KHÔNG ĐỤNG - đây là field duy nhất có
// hẳn 1 mã lỗi định danh riêng xác nhận rõ ràng (EHacker.HackerPoolCdt_GIN_CHECK_DATA_EMPTY_IOS=19)
// rằng rỗng field NÀY chắc chắn bị phát hiện - 3 field còn lại (tpsdk_str/file_exception/
// native_result) không có mã lỗi cụ thể nào được xác nhận gắn với việc rỗng, nên rủi ro thấp hơn.
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
    static const uint8_t kIntPatchBytes[4] = {0x08, 0x00, 0x80, 0x52};  // mov w8, #0

    struct IntPatchSite { uint64_t rva; const uint8_t original[4]; const char *label; };
    static const IntPatchSite intSites[] = {
        {GMCI_LIB_RESULT_RVA,      {0x08, 0x19, 0x40, 0xB9}, "lib_result"},       // ldr w8, [x8, #0x18]
        {GMCI_EXCEPTION_COUNT_RVA, {0x08, 0x5C, 0x40, 0xB9}, "exception_count"},  // ldr w8, [x0, #0x5c]
    };

    for (const auto &site : intSites) {
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
        if (CheckHackerPatch_writeBytes(target, kIntPatchBytes, 4)) {
            DeltaVFS_debugLogf("MatchClientInfoPatch: da ep %s ve 0 tai 0x%lx (RVA 0x%llx)",
                                site.label, (unsigned long)target, (unsigned long long)site.rva);
        } else {
            DeltaVFS_debugLogf("MatchClientInfoPatch: ghi patch %s that bai tai 0x%lx", site.label, (unsigned long)target);
        }
    }

    // 4 field pointer - "str xN,[..]!" doi thanh ghi nguon xN -> XZR (byte dau tien cua tu lenh,
    // little-endian, chinh la truong Rt) - KHONG doi kich thuoc/dia chi/control flow.
    struct PtrPatchSite { uint64_t rva; uint8_t original[4]; uint8_t patched[4]; const char *label; };
    static const PtrPatchSite ptrSites[] = {
        {0x3D828F0ULL, {0x81,0x0E,0x01,0xF8}, {0x9F,0x0E,0x01,0xF8}, "tpsdk_str@0x10"},
        {0x3D82974ULL, {0x01,0x0C,0x02,0xF8}, {0x1F,0x0C,0x02,0xF8}, "file_exception@0x20"},
        {0x3D829CCULL, {0x01,0x8C,0x02,0xF8}, {0x1F,0x8C,0x02,0xF8}, "native_result@0x28"},
        {0x3D829ECULL, {0x80,0x8E,0x03,0xF8}, {0x9F,0x8E,0x03,0xF8}, "native_result@0x38"},
    };

    for (const auto &site : ptrSites) {
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
        if (CheckHackerPatch_writeBytes(target, site.patched, 4)) {
            DeltaVFS_debugLogf("MatchClientInfoPatch: da ep %s ve null tai 0x%lx (RVA 0x%llx)",
                                site.label, (unsigned long)target, (unsigned long long)site.rva);
        } else {
            DeltaVFS_debugLogf("MatchClientInfoPatch: ghi patch %s that bai tai 0x%lx", site.label, (unsigned long)target);
        }
    }
}
