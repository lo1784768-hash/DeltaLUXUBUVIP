#pragma once
// CsOpsSpoof.h - fishhook csops() để ẩn cờ CS_GET_TASK_ALLOW/CS_DEBUGGED khỏi bất kỳ code nào
// trong process tự hỏi "app này có được ký cho phép debug / đang bị debug không". Đây là cách
// CHUẨN mà SDK anti-cheat/anti-tamper trên iOS dùng để phân biệt "bản build gốc App Store" với
// "bản bị ký lại để dev/debug" - IPA ký lại qua dịch vụ như Esign (dùng cert cá nhân/free Apple
// ID) gần như LUÔN có get-task-allow=true trong entitlements, khác hẳn IPA App Store thật (luôn
// false). HOÀN TOÀN không liên quan jailbreak - đúng với xác nhận của user (máy không jailbreak
// nhưng vẫn bị đá, cài qua Esign).
//
// Vì sao đáng thử: PHEEFAHAHFE/KIBPPIFNAKC (ffantihack.MFHPGMELLCC, xem FFAntiObserve.h) bật cờ
// NGAY TRONG SẢNH, không phụ thuộc thời điểm vào trận (log thật: bật ở t+78s kể từ lúc mở app,
// TRƯỚC khi vào trận rất lâu) - khớp đúng kiểu 1 giá trị TĨNH đọc 1 LẦN lúc khởi động rồi cache lại,
// khác hẳn IBJJDBEJMLD (bật đúng ~2s sau khi vào trận - có vẻ là bước riêng gắn với chính sự kiện
// vào trận, không phải ứng viên cho csops). csops(CS_OPS_STATUS) là ứng viên tự nhiên nhất cho loại
// kiểm tra "1 lần, không đổi theo thời gian" này.
//
// Kỹ thuật: giống hệt fishhook đã dùng ổn định cho open/stat/access/... trong AssetRedirect.h -
// KHÔNG đụng 1 byte code IL2CPP/game nào, chỉ chặn đúng 1 hàm libSystem. Rủi ro thấp hơn hẳn mọi
// patch tĩnh trên GameConfig.get_CheckHacker()/MFHPGMELLCC (cả 2 đã crash 100% các lần thử, xem
// CheckHackerPatch.h/FFAntiFlagsPatch.h).
//
// GIỚI HẠN THẬT (không phải quên): chỉ chặn được nếu caller gọi qua symbol "_csops" export chuẩn
// của libSystem (PLT/lazy-binding, đúng cơ chế fishhook rebind GOT) - nếu 1 SDK anti-cheat tự gọi
// thẳng số hiệu syscall (svc trực tiếp, không qua libSystem) thì hook này KHÔNG thấy được. Chưa xác
// nhận Free Fire dùng đường nào - đây là thử nghiệm có cơ sở kỹ thuật rõ ràng, không phải chắc chắn
// giải quyết được vấn đề.
#import <Foundation/Foundation.h>
#import <sys/types.h>
#import <unistd.h>
#include <stdint.h>
#include "fishhook.h"

// Forward-declare (KHÔNG #include "AssetRedirect.h") - AssetRedirect.h cũng include ngược lại file
// này để gọi installCsOpsSpoof() trong constructor của nó; include 2 chiều sẽ khiến 1 trong 2 file
// bị #pragma once chặn nửa chừng, có thể dùng hàm/kiểu CHƯA kịp định nghĩa. 2 hàm log này định
// nghĩa "inline" thật trong AssetRedirect.h - khai báo lại chữ ký ở đây không vi phạm ODR (cùng 1
// định nghĩa duy nhất khi cả 2 file được #import vào cùng 1 translation unit như Menu.mm).
extern void DeltaVFS_debugLog(const char *msg);
extern void DeltaVFS_debugLogf(const char *fmt, ...);

// csops() không nằm trong header public SDK (private/lightly-documented libSystem syscall
// wrapper) - khai báo thủ công đúng chữ ký thật (xem XNU bsd/sys/codesign.h), đây là kỹ thuật quen
// thuộc trong giới nghiên cứu bảo mật iOS (bypass/quan sát anti-debug), không phải suy đoán chữ ký.
extern "C" int csops(pid_t pid, unsigned int ops, void *useraddr, size_t usersize);

#define AR_CS_OPS_STATUS        0u
#define AR_CS_GET_TASK_ALLOW    0x00000004u
#define AR_CS_DEBUGGED          0x10000000u

typedef int (*ORIG_csops)(pid_t, unsigned int, void *, size_t);
static ORIG_csops orig_csops = NULL;

static int hooked_csops(pid_t pid, unsigned int ops, void *useraddr, size_t usersize) {
    int ret = orig_csops(pid, ops, useraddr, usersize);
    // Chỉ sửa khi hỏi đúng cờ trạng thái (CS_OPS_STATUS) của CHÍNH process này (pid==0 nghĩa là
    // "bản thân" theo đúng quy ước csops, hoặc pid trùng getpid() nếu caller tự truyền rõ) - không
    // đụng tới kết quả khi ai đó hỏi trạng thái của process KHÁC.
    if (ret == 0 && ops == AR_CS_OPS_STATUS && useraddr && usersize >= sizeof(uint32_t) &&
        (pid == 0 || pid == getpid())) {
        uint32_t *flags = (uint32_t *)useraddr;
        uint32_t before = *flags;
        uint32_t after = before & ~(AR_CS_GET_TASK_ALLOW | AR_CS_DEBUGGED);
        if (after != before) {
            *flags = after;
            DeltaVFS_debugLogf("CsOpsSpoof: csops(CS_OPS_STATUS) pid=%d flags 0x%08x -> 0x%08x (an get-task-allow/debugged)",
                                (int)pid, before, after);
        }
    }
    return ret;
}

inline void installCsOpsSpoof() {
    struct rebinding rebindings[1];
    rebindings[0].name = "csops";
    rebindings[0].replacement = (void *)hooked_csops;
    rebindings[0].replaced = (void **)&orig_csops;
    int ret = rebind_symbols(rebindings, 1);
    DeltaVFS_debugLogf("CsOpsSpoof: rebind csops ret=%d", ret);
}
