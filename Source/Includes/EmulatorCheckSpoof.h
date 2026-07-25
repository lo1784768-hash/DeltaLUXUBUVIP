// EmulatorCheckSpoof.h - ép COW.GameFacade.IsEmulator() luôn trả "không phải giả lập", bằng cách
// ghi thẳng vào cache tĩnh của chính nó thay vì hiểu chi tiết logic thật bên trong.
//
// BỐI CẢNH: user báo dấu hiệu THẬT - icon hình máy tính hiện cạnh tên lúc vào đội (TRƯỚC cả lúc
// vào trận) trên bản Delta, nhưng KHÔNG hiện khi dùng Monite hoặc bản App Store gốc - tức chỉ
// Delta bị nhận diện "giả lập PC". Tìm trong dump.cs (Il2CppDumper output, ob54/) thấy đúng cơ chế:
//   COW.GameFacade.IsEmulator()                     RVA 0x55DEEF0
//   COW.GameFacade.NeedffantihackEmulatorCheck()
//   COW.GameFacade.SendLoginTime(bool isEmulator)    - gửi kết quả lên server LÚC LOGIN (khớp
//                                                       đúng timing "hiện ngay khi vào đội")
//   ERoom.Proto_EMULATORCHECK_NTF / ErrCode_EMULATORCHECKFAILED - có hẳn notification/error code
//   riêng cho việc này.
// Disassemble trực tiếp IsEmulator() (UnityFramework, RVA 0x55DEEF0) xác nhận đây là hàm CACHE 1
// LẦN kiểu "if (!m_EmulatorChecked) { m_IsEmulator = <tinh toan that>; m_EmulatorChecked = true; }
// return m_IsEmulator;" - 2 field static private bool nằm NGAY TRONG COW.GameFacade (dump.cs):
//   private static bool m_EmulatorChecked;  // offset 0x35F (tính từ static_field_data)
//   private static bool m_IsEmulator;       // offset 0x360
// Logic THẬT tính m_IsEmulator (so sánh string phức tạp qua nhiều hàm nội bộ IL2CPP) CHƯA dịch
// ngược đầy đủ được - nhưng KHÔNG CẦN, vì chỉ cần tự ghi đè 2 field cache này SAU KHI class init
// xong: m_EmulatorChecked=true, m_IsEmulator=false - lần gọi IsEmulator() nào cũng đọc thẳng cache
// (đã có giá trị) mà KHÔNG BAO GIỜ chạy lại phần tính toán thật nữa.
//
// AN TOÀN: chỉ ghi 2 byte vào vùng static field data quản lý bởi IL2CPP runtime (như
// FFAntiObserve.h đọc, MatchClientInfoPatch.h ghi) - KHÔNG patch code thực thi nào, không đụng
// executable bytes. Ghi LẶP LẠI mỗi lần poll (không chỉ 1 lần) đề phòng code khác reset lại
// m_EmulatorChecked=false để chạy lại kiểm tra định kỳ.
//
// CHƯA KIỂM CHỨNG TRÊN THIẾT BỊ THẬT.
#pragma once
#import <Foundation/Foundation.h>
#import "AssetRedirect.h"  // DeltaVFS_debugLog/DeltaVFS_debugLogf
#import "Il2CppResolve.h"
#include "MemoryUtils.h"

