// EmulatorScorePatch.h - vá thẳng COW.UIModelUser.set_EmulatorScore(uint) (RVA 0x473C078) để LUÔN
// lưu 0 vào field EmulatorScore, bất kể giá trị thật được truyền vào là gì.
//
// BỐI CẢNH: user báo dấu hiệu THẬT - icon hình máy tính hiện cạnh tên lúc vào đội (TRƯỚC lúc vào
// trận), chỉ xảy ra với Delta, không xảy ra với Monite/bản App Store gốc. Đã thử ép
// COW.GameFacade.m_IsEmulator/m_EmulatorChecked (EmulatorCheckSpoof.h) - KHÔNG hết icon, vì đó chỉ
// là field dùng cho báo cáo ffantihack riêng (SendLoginTime), không phải field thật quyết định
// icon.
//
// Quét lại toàn bộ dump.cs (Il2CppDumper, ob54/) tìm đúng field mạng thật tên "emulator_score" -
// xuất hiện xuyên suốt: proto.MajorLoginRes.emulator_score (SERVER trả về NGAY LÚC ĐĂNG NHẬP,
// TRƯỚC CẢ lúc vào sảnh), tcp.GroupJoinReq.emulator_score (CLIENT tự gửi lúc vào đội),
// tcp.RoomPlayerInfo.emulator_score (dữ liệu đồng đội thấy - hiện icon). COW.UIModelUser có
// property EmulatorScore (backing field @0xB4 trên instance) - bản cache phía client của điểm số
// NÀY, dùng để điền vào GroupJoinReq lúc vào đội - đây LÀ điểm can thiệp đúng, vì nằm giữa lúc
// server trả điểm về và lúc client tự gửi lại điểm đó đi khi vào đội (không cần biết SERVER tính
// điểm ban đầu bằng cách nào).
//
// Disassemble trực tiếp set_EmulatorScore() (UnityFramework, RVA 0x473C078) xác nhận đây là
// setter auto-generated ĐƠN GIẢN NHẤT có thể: "str w1,[x0,#0xb4]; ret" (8 byte, không tính toán gì
// khác) - w1 là tham số uint value truyền vào, x0 là con trỏ instance UIModelUser, offset 0xb4
// khớp CHÍNH XÁC với backing field <EmulatorScore>k__BackingField trong dump.cs.
//
// Kỹ thuật: đổi thanh ghi nguồn của "str" từ w1 (giá trị thật) thành WZR (thanh ghi 0 phần cứng
// ARM64, luôn = 0) - "str wzr,[x0,#0xb4]" - CÙNG KÍCH THƯỚC 4 byte (chỉ đổi 5 bit thấp nhất của
// lệnh, byte đầu tiên: 0x01 -> 0x1F), không đụng lệnh "ret"/control flow/thanh ghi nào khác. Mỗi
// lần code khác gọi set_EmulatorScore(bất kỳ giá trị nào) từ giờ đều lưu 0 vào field, y hệt kỹ
// thuật XZR-swap đã dùng ổn định trong MatchClientInfoPatch.h.
//
// CHƯA kiểm chứng trên thiết bị thật.
#pragma once
#import <Foundation/Foundation.h>
#include "MemoryUtils.h"
#include "AssetRedirect.h"
#import "CheckHackerPatch.h"  // dùng lại CheckHackerPatch_writeBytes()

inline void installEmulatorScorePatch() {
    static const uint64_t kRva = 0x473C078ULL;
    static const uint8_t kOriginal[4] = {0x01, 0xB4, 0x00, 0xB9};  // str w1, [x0, #0xb4]
    static const uint8_t kPatched[4]  = {0x1F, 0xB4, 0x00, 0xB9};  // str wzr, [x0, #0xb4]

    uintptr_t target = (uintptr_t)getRealOffset(kRva);
    if (!target) {
        DeltaVFS_debugLog("EmulatorScorePatch: khong tim thay UnityFramework, bo qua");
        return;
    }
    if (memcmp((void *)target, kOriginal, 4) != 0) {
        const uint8_t *actual = (const uint8_t *)target;
        DeltaVFS_debugLogf("EmulatorScorePatch: byte goc tai 0x%lx KHONG khop du lieu ob54 da phan tich "
                            "(ky vong %02X %02X %02X %02X, thuc te %02X %02X %02X %02X) - HUY patch nay de an toan",
                            (unsigned long)target,
                            kOriginal[0], kOriginal[1], kOriginal[2], kOriginal[3],
                            actual[0], actual[1], actual[2], actual[3]);
        return;
    }
    if (CheckHackerPatch_writeBytes(target, kPatched, 4)) {
        DeltaVFS_debugLogf("EmulatorScorePatch: da ep UIModelUser.set_EmulatorScore luon luu 0 tai 0x%lx (RVA 0x%llx)",
                            (unsigned long)target, (unsigned long long)kRva);
    } else {
        DeltaVFS_debugLogf("EmulatorScorePatch: ghi patch that bai tai 0x%lx", (unsigned long)target);
    }
}
