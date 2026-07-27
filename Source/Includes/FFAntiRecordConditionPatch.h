// FFAntiRecordConditionPatch.h - vô hiệu hoá ffantihack.MFHPGMELLCC.EBCGNOGCKNA(string,
// Dictionary<string,string>) (RVA 0x20C34A4, dump.cs bản ob54) - hàm "ghi nhận điều kiện hacker"
// CHÍNH THỐNG của class MFHPGMELLCC, class đang chứa 6 field PHEEFAHAHFE/IBJJDBEJMLD/
// CAANLIGMMKP/KIBPPIFNAKC/GPANAPICKMA/ANFAMBFAHHB mà FFAntiObserve.h theo dõi riêng (IBJJDBEJMLD
// flip 0->1 đúng ngay trước lúc bị đá giữa trận).
//
// KHÁC HẲN FFAntiFlagsPatch.h (ĐÃ TẮT - ghi thẳng byte vào field của class này, crash 3/3 lần thử,
// nghi do class tự kiểm tra toàn vẹn nội bộ giữa các field): patch này KHÔNG đụng tới field nào cả,
// chỉ chặn HÀM GHI (report/setter) của chính nó hoạt động - field vẫn giữ nguyên trạng thái mặc
// định (không bao giờ được set) thay vì bị ghi đè từ bên ngoài thành giá trị KHÔNG NHẤT QUÁN - ít
// rủi ro hơn về mặt "tự kiểm tra toàn vẹn" vì không tạo ra trạng thái lạ giữa các field, chỉ đơn
// giản là hàm ghi nhận không bao giờ chạy.
//
// Đã disassemble đầy đủ EBCGNOGCKNA (UnityFramework THẬT, trích từ
// com.dts.freefireth_1.126.1_und3fined.ipa bản stock, RVA 0x20C34A4): hàm này KHÔNG phải setter
// đơn giản - có 2 đường: (1) fast path gọi 2 hàm helper kiểm tra trạng thái init rồi TAIL-CALL
// sang 1 hàm "thật sự ghi nhận" ở 0x68C8A8C; (2) fallback path khi fast-path chưa sẵn sàng, tự
// duyệt Dictionary extraData và gọi lặp lại vào CÙNG 1 kênh report (0x7A35258) cho từng entry. Cả
// 2 đường đều là "đường ghi nhận điều kiện hacker" - chặn ngay từ đầu hàm là đủ, không cần hiểu
// chi tiết bên trong từng nhánh.
//
// Kỹ thuật: vá 8 byte đầu (2 lệnh "stp" lưu thanh ghi) thành "mov w0,#0; ret" - CÙNG KÍCH THƯỚC
// (8 byte), hàm trả về false (không ghi nhận gì) ngay lập tức, KHÔNG đụng stack/frame (chưa hề
// push gì) nên an toàn return sớm - giống kỹ thuật GenerateLibMapAndMemPatch.h, nhưng khác ở chỗ
// hàm này KHÔNG spawn thread/chờ tín hiệu riêng nào theo disassemble (chỉ gọi hàm/tail-call đồng
// bộ) - ít rủi ro treo kiểu watchdog hơn đã gặp với GenerateLibMapAndMem.
//
// CHƯA kiểm chứng trên thiết bị thật - class MFHPGMELLCC CÓ LỊCH SỬ crash (FFAntiFlagsPatch.h,
// 3/3 lần) với KỸ THUẬT KHÁC (ghi field trực tiếp). Rủi ro thật sự vẫn còn, cần test cẩn thận,
// đặc biệt để ý crash sớm lúc logo/màn hình login (kiểu triệu chứng đã gặp với
// GenerateLibMapAndMemPatch.h).
#pragma once
#import <Foundation/Foundation.h>
#include "MemoryUtils.h"
#include "AssetRedirect.h"
#import "CheckHackerPatch.h"  // dùng lại CheckHackerPatch_writeBytes()

namespace FFAntiRecordConditionPatch {

static dispatch_source_t g_timer = NULL;
static int g_retryTick = 0;

// Trả true nếu ĐÃ XỬ LÝ XONG (patch thành công, hoặc thất bại vì lý do KHÔNG PHẢI timing) - false
// nếu chỉ là UnityFramework chưa map xong, cần tick sau thử lại (xem EmulatorScorePatch.h - cùng
// lỗi timing đã gặp và sửa ở đó, áp dụng luôn từ đầu ở đây để khỏi lặp lại lỗi tương tự).
inline bool TryPatchOnce() {
    static const uint64_t kRva = 0x20C34A4ULL;
    static const uint8_t kOriginal[8] = {
        0xF8, 0x5F, 0xBC, 0xA9,  // stp x24, x23, [sp, #-0x40]!
        0xF6, 0x57, 0x01, 0xA9,  // stp x22, x21, [sp, #0x10]
    };
    static const uint8_t kPatched[8] = {
        0x00, 0x00, 0x80, 0x52,  // mov w0, #0
        0xC0, 0x03, 0x5F, 0xD6,  // ret
    };

    uintptr_t target = (uintptr_t)getRealOffset(kRva);
    if (!target) return false;  // UnityFramework chua map - thu lai tick sau

    if (memcmp((void *)target, kOriginal, sizeof(kOriginal)) != 0) {
        const uint8_t *actual = (const uint8_t *)target;
        DeltaVFS_debugLogf("FFAntiRecordConditionPatch: byte goc tai 0x%lx KHONG khop du lieu ob54 da phan tich "
                            "(ky vong %02X%02X%02X%02X%02X%02X%02X%02X, thuc te %02X%02X%02X%02X%02X%02X%02X%02X) - HUY patch de an toan",
                            (unsigned long)target,
                            kOriginal[0], kOriginal[1], kOriginal[2], kOriginal[3], kOriginal[4], kOriginal[5], kOriginal[6], kOriginal[7],
                            actual[0], actual[1], actual[2], actual[3], actual[4], actual[5], actual[6], actual[7]);
        return true;
    }

    if (CheckHackerPatch_writeBytes(target, kPatched, sizeof(kPatched))) {
        DeltaVFS_debugLogf("FFAntiRecordConditionPatch: da vo hieu hoa MFHPGMELLCC.EBCGNOGCKNA() tai 0x%lx (RVA 0x%llx, tick #%d)",
                            (unsigned long)target, (unsigned long long)kRva, g_retryTick);
    } else {
        DeltaVFS_debugLogf("FFAntiRecordConditionPatch: ghi patch that bai tai 0x%lx", (unsigned long)target);
    }
    return true;
}

inline void Tick() {
    ++g_retryTick;
    if (TryPatchOnce()) {
        dispatch_source_cancel(g_timer);
    }
}

// installEarly() - bắt đầu poll NGAY TỪ +load (timer 50ms trên main queue, cùng kỹ thuật với
// EmulatorScorePatch::installEarly()/EmulatorCheckSpoof::installEarly()) - patch tĩnh 1 lần là đủ,
// timer tự huỷ ngay sau khi xử lý xong.
inline void installEarly() {
    dispatch_queue_t queue = dispatch_get_main_queue();
    g_timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, queue);
    dispatch_source_set_timer(g_timer, dispatch_time(DISPATCH_TIME_NOW, 0),
                               50 * NSEC_PER_MSEC, 10 * NSEC_PER_MSEC);
    dispatch_source_set_event_handler(g_timer, ^{
        Tick();
    });
    dispatch_resume(g_timer);
    DeltaVFS_debugLog("FFAntiRecordConditionPatch: installEarly() - bat dau polling moi 50ms tu +load");
}

} // namespace FFAntiRecordConditionPatch
