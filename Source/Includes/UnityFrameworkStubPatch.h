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

// SỬA SAU KHI TEST THẬT: debug.log cho thấy CẢ 11/11 site đều "KHONG khop" dù offset đúng - giá
// trị đọc được tại mỗi slot đều nằm gọn trong 1 dải ~0x15000 byte (không phải random/offset lệch)
// - đúng kích cỡ __stubs+__stub_helper của UnityFramework. Nguyên nhân: open/fopen/stat/... là
// LAZY BIND - ô __DATA ban đầu KHÔNG chứa địa chỉ hàm thật, mà chứa con trỏ tới trình phân giải
// nội bộ của dyld (__stub_helper), CHỈ được ghi đè thành địa chỉ thật SAU LẦN GỌI ĐẦU TIÊN qua
// đúng stub đó. installUnityFrameworkStubPatch() chạy rất sớm (+load, ngay sau constructor),
// TRƯỚC KHI UnityFramework kịp tự gọi bất kỳ hàm nào trong 11 hàm này - nên guard so với dlsym()
// (ép resolve ngay lập tức) luôn thấy "khác nhau" dù offset hoàn toàn đúng.
//
// FIX: gọi thẳng qua ĐÚNG stub 1 lần (tham số vô hại, chỉ cần CHẠY QUA để dyld resolve xong) cho
// từng hàm TRƯỚC KHI build sites[]/kiểm tra guard - sau lần gọi này ô __DATA đã chứa địa chỉ thật,
// dlsym() sẽ khớp bình thường.
//
// SỬA LẦN 2 (sau khi bản đầu CRASH-LOOP THẬT trên máy - CrashLogger bắt được pc lệch ĐÚNG BẰNG
// NHAU so với Base ở cả 4 lần crash, luôn = RVA của stub "access" dùng ở đây): RVA lấy từ danh
// sách "target" trong lúc phân tích __HOOK_TEXT của Monite - nhưng "target" đó thực ra là ĐỊA CHỈ
// CỦA LỆNH THỨ 2 (ldr) trong chuỗi 3 lệnh "adrp x16,#page; ldr x16,[x16,#imm]; br x16", KHÔNG
// PHẢI địa chỉ lệnh đầu (adrp) - xác nhận qua chính disassembly lúc phân tích (phải lùi target-4
// mới thấy "adrp" hợp lệ). Gọi thẳng vào "target" bỏ qua lệnh adrp, x16 còn rác từ trước đó, lệnh
// ldr kế tiếp đọc từ [rác + imm] -> bad access ngay lập tức, đúng khớp crash quan sát được. Toàn
// bộ 11 hằng số RVA bên dưới đã lùi lại 4 byte (target - 4) để trỏ đúng vào lệnh adrp - điểm gọi
// vào thật sự hợp lệ.
inline void uf_warmStubs() {
    struct stat stBuf;
    uintptr_t s;

    s = (uintptr_t)getRealOffset(0xa5ae2dcULL); // access (target-4)
    if (s) ((int (*)(const char *, int))s)("/", F_OK);

    s = (uintptr_t)getRealOffset(0xa5af2a8ULL); // open (target-4)
    if (s) { int fd = ((int (*)(const char *, int, ...))s)("/", O_RDONLY); if (fd >= 0) close(fd); }

    s = (uintptr_t)getRealOffset(0xa5afa4cULL); // stat (target-4)
    if (s) ((int (*)(const char *, struct stat *))s)("/", &stBuf);

    s = (uintptr_t)getRealOffset(0xa5aedbcULL); // lstat (target-4)
    if (s) ((int (*)(const char *, struct stat *))s)("/", &stBuf);

    s = (uintptr_t)getRealOffset(0xa5aea50ULL); // fstat (target-4)
    if (s) ((int (*)(int, struct stat *))s)(-1, &stBuf); // fd=-1 -> EBADF, an toàn, chỉ cần chạy qua

    s = (uintptr_t)getRealOffset(0xa5ae9a8ULL); // fopen (target-4)
    if (s) { FILE *f = ((FILE *(*)(const char *, const char *))s)("/", "r"); if (f) fclose(f); }

    DIR *warmDir = NULL;
    s = (uintptr_t)getRealOffset(0xa5af2c0ULL); // opendir (target-4)
    if (s) warmDir = ((DIR *(*)(const char *))s)("/");

    s = (uintptr_t)getRealOffset(0xa5af638ULL); // readdir (target-4)
    if (s && warmDir) ((struct dirent *(*)(DIR *))s)(warmDir);

    s = (uintptr_t)getRealOffset(0xa5ae564ULL); // closedir (target-4)
    if (s && warmDir) ((int (*)(DIR *))s)(warmDir);

    s = (uintptr_t)getRealOffset(0xa5ae1f8ULL); // _dyld_get_image_header (target-4)
    if (s) ((const struct mach_header *(*)(uint32_t))s)(0);

    s = (uintptr_t)getRealOffset(0xa5ae204ULL); // _dyld_get_image_name (target-4)
    if (s) ((const char *(*)(uint32_t))s)(0);

    s = (uintptr_t)getRealOffset(0xa5b0130ULL); // task_info (target-4)
    if (s) {
        struct task_dyld_info dyldInfo;
        mach_msg_type_number_t cnt = TASK_DYLD_INFO_COUNT;
        ((kern_return_t (*)(task_name_t, task_flavor_t, task_info_t, mach_msg_type_number_t *))s)(
            mach_task_self(), TASK_DYLD_INFO, (task_info_t)&dyldInfo, &cnt);
    }
}

inline void installUnityFrameworkStubPatch() {
    uf_warmStubs();

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
            // CHẨN ĐOÁN THÊM: warm-up (uf_warmStubs) đã chạy được không crash (xem SỬA LẦN 2),
            // nhưng giá trị "current" vẫn không khớp dlsym() - dùng dladdr() để biết CHÍNH XÁC
            // con trỏ đó thuộc file/symbol nào (thay vì tiếp tục đoán mù), phòng trường hợp đây
            // là 1 "stub island" trung gian của dyld chứ không phải giá trị lazy-chưa-resolve
            // như giả định ban đầu - guard so tuyệt đối với dlsym() có thể sai giả thuyết.
            Dl_info curInfo; memset(&curInfo, 0, sizeof(curInfo));
            bool curOk = dladdr((void *)(uintptr_t)current, &curInfo);
            Dl_info expInfo; memset(&expInfo, 0, sizeof(expInfo));
            bool expOk = dladdr(expected, &expInfo);
            DeltaVFS_debugLogf("UnityFrameworkStubPatch: gia tri goc tai slot %s (0x%lx) la %p [%s / %s], "
                                "dlsym mong doi %p [%s / %s] - KHONG khop (game update / offset lech?) - "
                                "HUY patch nay de an toan",
                                site.label, (unsigned long)slotAddr,
                                (void *)(uintptr_t)current,
                                curOk && curInfo.dli_fname ? curInfo.dli_fname : "?",
                                curOk && curInfo.dli_sname ? curInfo.dli_sname : "?",
                                expected,
                                expOk && expInfo.dli_fname ? expInfo.dli_fname : "?",
                                expOk && expInfo.dli_sname ? expInfo.dli_sname : "?");
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
