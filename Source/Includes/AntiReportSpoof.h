#pragma once
// ============================================================================
//  AntiReportSpoof.h - hook COW.UIModelCustomRoom.GetMatchClientInfo() (dump.cs OB54, RVA
//  0x3D82760) - hàm C# DUY NHẤT trong toàn bộ dump build ra đối tượng tcp.MatchClientInfo, gửi
//  lên server lúc chuẩn bị/vào trận, chứa tpsdk_str/file_exception/lib_result/exception_count/
//  scan_count/native_result/gin_check_data - khớp đúng các mã EHacker.HackerPoolCdt đã tìm được
//  (MD5_FILE_EXCEPTION/MD5_SCAN_COUNT/PMS_HOOK/NATIVE_RESULT/GIN_CHECK_DATA_EMPTY_IOS).
//
//  BỐI CẢNH ĐÃ LOẠI TRỪ BẰNG TEST THẬT TRÊN MÁY (không phải suy đoán):
//  - Đổi cách hook (fishhook -> Cocoa-swizzle) cho VFS redirect: KHÔNG hết bị đá.
//  - Tắt hết Aim/ESP/ModHacks: vẫn bị đá dù không bật gì.
//  - Giấu dylib khỏi 4 API liệt kê dyld image (DylibHide.h, xác nhận chạy đúng qua debug.log):
//    vẫn bị đá.
//  - Tắt hẳn nội dung redirect Delta.zip (AR_FORCE_DISABLE_VFS_FOR_TEST): vẫn bị đá.
//  Còn lại đúng field trong MatchClientInfo là hướng chưa thử - hook thẳng vào đây.
//
//  THỬ NGHIỆM CHƯA XÁC NHẬN, RỦI RO ĐÃ BIẾT VÀ CHẤP NHẬN:
//  1. gin_check_data KHÔNG bị đụng vào - để trống rỗng chính là 1 rule phát hiện riêng
//     (HackerPoolCdt_GIN_CHECK_DATA_EMPTY_IOS), sửa sai chỗ này có thể PHẢN TÁC DỤNG.
//  2. Đặt NULL cho field kiểu byte[]/string thay vì mảng rỗng thật - nếu có code C# nào khác
//     đọc lại field này TRƯỚC khi gửi lên server (ngoài phạm vi hàm này) mà không tự kiểm tra
//     null, có thể gây crash (NullReferenceException phía IL2CPP). Chưa kiểm chứng được trên
//     máy thật vì không có Ghidra để đọc caller.
//  3. RVA 0x3D82760 lấy từ dump OB54 - nếu bản game hiện tại lệch version, Il2CppResolve sẽ tự
//     tra theo tên trước (namespace COW, class UIModelCustomRoom, method GetMatchClientInfo,
//     0 tham số), chỉ rơi về RVA cứng khi tra tên thất bại - xem Il2CppResolve.h.
//
//  DÙNG DOBBY, KHÔNG DÙNG MSHookFunction: đã test thật trên máy - MSHookFunction (Substrate)
//  thất bại với CẢ 2 kiểu địa chỉ (tra theo tên lẫn RVA cứng) cho đúng hàm này, dù
//  installAimMagnetHook() (ESP.h) vẫn dùng MSHookFunction bình thường cho 1 hàm KHÁC cũng nằm
//  trong UnityFramework - khả năng cao do prologue của GetMatchClientInfo() có instruction PAC
//  (arm64e) mà Substrate không parse được. SystemInfoSpoof.h (project này) đã tự ghi chú trước
//  là nên dùng Dobby cho đúng trường hợp này, chỉ là chưa từng được thêm thật vào build - giờ
//  đã copy libdobby.a/dobby.h từ FAKEMENU vào Source/Includes/Dobby/ và link qua Makefile.
// ============================================================================
#import "Il2CppResolve.h"
#import "Dobby/dobby.h"

typedef void *(*ORIG_GetMatchClientInfo)();
static ORIG_GetMatchClientInfo orig_GetMatchClientInfo = NULL;

static void *hooked_GetMatchClientInfo() {
    void *info = orig_GetMatchClientInfo();
    if (!info) return info;

    // Offset tính từ đầu object (đã gồm header klass+monitor 0x10 byte, đúng quy ước dump.cs) -
    // xem tcp.MatchClientInfo trong dump.cs. GIỮ NGUYÊN gin_check_data (0x50) và start_time
    // (0x18)/client_version (0x40)/channel_type (0x48)/architecture_type (0x4C) - chỉ sửa đúng
    // các field trực tiếp mang dấu hiệu bị sửa đổi/hook.
    *(void **)((char *)info + 0x10) = NULL;   // tpsdk_str = null
    *(void **)((char *)info + 0x20) = NULL;   // file_exception = null
    *(void **)((char *)info + 0x28) = NULL;   // lib_result = null
    *(uint32_t *)((char *)info + 0x30) = 0;   // exception_count = 0
    *(uint32_t *)((char *)info + 0x34) = 0;   // scan_count = 0
    *(void **)((char *)info + 0x38) = NULL;   // native_result = null

    DeltaVFS_debugLog("AntiReportSpoof: da xoa tpsdk_str/file_exception/lib_result/exception_count/scan_count/native_result - GIU NGUYEN gin_check_data");
    return info;
}

// Gọi SAU khi IL2CPP domain chắc chắn sẵn sàng (cùng chỗ/cùng lúc với game_sdk->init() trong
// Menu.mm's +load, không phải constructor sớm).
inline void installAntiReportSpoof() {
    void *target = Il2CppResolve::GetMethod("Assembly-CSharp.dll", "COW", "UIModelCustomRoom", "GetMatchClientInfo", 0);
    if (target) {
        DeltaVFS_debugLog("AntiReportSpoof: tra theo ten OK (COW.UIModelCustomRoom.GetMatchClientInfo)");
    } else {
        target = (void *)getRealOffset(0x3D82760);
        DeltaVFS_debugLog("AntiReportSpoof: Il2CppResolve that bai, dung RVA cu 0x3D82760");
    }

    int ret = DobbyHook(target, (dobby_dummy_func_t)hooked_GetMatchClientInfo, (dobby_dummy_func_t *)&orig_GetMatchClientInfo);
    if (ret != 0 || !orig_GetMatchClientInfo) {
        DeltaVFS_debugLogf("AntiReportSpoof: DobbyHook that bai (ret=%d) - huy, khong sua gi ca", ret);
    } else {
        DeltaVFS_debugLog("AntiReportSpoof: DobbyHook cai thanh cong");
    }
}
