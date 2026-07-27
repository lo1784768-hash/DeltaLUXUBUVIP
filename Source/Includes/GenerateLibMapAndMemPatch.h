// GenerateLibMapAndMemPatch.h - vô hiệu hoá COW.UIModelLogin.GenerateLibMapAndMem() (RVA
// 0x43F81F4, dump.cs bản ob54), hàm quét bản đồ thư viện native + vùng nhớ đang load trong
// process, chạy trên 1 thread riêng (GenerateLibMapAndMemThread) NGAY SAU KHI LOGIN XONG, báo
// cáo qua 1 URL riêng cho anti-cheat (field m_ffantiUrl - do SERVER cấp động lúc login, không
// hardcode trong binary, không chặn được bằng DNSBlock vì không biết trước domain).
//
// Đây là ứng viên hàng đầu cho icon "máy tính giả lập" xuất hiện lúc ghép đội: icon hiện NGAY
// SAU LOGIN (lúc vào lobby/ghép đội), đúng thời điểm luồng này chạy - khác thời điểm với vụ bị
// đá giữa trận (đánh giá lúc GetMatchClientInfo(), xem MatchClientInfoPatch.h). m_IsEmulator
// local đã patch (EmulatorCheckSpoof.h) nhưng icon KHÔNG hết - khớp với giả thuyết verdict icon
// đến từ 1 kênh SERVER riêng (đúng kênh ffanti_url này), không phải field local client tự tính.
//
// Kỹ thuật: vá lệnh ĐẦU TIÊN của hàm (function entry) thành RET - hàm return ngay lập tức,
// KHÔNG chạm frame/stack (chưa hề push gì), x30 (LR) vẫn nguyên giá trị caller đặt qua "bl" -
// tương đương hàm không làm gì cả, không có rủi ro kiểu trampoline/relocation. Vì hàm trả về
// void, caller không đọc x0 nên không cần set giá trị trả về.
//
// An toàn cho GenerateLibMapAndMemThread/WaitGenerateLibMapAndMemThread (KHÔNG đụng tới): thread
// vẫn spawn/join bình thường, chỉ là thân GenerateLibMapAndMem() chạy xong ngay lập tức - không
// có nguy cơ treo join.
//
// Byte gốc đã disassemble trực tiếp từ UnityFramework THẬT (trích ra từ
// com.dts.freefireth_1.126.1_und3fined.ipa, bản stock chưa qua Esign/chỉnh sửa gì) bằng
// lief+capstone, KHÔNG suy đoán từ dump.cs - xem comment RVA để đối chiếu nếu game update.
//
// CHƯA kiểm chứng trên thiết bị thật - đây là 1 giả thuyết đang thử, chấp nhận rủi ro icon có
// thể không hết (xem EmulatorCheckSpoof.h - từng nghi verdict do server tự quyết định qua kênh
// khác nữa, không chỉ riêng report này).
#pragma once
#import <Foundation/Foundation.h>
#include <mach/mach.h>
#include <libkern/OSCacheControl.h>
#include "MemoryUtils.h"
#import "CheckHackerPatch.h"  // dùng lại CheckHackerPatch_writeBytes()/RegisterPatchedRegion()

#define GENLIBMAP_RVA 0x43F81F4ULL

inline void installGenerateLibMapAndMemPatch() {
    uintptr_t target = (uintptr_t)getRealOffset(GENLIBMAP_RVA);
    if (!target) {
        DeltaVFS_debugLogf("GenerateLibMapAndMemPatch: khong tim thay UnityFramework, bo qua");
        return;
    }

    static const uint8_t kPatchBytes[4] = {
        0xC0, 0x03, 0x5F, 0xD6  // ret
    };
    static const uint8_t kExpectedOriginal[4] = {
        0xF8, 0x5F, 0xBC, 0xA9  // stp x24, x23, [sp, #-0x40]!
    };

    if (memcmp((void *)target, kExpectedOriginal, sizeof(kExpectedOriginal)) != 0) {
        DeltaVFS_debugLogf("GenerateLibMapAndMemPatch: byte goc tai 0x%lx KHONG khop du lieu ob54 da phan tich "
                            "(game da update / offset lech) - HUY patch de an toan", (unsigned long)target);
        return;
    }

    if (CheckHackerPatch_writeBytes(target, kPatchBytes, sizeof(kPatchBytes))) {
        DeltaVFS_debugLogf("GenerateLibMapAndMemPatch: da vo hieu hoa GenerateLibMapAndMem() tai 0x%lx (RVA 0x%llx)",
                            (unsigned long)target, GENLIBMAP_RVA);
    } else {
        DeltaVFS_debugLogf("GenerateLibMapAndMemPatch: ghi patch that bai tai 0x%lx", (unsigned long)target);
    }
}
