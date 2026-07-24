// DlsymSpoof.h - hook chính dlsym() qua fishhook, dựa trên phát hiện THẬT từ Monite.dylib
// (MoniteAnalysis, FUN_000add00 @ 0xADD00 trong Monite.dylib): Monite có 1 trampoline tay viết
// (lưu toàn bộ thanh ghi q0-q7/x0-x7/x16 rồi gọi 1 hàm "bảng tráo đổi") can thiệp NGAY SAU khi 1
// con trỏ hàm được resolve, tráo các hàm sau bằng bản riêng của họ nếu trùng địa chỉ GOT thật:
// fopen, stat, access, lstat, statfs, readdir, opendir, closedir, fstat, fcntl, dlopen, VÀ ĐẶC
// BIỆT task_info, __dyld_get_image_name (thật: "_dyld_get_image_name"), __dyld_get_image_header
// ("_dyld_get_image_header"), CC_MD5. Danh sách này đọc y hệt "các hàm app hay dlsym() để tự
// kiểm tra hook / để né import tĩnh" - suy luận hợp lý nhất: Monite hook chính dlsym(), không phải
// hook trực tiếp các hàm libc đó (khớp đúng comment cũ của họ - né kiểu hook libc dễ bị PMS_HOOK
// phát hiện, ưu tiên swizzle/kỹ thuật gián tiếp hơn).
//
// LÝ DO ĐÁNG THỬ Ở PROJECT NÀY: fishhook (dùng cho open/stat/access/... trong AssetRedirect.h,
// _dyld_get_image_name/header trong DylibHide.h) CHỈ rebind GOT/lazy-pointer của TỪNG IMAGE, không
// đụng gì tới cách dlsym() tự tra cứu (dlsym đọc thẳng export trie của thư viện đích, HOÀN TOÀN
// KHÔNG bị ảnh hưởng bởi việc GOT của 1 image khác đã bị rebind). Nghĩa là: nếu Free Fire tự
// dlsym("_dyld_get_image_name") thay vì gọi thẳng qua import tĩnh, DylibHide.h HIỆN TẠI bị bỏ qua
// hoàn toàn - đây chính là lỗ hổng mà kỹ thuật của Monite vá lại, và ta nên vá tương tự.
//
// PHẠM VI: CHỈ can thiệp 2 hàm dyld introspection (_dyld_get_image_name/_dyld_get_image_header) -
// tái dùng ĐÚNG hooked_dyld_get_image_name/header đã có trong DylibHide.h (không viết lại logic
// giấu dylib 2 lần). CHƯA đụng tới các hàm file I/O (fopen/stat/access/...) vì project này đã có
// hệ thống VFS riêng (AssetRedirect.h) xử lý chúng qua đường khác - thêm 1 lớp dlsym-redirect nữa
// cho cùng hàm có thể chồng chéo, chưa có bằng chứng cần thiết. task_info/CC_MD5: CHƯA rõ Free
// Fire có thật sự dlsym 2 hàm này hay không và cần giả mạo gì cụ thể - tạm thời CHỈ GHI LOG khi có
// ai dlsym 2 tên này (quan sát trước, không đoán mù) thay vì tự chế dữ liệu giả không có căn cứ.
// LƯU Ý TƯƠNG THÍCH VỚI DylibSpy.h: file đó CŨNG hook "dlsym" nhưng qua rebind_symbols_image()
// (chỉ vá GOT của RIÊNG Monite.dylib, không đụng ảnh khác) và CHỈ khi user chủ động bấm "Bắt đầu
// giám sát" - luôn cài SAU constructor này (nếu có). rebind_symbols_image() của DylibSpy sẽ tự
// chụp lại "giá trị cũ" tại đúng thời điểm nó chạy - lúc đó giá trị cũ CHÍNH LÀ hooked_dlsym() ở
// đây (vì constructor này chạy trước mọi tương tác người dùng) - nên chuỗi gọi vẫn đúng thứ tự
// (DylibSpy log trước, rồi gọi qua hooked_dlsym này, rồi mới tới dlsym thật) - không xung đột.
#pragma once
#import <Foundation/Foundation.h>
#import <dlfcn.h>
#import <string.h>
#include "fishhook.h"
#include "DylibHide.h"  // dùng lại hooked_dyld_get_image_name/header (static, cùng translation unit)

extern void DeltaVFS_debugLog(const char *msg);
extern void DeltaVFS_debugLogf(const char *fmt, ...);

typedef void *(*ORIG_dlsym)(void *, const char *);
static ORIG_dlsym orig_dlsym_real = NULL;

static void *hooked_dlsym(void *handle, const char *symbol) {
    void *real = orig_dlsym_real(handle, symbol);
    if (!symbol) return real;

    // Chi thay the dung khi dlsym THAT SU tra ve dung dia chi ham that (real == dia chi goc cua
    // no trong libdyld) - so sanh voi orig_dyld_get_image_name/header (da resolve san trong
    // DylibHide_install()) de chac chan khong thay the nham 1 symbol trung ten tinh co khac.
    if (strcmp(symbol, "_dyld_get_image_name") == 0 && real == (void *)orig_dyld_get_image_name) {
        DeltaVFS_debugLog("DlsymSpoof: co ai do dlsym('_dyld_get_image_name') - tra ve ban da giau dylib thay vi ham that");
        return (void *)hooked_dyld_get_image_name;
    }
    if (strcmp(symbol, "_dyld_get_image_header") == 0 && real == (void *)orig_dyld_get_image_header) {
        DeltaVFS_debugLog("DlsymSpoof: co ai do dlsym('_dyld_get_image_header') - tra ve ban da giau dylib thay vi ham that");
        return (void *)hooked_dyld_get_image_header;
    }

    // Quan sat thuan tuy (khong sua gi) - xem Free Fire co thuc su dlsym 2 ham nay hay khong, va
    // luc nao, truoc khi quyet dinh can gia mao gi cu the.
    if (strcmp(symbol, "task_info") == 0 || strcmp(symbol, "CC_MD5") == 0 ||
        strcmp(symbol, "ptrace") == 0 || strcmp(symbol, "csops") == 0 || strcmp(symbol, "sysctl") == 0) {
        DeltaVFS_debugLogf("DlsymSpoof: quan sat - co ai do dlsym('%s')", symbol);
    }

    return real;
}

inline void installDlsymSpoof() {
    struct rebinding rebindings[1];
    rebindings[0].name = "dlsym";
    rebindings[0].replacement = (void *)hooked_dlsym;
    rebindings[0].replaced = (void **)&orig_dlsym_real;
    int ret = rebind_symbols(rebindings, 1);
    DeltaVFS_debugLogf("DlsymSpoof: rebind dlsym ret=%d", ret);
}
