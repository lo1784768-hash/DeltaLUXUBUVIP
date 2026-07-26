// CC_MD5Spoof.h - hook CC_MD5 qua fishhook: khi vùng [data, data+len) được hash TRÙNG (dù chỉ 1
// phần) với 1 vùng Delta đã tự patch (đăng ký qua RegisterPatchedRegion() trong
// CheckHackerPatch_writeBytes - xem CheckHackerPatch.h), hash được tính trên 1 BẢN COPY đã phục
// hồi lại byte GỐC cho đúng phần trùng đó - kết quả CC_MD5 giống HỆT như UnityFramework chưa hề
// bị patch, dù trong RAM đang chạy bản đã sửa. Mọi lời gọi CC_MD5 KHÔNG trùng vùng nào đã patch
// (tuyệt đại đa số - Firebase, network signing, cache checksum...) đi thẳng qua bản thật, không
// đổi hành vi.
//
// BỐI CẢNH: FFAntiFlagsPatch.h (patch trực tiếp 7 điểm ghi field trong ffantihack.MFHPGMELLCC để
// ép cờ phát hiện luôn false) đã thử 3 cách gọi khác nhau (lúc +load, giữa chừng, sau khi
// FFAntiObserve xác nhận class init xong) - CẢ 3 đều crash ngay sau khi patch ghi xong, kết luận
// trong chính file đó: sửa BẤT KỲ byte nào bên trong class này là không an toàn, không liên quan
// field/thời điểm cụ thể - dấu hiệu rõ ràng của 1 cơ chế tự kiểm tra toàn vẹn (checksum) trên
// chính vùng nhớ đó. Đối chiếu với phân tích UnityFramework gốc trước đó: CC_MD5 là 1 trong ~15
// hàm native (cụm __stubs, 0xa5a7000-0xa5d0000) mà toàn bộ hệ thống scan/hash phụ thuộc vào -
// hook Ở ĐÂY thay vì patch trực tiếp field: không đụng 1 byte nào trong MFHPGMELLCC (né hẳn cái
// bẫy tự huỷ), chỉ khiến HÀM MÀ NÓ DÙNG ĐỂ TỰ KIỂM TRA luôn thấy "giống bản gốc" cho đúng 7 vùng
// Delta biết chắc là đã bị patch (MatchClientInfoPatch.h).
//
// CHƯA kiểm chứng trên thiết bị thật - lần đầu hook CC_MD5 toàn tiến trình.
#pragma once
#import <Foundation/Foundation.h>
#import <CommonCrypto/CommonDigest.h>
#include <string.h>
#include "fishhook.h"
#include "CheckHackerPatch.h"  // PatchedRegion / g_patchedRegions / g_patchedRegionCount

extern void DeltaVFS_debugLog(const char *msg);
extern void DeltaVFS_debugLogf(const char *fmt, ...);

typedef unsigned char *(*ORIG_CC_MD5)(const void *, CC_LONG, unsigned char *);
static ORIG_CC_MD5 orig_CC_MD5_real = NULL;

static unsigned char *hooked_CC_MD5(const void *data, CC_LONG len, unsigned char *md) {
    if (!data || len <= 0) return orig_CC_MD5_real(data, len, md);

    uintptr_t dataAddr = (uintptr_t)data;
    uintptr_t dataEnd = dataAddr + (size_t)len;

    // Buffer tạm trên stack - chỉ cấp phát/copy khi THẬT SỰ có overlap, để không tốn phí cho
    // tuyệt đại đa số lời gọi CC_MD5 khác trong app không liên quan gì tới vùng đã patch. Giới
    // hạn kích cỡ hợp lý cho stack - nếu buffer hash lớn hơn mức này mà lại trùng vùng patch (rất
    // hiếm, patch hiện tại chỉ 4-8 byte mỗi vùng nên overlap luôn nằm trong buffer nhỏ), bỏ qua
    // và log lại thay vì risk stack overflow.
    static const size_t kStackCopyLimit = 8192;
    uint8_t stackCopy[kStackCopyLimit];
    bool patched = false;

    int count = g_patchedRegionCount.load(std::memory_order_relaxed);
    if (count > MAX_PATCHED_REGIONS) count = MAX_PATCHED_REGIONS;
    for (int i = 0; i < count; i++) {
        const PatchedRegion &region = g_patchedRegions[i];
        uintptr_t regionEnd = region.addr + region.len;
        if (region.addr >= dataEnd || regionEnd <= dataAddr) continue; // khong trung

        if (!patched) {
            if ((size_t)len > kStackCopyLimit) {
                DeltaVFS_debugLogf("CC_MD5Spoof: hash [%p,+%u) trung vung da patch nhung qua lon "
                                    "de copy an toan (>%zu byte) - bo qua, tra ve hash THAT",
                                    data, (unsigned)len, kStackCopyLimit);
                return orig_CC_MD5_real(data, len, md);
            }
            memcpy(stackCopy, data, (size_t)len);
            patched = true;
        }

        uintptr_t overlapStart = region.addr > dataAddr ? region.addr : dataAddr;
        uintptr_t overlapEnd = regionEnd < dataEnd ? regionEnd : dataEnd;
        size_t copyOff = (size_t)(overlapStart - dataAddr);
        size_t regionOff = (size_t)(overlapStart - region.addr);
        size_t copyLen = (size_t)(overlapEnd - overlapStart);
        memcpy(stackCopy + copyOff, region.original + regionOff, copyLen);
    }

    if (patched) {
        DeltaVFS_debugLogf("CC_MD5Spoof: hash [%p,+%u) trung vung da patch - tra ve hash tren byte GOC (chua patch)",
                            data, (unsigned)len);
        return orig_CC_MD5_real(stackCopy, len, md);
    }
    return orig_CC_MD5_real(data, len, md);
}

inline void installCC_MD5Spoof() {
    struct rebinding rebindings[1];
    rebindings[0].name = "CC_MD5";
    rebindings[0].replacement = (void *)hooked_CC_MD5;
    rebindings[0].replaced = (void **)&orig_CC_MD5_real;
    int ret = rebind_symbols(rebindings, 1);
    DeltaVFS_debugLogf("CC_MD5Spoof: rebind CC_MD5 ret=%d", ret);
}
