// UnityFrameworkSyscallHook.h - ghi con tro callback (DeltaSyscallRedirectCallback) vao "data
// slot" trong CHINH UnityFramework - noi ma Tools/patch_unityframework_syscalls.py da chen san
// 1 trampoline redirect cho ~50 diem Free Fire tu goi THANG syscall stat/open/access (mov
// x16,#N; svc #0 - KHONG qua symbol libSystem, nen fishhook cua AssetRedirect.h KHONG BAO GIO
// thay duoc cac diem nay).
//
// BOI CANH DAY DU: xem comment dau Tools/patch_unityframework_syscalls.py. Tom tat: doi chieu
// UnityFramework "und3fined" (dang chay tren may) voi UnityFramework trong MoniteV2.ipa
// (Monite.dylib CHAY DUOC, khong bi da) lo ra Monite tu chuan bi rieng 1 ban UnityFramework co
// them code cho DUNG ~50+ diem nay - giai thich tai sao Monite tranh duoc 1 loai kiem tra file
// ma toan bo cong suc patch MatchClientInfo/mtime/... o phia dylib CHUA BAO GIO cham toi duoc.
//
// KHAC VOI Monite (them segment moi vao Mach-O, rui ro cau truc file chua kiem chung duoc qua
// lief o moi truong nay): Tools/patch_unityframework_syscalls.py dung 2 khoang trong CO SAN,
// TOAN SO 0 (padding align trang cuoi __TEXT that + cuoi phan noi dung that cua __DATA) - AN
// TOAN HON, khong dung lief them/mo rong section.
//
// VI SAO DELTA.DYLIB PHAI TU GHI (khong the dung nguyen UnityFramework cua Monite): trampoline
// ho chen doc con tro callback tu __HOOK_DATA/__MONITE_DATA cua HO - con tro do CHI duoc dien
// gia tri THAT luc CHINH Monite.dylib chay (constructor cua ho tu ghi). Neu chi lay file
// UnityFramework cua ho ma khong chay Monite.dylib, o nho do mai la 0 -> nhanh "callback rong,
// chay syscall goc binh thuong" luon kich hoat -> hieu qua nhu KHONG va gi ca. Phai co ban
// UnityFramework rieng (Tools/patch_unityframework_syscalls.py tao ra) voi data slot RIENG de
// CHINH Delta.dylib ghi dia chi ham cua minh vao.
//
// AN TOAN: __DATA la segment doc-ghi binh thuong luc chay (khac __TEXT read-execute-only) - ghi
// con tro qua 1 phep gan con tro binh thuong, KHONG can ky thuat vm_remap nhu vá code. Co kiem
// tra "chu ky" 24 byte dau cua shared_routine truoc khi ghi - neu UnityFramework hien tai KHONG
// phai ban da duoc cong cu va (vi du: quen thay file, hoac game update offset lech), HUY ghi de
// an toan thay vi ghi mu vao 1 offset co the la du lieu that khac.
//
// CHUA KIEM CHUNG TREN THIET BI THAT - ca file UnityFramework da va LAN code ghi data slot nay.
#pragma once
#import <Foundation/Foundation.h>
#include <string.h>
#include "MemoryUtils.h"
#include "AssetRedirect.h"  // redirectAllTrafficPath(), DeltaVFS_debugLog*

// PHAI KHOP CHINH XAC voi DATA_SLACK_START/CODE_SLACK_START trong
// Tools/patch_unityframework_syscalls.py - doi 1 cho phai doi ca 2 noi.
#define UFSH_DATA_SLOT_RVA        0xC419810ULL
#define UFSH_SHARED_ROUTINE_RVA   0xB64AB44ULL

// 24 byte dau cua shared_routine (sub sp,#0x30; stp x1,x2,[sp]; str x16,[sp,#0x10];
// str x30,[sp,#0x18]; adrp x9,#...) - chu ky de xac nhan UnityFramework hien tai DUNG LA ban da
// duoc Tools/patch_unityframework_syscalls.py va, khong phai ban goc/ban build khac.
static const uint8_t UFSH_EXPECTED_SIGNATURE[24] = {
    0xFF, 0xC3, 0x00, 0xD1, 0xE1, 0x0B, 0x00, 0xA9, 0xF0, 0x0B, 0x00, 0xF9, 0xFE, 0x0F, 0x00, 0xF9,
    0x69, 0x6E, 0x00, 0xF0, 0x29, 0x41, 0x20, 0x91,
};

// Callback duoc trampoline trong UnityFramework goi (qua BLR) voi x0 = path goc (con tro C
// string that syscall dinh doc/mo/kiem tra) - tra ve path (co the da redirect) qua x0. Dung lai
// DUNG redirectAllTrafficPath() da co san trong AssetRedirect.h - KHONG viet logic redirect
// rieng lan 2.
extern "C" inline const char *DeltaSyscallRedirectCallback(const char *path) {
    if (!path) return path;
    const char *redirected = redirectAllTrafficPath(path);
    return redirected ? redirected : path;
}

inline void installUnityFrameworkSyscallHook() {
    uintptr_t base = (uintptr_t)getRealOffset(0);
    if (!base) {
        DeltaVFS_debugLog("UnityFrameworkSyscallHook: khong tim thay UnityFramework, bo qua");
        return;
    }

    uintptr_t sharedRoutineAddr = base + UFSH_SHARED_ROUTINE_RVA;
    if (memcmp((void *)sharedRoutineAddr, UFSH_EXPECTED_SIGNATURE, sizeof(UFSH_EXPECTED_SIGNATURE)) != 0) {
        DeltaVFS_debugLog("UnityFrameworkSyscallHook: KHONG thay chu ky shared_routine tai vi tri "
                           "du kien - UnityFramework nay CHUA duoc Tools/patch_unityframework_syscalls.py "
                           "va (hoac khac ban da phan tich) - HUY, khong ghi gi ca de an toan");
        return;
    }

    void **dataSlot = (void **)(base + UFSH_DATA_SLOT_RVA);
    *dataSlot = (void *)&DeltaSyscallRedirectCallback;
    DeltaVFS_debugLogf("UnityFrameworkSyscallHook: xac nhan chu ky OK - da ghi callback %p vao data slot %p (base=%p)",
                        (void *)&DeltaSyscallRedirectCallback, (void *)dataSlot, (void *)base);
}
