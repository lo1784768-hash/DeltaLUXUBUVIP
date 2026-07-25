#pragma once
// ============================================================================
//  CheatDetectTiming.h - hook PASSTHROUGH (chỉ log, KHÔNG sửa gì) vào 1 nhóm hàm anti-cheat
//  nội bộ khác đã tìm được trong dump.cs (ob54), để biết CHÍNH XÁC lúc nào từng hàm được gọi
//  (so với mốc login/vào trận) - bổ sung cho FFAntiObserve.h (chỉ biết lúc KẾT QUẢ đổi, không
//  biết lúc hàm được GỌI).
//
//  RỦI RO ĐÃ BIẾT (đọc kỹ trước khi bật thêm hàm nào): 2 lần hook trampoline trước đây trên
//  UnityFramework - AntiReportSpoof.h (GetMatchClientInfo, static, 0 tham số, trả về object) và
//  PacketCapture.h (Send/OnHandlePacket, instance, 2-3 tham số) - ĐỀU crash-loop trên máy thật,
//  kể cả bản passthrough không sửa field nào (xem commit ec6e781, c6d0dc0). Chưa rõ nguyên nhân
//  gốc là do 2 hàm ĐÓ cụ thể (VD: được gọi từ nhiều thread cùng lúc, PAC prologue arm64e) hay do
//  BẤT KỲ hook trampoline nào trên UnityFramework đều rủi ro như nhau - CHƯA CÓ ĐỦ BẰNG CHỨNG để
//  loại trừ khả năng thứ 2. User đã yêu cầu thử tiếp (thay vì chỉ đọc field an toàn) - nên làm
//  TỪNG HÀM MỘT, build/test kỹ mỗi hàm trước khi bật hàm tiếp theo, KHÔNG bật hết 1 lượt.
//
//  THỨ TỰ BẬT ĐỀ XUẤT (từ Menu.mm, mỗi lần chỉ 1 dòng installXxx() không bị comment):
//    1. installProcessAHTiming()               - "AH" gần như chắc chắn = Anti-Hack, ưu tiên cao nhất
//    2. installGenerateLibMapAndMemTiming()     - quét dylib/bộ nhớ lúc login, nghi ngờ phát hiện Delta.dylib
//    3. installSecPlayerLoginTiming()
//    4. installProcessffantihackGGPTiming()
//    5. installSkinModMD5CheckTiming()          - self=0 tham số ngoài self, đơn giản nhất
//    6. installHackerDetectedUITiming()         - CHỈ fire khi UI "Hacker Detected" thật sự hiện lên
//  Tất cả đều là instance method của UIModelLogin (1-5) hoặc lớp khác (6), tham số ĐỀU là
//  con trỏ/bool đơn giản (không có struct-by-value) - ABI đơn giản hơn nhiều so với 2 hàm đã
//  crash trước đây (GetMatchClientInfo trả về object; Send có 3 tham số hỗn hợp con trỏ+byte).
//
//  Sau khi test xong 1 hàm KHÔNG crash trong >= 1 trận đấu đầy đủ, mới bật tiếp hàm kế - nếu 1
//  hàm bất kỳ gây crash, XOÁ lời gọi installXxx() của riêng hàm đó (giữ nguyên các hàm đã xác
//  nhận ổn định), không cần nghi ngờ toàn bộ cơ chế.
// ============================================================================
#import "Il2CppResolve.h"
#import "AssetRedirect.h"  // DeltaVFS_debugLog/DeltaVFS_debugLogf

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmodule-import-in-extern-c"
#import "Dobby/dobby.h"
#pragma clang diagnostic pop

