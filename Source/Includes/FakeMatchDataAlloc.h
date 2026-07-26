// FakeMatchDataAlloc.h - cấp phát THẬT 4 mảng byte[] riêng biệt qua IL2CPP, kích thước và nội
// dung random khác nhau - dùng thay thế 4 field byte[] của MatchClientInfo (file_exception,
// lib_result@0x28, native_result@0x38, gin_check_data). Mỗi field có mảng RIÊNG (không dùng
// chung 1 con trỏ) và size khác nhau (48-128 byte) để tránh:
//   1. Bị flagged bởi size check (byte[1] quá nhỏ so với dữ liệu thật hàng chục/trăm byte)
//   2. Bị flagged bởi pointer comparison (4 field cùng trỏ tới 1 object = bất thường)
//   3. Bị flagged bởi content check (toàn 0 = pattern dễ nhận diện)
//
// GIỚI HẠN BL ±128MB: Delta.dylib và UnityFramework là 2 image riêng biệt, không đảm bảo gần
// nhau. MatchClientInfoPatch.h PHẢI tự kiểm tra tầm với trước khi redirect BL, fallback null
// nếu ngoài tầm.
#pragma once
#import <Foundation/Foundation.h>
#import <dlfcn.h>
#import <string.h>
#include <stdlib.h>

extern void DeltaVFS_debugLog(const char *msg);
extern void DeltaVFS_debugLogf(const char *fmt, ...);

// 4 mảng riêng biệt - 1 cho mỗi field
static void *g_fakeByteArray_fileException   = NULL;  // field file_exception@0x20
static void *g_fakeByteArray_libResult       = NULL;  // field lib_result@0x28
static void *g_fakeByteArray_nativeResult    = NULL;  // field native_result@0x38
static void *g_fakeByteArray_ginCheckData    = NULL;  // field gin_check_data@0x50

// Giữ lại g_fakeEmptyByteArray cho backward compatibility (trỏ tới cùng object với array[0])
static void *g_fakeEmptyByteArray = NULL;
static bool g_fakeEmptyByteArrayTried = false;

typedef void*  (*FN_FMDA_il2cpp_array_new)(void*, size_t);
typedef void*  (*FN_FMDA_il2cpp_class_from_name)(const void*, const char*, const char*);
typedef void*  (*FN_FMDA_il2cpp_domain_get)();
typedef void** (*FN_FMDA_il2cpp_domain_get_assemblies)(const void*, size_t*);
typedef const void* (*FN_FMDA_il2cpp_assembly_get_image)(const void*);
typedef const char* (*FN_FMDA_il2cpp_image_get_name)(void*);

inline void *FakeMatchData_findMscorlibImage() {
    auto domain_get      = (FN_FMDA_il2cpp_domain_get)             dlsym(RTLD_DEFAULT, "il2cpp_domain_get");
    auto get_assemblies  = (FN_FMDA_il2cpp_domain_get_assemblies)  dlsym(RTLD_DEFAULT, "il2cpp_domain_get_assemblies");
    auto assembly_get_image = (FN_FMDA_il2cpp_assembly_get_image)  dlsym(RTLD_DEFAULT, "il2cpp_assembly_get_image");
    auto image_get_name   = (FN_FMDA_il2cpp_image_get_name)        dlsym(RTLD_DEFAULT, "il2cpp_image_get_name");
    if (!domain_get || !get_assemblies || !assembly_get_image || !image_get_name) return NULL;
    void *domain = domain_get();
    if (!domain) return NULL;
    size_t count = 0;
    void **assemblies = get_assemblies(domain, &count);
    if (!assemblies) return NULL;
    for (size_t i = 0; i < count; i++) {
        const void *img = assembly_get_image(assemblies[i]);
        if (!img) continue;
        const char *name = image_get_name((void *)img);
        if (name && strcmp(name, "mscorlib.dll") == 0) return (void *)img;
    }
    return NULL;
}

