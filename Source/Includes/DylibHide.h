#pragma once
// ============================================================================
//  DylibHide.h - giấu dylib của CHÍNH MÌNH khỏi 4 API liệt kê dyld image chuẩn:
//  _dyld_image_count / _dyld_get_image_name / _dyld_get_image_header /
//  _dyld_get_image_vmaddr_slide.
//
//  LÝ DO: đối chiếu dump.cs (OB54) tìm ra message tcp.MatchClientInfo - gửi lúc chuẩn bị/đang
//  trong trận, có field tpsdk_str + lib_result (rất có thể là danh sách tên dylib/thư viện lạ
//  quét được trong process) - đã TEST THẬT trên máy: đổi fishhook -> Cocoa-swizzle (VFS redirect)
//  không đổi được gì, tắt hết Aim/ESP/ModHacks vẫn bị đá ở cùng mốc ~30s vào trận -> loại trừ cả
//  kỹ thuật hook LẪN hành vi gameplay, chỉ còn lại "sự tồn tại của dylib trong danh sách image"
//  là nghi phạm hợp lý nhất còn sót.
//
//  ĐÂY LÀ THỬ NGHIỆM, CHƯA XÁC NHẬN CHẮC CHẮN - suy luận có cơ sở tốt nhất hiện có, không phải
//  bằng chứng trực tiếp (không có Ghidra/dynamic trace để đọc thẳng hàm build tpsdk_str thật).
//
//  RỦI RO ĐÃ BIẾT, CHẤP NHẬN:
//  1. Nếu có code nào khác trong process (kể cả UnityFramework/hệ thống) phụ thuộc đúng vào số
//     lượng/thứ tự image thật để làm việc gì đó khác thường - hành vi đó có thể sai lệch. Hiếm,
//     nhưng không loại trừ hoàn toàn.
//  2. Bản thân việc này CŨNG LÀ 1 dạng hook - nếu tồn tại 1 cơ chế KHÁC quét chữ ký hook, việc
//     hook 4 hàm dyld_* này có thể lại là dấu hiệu MỚI bị phát hiện - đây là đánh đổi thật, không
//     phải giải pháp miễn phí. Không có cách nào loại trừ hoàn toàn rủi ro bị phát hiện khi đã có
//     1 dylib ngoài load trong process - chỉ đang thu hẹp lại 1 hướng cụ thể.
//
//  DÙNG FISHHOOK (rebind_symbols), KHÔNG DÙNG MSHookFunction: 4 hàm _dyld_image_count/
//  _dyld_get_image_name/_dyld_get_image_header/_dyld_get_image_vmaddr_slide nằm trong
//  libdyld.dylib - 1 phần của dyld SHARED CACHE. Project này đã tự xác nhận trước đó
//  (memory: "MSHookFunction shared-cache limit") rằng MSHookFunction/Substrate KHÔNG hook được
//  hàm nằm trong shared cache (chính lý do fishhook được chọn thay thế cho toàn bộ VFS redirect,
//  xem AssetRedirect.h) - dùng lại đúng fishhook ở đây cho nhất quán và chắc chắn hoạt động.
// ============================================================================
#import <mach-o/dyld.h>
#import <mach-o/loader.h>
#import <dlfcn.h>
#import <string.h>
#include <atomic>
#import "fishhook.h"

// -1 = chưa xác định được index của chính mình (chưa tìm thấy, hoặc dladdr thất bại) - trong
// trạng thái này 4 hàm hook bên dưới hoàn toàn PASSTHROUGH, không giấu gì cả (an toàn, không
// tệ hơn không hook).
static std::atomic<int32_t> g_dylibHideIndex{-1};

typedef uint32_t (*ORIG_dyld_image_count)(void);
static ORIG_dyld_image_count orig_dyld_image_count = NULL;
static uint32_t hooked_dyld_image_count(void) {
    uint32_t real = orig_dyld_image_count();
    int32_t hide = g_dylibHideIndex.load(std::memory_order_relaxed);
    return (hide >= 0 && (uint32_t)hide < real) ? real - 1 : real;
}

typedef const char *(*ORIG_dyld_get_image_name)(uint32_t);
static ORIG_dyld_get_image_name orig_dyld_get_image_name = NULL;
static const char *hooked_dyld_get_image_name(uint32_t index) {
    int32_t hide = g_dylibHideIndex.load(std::memory_order_relaxed);
    if (hide >= 0 && index >= (uint32_t)hide) index += 1; // nhảy qua đúng slot của chính mình
    return orig_dyld_get_image_name(index);
}

