// MoniteUFPiggyback.h - thay vì tự vá UnityFramework (Tools/patch_unityframework_syscalls.py -
// UnityFrameworkSyscallHook.h, ĐÃ TẮT HẲN vì bị crash y hệt nhau ở CẢ 3 bản trampoline khác nhau,
// khả năng cao do chính việc SỬA FILE UnityFramework bị phát hiện, không phải lỗi logic
// trampoline), file này dùng NGUYÊN VẸN UnityFramework THẬT của Monite (không sửa 1 byte nào cả -
// lấy trực tiếp từ MoniteV2.ipa) làm bản thay thế trong IPA. Delta.dylib chỉ tự ghi con trỏ
// callback CỦA MÌNH vào đúng các "data slot" mà trampoline SẴN CÓ của Monite đọc lúc chạy - đúng
// việc Monite.dylib vẫn làm bình thường, chỉ là Delta.dylib làm thay. Vì KHÔNG đụng byte nào của
// UnityFramework, né hoàn toàn nghi vấn "sửa file bị phát hiện ở mức file/code-signing".
//
// QUY ƯỚC GỌI CALLBACK CỦA MONITE (dịch ngược THUẦN TÚY từ disassemble __HOOK_TEXT trong chính
// UnityFramework của họ, KHÔNG cần source Monite.dylib):
//   Mỗi 1 trong 50 điểm syscall có cấu trúc 2 lớp:
//     Layer1 (12 byte, ngay tại vị trí RVA gốc, thay "mov x16,#N" bằng lệnh nhảy tới đây):
//       ldr x17, [slotA]      ; slotA = con trỏ TỚI mã Layer2 (rỗng = 0 -> bỏ qua, chạy syscall gốc)
//       cbz x17, do_syscall
//       br  x17
//     Layer2 (56 byte, tại địa chỉ layer2Addr - Delta.dylib KHÔNG cần sửa, chỉ cần GHI slotA/slotB):
//       ... luu x30, doc callback that tu slotB, goi shared "luu toan bo thanh ghi" (0xc85c000),
//       ... khoi phuc x30, nhay ve do_syscall cua Layer1 tuong ung.
//   Shared "luu toan bo thanh ghi" routine (tai RVA co dinh 0xC85C000 trong UnityFramework cua
//   Monite) luu q0-q7 + x0-x30, roi goi callback(ctx) voi:
//     x0 = ctx = con tro toi 1 vung stack 16 byte: [ctx+0x00]=khong dung (scratch/khong xac dinh),
//                                                    [ctx+0x08]=SP goc cua game tai diem bi cham (thong tin).
//   NHUNG quan trong hon: do cach shared routine to chuc frame, muon SUA duoc x0/path (tham so
//   syscall THAT), callback PHAI ghi vao dung dia chi (char*)ctx + 0x18 - do la vi tri shared
//   routine se doc lai de nap vao x0 truoc khi chay "svc #0" that. Da xac nhan CHINH XAC bang cach
//   mo phong tung buoc sub/stp/str sp trong toan bo shared routine (khong doan boi tay).
//
// CHUA KIEM CHUNG TREN THIET BI THAT.
#pragma once
#import <Foundation/Foundation.h>
#include <string.h>
#include "MemoryUtils.h"
#include "AssetRedirect.h"  // redirectAllTrafficPath(), DeltaVFS_debugLog*

// RVA co dinh - CHI dung voi dung ban UnityFramework trich xuat tu MoniteV2.ipa (khong phai ban
// vanilla/und3fined, khong phai ban da tu vá cua Delta) - kiem tra chu ky truoc khi ghi de an toan.
#define MONITE_UF_SHARED_ROUTINE_RVA 0xC85C000ULL

// 24 byte dau cua shared routine that cua Monite (sub sp,#0x80; stp q6,q7,[sp,#0x60]; stp q4,q5,
// [sp,#0x40]; stp q2,q3,[sp,#0x20]; stp q0,q1,[sp]; sub sp,#0xf0) - trich xuat truc tiep tu binary
// that, dung de xac nhan dung ban UnityFramework nay truoc khi ghi.
static const uint8_t MONITE_UF_EXPECTED_SIGNATURE[24] = {
    0xFF, 0x03, 0x02, 0xD1, 0xE6, 0x1F, 0x03, 0xAD, 0xE4, 0x17, 0x02, 0xAD, 0xE2, 0x0F, 0x01, 0xAD,
    0xE0, 0x07, 0x00, 0xAD, 0xFF, 0xC3, 0x03, 0xD1,
};