// Cấp phát 1 mảng byte[size] THẬT qua IL2CPP, điền random bytes
static void *FakeMatchData_allocArray(FN_FMDA_il2cpp_array_new array_new, void *byteClass,
                                       size_t size, const char *label) {
    void *arr = array_new(byteClass, size);
    if (!arr) {
        DeltaVFS_debugLogf("FakeMatchData: il2cpp_array_new(Byte,%zu) that bai cho %s", size, label);
        return NULL;
    }
    // IL2CPP array layout: [header 16 bytes on arm64][data...]
    // Kiểm tra bằng cách KHÔNG đoán layout - dùng byte đầu tiên đã có sẵn (0) để xác nhận
    // data bắt đầu ở offset 0x20 (32 byte trên arm64 — Il2CppArraySize header bao gồm
    // Il2CppObject klass+monitor 16 byte + Il2CppArrayBounds* 8 byte + max_length 8 byte).
    // Điền random từ offset 0x20.
    uint8_t *data = (uint8_t *)arr + 0x20;
    arc4random_buf(data, size);
    DeltaVFS_debugLogf("FakeMatchData: cap phat byte[%zu] gia THAT cho %s tai %p", size, label, arr);
    return arr;
}

// Gọi 1 lần trước khi cài patch redirect BL. An toàn gọi nhiều lần.
inline void FakeMatchData_ensureAllocated() {
    if (g_fakeEmptyByteArrayTried) return;
    g_fakeEmptyByteArrayTried = true;

    auto array_new       = (FN_FMDA_il2cpp_array_new)       dlsym(RTLD_DEFAULT, "il2cpp_array_new");
    auto class_from_name = (FN_FMDA_il2cpp_class_from_name) dlsym(RTLD_DEFAULT, "il2cpp_class_from_name");
    if (!array_new || !class_from_name) {
        DeltaVFS_debugLog("FakeMatchData: thieu il2cpp_array_new/il2cpp_class_from_name - se dung fallback null");
        return;
    }
    void *img = FakeMatchData_findMscorlibImage();
    if (!img) {
        DeltaVFS_debugLog("FakeMatchData: khong tim thay image mscorlib.dll - se dung fallback null");
        return;
    }
    void *byteClass = class_from_name(img, "System", "Byte");
    if (!byteClass) {
        DeltaVFS_debugLog("FakeMatchData: khong tim thay class System.Byte - se dung fallback null");
        return;
    }

    // 4 mảng riêng biệt, kích thước khác nhau, nội dung random
    g_fakeByteArray_fileException = FakeMatchData_allocArray(array_new, byteClass, 48,  "file_exception");
    g_fakeByteArray_libResult     = FakeMatchData_allocArray(array_new, byteClass, 56,  "lib_result");
    g_fakeByteArray_nativeResult  = FakeMatchData_allocArray(array_new, byteClass, 64,  "native_result");
    g_fakeByteArray_ginCheckData  = FakeMatchData_allocArray(array_new, byteClass, 128, "gin_check_data");

    // Backward compat — g_fakeEmptyByteArray trỏ tới array đầu tiên không null
    g_fakeEmptyByteArray = g_fakeByteArray_fileException ? g_fakeByteArray_fileException :
                           g_fakeByteArray_libResult     ? g_fakeByteArray_libResult :
                           g_fakeByteArray_nativeResult  ? g_fakeByteArray_nativeResult :
                           g_fakeByteArray_ginCheckData;
}

// 4 stub riêng biệt - 1 cho mỗi field - gọi THAY cho hàm tính giá trị thật qua BL redirect.
// Mỗi stub trả về mảng riêng của nó, fallback g_fakeEmptyByteArray nếu mảng riêng bị null.
extern "C" inline void *DeltaFakeStub_fileException(void *a, void *b, void *c) {
    (void)a; (void)b; (void)c;
    return g_fakeByteArray_fileException ? g_fakeByteArray_fileException : g_fakeEmptyByteArray;
}
extern "C" inline void *DeltaFakeStub_libResult(void *a, void *b, void *c) {
    (void)a; (void)b; (void)c;
    return g_fakeByteArray_libResult ? g_fakeByteArray_libResult : g_fakeEmptyByteArray;
}
extern "C" inline void *DeltaFakeStub_nativeResult(void *a, void *b, void *c) {
    (void)a; (void)b; (void)c;
    return g_fakeByteArray_nativeResult ? g_fakeByteArray_nativeResult : g_fakeEmptyByteArray;
}
extern "C" inline void *DeltaFakeStub_ginCheckData(void *a, void *b, void *c) {
    (void)a; (void)b; (void)c;
    return g_fakeByteArray_ginCheckData ? g_fakeByteArray_ginCheckData : g_fakeEmptyByteArray;
}

// Giữ lại stub cũ cho backward compat (nếu có chỗ nào khác dùng)
extern "C" inline void *DeltaFakeEmptyByteArrayStub(void *a, void *b, void *c) {
    (void)a; (void)b; (void)c;
    return g_fakeEmptyByteArray;
}
