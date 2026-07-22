#pragma once
// ============================================================================
//  PacketCapture.h - hook thẳng COW.ServiceConnectionHandler.Send()/OnHandlePacket() (dump.cs
//  OB54) để log ra plaintext CHÍNH XÁC module (EProtocol.Proto) + cmd con + độ dài data của mọi
//  gói tin lobby gửi đi/nhận về - CÔNG CỤ CHẨN ĐOÁN TẠM THỜI, không phải tính năng lâu dài.
//
//  LÝ DO: sau khi loại trừ hết fishhook/Cocoa-swizzle, giấu dylib khỏi dyld, tắt VFS, và cả việc
//  spoof MatchClientInfo (gây crash mới, đã gỡ - xem AntiReportSpoof.h) mà vẫn không biết CHẮC
//  CHẮN gói tin nào khiến bị đá giữa trận - thay vì tiếp tục đoán field, bắt thẳng gói tin thật
//  để đối chiếu trực tiếp với bảng EProtocol.Proto đã biết (FFANTI=29, GIN=40, CREDITSCORE=34,
//  MATCHMAKING=3, ROOM=14...).
//
//  2 điểm hook (KHÔNG ĐI QUA TCPMsgPacket.Serialize/Unserialize như bàn trước - Send()/
//  OnHandlePacket() cho dữ liệu SẠCH HƠN, cmdType là enum thật thay vì phải tự giải mã byte thô):
//  - COW.ServiceConnectionHandler.Send(EProtocol.Proto cmdType, ProtoReq message, byte regionID)
//    - GỬI ĐI. RVA 0x6D93BF8, instance, 3 tham số.
//  - COW.ServiceConnectionHandler.OnHandlePacket(TCPMsgPacket tcpPacket, object tcpMsg)
//    - NHẬN VỀ (virtual, Slot: 9). RVA 0x6D94A48, instance, 2 tham số.
//
//  CHỈ QUAN SÁT, KHÔNG SỬA GÌ CẢ - luôn gọi hàm gốc với đúng tham số nguyên bản, chỉ đọc để log.
//  RỦI RO ĐÃ BIẾT (bài học từ AntiReportSpoof): ngay cả hook HOÀN TOÀN PASSTHROUGH cũng có thể
//  gây bất ổn nếu đúng hàm đó "nhạy cảm" với việc bị patch inline (đã xác nhận thật với
//  GetMatchClientInfo, không liên quan gì tới nội dung hook body) - CHƯA CÓ CÁCH nào loại trừ
//  hoàn toàn rủi ro này trước khi thử trên máy thật. Đây là công cụ dùng 1 lần để lấy bằng
//  chứng - nên GỠ RA sau khi đã bắt được đủ log cần thiết, không nên để lại vĩnh viễn.
// ============================================================================
#import "Il2CppResolve.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmodule-import-in-extern-c"
#import "Dobby/dobby.h"
#pragma clang diagnostic pop

// Tên module cho vài giá trị EProtocol.Proto đã biết (dump.cs) - chỉ để log dễ đọc, không đầy đủ.
inline const char *pcModuleName(int32_t cmdType) {
    switch (cmdType) {
        case 1: return "INIT";
        case 2: return "HEARTBEAT";
        case 3: return "MATCHMAKING";
        case 4: return "STATS";
        case 5: return "GROUP";
        case 11: return "ACCOUNT";
        case 14: return "ROOM";
        case 29: return "FFANTI";
        case 34: return "CREDITSCORE";
        case 35: return "GAMESERVERMANAGER";
        case 40: return "GIN";
        case 42: return "CSRANKINGMATCH";
        case 43: return "BRRANKINGMATCH";
        case 45: return "MODESTATS";
        default: return "?";
    }
}

// Đọc byte[] IL2CPP thật - klass(8)+monitor(8)+bounds(8)+max_length(8, il2cpp_array_size_t =
// uintptr_t, xem il2cpp.h dòng 108) rồi mới tới data - offset 0x20, KHÔNG phải 0x18 như kiểu
// mảng "monoArray" đơn giản hoá trong UnityTypes.h (max_length ở đó giả định 4 byte, sai cho
// mục đích đọc chính xác ở đây).
inline void pcHexDumpByteArray(void *arr, char *out, size_t outCap) {
    if (!arr) { snprintf(out, outCap, "(null)"); return; }
    uintptr_t len = *(uintptr_t *)((char *)arr + 0x18);
    uint8_t *data = (uint8_t *)arr + 0x20;
    size_t show = len < 48 ? (size_t)len : 48;
    size_t pos = 0;
    for (size_t i = 0; i < show && pos + 3 < outCap; i++) {
        pos += snprintf(out + pos, outCap - pos, "%02x", data[i]);
    }
    if (len > show && pos + 4 < outCap) snprintf(out + pos, outCap - pos, "...");
    if (len == 0) snprintf(out, outCap, "(rong)");
}