typedef const struct mach_header *(*ORIG_dyld_get_image_header)(uint32_t);
static ORIG_dyld_get_image_header orig_dyld_get_image_header = NULL;
static const struct mach_header *hooked_dyld_get_image_header(uint32_t index) {
    int32_t hide = g_dylibHideIndex.load(std::memory_order_relaxed);
    if (hide >= 0 && index >= (uint32_t)hide) index += 1;
    return orig_dyld_get_image_header(index);
}

typedef intptr_t (*ORIG_dyld_get_image_vmaddr_slide)(uint32_t);
static ORIG_dyld_get_image_vmaddr_slide orig_dyld_get_image_vmaddr_slide = NULL;
static intptr_t hooked_dyld_get_image_vmaddr_slide(uint32_t index) {
    int32_t hide = g_dylibHideIndex.load(std::memory_order_relaxed);
    if (hide >= 0 && index >= (uint32_t)hide) index += 1;
    return orig_dyld_get_image_vmaddr_slide(index);
}

// Tìm index THẬT của chính dylib này bằng dladdr trên 1 hàm định nghĩa ngay trong file này -
// dli_fname trả về đúng path file .dylib chứa hàm đó, dùng để so khớp với từng entry thật của
// _dyld_get_image_name(). Luôn gọi orig_* (KHÔNG gọi qua bản đã hook) để chắc chắn thấy danh
// sách THẬT, không bị chính mình làm lệch trong lúc đang tìm.
static void DylibHide_findSelfIndex() {
    Dl_info info;
    memset(&info, 0, sizeof(info));
    if (!dladdr((void *)&DylibHide_findSelfIndex, &info) || !info.dli_fname) {
        DeltaVFS_debugLog("DylibHide: dladdr that bai, khong xac dinh duoc dylib cua chinh minh - bo qua, khong giau gi ca");
        return;
    }
    uint32_t count = orig_dyld_image_count();
    for (uint32_t i = 0; i < count; i++) {
        const char *name = orig_dyld_get_image_name(i);
        if (name && strcmp(name, info.dli_fname) == 0) {
            g_dylibHideIndex.store((int32_t)i, std::memory_order_relaxed);
            DeltaVFS_debugLogf("DylibHide: tim thay chinh minh o index %u (%s) - se giau khoi 4 ham dyld_*", i, name);
            return;
        }
    }
    DeltaVFS_debugLogf("DylibHide: khong tim thay '%s' trong %u image - bo qua, khong giau gi ca", info.dli_fname, count);
}

// Gọi CÀNG SỚM CÀNG TỐT (constructor, trước khi game/Unity có cơ hội tự quét danh sách image
// lần nào) - an toàn gọi vô điều kiện, không đụng gì tới VFS/redirect/crash-logger. Rebind qua
// fishhook (không phải MSHookFunction) vì 4 hàm này nằm trong shared cache - xem comment đầu file.
inline void DylibHide_install() {
    struct rebinding rebindings[4];
    int n = 0;
    rebindings[n].name = "_dyld_image_count";            rebindings[n].replacement = (void *)hooked_dyld_image_count;            rebindings[n].replaced = (void **)&orig_dyld_image_count;            n++;
    rebindings[n].name = "_dyld_get_image_name";         rebindings[n].replacement = (void *)hooked_dyld_get_image_name;         rebindings[n].replaced = (void **)&orig_dyld_get_image_name;         n++;
    rebindings[n].name = "_dyld_get_image_header";       rebindings[n].replacement = (void *)hooked_dyld_get_image_header;       rebindings[n].replaced = (void **)&orig_dyld_get_image_header;       n++;
    rebindings[n].name = "_dyld_get_image_vmaddr_slide"; rebindings[n].replacement = (void *)hooked_dyld_get_image_vmaddr_slide; rebindings[n].replaced = (void **)&orig_dyld_get_image_vmaddr_slide; n++;

    int ret = rebind_symbols(rebindings, n);
    if (ret != 0 || !orig_dyld_image_count || !orig_dyld_get_image_name) {
        DeltaVFS_debugLogf("DylibHide: rebind_symbols that bai (ret=%d) cho 1 hoac nhieu ham dyld_* - huy, khong giau gi ca", ret);
        return;
    }
    DylibHide_findSelfIndex();
}
