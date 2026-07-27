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
// ĐÃ TEST TRÊN THIẾT BỊ THẬT (bản gọi 1 LẦN DUY NHẤT tại +load): debug.log cho thấy
// "EmulatorScorePatch: khong tim thay UnityFramework, bo qua" - tức installEmulatorScorePatch()
// chạy quá sớm, TRƯỚC KHI dyld map xong UnityFramework vào bộ nhớ (+load của Delta.dylib có thể
// chạy trước khi UnityFramework.framework kịp load, khác EmulatorCheckSpoof.h - hàm đó KHÔNG gặp
// vấn đề này vì tự có sẵn vòng lặp retry). getRealOffset() trả 0 tức thời -> patch bị bỏ qua vĩnh
// viễn (gọi 1 lần rồi thôi, không tự thử lại) - ĐÂY LÀ LÝ DO patch chưa từng thực sự chạy được,
// không phải do RVA/byte gốc sai. Chuyển sang cơ chế poll như EmulatorCheckSpoof::installEarly()
// (timer 50ms trên main queue, bắt đầu ngay từ +load) - vì đây là patch TĨNH chỉ cần thành công 1
// LẦN (không như EmulatorCheckSpoof phải ghi lại mỗi tick), timer tự huỷ ngay sau khi patch xong
// (thành công hay thất bại do lý do KHÁC timing, vd byte gốc lệch do game update).
#pragma once
#import <Foundation/Foundation.h>
#include "MemoryUtils.h"
#include "AssetRedirect.h"
#import "CheckHackerPatch.h"  // dùng lại CheckHackerPatch_writeBytes()

namespace EmulatorScorePatch {

static dispatch_source_t g_timer = NULL;
static int g_retryTick = 0;

// Trả true nếu ĐÃ XỬ LÝ XONG (patch thành công, hoặc thất bại vì lý do KHÔNG PHẢI timing - vd byte
// gốc lệch/ghi thất bại) - false nếu CHỈ là UnityFramework chưa map xong, cần tick sau thử lại.
inline bool TryPatchOnce() {
    static const uint64_t kRva = 0x473C078ULL;
    static const uint8_t kOriginal[4] = {0x01, 0xB4, 0x00, 0xB9};  // str w1, [x0, #0xb4]
    static const uint8_t kPatched[4]  = {0x1F, 0xB4, 0x00, 0xB9};  // str wzr, [x0, #0xb4]

    uintptr_t target = (uintptr_t)getRealOffset(kRva);
    if (!target) return false;  // UnityFramework chua map - thu lai tick sau

    if (memcmp((void *)target, kOriginal, 4) != 0) {
        const uint8_t *actual = (const uint8_t *)target;
        DeltaVFS_debugLogf("EmulatorScorePatch: byte goc tai 0x%lx KHONG khop du lieu ob54 da phan tich "
                            "(ky vong %02X %02X %02X %02X, thuc te %02X %02X %02X %02X) - HUY patch nay de an toan",
                            (unsigned long)target,
                            kOriginal[0], kOriginal[1], kOriginal[2], kOriginal[3],
                            actual[0], actual[1], actual[2], actual[3]);
        return true;
    }
    if (CheckHackerPatch_writeBytes(target, kPatched, 4)) {
        DeltaVFS_debugLogf("EmulatorScorePatch: da ep UIModelUser.set_EmulatorScore luon luu 0 tai 0x%lx (RVA 0x%llx, tick #%d)",
                            (unsigned long)target, (unsigned long long)kRva, g_retryTick);
    } else {
        DeltaVFS_debugLogf("EmulatorScorePatch: ghi patch that bai tai 0x%lx", (unsigned long)target);
    }
    return true;
}

inline void Tick() {
    ++g_retryTick;
    if (TryPatchOnce()) {
        dispatch_source_cancel(g_timer);
    }
}

// installEarly() - bắt đầu poll NGAY TỪ +load, cùng kỹ thuật với EmulatorCheckSpoof::installEarly()
// (timer trên main queue, 50ms/lần) - khác EmulatorCheckSpoof ở chỗ timer TỰ HUỶ sau khi patch
// xong (patch tĩnh 1 lần là đủ, không cần ghi lại liên tục).
inline void installEarly() {
    dispatch_queue_t queue = dispatch_get_main_queue();
    g_timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, queue);
    dispatch_source_set_timer(g_timer, dispatch_time(DISPATCH_TIME_NOW, 0),
                               50 * NSEC_PER_MSEC, 10 * NSEC_PER_MSEC);
    dispatch_source_set_event_handler(g_timer, ^{
        Tick();
    });
    dispatch_resume(g_timer);
    DeltaVFS_debugLog("EmulatorScorePatch: installEarly() - bat dau polling moi 50ms tu +load, khong cho 3s");
}

} // namespace EmulatorScorePatch
