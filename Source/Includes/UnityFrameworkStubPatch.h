// UnityFrameworkStubPatch.h - ghi thẳng con trỏ hàm vào các ô __TEXT,__stubs data-slot BÊN
// TRONG chính UnityFramework, thay vì chỉ trông cậy fishhook (rebind_symbols()) quét ảnh này
// từ dylib ngoài vào.
//
// LÝ DO CẦN FILE NÀY: rebind_symbols() global quét TẤT CẢ image đã nạp, nhưng thứ tự chạy
// constructor giữa các __attribute__((constructor)) của các dylib/image KHÁC NHAU không có gì
// đảm bảo (đã từng gây bug thật ở AssetRedirect.h - xem comment ar_ensureFirstRunChecked về
// orig_stat còn NULL do thứ tự constructor). Nếu UnityFramework tự gọi open/stat/... TRƯỚC KHI
// fishhook của Delta.dylib kịp vá GOT của nó, những lần gọi đó lọt qua bản gốc, không được
// redirect vào Documents/<hash>/ - đúng triệu chứng "UnityFramework không đọc thấy file mod".
//
// Đối chiếu ngược: disassemble trực tiếp UnityFramework bản gốc (KHÔNG phải bản Monite) cho
// thấy chính Monite cũng không dùng fishhook kiểu này để redirect I/O của UnityFramework - họ
// ghi thẳng vào đúng các ô __stubs này (đã xác nhận qua bảng bind: open/fopen/stat/fstat/
// lstat/access/opendir/readdir/closedir/fcntl/statfs/CC_MD5/_dyld_get_image_header/
// _dyld_get_image_name/task_info đều là stub trỏ tới các ô cố định trong __DATA). File này áp
// dụng ĐÚNG kỹ thuật đó cho phần I/O, dùng lại NGUYÊN các hàm hooked_* đã có sẵn - không viết
// lại logic redirect nào cả, chỉ đổi ĐƯỜNG mà UnityFramework tự gọi tới.
//
// CƠ CHẾ: mỗi symbol imported mà UnityFramework gọi qua __TEXT,__stubs thực máy là 3 lệnh
// "adrp x16,#page; ldr x16,[x16,#imm]; br x16" - con trỏ hàm thật nằm ở 1 Ô CỐ ĐỊNH trong
// __DATA (dyld ghi vào đó 1 lần lúc load qua lazy/non-lazy bind). Ghi ĐÈ đúng ô đó bằng địa
// chỉ hàm hook của Delta - MỌI lệnh gọi từ code UnityFramework từ đó về sau tự động đi qua
// hàm hook, không phụ thuộc fishhook có quét kịp ảnh này hay không nữa.
//
// OFFSET đo trực tiếp (lief + bảng bind) trên UnityFramework của
// com.dts.freefireth_1.126.1 (bản ob54 hiện tại) - guard so khớp qua dlsym() trước khi ghi,
// TỰ HUỶ patch nếu game update làm lệch offset (đúng pattern an toàn đã dùng ở
// CheckHackerPatch.h/MatchClientInfoPatch.h). CHƯA kiểm chứng trên thiết bị thật.
//
// CHƯA LÀM: fstat/fcntl/statfs/CC_MD5 - 4 stub này cũng đã xác định được vị trí (xem phân
// tích) nhưng Delta chưa có hàm hooked_* tương ứng (fstat/fcntl/statfs chỉ cần thiết nếu cần
// giấu bằng chứng chỉnh sửa trước công cụ quét toàn vẹn file, không phải để VFS asset-redirect
// hoạt động; CC_MD5 tương tự - việc riêng cho bài toán né anti-cheat, không phải bài toán
// "UnityFramework đọc file mod" đang giải ở đây) - thêm sau, theo ĐÚNG mẫu bên dưới, khi có
// hàm hook tương ứng.
#pragma once
#import <Foundation/Foundation.h>
#import <dlfcn.h>
#include <string.h>
#include "MemoryUtils.h"       // getRealOffset()
#include "CheckHackerPatch.h"  // CheckHackerPatch_writeBytes() - dùng lại nguyên, đã có vm_remap
                                // an toàn cho ghi vào vùng nhớ UnityFramework
