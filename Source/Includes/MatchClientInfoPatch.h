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
// đổi control flow).
//
// MỞ RỘNG LẦN 2: sau khi vá cả 6 field trên, test thật vẫn bị đá (thậm chí chỉ 3s sau khi vào trận -
// xác nhận thêm lần nữa: không phải chờ đủ lâu, đánh giá gần như ngay lúc vào trận). User đồng ý vá
// luôn gin_check_data@0x50 dù đây là field DUY NHẤT có mã lỗi định danh riêng xác nhận rõ ràng
// (EHacker.HackerPoolCdt_GIN_CHECK_DATA_EMPTY_IOS=19) rằng rỗng field NÀY chắc chắn bị phát hiện -
// chấp nhận rủi ro đã biết vì 6 field kia không giải quyết được vấn đề.
// MỞ RỘNG LẦN 3 (sau khi xác nhận: phòng tự tạo KHÔNG gọi GetMatchClientInfo() và KHÔNG bị đá,
// trong khi ghép trận ngẫu nhiên CÓ gọi và VẪN bị đá dù đã null 7/7 field) - nghi ngờ "null hoàn
// toàn" chính là dấu hiệu bị bắt (enum CLIENT_INFO_EMPTY=101/GIN_CHECK_DATA_EMPTY_IOS=19 đặt tên
// riêng cho tình huống "rỗng"). Thử thay 4 field byte[] (file_exception, lib_result@0x28 - tên
// thật theo dump.cs, khác "native_result" đã ghi nhầm trước đây -, native_result@0x38,
// gin_check_data) bằng 1 byte[1] THẬT (không null) qua kỹ thuật redirect lệnh "bl <hàm tính giá
// trị thật>" đứng trước mỗi "str" sang stub riêng (xem FakeMatchDataAlloc.h) - str phía sau giữ
// NGUYÊN, chỉ giá trị được lưu đổi. Có fallback null (kỹ thuật cũ) nếu redirect thất bại (cấp phát
// byte[] thất bại, hoặc BL ngoài tầm với ±128MB giữa Delta.dylib và UnityFramework - 2 image riêng
// biệt, không đảm bảo gần nhau).
#pragma once
#import <Foundation/Foundation.h>
#include <mach/mach.h>
#include <libkern/OSCacheControl.h>
#include "MemoryUtils.h"
#include "AssetRedirect.h"
#import "CheckHackerPatch.h"  // dùng lại CheckHackerPatch_writeBytes() - #import (không #include)
                                // để tránh include lại 2 lần trong cùng translation unit khi
                                // Menu.mm cũng #import trực tiếp file này
