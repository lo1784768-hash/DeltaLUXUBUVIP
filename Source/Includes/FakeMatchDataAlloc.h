// FakeMatchDataAlloc.h - cấp phát THẬT 1 mảng byte[1] hợp lệ qua chính API IL2CPP (không phải chỉ
// null hoá con trỏ như MatchClientInfoPatch.h bản cũ) - dùng để thay thế 4 field byte[] của
// MatchClientInfo (file_exception, lib_result@0x28, native_result@0x38, gin_check_data) bằng 1
// object THẬT, KHÔNG NULL, thay vì null hoàn toàn.
//
// LÝ DO: enum thật EHacker.HackerCdtID.CLIENT_INFO_EMPTY=101 và
// EHacker.HackerPoolCdt_GIN_CHECK_DATA_EMPTY_IOS=19 (dump.cs) đặt tên RÕ RÀNG cho tình huống
// "field này RỖNG" như 1 mã lỗi RIÊNG - nghĩa là bản thân việc null/rỗng CÓ THỂ tự nó là dấu hiệu
// bị phát hiện, tách biệt với nội dung thật của field. Test thật xác nhận: null hoàn toàn 7 field
// (MatchClientInfoPatch.h) KHÔNG ngăn được bị đá ở ghép trận ngẫu nhiên (trong khi phòng tự tạo -
// không gọi GetMatchClientInfo() - không bị đá) - nghi ngờ chính null này là vấn đề.
//
// KỸ THUẬT DÙNG Ở ĐÂY (xem MatchClientInfoPatch.h): KHÔNG đổi lệnh "str" (như bản cũ) mà đổi ĐÍCH
// của lệnh "bl <hàm tính giá trị thật>" đứng NGAY TRƯỚC mỗi lệnh str đó, trỏ sang stub bên dưới
// (CÙNG KÍCH THƯỚC 4 byte - BL luôn 4 byte bất kể địa chỉ đích) - lệnh str phía sau giữ NGUYÊN
// 100%, chỉ có GIÁ TRỊ được lưu là đổi (byte[1] giả thay vì kết quả thật/null).
//
// GIỚI HẠN THẬT: BL chỉ mã hoá được offset ±128MB (imm26 26-bit * 4). Delta.dylib và
// UnityFramework là 2 image RIÊNG BIỆT, dyld nạp ở địa chỉ không đảm bảo gần nhau - CÓ THỂ ngoài
// tầm với. Nơi gọi (MatchClientInfoPatch.h) PHẢI tự kiểm tra tầm với trước khi ghi, và tự lùi về kỹ
// thuật null hoá cũ (str -> XZR) nếu không ghi được - không có trường hợp nào bị "gãy cứng".
#pragma once
#import <Foundation/Foundation.h>
#import <dlfcn.h>
#import <string.h>

extern void DeltaVFS_debugLog(const char *msg);
extern void DeltaVFS_debugLogf(const char *fmt, ...);

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

// Gọi 1 lần trước khi cài patch redirect BL - cấp phát byte[1] THẬT (nội dung 1 byte = 0, không
// quan trọng, chỉ cần KHÔNG NULL). An toàn gọi nhiều lần (tự bỏ qua nếu đã thử/đã có).
inline void FakeMatchData_ensureAllocated() {
    if (g_fakeEmptyByteArray || g_fakeEmptyByteArrayTried) return;
    g_fakeEmptyByteArrayTried = true;

    auto array_new       = (FN_FMDA_il2cpp_array_new)       dlsym(RTLD_DEFAULT, "il2cpp_array_new");
    auto class_from_name = (FN_FMDA_il2cpp_class_from_name) dlsym(RTLD_DEFAULT, "il2cpp_class_from_name");
    if (!array_new || !class_from_name) {
        DeltaVFS_debugLog("FakeMatchData: thieu il2cpp_array_new/il2cpp_class_from_name trong export table - bo qua, se dung fallback null");
        return;
    }
    void *img = FakeMatchData_findMscorlibImage();
    if (!img) {
        DeltaVFS_debugLog("FakeMatchData: khong tim thay image mscorlib.dll - bo qua, se dung fallback null");
        return;
    }
    void *byteClass = class_from_name(img, "System", "Byte");
    if (!byteClass) {
        DeltaVFS_debugLog("FakeMatchData: khong tim thay class System.Byte - bo qua, se dung fallback null");
        return;
    }
    g_fakeEmptyByteArray = array_new(byteClass, 1);
    if (!g_fakeEmptyByteArray) {
        DeltaVFS_debugLog("FakeMatchData: il2cpp_array_new(Byte,1) that bai - bo qua, se dung fallback null");
        return;
    }
    DeltaVFS_debugLogf("FakeMatchData: cap phat byte[1] gia THAT thanh cong tai %p", g_fakeEmptyByteArray);
}

// Stub gọi THAY cho các hàm tính giá trị thật (0x6889018/0x827ab24 trong UnityFramework) qua BL
// redirect - PHẢI giữ đúng calling convention ARM64 (trả về con trỏ qua x0), bỏ qua toàn bộ tham
// số đầu vào (không cần dùng, hàm gốc nhận object/context để tính toán, ta chỉ cần trả 1 giá trị
// cố định không null).
extern "C" inline void *DeltaFakeEmptyByteArrayStub(void *a, void *b, void *c) {
    (void)a; (void)b; (void)c;
    return g_fakeEmptyByteArray;
}
