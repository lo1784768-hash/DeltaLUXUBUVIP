#pragma once
#import <Foundation/Foundation.h>
#include <dlfcn.h>
#include <cstdint>
#include <cstring>

#include "dobby.h" // Cần thêm libdobby.a + dobby.h vào target (chưa có sẵn trong project này)

// ============================================================================
//  SPOOF THÔNG TIN HỆ THỐNG (uname) MÀ IL2CPP LẤY QUA P/INVOKE
// ----------------------------------------------------------------------------
//  4 hàm native `getSystemInfo_Machine/SysName/Release/Version` được C# gọi
//  qua [DllImport] -> địa chỉ RVA nằm THẲNG TRONG GameAssembly.dylib (không
//  phải libc/libSystem), nên Dobby inline-hook được bình thường (không dính
//  giới hạn dyld shared cache như MSHookFunction trên libc - xem note trong
//  [[mshookfunction-shared-cache-limit]]).
// ============================================================================

#pragma pack(push, 1)
// Layout chuẩn Il2Cpp cho object string (khớp mọi phiên bản il2cpp hiện đại,
// arm64). Object header = klass + monitor giống mọi Il2CppObject, sau đó tới
// length (int32) và mảng ký tự UTF-16 (có \0 kết thúc).
struct Il2CppString {
    void    *klass;     // Il2CppClass* của System.String
    void    *monitor;   // MonitorData* dùng cho lock() - GC quản lý
    int32_t  length;     // Số ký tự UTF-16 (KHÔNG tính null terminator)
    uint16_t chars[1];   // Mảng ký tự UTF-16, flexible array member
};
#pragma pack(pop)

// ----------------------------------------------------------------------------
//  Tạo Il2CppString AN TOÀN: không tự tay ghép struct ở trên (klass trỏ bậy
//  sẽ khiến GC của il2cpp crash ngay lần collect kế tiếp). Thay vào đó gọi
//  thẳng hàm `il2cpp_string_new` mà GameAssembly.dylib export sẵn - đây là API
//  chính chủ để cấp phát 1 System.String hợp lệ (klass/monitor được GC gán
//  đúng), UTF-8 -> UTF-16 do chính runtime lo, không cần tự chuyển đổi.
// ----------------------------------------------------------------------------
typedef Il2CppString *(*il2cpp_string_new_t)(const char *);

inline il2cpp_string_new_t resolveIl2CppStringNew() {
    // static local -> khởi tạo 1 lần duy nhất, thread-safe (C++11 magic statics)
    static il2cpp_string_new_t fn =
        (il2cpp_string_new_t)dlsym(RTLD_DEFAULT, "il2cpp_string_new");
    return fn;
}

// Chuyển const char* (UTF-8) -> Il2CppString*. Trả NULL nếu il2cpp chưa init
// xong (symbol chưa resolve được) - caller PHẢI kiểm tra NULL trước khi trả
// về cho game để tránh crash.
inline Il2CppString *il2cppStringFromCString(const char *str) {
    if (!str) return NULL;
    il2cpp_string_new_t fn = resolveIl2CppStringNew();
    if (!fn) return NULL; // Không tìm thấy symbol -> để nguyên hành vi gốc, đừng tự chế struct
    return fn(str);
}

inline Il2CppString *il2cppStringFromNSString(NSString *str) {
    if (!str) return NULL;
    return il2cppStringFromCString([str UTF8String]);
}

// ----------------------------------------------------------------------------
//  4 hàm thay thế - cache lại Il2CppString* sau lần tạo đầu tiên (Boehm GC
//  của il2cpp không di chuyển object nên cache con trỏ tĩnh là an toàn, khỏi
//  phải gọi il2cpp_string_new lại mỗi lần game hỏi uname).
// ----------------------------------------------------------------------------
static void *orig_getSystemInfo_Machine  = NULL;
static void *orig_getSystemInfo_SysName  = NULL;
static void *orig_getSystemInfo_Release  = NULL;
static void *orig_getSystemInfo_Version  = NULL;

inline Il2CppString *hk_getSystemInfo_Machine() {
    static Il2CppString *cached = il2cppStringFromCString("iPhone16,1");
    return cached;
}

inline Il2CppString *hk_getSystemInfo_SysName() {
    static Il2CppString *cached = il2cppStringFromCString("iPhone OS");
    return cached;
}

inline Il2CppString *hk_getSystemInfo_Release() {
    static Il2CppString *cached = il2cppStringFromCString("17.4.1");
    return cached;
}

inline Il2CppString *hk_getSystemInfo_Version() {
    static Il2CppString *cached = il2cppStringFromCString("21E236");
    return cached;
}

// ----------------------------------------------------------------------------
//  Cài hook tập trung. `baseAddress` = địa chỉ nạp thực tế (sau ASLR slide)
//  của GameAssembly.dylib, KHÔNG phải mach_header tĩnh trong file. Lấy nó qua
//  getMemoryFileInfo("GameAssembly.dylib").address (xem MemoryUtils.h) rồi
//  cộng với RVA lấy từ dump.cs -> ra địa chỉ thực để Dobby patch.
// ----------------------------------------------------------------------------
inline void initSystemInfoHooks(uintptr_t baseAddress) {
    if (baseAddress == 0) return;

    DobbyHook((void *)(baseAddress + 0x6738BE4),
              (void *)hk_getSystemInfo_Machine,
              &orig_getSystemInfo_Machine);

    DobbyHook((void *)(baseAddress + 0x6738C4C),
              (void *)hk_getSystemInfo_SysName,
              &orig_getSystemInfo_SysName);

    DobbyHook((void *)(baseAddress + 0x6738D1C),
              (void *)hk_getSystemInfo_Release,
              &orig_getSystemInfo_Release);

    DobbyHook((void *)(baseAddress + 0x6738CB4),
              (void *)hk_getSystemInfo_Version,
              &orig_getSystemInfo_Version);
}