namespace EmulatorCheckSpoof {

static void *g_klass = NULL;
static void *g_staticData = NULL;
static int g_retryTick = 0;
static bool g_everWritten = false;
static uint8_t g_lastPoolValue = 0xFF;  // 0xFF = "chua doc lan nao" - IsEmulatorPool CHI DOC, khong ghi

#define EMU_OFF_CHECKED  0x35F
#define EMU_OFF_ISEMU    0x360
#define EMU_OFF_POOL     0x17E  // public static bool IsEmulatorPool (dump.cs) - CHỈ ĐỌC, không ghi

// KHÁC FFAntiObserve.h: KHÔNG có "g_classLookupFailed" bỏ cuộc vĩnh viễn sau 1 lần NULL - hàm này
// giờ được gọi từ installEarly() (timer 50ms bắt đầu NGAY lúc +load, xem bên dưới), rất có thể
// IL2CPP/Assembly-CSharp CHƯA nạp xong ở những tick đầu tiên (khác FFAntiObserve, luôn gọi từ vòng
// lặp SAU mốc 3 giây, lúc IL2CPP chắc chắn đã sẵn sàng) - phải tự retry CẢ bước tìm class, không
// chỉ bước lấy static_field_data.
inline void Tick() {
    ++g_retryTick;
    if (!g_staticData) {
        if (!g_klass) {
            if (g_retryTick % 10 != 0) return;  // ~500ms/lan thu tim class, tranh spam log/CPU
            g_klass = Il2CppResolve::GetClass("Assembly-CSharp.dll", "COW", "GameFacade");
            if (!g_klass) return;  // chua san sang - thu lai tick sau, KHONG bo cuoc
            DeltaVFS_debugLogf("EmulatorCheckSpoof: tim thay class COW.GameFacade %p, cho static constructor chay...", g_klass);
        }
        if (!Il2CppResolve::p_il2cpp_class_get_static_field_data) return;
        g_staticData = Il2CppResolve::p_il2cpp_class_get_static_field_data(g_klass);
        if (!g_staticData) return;
        DeltaVFS_debugLogf("EmulatorCheckSpoof: static field data san sang tai %p (tick #%d)",
                            g_staticData, g_retryTick);
    }

    uint8_t *checkedField = (uint8_t *)g_staticData + EMU_OFF_CHECKED;
    uint8_t *isEmuField   = (uint8_t *)g_staticData + EMU_OFF_ISEMU;
    bool needWrite = (*checkedField != 1) || (*isEmuField != 0);
    if (needWrite) {
        *checkedField = 1;  // m_EmulatorChecked = true - IsEmulator() se doc thang cache, khong tinh lai
        *isEmuField = 0;    // m_IsEmulator = false
        if (!g_everWritten) {
            DeltaVFS_debugLog("EmulatorCheckSpoof: da ep m_EmulatorChecked=true, m_IsEmulator=false");
            g_everWritten = true;
        }
    }

    // CHỈ ĐỌC (không ghi) IsEmulatorPool - field CÔNG KHAI riêng cho biết runtime có đang bị nhét
    // vào "pool giả lập" cho matchmaking hay không (khác m_IsEmulator/m_EmulatorChecked, vốn chỉ
    // dùng cho báo cáo SendLoginTime). Cùng cách an toàn với FFAntiObserve.h - chỉ đọc, log lúc đổi.
    uint8_t poolValue = *((uint8_t *)g_staticData + EMU_OFF_POOL);
    if (poolValue != g_lastPoolValue) {
        DeltaVFS_debugLogf("EmulatorCheckSpoof: GameFacade.IsEmulatorPool doi %u -> %u", g_lastPoolValue, poolValue);
        g_lastPoolValue = poolValue;
    }
}

inline bool IsReady() { return g_staticData != NULL; }

// installEarly() - BẮT ĐẦU POLLING NGAY TỪ +load, KHÔNG chờ 3 giây như vòng lặp updateMenu bình
// thường. Lý do: test thật xác nhận Tick() gọi từ vòng lặp mỗi frame (chỉ bắt đầu SAU mốc "3s
// dispatch_after") vẫn KHÔNG xoá được icon máy tính - rất có thể quá trình đăng nhập/xác thực với
// server (nơi SendLoginTime(isEmulator) gửi kết quả ĐI) đã chạy xong TRƯỚC mốc 3 giây đó, ngay
// những giây đầu app mở - sửa cache ở client sau khi đã gửi lên server rồi thì không rút lại được.
//
// PHẢI chạy trên MAIN QUEUE, KHÔNG phải background queue - test thật bản đầu (timer trên
// dispatch_get_global_queue) crash NGAY LẬP TỨC (exc=1 bên trong UnityFramework, rất sớm, trước cả
// mốc 3s) - gọi API IL2CPP (GetClass/il2cpp_class_get_static_field_data) từ 1 thread KHÔNG PHẢI
// main thread rất có thể không an toàn (FFAntiObserve.h/MatchClientInfoPatch.h trước giờ CHỈ gọi
// từ main thread qua CADisplayLink, chưa từng thử thread khác). Timer trên main queue vẫn chạy
// SỚM HƠN nhiều so với "3s dispatch_after" (không cần đợi UI setup), chỉ là không phải 1 thread
// riêng - main run loop vẫn xử lý các timer/dispatch khác đủ nhanh để không đáng lo về độ trễ.
inline void installEarly() {
    static dispatch_source_t timer;
    dispatch_queue_t queue = dispatch_get_main_queue();
    timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, queue);
    dispatch_source_set_timer(timer, dispatch_time(DISPATCH_TIME_NOW, 0),
                               50 * NSEC_PER_MSEC, 10 * NSEC_PER_MSEC);
    dispatch_source_set_event_handler(timer, ^{
        Tick();
        // Sau khi ghi thành công LẦN ĐẦU, vẫn giữ timer chạy tiếp (rẻ, chỉ so 2 byte) đề phòng
        // reset - KHÔNG huỷ timer, khác với các lookup "1 lần rồi thôi" khác trong project.
    });
    dispatch_resume(timer);
    DeltaVFS_debugLog("EmulatorCheckSpoof: installEarly() - bat dau polling moi 50ms tu +load, khong cho 3s");
}

} // namespace EmulatorCheckSpoof
