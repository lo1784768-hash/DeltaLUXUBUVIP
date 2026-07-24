// FFAntiObserve.h - ĐỌC THUẦN TUÝ (không hook/patch gì cả) các field cờ kết quả phát hiện trong
// ffantihack.MFHPGMELLCC, log ra debug.log MỖI KHI GIÁ TRỊ THAY ĐỔI - để biết CHẮC CHẮN dữ liệu
// thật lúc vào trận thay vì tiếp tục đoán qua disassemble tĩnh.
//
// Vì sao đọc thuần thay vì hook Send()/OnHandlePacket() (PacketCapture.h): CẢ 2 kỹ thuật hook
// trampoline (MSHookFunction/Dobby) từng thử trên các hàm "nhạy cảm" (GetMatchClientInfo qua
// AntiReportSpoof, Send/OnHandlePacket qua PacketCapture, CheckHacker) ĐỀU crash - kể cả bản
// passthrough không sửa gì. Ngược lại patch TĨNH tại chỗ (đổi 1 thanh ghi, không đổi entry point)
// trên GetMatchClientInfo lại ổn định (MatchClientInfoPatch.h, 7 điểm, không crash). Đọc field
// tĩnh qua il2cpp_class_get_static_field_data() KHÔNG đụng code game chút nào (không patch,
// không hook, không đổi 1 byte executable nào) - nên rủi ro thấp hơn cả patch tĩnh.
//
// 5 field private static bool nghi la ket qua phat hien rieng le (xem CheckHackerPatch.h/
// FFAntiFlagsPatch.h de biet nguon goc): PHEEFAHAHFE@0x38, IBJJDBEJMLD@0x39, CAANLIGMMKP@0x3A,
// KIBPPIFNAKC@0x3B, GPANAPICKMA@0x3C. Them ANFAMBFAHHB@0x48 (public bool, co the la co tong hop).
#pragma once
#import <Foundation/Foundation.h>
#import "AssetRedirect.h"  // DeltaVFS_debugLog/DeltaVFS_debugLogf
#import "Il2CppResolve.h"

namespace FFAntiObserve {

static void *g_staticData = NULL;
static bool g_resolveAttempted = false;
static uint8_t g_lastValues[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};  // 0xFF = "chua doc lan nao"

inline void CheckAndLog() {
    if (!g_staticData) {
        if (g_resolveAttempted) return;  // da thu 1 lan, khong tim thay class thi thoi
        g_resolveAttempted = true;
        g_staticData = Il2CppResolve::GetStaticFieldData("Assembly-CSharp.dll", "ffantihack", "MFHPGMELLCC");
        if (!g_staticData) {
            DeltaVFS_debugLog("FFAntiObserve: khong resolve duoc ffantihack.MFHPGMELLCC (ten class co the da doi, hoac thieu il2cpp_class_get_static_field_data trong binary) - tat quan sat nay");
            return;
        }
        DeltaVFS_debugLogf("FFAntiObserve: resolve OK, static field data tai %p", g_staticData);
    }

    static const struct { size_t off; const char *name; } fields[6] = {
        {0x38, "PHEEFAHAHFE"},
        {0x39, "IBJJDBEJMLD"},
        {0x3A, "CAANLIGMMKP"},
        {0x3B, "KIBPPIFNAKC"},
        {0x3C, "GPANAPICKMA"},
        {0x48, "ANFAMBFAHHB(public)"},
    };

    for (int i = 0; i < 6; i++) {
        uint8_t cur = *(uint8_t *)((char *)g_staticData + fields[i].off);
        if (cur != g_lastValues[i]) {
            DeltaVFS_debugLogf("FFAntiObserve: %s doi %u -> %u", fields[i].name, g_lastValues[i], cur);
            g_lastValues[i] = cur;
        }
    }
}

} // namespace FFAntiObserve