// {slotA_rva, layer2_rva, slotB_rva} cho ca 50 diem - trich xuat bang script doc __HOOK_TEXT that,
// mo phong dung tung buoc offset (khong doan). RVA nay LA CUA UnityFramework trong MoniteV2.ipa.
static const uint64_t MONITE_UFHOOK_SITES[50][3] = {
    {0xC864048ULL, 0xC85C0F0ULL, 0xC864050ULL}, {0xC8640A0ULL, 0xC85C144ULL, 0xC8640A8ULL},
    {0xC8640F8ULL, 0xC85C198ULL, 0xC864100ULL}, {0xC864150ULL, 0xC85C1ECULL, 0xC864158ULL},
    {0xC8641A8ULL, 0xC85C240ULL, 0xC8641B0ULL}, {0xC864200ULL, 0xC85C294ULL, 0xC864208ULL},
    {0xC864258ULL, 0xC85C2E8ULL, 0xC864260ULL}, {0xC8642B0ULL, 0xC85C33CULL, 0xC8642B8ULL},
    {0xC864308ULL, 0xC85C390ULL, 0xC864310ULL}, {0xC864360ULL, 0xC85C3E4ULL, 0xC864368ULL},
    {0xC8643B8ULL, 0xC85C438ULL, 0xC8643C0ULL}, {0xC864410ULL, 0xC85C48CULL, 0xC864418ULL},
    {0xC864468ULL, 0xC85C4E0ULL, 0xC864470ULL}, {0xC8644C0ULL, 0xC85C534ULL, 0xC8644C8ULL},
    {0xC864518ULL, 0xC85C588ULL, 0xC864520ULL}, {0xC864570ULL, 0xC85C5DCULL, 0xC864578ULL},
    {0xC8645C8ULL, 0xC85C630ULL, 0xC8645D0ULL}, {0xC864620ULL, 0xC85C684ULL, 0xC864628ULL},
    {0xC864678ULL, 0xC85C6D8ULL, 0xC864680ULL}, {0xC8646D0ULL, 0xC85C72CULL, 0xC8646D8ULL},
    {0xC864728ULL, 0xC85C780ULL, 0xC864730ULL}, {0xC864780ULL, 0xC85C7D4ULL, 0xC864788ULL},
    {0xC8647D8ULL, 0xC85C828ULL, 0xC8647E0ULL}, {0xC864830ULL, 0xC85C87CULL, 0xC864838ULL},
    {0xC864888ULL, 0xC85C8D0ULL, 0xC864890ULL}, {0xC8648E0ULL, 0xC85C924ULL, 0xC8648E8ULL},
    {0xC864938ULL, 0xC85C978ULL, 0xC864940ULL}, {0xC864990ULL, 0xC85C9CCULL, 0xC864998ULL},
    {0xC8649E8ULL, 0xC85CA20ULL, 0xC8649F0ULL}, {0xC864A40ULL, 0xC85CA74ULL, 0xC864A48ULL},
    {0xC864A98ULL, 0xC85CAC8ULL, 0xC864AA0ULL}, {0xC864AF0ULL, 0xC85CB1CULL, 0xC864AF8ULL},
    {0xC864B48ULL, 0xC85CB70ULL, 0xC864B50ULL}, {0xC864BA0ULL, 0xC85CBC4ULL, 0xC864BA8ULL},
    {0xC864BF8ULL, 0xC85CC18ULL, 0xC864C00ULL}, {0xC864C50ULL, 0xC85CC6CULL, 0xC864C58ULL},
    {0xC864CA8ULL, 0xC85CCC0ULL, 0xC864CB0ULL}, {0xC864D00ULL, 0xC85CD14ULL, 0xC864D08ULL},
    {0xC864D58ULL, 0xC85CD68ULL, 0xC864D60ULL}, {0xC864DB0ULL, 0xC85CDBCULL, 0xC864DB8ULL},
    {0xC864E08ULL, 0xC85CE10ULL, 0xC864E10ULL}, {0xC864E60ULL, 0xC85CE64ULL, 0xC864E68ULL},
    {0xC864EB8ULL, 0xC85CEB8ULL, 0xC864EC0ULL}, {0xC864F10ULL, 0xC85CF0CULL, 0xC864F18ULL},
    {0xC864F68ULL, 0xC85CF60ULL, 0xC864F70ULL}, {0xC864FC0ULL, 0xC85CFB4ULL, 0xC864FC8ULL},
    {0xC865018ULL, 0xC85D008ULL, 0xC865020ULL}, {0xC865070ULL, 0xC85D05CULL, 0xC865078ULL},
    {0xC8650C8ULL, 0xC85D0B0ULL, 0xC8650D0ULL}, {0xC865120ULL, 0xC85D104ULL, 0xC865128ULL},
};