#import "FakeMatchDataAlloc.h"

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

    // 1 field pointer con lai xu ly theo kieu cu (null qua XZR swap) - tpsdk_str co 2 nhanh nguon
    // gia tri hoi tu truoc luc str (1 qua bl ffantihack.MFHPGMELLCC.NHOPEKHBCBF, 1 qua chuoi cache
    // co san) nen KHONG fit kieu "redirect 1 bl duy nhat truoc str" nhu 4 field byte[] ben duoi -
    // giu nguyen ky thuat null cu cho field nay, chua mo rong.
    struct PtrPatchSite { uint64_t rva; uint8_t original[4]; uint8_t patched[4]; const char *label; };
    static const PtrPatchSite ptrSites[] = {
        {0x3D828F0ULL, {0x81,0x0E,0x01,0xF8}, {0x9F,0x0E,0x01,0xF8}, "tpsdk_str@0x10"},
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

    // 4 field byte[] - THAY VI null (XZR swap, kieu cu), redirect dung lenh "bl <ham tinh gia tri
    // that>" dung TRUOC lenh str sang stub tra ve byte[1] THAT (xem FakeMatchDataAlloc.h) - str
    // phia sau GIU NGUYEN, khong dung toi. Neu redirect that bai vi bat ky ly do gi (cap phat
    // byte[] that bai, bl khong con dung target nhu da phan tich, hoac ngoai tam voi ±128MB giua
    // Delta.dylib va UnityFramework) - FALLBACK ve null qua str (kieu cu, da xac nhan an toan)
    // thay vi bo qua hoan toan, de KHONG BAO GIO de lai gia tri that/nghi ngo o day.
    FakeMatchData_ensureAllocated();

    struct BlRedirectSite {
        uint64_t blRva; uint64_t originalTargetRva;
        uint64_t strRva; uint8_t strOriginal[4]; uint8_t strNullPatched[4];
        const char *label;
    };
    static const BlRedirectSite blSites[] = {
        {0x3D82968ULL, 0x6889018ULL, 0x3D82974ULL, {0x01,0x0C,0x02,0xF8}, {0x1F,0x0C,0x02,0xF8}, "file_exception@0x20"},
        {0x3D829C0ULL, 0x6889018ULL, 0x3D829CCULL, {0x01,0x8C,0x02,0xF8}, {0x1F,0x8C,0x02,0xF8}, "lib_result@0x28 (ten dung theo dump.cs)"},
        {0x3D829E0ULL, 0x6889018ULL, 0x3D829ECULL, {0x80,0x8E,0x03,0xF8}, {0x9F,0x8E,0x03,0xF8}, "native_result@0x38"},
        {0x3D82A6CULL, 0x827AB24ULL, 0x3D82A78ULL, {0x01,0x0C,0x05,0xF8}, {0x1F,0x0C,0x05,0xF8}, "gin_check_data@0x50"},
    };

    for (const auto &site : blSites) {
        bool redirected = false;
        uintptr_t blAddr = (uintptr_t)getRealOffset(site.blRva);
        if (blAddr && g_fakeEmptyByteArray) {
            uint32_t word = 0;
            memcpy(&word, (void *)blAddr, 4);
            if ((word >> 26) == 0b100101) {
                int32_t imm26 = word & 0x3FFFFFF;
                if (imm26 & 0x2000000) imm26 -= 0x4000000;
                uintptr_t decodedTarget = blAddr + (intptr_t)imm26 * 4;
                uintptr_t expectedTarget = (uintptr_t)getRealOffset(site.originalTargetRva);
                if (decodedTarget == expectedTarget) {
                    uintptr_t stubAddr = (uintptr_t)&DeltaFakeEmptyByteArrayStub;
                    intptr_t delta = (intptr_t)stubAddr - (intptr_t)blAddr;
                    if ((delta % 4) == 0 && delta >= -(1LL << 27) && delta < (1LL << 27)) {
                        int32_t newImm26 = (int32_t)((delta / 4) & 0x3FFFFFF);
                        uint32_t newWord = 0x94000000u | (uint32_t)newImm26;
                        uint8_t newBytes[4];
                        memcpy(newBytes, &newWord, 4);
                        if (CheckHackerPatch_writeBytes(blAddr, newBytes, 4)) {
                            DeltaVFS_debugLogf("MatchClientInfoPatch: da redirect %s sang byte[1] gia tai 0x%lx (RVA 0x%llx)",
                                                site.label, (unsigned long)blAddr, (unsigned long long)site.blRva);
                            redirected = true;
                        }
                    } else {
                        DeltaVFS_debugLogf("MatchClientInfoPatch: %s - stub qua xa (ngoai tam BL 128MB), fallback null", site.label);
                    }
                } else {
                    DeltaVFS_debugLogf("MatchClientInfoPatch: %s - bl khong dung target nhu da phan tich (game update?), fallback null", site.label);
                }
            } else {
                DeltaVFS_debugLogf("MatchClientInfoPatch: %s - byte goc khong phai BL (game update?), fallback null", site.label);
            }
        } else if (!g_fakeEmptyByteArray) {
            DeltaVFS_debugLogf("MatchClientInfoPatch: %s - chua co byte[] gia (cap phat that bai), fallback null", site.label);
        }

        if (!redirected) {
            uintptr_t strTarget = (uintptr_t)getRealOffset(site.strRva);
            if (!strTarget) {
                DeltaVFS_debugLogf("MatchClientInfoPatch: khong tim thay UnityFramework, bo qua %s", site.label);
                continue;
            }
            if (memcmp((void *)strTarget, site.strOriginal, 4) != 0) {
                DeltaVFS_debugLogf("MatchClientInfoPatch: byte goc tai %s (0x%lx) KHONG khop - HUY patch nay de an toan",
                                    site.label, (unsigned long)strTarget);
                continue;
            }
            if (CheckHackerPatch_writeBytes(strTarget, site.strNullPatched, 4)) {
                DeltaVFS_debugLogf("MatchClientInfoPatch: da ep %s ve null (fallback) tai 0x%lx", site.label, (unsigned long)strTarget);
            } else {
                DeltaVFS_debugLogf("MatchClientInfoPatch: ghi patch fallback %s that bai tai 0x%lx", site.label, (unsigned long)strTarget);
            }
        }
    }
}
