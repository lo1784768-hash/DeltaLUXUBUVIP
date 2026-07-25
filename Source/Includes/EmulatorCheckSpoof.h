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
static bool g_classLookupFailed = false;
static void *g_staticData = NULL;
static int g_retryTick = 0;
static bool g_everWritten = false;

#define EMU_OFF_CHECKED  0x35F
#define EMU_OFF_ISEMU    0x360

inline void Tick() {
    if (g_classLookupFailed) return;
    if (!g_staticData) {
        if (!g_klass) {
            g_klass = Il2CppResolve::GetClass("Assembly-CSharp.dll", "COW", "GameFacade");
            if (!g_klass) {
                DeltaVFS_debugLog("EmulatorCheckSpoof: khong tim thay class COW.GameFacade - tat");
                g_classLookupFailed = true;
                return;
            }
            DeltaVFS_debugLogf("EmulatorCheckSpoof: tim thay class COW.GameFacade %p, cho static constructor chay...", g_klass);
        }
        if (++g_retryTick % 60 != 0) return;
        if (!Il2CppResolve::p_il2cpp_class_get_static_field_data) return;
        g_staticData = Il2CppResolve::p_il2cpp_class_get_static_field_data(g_klass);
        if (!g_staticData) return;
        DeltaVFS_debugLogf("EmulatorCheckSpoof: static field data san sang tai %p (sau %d lan thu)",
                            g_staticData, g_retryTick / 60);
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
}

inline bool IsReady() { return g_staticData != NULL; }

} // namespace EmulatorCheckSpoof
