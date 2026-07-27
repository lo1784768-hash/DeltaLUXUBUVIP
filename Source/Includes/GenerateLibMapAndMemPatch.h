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
// Byte gốc đã disassemble trực tiếp từ UnityFramework THẬT (trích ra từ
// com.dts.freefireth_1.126.1_und3fined.ipa, bản stock chưa qua Esign/chỉnh sửa gì) bằng
// lief+capstone, KHÔNG suy đoán từ dump.cs - xem comment RVA để đối chiếu nếu game update.
//
// ĐÃ TEST TRÊN THIẾT BỊ THẬT - SAI, GÂY CRASH-LOOP: dự đoán ban đầu ở đoạn trên (thread vẫn
// spawn/join bình thường, không có nguy cơ treo join) KHÔNG ĐÚNG. debug.log thật cho thấy app
// crash-loop lặp lại (sống 68s/16s/8s, không cố định) đúng lúc chuẩn bị login (traffic Firebase
// Installations/FCM/app-measurement) - khớp thời điểm GenerateLibMapAndMem(loginRes) thực sự được
// gọi lần đầu. CrashLogger (bắt EXC_BAD_ACCESS/BAD_INSTRUCTION/ARITHMETIC) KHÔNG bắt được gì cả 3
// lần - dấu hiệu watchdog/jetsam SIGKILL, không phải lỗi truy cập bộ nhớ thường. Nghi ngờ thật sự:
// có code khác CHỜ (Thread.Join()/semaphore/WaitHandle) 1 tín hiệu vốn được SET BÊN TRONG thân
// GenerateLibMapAndMem() (không chỉ đơn thuần "thread tự kết thúc" như suy đoán ban đầu) - RET
// ngay từ đầu khiến tín hiệu đó không bao giờ đến, treo main thread, bị OS kill. KHÔNG gọi
// installGenerateLibMapAndMemPatch() nữa (xem Menu.mm) cho tới khi có cách patch an toàn hơn - vd
// disassemble đầy đủ thân hàm để tìm đúng điểm set-signal rồi giữ nguyên đoạn đó, chỉ chặn phần
// build+gửi report.
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
