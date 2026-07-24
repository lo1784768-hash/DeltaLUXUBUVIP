// GameMsgFlagPatch.h - vá 1 lệnh trong COW.GamePlay.FFMOIGKIEPE.OIPCOIFFHBC() để ffantihack.
// MFHPGMELLCC.IBJJDBEJMLD KHÔNG BAO GIỜ được set = true bởi gói tin server gửi xuống, bất kể
// server gửi gì.
//
// Bối cảnh: FFAntiObserve.h (đọc thuần, xem file đó) xác nhận IBJJDBEJMLD là cờ DUY NHẤT gắn
// trực tiếp với thời điểm vào trận thật (bật đúng ~2s sau khi Curent_Match != null - không phụ
// thuộc PHEEFAHAHFE/KIBPPIFNAKC, 2 cờ này luôn bật SỚM HƠN nhiều, ngay trong sảnh). Tìm nơi ghi
// field này (2 lệnh strb trong ffantihack.MFHPGMELLCC.JJOKHHBINIJ(bool), RVA 0x20C3924/0x20C3940 -
// xem FFAntiFlagsPatch.h) rồi quét TOÀN BỘ __TEXT tìm ai "bl" trực tiếp tới JJOKHHBINIJ (RVA
// 0x20C38A8, kỹ thuật scan BL y hệt CheckHacker/AntiHackerMemCheckDesc trước đây) - CHỈ 1 nơi gọi
// duy nhất: COW.GamePlay.FFMOIGKIEPE.OIPCOIFFHBC(message.NDEIFCEBDFF) tại RVA 0x5E6D85C.
//
// FFMOIGKIEPE (namespace COW.GamePlay, field "MatchGameClient") là 1 class dispatcher gói tin
// TRONG TRẬN với ~50 hàm xử lý khác nhau, mỗi hàm ứng 1 loại message - nghĩa là IBJJDBEJMLD KHÔNG
// PHẢI logic nội bộ client, mà được ĐIỀU KHIỂN TRỰC TIẾP BỞI 1 GÓI TIN UDP TỪ SERVER
// (message.NDEIFCEBDFF : UDPClientMessageBase, có 3 field bool @0x11/0x12/0x13 + 1 ushort @0x14).
// Disassemble OIPCOIFFHBC (lief+capstone, IPA "com.dts.freefireth_1.126.1_und3fined.ipa" - CÙNG
// build 1.126.1/2019120772 đang chạy trên máy, xác nhận qua crash log) cho thấy chuỗi lệnh:
//   0x5e6d9e8  ldrb w20, [x21, #0x11]   ; w20 = msg.JPPMHKMPEIG (field @0x11 - đúng byte offset
//                                       ; field bool đầu tiên trong NDEIFCEBDFF)
//   ...
//   0x5e6da04  mov x0, x20              ; x0 = giá trị đọc từ gói tin
//   0x5e6da08  mov x1, #0
//   0x5e6da0c  bl  0x20c38a8            ; JJOKHHBINIJ(x0) -> ghi thẳng IBJJDBEJMLD
//
// Ý NGHĨA: server đã tự quyết định "phát hiện" TỪ TRƯỚC (dựa trên dữ liệu gửi lên lúc vào hàng
// chờ/vào trận), gói tin NDEIFCEBDFF chỉ là THÔNG BÁO LẠI cho client ~2s sau khi vào trận - không
// phải client tự tính. Vá tại đây (chặn client "nhận/tin" giá trị server gửi) KHÔNG chắc ngăn được
// server tự kick qua đường khác (nếu server enforce độc lập, không phụ thuộc IBJJDBEJMLD phía
// client) - nhưng đây là bước thử rẻ nhất, an toàn nhất (patch tĩnh, không đụng CheckHacker/
// MFHPGMELLCC - 2 class ĐÃ XÁC NHẬN sửa gì cũng crash) để biết CHẮC gói tin này có phải nguyên
// nhân hay chỉ là hệ quả hiển thị.
//
// Kỹ thuật: đổi "mov x0, x20" (4 byte, x0 = giá trị thật từ gói tin) thành "mov x0, #0" (4 byte) -
// CÙNG KÍCH THƯỚC, không đụng control flow/lệnh gọi/thanh ghi khác ngoài x0 - JJOKHHBINIJ() vẫn
// được gọi y hệt (không né được việc server biết client đã "nhận" gói, chỉ né được việc client tự
// set cờ nội bộ theo giá trị đó).
//
// CHƯA kiểm chứng trên thiết bị thật.
#pragma once
#import <Foundation/Foundation.h>
#include "MemoryUtils.h"
#include "AssetRedirect.h"
#import "CheckHackerPatch.h"  // dùng lại CheckHackerPatch_writeBytes()

inline void installGameMsgFlagPatch() {
    static const uint64_t kRva = 0x5E6DA04ULL;
    static const uint8_t kOriginal[4] = {0xE0, 0x03, 0x14, 0xAA};  // mov x0, x20
    static const uint8_t kPatched[4]  = {0x00, 0x00, 0x80, 0xD2};  // mov x0, #0

    uintptr_t target = (uintptr_t)getRealOffset(kRva);
    if (!target) {
        DeltaVFS_debugLog("GameMsgFlagPatch: khong tim thay UnityFramework, bo qua");
        return;
    }
    if (memcmp((void *)target, kOriginal, 4) != 0) {
        DeltaVFS_debugLogf("GameMsgFlagPatch: byte goc tai 0x%lx KHONG khop du lieu ob54 da phan tich "
                            "(game da update / offset lech) - HUY patch nay de an toan", (unsigned long)target);
        return;
    }
    if (CheckHackerPatch_writeBytes(target, kPatched, 4)) {
        DeltaVFS_debugLogf("GameMsgFlagPatch: da ep OIPCOIFFHBC bo qua gia tri server gui tai 0x%lx (RVA 0x%llx)",
                            (unsigned long)target, (unsigned long long)kRva);
    } else {
        DeltaVFS_debugLogf("GameMsgFlagPatch: ghi patch that bai tai 0x%lx", (unsigned long)target);
    }
}