#include "AssetRedirect.h"     // hooked_open/hooked_fopen/hooked_access/hooked_stat/
                                // hooked_lstat/hooked_opendir - dùng lại NGUYÊN, không viết mới
#include "DylibHide.h"         // hooked_dyld_get_image_header/hooked_dyld_get_image_name
#include "TaskDyldInfoSpoof.h" // hooked_task_info

extern void DeltaVFS_debugLog(const char *msg);
extern void DeltaVFS_debugLogf(const char *fmt, ...);

struct UfStubPatchSite {
    uint64_t dataSlotRva;   // offset (từ base UnityFramework) tới ô con trỏ __DATA mà stub đọc
    const char *dlsymName;  // tên symbol thật - dùng dlsym() làm giá trị gốc mong đợi để guard
    void *hookFn;           // hàm hook của Delta sẽ thay vào chỗ này
    const char *label;
};

inline void installUnityFrameworkStubPatch() {
    UfStubPatchSite sites[] = {
        {0xb655540ULL, "open",    (void *)hooked_open,    "open"},
        {0xb654f40ULL, "fopen",   (void *)hooked_fopen,   "fopen"},
        {0xb654ab8ULL, "access",  (void *)hooked_access,  "access"},
        {0xb655a58ULL, "stat",    (void *)hooked_stat,    "stat"},
        {0xb6551f8ULL, "lstat",   (void *)hooked_lstat,   "lstat"},
        {0xb655550ULL, "opendir", (void *)hooked_opendir, "opendir"},
        {0xb654c68ULL, "closedir", (void *)hooked_closedir, "closedir"},
        {0xb6557a0ULL, "readdir", (void *)hooked_readdir, "readdir"},
        {0xb654a20ULL, "_dyld_get_image_header", (void *)hooked_dyld_get_image_header, "_dyld_get_image_header"},
        {0xb654a28ULL, "_dyld_get_image_name",   (void *)hooked_dyld_get_image_name,   "_dyld_get_image_name"},
        {0xb655ef0ULL, "task_info", (void *)hooked_task_info, "task_info"},
    };

    int ok = 0, fail = 0;
    for (const auto &site : sites) {
        uintptr_t slotAddr = (uintptr_t)getRealOffset(site.dataSlotRva);
        if (!slotAddr) {
            DeltaVFS_debugLogf("UnityFrameworkStubPatch: khong tim thay UnityFramework, bo qua %s", site.label);
            fail++;
            continue;
        }

        void *expected = dlsym(RTLD_DEFAULT, site.dlsymName);
        uint64_t current = 0;
        memcpy(&current, (void *)slotAddr, sizeof(current));
        if (!expected || current != (uint64_t)(uintptr_t)expected) {
            DeltaVFS_debugLogf("UnityFrameworkStubPatch: gia tri goc tai slot %s (0x%lx) la %p, dlsym mong doi %p - "
                                "KHONG khop (game update / offset lech?) - HUY patch nay de an toan",
                                site.label, (unsigned long)slotAddr, (void *)(uintptr_t)current, expected);
            fail++;
            continue;
        }

        uint64_t newVal = (uint64_t)(uintptr_t)site.hookFn;
        if (CheckHackerPatch_writeBytes(slotAddr, (const uint8_t *)&newVal, sizeof(newVal))) {
            ok++;
            DeltaVFS_debugLogf("UnityFrameworkStubPatch: da redirect stub %s -> hook cua Delta tai 0x%lx (RVA 0x%llx)",
                                site.label, (unsigned long)slotAddr, (unsigned long long)site.dataSlotRva);
        } else {
            fail++;
            DeltaVFS_debugLogf("UnityFrameworkStubPatch: ghi slot %s that bai", site.label);
        }
    }

    DeltaVFS_debugLogf("UnityFrameworkStubPatch: xong - %d stub da redirect, %d that bai/bo qua", ok, fail);
}
