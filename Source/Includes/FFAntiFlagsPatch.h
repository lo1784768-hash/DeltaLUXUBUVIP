// FFAntiFlagsPatch.h - vá 7 lệnh ghi field bên trong ffantihack.MFHPGMELLCC (UnityFramework)
// để các cờ kết quả phát hiện của SDK FFAnti riêng biệt (KHÁC hệ HackerPoolCdt/CheckHacker
// đã xử lý ở CheckHackerPatch.h/MatchClientInfoPatch.h) luôn được ghi là false/0.
//
// Bối cảnh: sau khi patch CheckHacker + GetMatchClientInfo, test thật vẫn bị đá nhưng timing
// đổi từ ~11s thành ~23s - xác nhận có 1 cơ chế phát hiện ĐỘC LẬP khác. Tìm ra namespace
// "ffantihack" (SDK anti-cheat riêng, có protocol/network channel riêng EProtocol.Proto
// FFANTI=29, message handler riêng OnMsgFFAnti). Class lõi MFHPGMELLCC bị obfuscate tên hoàn
// toàn (field lẫn method đều là chuỗi ngẫu nhiên) - không tra được theo tên như HackerPoolCdt.
//
// Vì sao KHÔNG tìm "nơi gọi" như CheckHacker: các hàm trong MFHPGMELLCC được gọi qua token
// CỤC BỘ theo từng class (IDMAP0: 0,1,2...55 - số nhỏ, trùng lặp giữa nhiều class khác nhau),
// khác với CheckHacker dùng token TOÀN CỤC duy nhất (IDMAP1: 45347) nên không thể quét toàn
// bộ binary tìm bl trực tiếp một cách đáng tin cậy. Thay vào đó: disassemble trực tiếp TỪNG
// hàm của MFHPGMELLCC (RVA đã biết sẵn từ dump.cs, không cần tìm caller) để tìm chỗ GHI vào
// field kết quả, rồi vá tại nguồn - cùng triết lý với MatchClientInfoPatch.h.
//
// 5 field private static bool nghi là kết quả phát hiện riêng lẻ (offset trong struct static
// field của MFHPGMELLCC): PHEEFAHAHFE@0x38, IBJJDBEJMLD@0x39, CAANLIGMMKP@0x3A,
// KIBPPIFNAKC@0x3B, GPANAPICKMA@0x3C. Quét toàn bộ 24 method của class tìm lệnh strb/str ghi
// vào các offset này - tìm được 7 lệnh ghi GIÁ TRỊ THẬT (không phải hằng số 0 có sẵn) tại 4/5
// field (thiếu CAANLIGMMKP@0x3A và cờ public tổng hợp ANFAMBFAHHB@0x48 - không tìm thấy nơi
// ghi trực tiếp trong các hàm đã quét, có thể set qua đường khác chưa lần ra được).
//
// Kỹ thuật vá: MỖI lệnh là "strb wN, [x8, #offset]" (ghi 1 byte từ thanh ghi wN chứa giá trị
// thật) - đổi thanh ghi nguồn wN thành WZR (thanh ghi luôn = 0 của ARM64, không cần lệnh nào
// khác load giá trị 0 vào nó) bằng cách đổi ĐÚNG 5 bit thấp nhất của lệnh (trường Rt) - CÙNG
// KÍCH THƯỚC 4 byte, không đụng gì khác trong lệnh (địa chỉ, offset, opcode y hệt), an toàn
// tương đương các patch đã xác nhận ổn định (MatchClientInfoPatch.h).
//
// CHƯA kiểm chứng trên thiết bị thật - và CHƯA CHẮC giải quyết hết được vụ bị đá (mới phủ
// 4/5 field private, thiếu CAANLIGMMKP + ANFAMBFAHHB, và có thể còn cơ chế khác ngoài
// MFHPGMELLCC nữa).
#pragma once
#import <Foundation/Foundation.h>
#include "MemoryUtils.h"
#include "AssetRedirect.h"
#import "CheckHackerPatch.h"  // dùng lại CheckHackerPatch_writeBytes()

inline void installFFAntiFlagsPatch() {
    struct PatchSite {
        uint64_t rva;
        uint8_t original[4];
        uint8_t patched[4];
        const char *label;
    };
    static const PatchSite sites[] = {
        {0x20C3874ULL, {0x09,0xE1,0x00,0x39}, {0x1F,0xE1,0x00,0x39}, "PHEEFAHAHFE@0x38 (site1)"},
        {0x20C3894ULL, {0x09,0xE1,0x00,0x39}, {0x1F,0xE1,0x00,0x39}, "PHEEFAHAHFE@0x38 (site2)"},
        {0x20C3924ULL, {0x13,0xE5,0x00,0x39}, {0x1F,0xE5,0x00,0x39}, "IBJJDBEJMLD@0x39 (site1)"},
        {0x20C3940ULL, {0x13,0xE5,0x00,0x39}, {0x1F,0xE5,0x00,0x39}, "IBJJDBEJMLD@0x39 (site2)"},
        {0x20C36F0ULL, {0x13,0xED,0x00,0x39}, {0x1F,0xED,0x00,0x39}, "KIBPPIFNAKC@0x3B"},
        {0x20C3E08ULL, {0x09,0xF1,0x00,0x39}, {0x1F,0xF1,0x00,0x39}, "GPANAPICKMA@0x3C (site1)"},
        {0x20C3E2CULL, {0x09,0xF1,0x00,0x39}, {0x1F,0xF1,0x00,0x39}, "GPANAPICKMA@0x3C (site2)"},
    };

    int ok = 0, fail = 0, skip = 0;
    for (const auto &site : sites) {
        uintptr_t target = (uintptr_t)getRealOffset(site.rva);
        if (!target) {
            DeltaVFS_debugLogf("FFAntiFlagsPatch: khong tim thay UnityFramework, bo qua %s", site.label);
            skip++;
            continue;
        }
        if (memcmp((void *)target, site.original, 4) != 0) {
            DeltaVFS_debugLogf("FFAntiFlagsPatch: byte goc %s (0x%lx) KHONG khop - HUY patch nay de an toan",
                                site.label, (unsigned long)target);
            skip++;
            continue;
        }
        if (CheckHackerPatch_writeBytes(target, site.patched, 4)) {
            ok++;
        } else {
            DeltaVFS_debugLogf("FFAntiFlagsPatch: ghi patch %s that bai tai 0x%lx", site.label, (unsigned long)target);
            fail++;
        }
    }
    DeltaVFS_debugLogf("FFAntiFlagsPatch: xong - ok=%d fail=%d skip=%d (tren %d diem)", ok, fail, skip, (int)(sizeof(sites)/sizeof(sites[0])));
}