typedef bool (*ORIG_SCH_Send)(void *, int32_t, void *, uint8_t);
static ORIG_SCH_Send orig_SCH_Send = NULL;
static bool hooked_SCH_Send(void *self, int32_t cmdType, void *message, uint8_t regionID) {
    if (message) {
        uint32_t innerCmd = *(uint32_t *)((char *)message + 0x10);
        void *data = *(void **)((char *)message + 0x18);
        char hex[160];
        pcHexDumpByteArray(data, hex, sizeof(hex));
        DeltaVFS_debugLogf("PacketCapture GUI: module=%d(%s) innerCmd=%u region=%u data=%s",
            cmdType, pcModuleName(cmdType), innerCmd, (unsigned)regionID, hex);
    }
    return orig_SCH_Send(self, cmdType, message, regionID);
}

typedef bool (*ORIG_SCH_OnHandlePacket)(void *, void *, void *);
static ORIG_SCH_OnHandlePacket orig_SCH_OnHandlePacket = NULL;
static bool hooked_SCH_OnHandlePacket(void *self, void *tcpPacket, void *tcpMsg) {
    if (tcpPacket) {
        // TCPMsgPacket: Cmd@0x10(byte) Region@0x11(byte) Length@0x14(int) Data@0x18(byte[]*)
        uint8_t cmd = *(uint8_t *)((char *)tcpPacket + 0x10);
        uint8_t region = *(uint8_t *)((char *)tcpPacket + 0x11);
        int32_t length = *(int32_t *)((char *)tcpPacket + 0x14);
        void *data = *(void **)((char *)tcpPacket + 0x18);
        char hex[160];
        pcHexDumpByteArray(data, hex, sizeof(hex));
        DeltaVFS_debugLogf("PacketCapture NHAN: module=%u(%s) region=%u length=%d data=%s",
            (unsigned)cmd, pcModuleName(cmd), (unsigned)region, length, hex);
    }
    return orig_SCH_OnHandlePacket(self, tcpPacket, tcpMsg);
}

// Thử MSHookFunction trước (rẻ hơn, đã xác nhận hoạt động cho 1 số hàm UnityFramework khác),
// Dobby làm phương án dự phòng nếu MSHookFunction thất bại (xem AntiReportSpoof.h - MSHookFunction
// không phải lúc nào cũng hook được hàm trong UnityFramework).
inline void installPacketCapture() {
    void *sendTarget = Il2CppResolve::GetMethod("Assembly-CSharp.dll", "COW", "ServiceConnectionHandler", "Send", 3);
    if (!sendTarget) sendTarget = (void *)getRealOffset(0x6D93BF8);
    MSHookFunction(sendTarget, (void *)hooked_SCH_Send, (void **)&orig_SCH_Send);
    if (!orig_SCH_Send) {
        DobbyHook(sendTarget, (dobby_dummy_func_t)hooked_SCH_Send, (dobby_dummy_func_t *)&orig_SCH_Send);
    }
    DeltaVFS_debugLogf("PacketCapture: hook Send() %s", orig_SCH_Send ? "OK" : "THAT BAI");

    void *recvTarget = Il2CppResolve::GetMethod("Assembly-CSharp.dll", "COW", "ServiceConnectionHandler", "OnHandlePacket", 2);
    if (!recvTarget) recvTarget = (void *)getRealOffset(0x6D94A48);
    MSHookFunction(recvTarget, (void *)hooked_SCH_OnHandlePacket, (void **)&orig_SCH_OnHandlePacket);
    if (!orig_SCH_OnHandlePacket) {
        DobbyHook(recvTarget, (dobby_dummy_func_t)hooked_SCH_OnHandlePacket, (dobby_dummy_func_t *)&orig_SCH_OnHandlePacket);
    }
    DeltaVFS_debugLogf("PacketCapture: hook OnHandlePacket() %s", orig_SCH_OnHandlePacket ? "OK" : "THAT BAI");
}
