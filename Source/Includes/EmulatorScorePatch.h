// EmulatorScorePatch.h - ép COW.UIModelUser.set_EmulatorScore(uint) LUÔN lưu 0 vào field
// EmulatorScore, bất kể giá trị thật được truyền vào là gì.
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
// ===== LỊCH SỬ 2 BẢN VÁ BYTE TRỰC TIẾP (V1/V2) - CẢ 2 ĐÃ THẤT BẠI, GIỮ LẠI ĐỂ THAM KHẢO =====
// Disassemble set_EmulatorScore() (UnityFramework THẬT, RVA 0x473C078) xác nhận đây là setter
// auto-generated đơn giản nhất có thể: "str w1,[x0,#0xb4]; ret" (8 byte). V1/V2 đổi thanh ghi
// nguồn "str" từ w1 (giá trị thật) thành WZR (luôn = 0) - cùng kích thước 4 byte, cùng kỹ thuật
// XZR-swap ổn định trong MatchClientInfoPatch.h. V1 gọi 1 lần tại +load (thất bại do timing -
// UnityFramework chưa map). V2 sửa timing bằng poll 50ms (TryPatchOnce()/Tick()/installEarly() ở
// dưới, giữ lại làm tham khảo) - test thật trên máy: KHÔNG AN TOÀN, dù áp dụng SỚM (poll tự động
// ngay +load) HAY MUỘN (bấm nút tay sau khi game chạy được 1 lúc) - CẢ 2 CÁCH ĐỀU CRASH đều đặn
// (bản sớm: crash ~5.5s sau launch, rất đều; bản muộn: 10/10 lần bấm nút đều crash sau đó,
// 0.3s-43s). Vì SỚM cũng crash y hệt MUỘN, loại được giả thuyết "race condition do vá lúc code
// đang chạy" - nghi ngờ thật sự: chỉ cần SỬA ĐỔI BYTE THỰC THI của chính hàm này (dù chỉ 1 bit) đã
// tự nó bị phát hiện/gây crash, không phụ thuộc thời điểm - giống cơ chế nghi ngờ ở
// ffantihack.MFHPGMELLCC (ghi field cũng crash 3/3 lần dù giá trị ghi hợp lý, xem
// FFAntiFlagsPatch.h).
//
// ===== V3 (HIỆN TẠI) - DOBBY HOOK, KHÔNG ĐỤNG 1 BYTE CODE NÀO CỦA set_EmulatorScore() =====
// Theo đề xuất user "đổi qua cách mempatch xem" - KHÔNG tự ghi đè byte thực thi (vm_remap) nữa,
// dùng DobbyHook() (trampoline chuẩn, đã tích hợp sẵn trong project qua libdobby.a - xem
// AntiReportSpoof.h) để CHẶN THAM SỐ trước khi hàm gốc chạy, rồi gọi THẲNG hàm gốc với value=0
// thay vì giá trị thật - hàm gốc (2 lệnh str+ret) vẫn tự thực thi y hệt logic thật của nó, không
// bị bỏ qua/thay thế gì cả, chỉ khác đầu vào. Nếu cơ chế phát hiện ở V1/V2 thực sự dựa trên so
// sánh/hash byte code của hàm thì cách này né được - vì code hàm không đổi 1 bit.
//
// RỦI RO ĐÃ BIẾT, CHƯA LOẠI TRỪ ĐƯỢC: DobbyHook() từng dùng cho 1 hàm KHÁC
// (COW.UIModelCustomRoom.GetMatchClientInfo(), xem AntiReportSpoof.h) và CŨNG bị crash trên máy
// thật (crash log rơi vào nội bộ Firebase Crashlytics, 2/3 lần, kể cả không vào trận) - crash
// signature đó KHÁC HẲN kiểu crash V1/V2 gặp phải (không phải watchdog/SIGKILL im lặng, mà bị
// chính Crashlytics bắt được) - chưa rõ nguyên nhân đó (nếu là do bản chất DobbyHook/trampoline
// trong game này) có lặp lại với hàm set_EmulatorScore() hay không, vì đây là class/hàm khác hẳn.
// CHƯA kiểm chứng trên thiết bị thật.
#pragma once
#import <Foundation/Foundation.h>
#include "MemoryUtils.h"
#include "AssetRedirect.h"
#import "Il2CppResolve.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmodule-import-in-extern-c"
#import "Dobby/dobby.h"
#pragma clang diagnostic pop

namespace EmulatorScorePatch {

typedef void (*ORIG_set_EmulatorScore)(void *thisPtr, uint32_t value);
static ORIG_set_EmulatorScore orig_set_EmulatorScore = NULL;

static void hooked_set_EmulatorScore(void *thisPtr, uint32_t value) {
    // Gọi THẲNG hàm gốc - chỉ đổi value thành 0, hàm gốc chạy đúng logic thật của nó (dù chỉ có
    // str+ret), không có bước nào bị bỏ qua/giả lập.
    orig_set_EmulatorScore(thisPtr, 0);
}

// Gọi SAU khi IL2CPP domain chắc chắn sẵn sàng (giống AntiReportSpoof.h - cùng chỗ/cùng lúc với
// game_sdk->init() trong Menu.mm +load, KHÔNG phải constructor sớm hay ngay +load như V1/V2, vì
// Il2CppResolve::GetMethod() cần domain đã init xong mới tra được theo tên).
inline void install() {
    void *target = Il2CppResolve::GetMethod("Assembly-CSharp.dll", "COW", "UIModelUser", "set_EmulatorScore", 1);
    if (target) {
        DeltaVFS_debugLog("EmulatorScorePatch: tra theo ten OK (COW.UIModelUser.set_EmulatorScore)");
    } else {
        target = (void *)getRealOffset(0x473C078ULL);
        DeltaVFS_debugLog("EmulatorScorePatch: Il2CppResolve that bai, dung RVA cu 0x473C078");
    }
    if (!target) {
        DeltaVFS_debugLog("EmulatorScorePatch: khong tim thay set_EmulatorScore, bo qua");
        return;
    }

    int ret = DobbyHook(target, (dobby_dummy_func_t)hooked_set_EmulatorScore, (dobby_dummy_func_t *)&orig_set_EmulatorScore);
    if (ret != 0 || !orig_set_EmulatorScore) {
        DeltaVFS_debugLogf("EmulatorScorePatch: DobbyHook that bai (ret=%d) - huy, khong sua gi ca", ret);
    } else {
        DeltaVFS_debugLog("EmulatorScorePatch: DobbyHook cai thanh cong (set_EmulatorScore -> luon ep value=0)");
    }
}

} // namespace EmulatorScorePatch
