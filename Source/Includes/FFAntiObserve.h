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
#include "MemoryUtils.h"  // getRealOffset(0) - lay base UnityFramework de tinh RVA that

namespace FFAntiObserve {

static void *g_klass = NULL;             // cache class - tim 1 lan la du, class luon ton tai
static bool g_classLookupFailed = false; // tim class that bai han (khac RVA/ten) - khong thu lai
static void *g_staticData = NULL;        // static_field_data co the tra NULL nhieu lan - class
                                          // CHUA CHAY static constructor (chua vao trang thai can
                                          // thiet) - PHAI THU LAI moi frame cho toi khi khac NULL,
                                          // khong duoc bo cuoc sau 1 lan (khac voi GetClass()).
static int g_retryTick = 0;
static uint8_t g_lastValues[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};  // 0xFF = "chua doc lan nao"

inline void CheckAndLog() {
    if (g_classLookupFailed) return;
    if (!g_staticData) {
        if (!g_klass) {
            g_klass = Il2CppResolve::GetClass("Assembly-CSharp.dll", "ffantihack", "MFHPGMELLCC");
            if (!g_klass) {
                DeltaVFS_debugLog("FFAntiObserve: khong tim thay class ffantihack.MFHPGMELLCC - tat quan sat nay");
                g_classLookupFailed = true;
                return;
            }
            DeltaVFS_debugLogf("FFAntiObserve: tim thay class %p, cho static constructor chay...", g_klass);
        }
        // Thu lai il2cpp_class_get_static_field_data() moi ~60 frame (~1s) - class co the CHUA
        // chay static constructor luc ung dung moi mo, se chay khi FFAnti thuc su duoc dung.
        if (++g_retryTick % 60 != 0) return;
        if (!Il2CppResolve::p_il2cpp_class_get_static_field_data) return;
        g_staticData = Il2CppResolve::p_il2cpp_class_get_static_field_data(g_klass);
        if (!g_staticData) return;  // van chua san sang, thu lai sau
        uintptr_t base = (uintptr_t)getRealOffset(0);
        uintptr_t rva = base ? ((uintptr_t)g_staticData - base) : 0;
        DeltaVFS_debugLogf("FFAntiObserve: static field data san sang tai %p (sau %d lan thu) - base=0x%lx RVA=0x%lx",
                            g_staticData, g_retryTick / 60, (unsigned long)base, (unsigned long)rva);
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
            // User yeu cau: bat 0 -> 1 la crash NGAY de biet chinh xac luc nao/dang lam gi trong
            // game khi co flip - de "test cho de" thay vi phai doi mo debug.log doi chieu timestamp
            // voi tri nho. Dung 1 dia chi loi RIENG BIET (0xDEADBEEF00 + index field) thay vi
            // abort() vi CrashLogger (HWBreakHook.h) chi bat EXC_MASK_BAD_ACCESS/BAD_INSTRUCTION/
            // ARITHMETIC (KHONG bat SIGABRT) - ghi vao dia chi nay tao EXC_BAD_ACCESS that, chac
            // chan duoc CrashLogger bat va ghi backtrace, dong thoi dia chi de nhan ra ngay la crash
            // CO Y (khong phai bug that) khi doc log.
            //
            // CHI crash cho IBJJDBEJMLD (i==1) - test that (debug.log) xac nhan PHEEFAHAHFE/
            // KIBPPIFNAKC (i==0,3) LUON bat rat som TRONG SANH (2-55s sau khi class san sang, truoc
            // ca luc vao tran), nen crash ca 2 co do se chan khong cho vao tran duoc de test tiep
            // IBJJDBEJMLD/luc bi da that. IBJJDBEJMLD la co DUY NHAT gan truc tiep voi thoi diem vao
            // tran that (bat dung ~2s sau "DA VAO TRAN"), nen chi no dang crash luc nay.
            if (i == 1 && g_lastValues[i] == 0 && cur == 1) {
                DeltaVFS_debugLogf("FFAntiObserve: %s tu 0 -> 1 - CO Y CRASH NGAY (debug) de danh dau thoi diem chinh xac", fields[i].name);
                volatile int *trap = (volatile int *)(uintptr_t)(0xDEADBEEF00ULL + (unsigned)i);
                *trap = 1;
            }
            g_lastValues[i] = cur;
        }
    }
}

// True khi class ffantihack.MFHPGMELLCC ĐÃ chay xong static constructor (static field data san
// sang) - dung de tri hoan installFFAntiFlagsPatch() cho toi luc nay thay vi chay ngay luc +load
// (~3s sau khi mo app, LUC CLASS CHUA INIT XONG - nghi ngo chinh la ly do patch gay crash truoc
// day, khong phai do noi dung patch).
inline bool IsReady() { return g_staticData != NULL; }

} // namespace FFAntiObserve