// Callback that Layer2 cua Monite se BLR toi (x0 = ctx, xem giai thich quy uoc goi o dau file).
// KHONG dung return value binh thuong - phai GHI TRUC TIEP vao *(ctx+0x18) de doi duoc path/x0
// that su dung cho syscall that chay ngay sau do. Dung lai redirectAllTrafficPath() da co san
// trong AssetRedirect.h - KHONG viet logic redirect rieng lan 2 (giong huong da lam voi
// UnityFrameworkSyscallHook.h truoc do).
// DIAGNOSTIC TAM: dem + log vai lan goi dau tien - de biet CHAC callback nay co thuc su duoc
// trampoline cua Monite goi toi hay khong (khong chi "ghi slot xong" ma con phai THAT SU chay).
// XOA sau khi da xac dinh xong.
static std::atomic<int> g_moniteUFHookCallCount{0};

extern "C" inline void MoniteUFHook_Callback(void *ctx) {
    if (!ctx) return;
    char **pathSlot = (char **)((char *)ctx + 0x18);
    const char *origPath = *pathSlot;
    int n = g_moniteUFHookCallCount.fetch_add(1, std::memory_order_relaxed);
    if (n < 20) {
        DeltaVFS_debugLogf("MoniteUFHook_Callback: goi lan #%d, ctx=%p, path=%s", n + 1, ctx, origPath ? origPath : "(null)");
    }
    if (!origPath) return;
    const char *redirected = redirectAllTrafficPath(origPath);
    if (redirected && redirected != origPath) {
        if (n < 20) {
            DeltaVFS_debugLogf("MoniteUFHook_Callback: redirect %s -> %s", origPath, redirected);
        }
        *pathSlot = (char *)redirected;
    }
}

inline void installMoniteUFPiggyback() {
    uintptr_t base = (uintptr_t)getRealOffset(0);
    if (!base) {
        DeltaVFS_debugLog("MoniteUFPiggyback: khong tim thay UnityFramework, bo qua");
        return;
    }

    uintptr_t sharedRoutineAddr = base + MONITE_UF_SHARED_ROUTINE_RVA;
    if (memcmp((void *)sharedRoutineAddr, MONITE_UF_EXPECTED_SIGNATURE, sizeof(MONITE_UF_EXPECTED_SIGNATURE)) != 0) {
        DeltaVFS_debugLog("MoniteUFPiggyback: KHONG thay chu ky shared_routine cua Monite tai vi tri "
                           "du kien - UnityFramework nay KHONG PHAI ban trich tu MoniteV2.ipa (hoac "
                           "khac ban da phan tich) - HUY, khong ghi gi ca de an toan");
        return;
    }

    int written = 0;
    for (int i = 0; i < 50; i++) {
        uint64_t slotA_rva = MONITE_UFHOOK_SITES[i][0];
        uint64_t layer2_rva = MONITE_UFHOOK_SITES[i][1];
        uint64_t slotB_rva = MONITE_UFHOOK_SITES[i][2];

        void **slotA = (void **)(base + slotA_rva);
        void **slotB = (void **)(base + slotB_rva);

        *slotA = (void *)(base + layer2_rva);      // "arm" Layer1 - tro toi Layer2 cua CHINH Monite (co san, khong sua)
        *slotB = (void *)&MoniteUFHook_Callback;    // Layer2 se BLR toi callback CUA Delta thay vi cua Monite
        written++;
    }

    DeltaVFS_debugLogf("MoniteUFPiggyback: xac nhan chu ky OK - da ghi %d/50 slot (base=%p)", written, (void *)base);
}