namespace CheatDetectTiming {

inline uint64_t nowMs() {
    return (uint64_t)([[NSDate date] timeIntervalSince1970] * 1000.0);
}

// ---- 1. UIModelLogin.ProcessAH(EGLJDBDMENB loginRes) - RVA 0x43F9620 ----
typedef void (*ORIG_ProcessAH)(void *, void *);
static ORIG_ProcessAH orig_ProcessAH = NULL;
static void hooked_ProcessAH(void *self, void *loginRes) {
    DeltaVFS_debugLogf("CheatDetectTiming: ProcessAH() GOI luc t=%llu ms (thread=%p)",
                        (unsigned long long)nowMs(), (__bridge void *)[NSThread currentThread]);
    orig_ProcessAH(self, loginRes);
}
inline void installProcessAHTiming() {
    void *target = Il2CppResolve::GetMethod("Assembly-CSharp.dll", "COW", "UIModelLogin", "ProcessAH", 1);
    if (!target) target = (void *)getRealOffset(0x43F9620);
    int ret = DobbyHook(target, (dobby_dummy_func_t)hooked_ProcessAH, (dobby_dummy_func_t *)&orig_ProcessAH);
    DeltaVFS_debugLogf("CheatDetectTiming: hook ProcessAH() %s (ret=%d)", (ret == 0 && orig_ProcessAH) ? "OK" : "THAT BAI", ret);
}

// ---- 2. UIModelLogin.GenerateLibMapAndMem(EGLJDBDMENB loginRes) - RVA 0x43F81F4 ----
typedef void (*ORIG_GenerateLibMapAndMem)(void *, void *);
static ORIG_GenerateLibMapAndMem orig_GenerateLibMapAndMem = NULL;
static void hooked_GenerateLibMapAndMem(void *self, void *loginRes) {
    DeltaVFS_debugLogf("CheatDetectTiming: GenerateLibMapAndMem() GOI luc t=%llu ms (thread=%p)",
                        (unsigned long long)nowMs(), (__bridge void *)[NSThread currentThread]);
    orig_GenerateLibMapAndMem(self, loginRes);
}
inline void installGenerateLibMapAndMemTiming() {
    void *target = Il2CppResolve::GetMethod("Assembly-CSharp.dll", "COW", "UIModelLogin", "GenerateLibMapAndMem", 1);
    if (!target) target = (void *)getRealOffset(0x43F81F4);
    int ret = DobbyHook(target, (dobby_dummy_func_t)hooked_GenerateLibMapAndMem, (dobby_dummy_func_t *)&orig_GenerateLibMapAndMem);
    DeltaVFS_debugLogf("CheatDetectTiming: hook GenerateLibMapAndMem() %s (ret=%d)", (ret == 0 && orig_GenerateLibMapAndMem) ? "OK" : "THAT BAI", ret);
}

// ---- 3. UIModelLogin.SecPlayerLogin(bool isReconnect) - RVA 0x43F9B14 ----
typedef void (*ORIG_SecPlayerLogin)(void *, bool);
static ORIG_SecPlayerLogin orig_SecPlayerLogin = NULL;
static void hooked_SecPlayerLogin(void *self, bool isReconnect) {
    DeltaVFS_debugLogf("CheatDetectTiming: SecPlayerLogin(isReconnect=%d) GOI luc t=%llu ms (thread=%p)",
                        (int)isReconnect, (unsigned long long)nowMs(), (__bridge void *)[NSThread currentThread]);
    orig_SecPlayerLogin(self, isReconnect);
}
inline void installSecPlayerLoginTiming() {
    void *target = Il2CppResolve::GetMethod("Assembly-CSharp.dll", "COW", "UIModelLogin", "SecPlayerLogin", 1);
    if (!target) target = (void *)getRealOffset(0x43F9B14);
    int ret = DobbyHook(target, (dobby_dummy_func_t)hooked_SecPlayerLogin, (dobby_dummy_func_t *)&orig_SecPlayerLogin);
    DeltaVFS_debugLogf("CheatDetectTiming: hook SecPlayerLogin() %s (ret=%d)", (ret == 0 && orig_SecPlayerLogin) ? "OK" : "THAT BAI", ret);
}

// ---- 4. UIModelLogin.ProcessffantihackGGP(EGLJDBDMENB loginRes) - RVA 0x43F5164 ----
typedef void (*ORIG_ProcessffantihackGGP)(void *, void *);
static ORIG_ProcessffantihackGGP orig_ProcessffantihackGGP = NULL;
static void hooked_ProcessffantihackGGP(void *self, void *loginRes) {
    DeltaVFS_debugLogf("CheatDetectTiming: ProcessffantihackGGP() GOI luc t=%llu ms (thread=%p)",
                        (unsigned long long)nowMs(), (__bridge void *)[NSThread currentThread]);
    orig_ProcessffantihackGGP(self, loginRes);
}
inline void installProcessffantihackGGPTiming() {
    void *target = Il2CppResolve::GetMethod("Assembly-CSharp.dll", "COW", "UIModelLogin", "ProcessffantihackGGP", 1);
    if (!target) target = (void *)getRealOffset(0x43F5164);
    int ret = DobbyHook(target, (dobby_dummy_func_t)hooked_ProcessffantihackGGP, (dobby_dummy_func_t *)&orig_ProcessffantihackGGP);
    DeltaVFS_debugLogf("CheatDetectTiming: hook ProcessffantihackGGP() %s (ret=%d)", (ret == 0 && orig_ProcessffantihackGGP) ? "OK" : "THAT BAI", ret);
}

// ---- 5. UIModelSkinModCheck.MD5Check() - RVA 0x467F22C (0 tham số ngoài self, tra bool) ----
typedef bool (*ORIG_SkinModMD5Check)(void *);
static ORIG_SkinModMD5Check orig_SkinModMD5Check = NULL;
static bool hooked_SkinModMD5Check(void *self) {
    bool result = orig_SkinModMD5Check(self);
    DeltaVFS_debugLogf("CheatDetectTiming: UIModelSkinModCheck.MD5Check() GOI luc t=%llu ms, ket qua=%d (thread=%p)",
                        (unsigned long long)nowMs(), (int)result, (__bridge void *)[NSThread currentThread]);
    return result;
}
inline void installSkinModMD5CheckTiming() {
    void *target = Il2CppResolve::GetMethod("Assembly-CSharp.dll", "COW", "UIModelSkinModCheck", "MD5Check", 0);
    if (!target) target = (void *)getRealOffset(0x467F22C);
    int ret = DobbyHook(target, (dobby_dummy_func_t)hooked_SkinModMD5Check, (dobby_dummy_func_t *)&orig_SkinModMD5Check);
    DeltaVFS_debugLogf("CheatDetectTiming: hook UIModelSkinModCheck.MD5Check() %s (ret=%d)", (ret == 0 && orig_SkinModMD5Check) ? "OK" : "THAT BAI", ret);
}

// ---- 6. UIHudHackerDetectedController.SetDelayCloseAction(Action, bool) - RVA 0x30929F8 ----
// CHỈ fire khi UI "Hacker Detected" THẬT SỰ hiện lên trong trận - tín hiệu rõ ràng nhất trong cả
// nhóm này, nhưng hiếm khi xảy ra lúc test bình thường (không bị đá) nên có thể không log được gì.
typedef void (*ORIG_SetDelayCloseAction)(void *, void *, bool);
static ORIG_SetDelayCloseAction orig_SetDelayCloseAction = NULL;
static void hooked_SetDelayCloseAction(void *self, void *callback, bool isHackerTeam) {
    DeltaVFS_debugLogf("CheatDetectTiming: !!! UIHudHackerDetectedController.SetDelayCloseAction(isHackerTeam=%d) GOI luc t=%llu ms - UI HACKER DETECTED VUA HIEN!",
                        (int)isHackerTeam, (unsigned long long)nowMs());
    orig_SetDelayCloseAction(self, callback, isHackerTeam);
}
inline void installHackerDetectedUITiming() {
    void *target = Il2CppResolve::GetMethod("Assembly-CSharp.dll", "COW", "UIHudHackerDetectedController", "SetDelayCloseAction", 2);
    if (!target) target = (void *)getRealOffset(0x30929F8);
    int ret = DobbyHook(target, (dobby_dummy_func_t)hooked_SetDelayCloseAction, (dobby_dummy_func_t *)&orig_SetDelayCloseAction);
    DeltaVFS_debugLogf("CheatDetectTiming: hook SetDelayCloseAction() %s (ret=%d)", (ret == 0 && orig_SetDelayCloseAction) ? "OK" : "THAT BAI", ret);
}

} // namespace CheatDetectTiming
