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

// Đây là patch CODE TĨNH (vá lệnh máy của set_EmulatorScore), không phải ghi field runtime theo
// từng sự kiện - nên KHÔNG cần biết chính xác lúc nào là "login xong"/"vào sảnh"/"bấm vào đội".
// Chỉ cần patch xong TRƯỚC LẦN GỌI ĐẦU TIÊN của set_EmulatorScore (lúc client lưu điểm server trả
// về lúc login) - sau đó mọi lệnh gọi tiếp theo, bất kể lúc nào, đều tự động lưu 0.
//
// Điều kiện thật quyết định "patch được chưa" là "UnityFramework đã map xong vào bộ nhớ và đăng ký
// với dyld chưa" (để getRealOffset() tính được địa chỉ) - đây là 1 sự kiện THẬT có thể tự phát hiện
// bằng retry (giống EmulatorCheckSpoof.h), KHÔNG cần đoán mò bằng số giây cố định như bản trước
// (dispatch_after 3s) - vốn chỉ tình cờ đủ muộn để né lỗi getRealOffset(), không phải vì 3s có ý
// nghĩa game-logic gì. installEarly() retry mỗi 20ms ngay từ +load, patch ngay khi
// UnityFramework sẵn sàng (thường nhanh hơn 3s nhiều), rồi tự huỷ timer - không phụ thuộc mốc thời
// gian đoán mò nào nữa.
inline bool tryInstallEmulatorScorePatch() {
    static const uint64_t kRva = 0x473C078ULL;
    static const uint8_t kOriginal[4] = {0x01, 0xB4, 0x00, 0xB9};  // str w1, [x0, #0xb4]
    static const uint8_t kPatched[4]  = {0x1F, 0xB4, 0x00, 0xB9};  // str wzr, [x0, #0xb4]

    uintptr_t target = (uintptr_t)getRealOffset(kRva);
    if (!target) {
        return false;  // UnityFramework chua map xong - thu lai tick sau, KHONG bo cuoc
    }
    if (memcmp((void *)target, kOriginal, 4) != 0) {
        const uint8_t *actual = (const uint8_t *)target;
        DeltaVFS_debugLogf("EmulatorScorePatch: byte goc tai 0x%lx KHONG khop du lieu ob54 da phan tich "
                            "(ky vong %02X %02X %02X %02X, thuc te %02X %02X %02X %02X) - HUY patch nay de an toan",
                            (unsigned long)target,
                            kOriginal[0], kOriginal[1], kOriginal[2], kOriginal[3],
                            actual[0], actual[1], actual[2], actual[3]);
        return true;  // da co ket qua (that bai vinh vien do mismatch) - khong retry nua
    }
    if (CheckHackerPatch_writeBytes(target, kPatched, 4)) {
        DeltaVFS_debugLogf("EmulatorScorePatch: da ep UIModelUser.set_EmulatorScore luon luu 0 tai 0x%lx (RVA 0x%llx)",
                            (unsigned long)target, (unsigned long long)kRva);
    } else {
        DeltaVFS_debugLogf("EmulatorScorePatch: ghi patch that bai tai 0x%lx", (unsigned long)target);
    }
    return true;
}

// Gọi 1 LẦN DUY NHẤT từ +load - tự retry bên trong bằng timer, không cần caller lo timing.
inline void installEmulatorScorePatchEarly() {
    static dispatch_source_t timer;
    static int tickCount = 0;
    dispatch_queue_t queue = dispatch_get_main_queue();
    timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, queue);
    dispatch_source_set_timer(timer, dispatch_time(DISPATCH_TIME_NOW, 0),
                               20 * NSEC_PER_MSEC, 5 * NSEC_PER_MSEC);
    dispatch_source_set_event_handler(timer, ^{
        ++tickCount;
        bool done = tryInstallEmulatorScorePatch();
        if (done) {
            dispatch_source_cancel(timer);
        } else if (tickCount >= 500) {  // ~10s tran retry - UnityFramework le ra phai map xong tu lau
            DeltaVFS_debugLog("EmulatorScorePatch: qua 500 lan retry (~10s) van khong tim thay UnityFramework - bo cuoc");
            dispatch_source_cancel(timer);
        }
    });
    dispatch_resume(timer);
    DeltaVFS_debugLog("EmulatorScorePatch: installEarly() - retry moi 20ms tu +load cho den khi UnityFramework san sang");
}
