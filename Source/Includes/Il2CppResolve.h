#pragma once
// ============================================================================
//  Il2CppResolve.h - tra class/method/field của UnityFramework BẰNG TÊN qua chính API
//  reflection runtime của IL2CPP, thay cho địa chỉ RVA cứng (getRealOffset(0x...) trong
//  game_sdk_t::init(), Menu.mm) - RVA cứng LUÔN đổi mỗi lần game build lại (kể cả không sửa
//  1 dòng code nào, xem chính bản Monite 1.6.7->1.6.8 đã đối chiếu: __TEXT cùng size mà khác
//  88.69% byte do dịch chuyển dây chuyền), trong khi TÊN class/method/field ổn định hơn nhiều
//  qua các bản update (trừ khi Garena đổi hẳn tên - hiếm hơn hẳn việc recompile).
//
//  Kỹ thuật này không phải tự nghĩ ra - đã xác nhận qua tham khảo trực tiếp
//  C:\Users\admin\Downloads\FAKEMENU\FAKEMENU\Il2cpp.cpp (tool modding khác, tác giả "feendly"),
//  dùng đúng chuỗi hàm il2cpp_domain_get -> il2cpp_domain_get_assemblies ->
//  il2cpp_assembly_get_image -> il2cpp_class_from_name -> il2cpp_class_get_method_from_name,
//  và đã tự kiểm chứng TRỰC TIẾP trên UnityFramework THẬT của Free Fire (trích từ
//  FreeFire_Monite_FridaGadget.ipa, đọc bằng lief) - toàn bộ hàm dưới đây CÓ THẬT trong export
//  table của binary, không phải suy đoán:
//    il2cpp_domain_get, il2cpp_domain_get_assemblies, il2cpp_assembly_get_image,
//    il2cpp_image_get_name, il2cpp_class_from_name, il2cpp_class_get_method_from_name,
//    il2cpp_class_get_field_from_name, il2cpp_field_get_offset
//
//  MethodInfo::methodPointer là field ĐẦU TIÊN (offset 0x0) trong struct MethodInfo thật -
//  xem il2cpp.h (dump Il2CppDumper đầy đủ) dòng 117-142 - nên lấy con trỏ hàm gọi được chỉ cần
//  đọc thẳng *(void**)method, không cần API il2cpp_method_get_pointer (hàm này KHÔNG có trong
//  export table, đã kiểm tra không thấy).
//
//  GIỚI HẠN THẬT (không phải điểm yếu do làm ẩu): 1 vài class/method bị Garena OBFUSCATE tên
//  (VD "GetLocalPlayer" thật ra ứng với class "EMKJHAJNPDH", method "MBEDKMKBFIE" - xem comment
//  OB54 OFFSETS phía trên game_sdk_t::init() trong Menu.mm) - tra theo tên KHÔNG giúp được gì
//  cho các trường hợp này nếu tên bị obfuscate ĐỔI MỚI mỗi lần build (chưa xác minh được đằng
//  nào đúng - có thể vẫn ổn định qua các bản update nếu obfuscator sinh tên theo vị trí source
//  cố định). Vẫn giữ nguyên đường RVA cứng làm phương án dự phòng cho MỌI hàm (xem
//  Il2CppResolve_getMethod/Il2CppResolve_getFieldOffset - trả 0/NULL khi không tra được, gọi nơi
//  dùng tự fallback về getRealOffset(RVA cũ), không có hàm nào bị "gãy cứng" nếu resolve thất bại.
// ============================================================================
#import <Foundation/Foundation.h>
#import <dlfcn.h>
#import <string.h>
#include <atomic>

namespace Il2CppResolve {

typedef char*  (*FN_il2cpp_thread_get_name)(void*, uint32_t*);
typedef void*  (*FN_il2cpp_thread_attach)(void*);
typedef void*  (*FN_il2cpp_domain_get)();
typedef void** (*FN_il2cpp_domain_get_assemblies)(const void*, size_t*);
typedef const void* (*FN_il2cpp_assembly_get_image)(const void*);
typedef const char* (*FN_il2cpp_image_get_name)(void*);
typedef void*  (*FN_il2cpp_class_from_name)(const void*, const char*, const char*);
typedef void*  (*FN_il2cpp_class_get_method_from_name)(void*, const char*, int);
typedef void*  (*FN_il2cpp_class_get_field_from_name)(void*, const char*);
typedef size_t (*FN_il2cpp_field_get_offset)(void*);

static FN_il2cpp_thread_attach                 p_il2cpp_thread_attach = NULL;
static FN_il2cpp_domain_get                    p_il2cpp_domain_get = NULL;
static FN_il2cpp_domain_get_assemblies         p_il2cpp_domain_get_assemblies = NULL;
static FN_il2cpp_assembly_get_image            p_il2cpp_assembly_get_image = NULL;
static FN_il2cpp_image_get_name                p_il2cpp_image_get_name = NULL;
static FN_il2cpp_class_from_name               p_il2cpp_class_from_name = NULL;
static FN_il2cpp_class_get_method_from_name    p_il2cpp_class_get_method_from_name = NULL;
static FN_il2cpp_class_get_field_from_name     p_il2cpp_class_get_field_from_name = NULL;
static FN_il2cpp_field_get_offset              p_il2cpp_field_get_offset = NULL;

static void *g_domain = NULL;
static std::atomic<bool> g_attached{false};

// Gọi 1 LẦN, sau khi UnityFramework chắc chắn đã load xong metadata (an toàn nhất là gọi trong
// dispatch_after 3s như Menu.mm's +load đang làm cho setup menu, KHÔNG gọi trong constructor
// sớm - lúc đó il2cpp domain gần như chắc chắn CHƯA init xong).
inline bool Attach() {
    if (g_attached.load(std::memory_order_relaxed)) return g_domain != NULL;

    p_il2cpp_thread_attach              = (FN_il2cpp_thread_attach)             dlsym(RTLD_DEFAULT, "il2cpp_thread_attach");
    p_il2cpp_domain_get                 = (FN_il2cpp_domain_get)                dlsym(RTLD_DEFAULT, "il2cpp_domain_get");
    p_il2cpp_domain_get_assemblies      = (FN_il2cpp_domain_get_assemblies)     dlsym(RTLD_DEFAULT, "il2cpp_domain_get_assemblies");
    p_il2cpp_assembly_get_image         = (FN_il2cpp_assembly_get_image)        dlsym(RTLD_DEFAULT, "il2cpp_assembly_get_image");
    p_il2cpp_image_get_name             = (FN_il2cpp_image_get_name)            dlsym(RTLD_DEFAULT, "il2cpp_image_get_name");
    p_il2cpp_class_from_name            = (FN_il2cpp_class_from_name)           dlsym(RTLD_DEFAULT, "il2cpp_class_from_name");
    p_il2cpp_class_get_method_from_name = (FN_il2cpp_class_get_method_from_name)dlsym(RTLD_DEFAULT, "il2cpp_class_get_method_from_name");
    p_il2cpp_class_get_field_from_name  = (FN_il2cpp_class_get_field_from_name) dlsym(RTLD_DEFAULT, "il2cpp_class_get_field_from_name");
    p_il2cpp_field_get_offset           = (FN_il2cpp_field_get_offset)          dlsym(RTLD_DEFAULT, "il2cpp_field_get_offset");

    g_attached.store(true, std::memory_order_relaxed);

    if (!p_il2cpp_domain_get || !p_il2cpp_domain_get_assemblies || !p_il2cpp_assembly_get_image ||
        !p_il2cpp_image_get_name || !p_il2cpp_class_from_name || !p_il2cpp_class_get_method_from_name ||
        !p_il2cpp_class_get_field_from_name || !p_il2cpp_field_get_offset) {
        DeltaVFS_debugLog("Il2CppResolve: thieu 1 hoac nhieu ham il2cpp_* trong export table - resolve theo ten se luon that bai, moi noi goi se tu fallback RVA");
        return false;
    }

    g_domain = p_il2cpp_domain_get();
    if (p_il2cpp_thread_attach && g_domain) p_il2cpp_thread_attach(g_domain);
    if (!g_domain) {
        DeltaVFS_debugLog("Il2CppResolve: il2cpp_domain_get() tra ve NULL - resolve theo ten se luon that bai");
    }
    return g_domain != NULL;
}

// Tìm đúng image (assembly) theo tên - Free Fire chỉ có 1-2 assembly C# thật (Assembly-CSharp
// dạng nào đó), thử "Assembly-CSharp" trước, cho phép truyền tên khác nếu cần.
inline void *GetImage(const char *imageName) {
    if (!g_domain) return NULL;
    size_t count = 0;
    void **assemblies = p_il2cpp_domain_get_assemblies(g_domain, &count);
    if (!assemblies) return NULL;
    for (size_t i = 0; i < count; i++) {
        const void *img = p_il2cpp_assembly_get_image(assemblies[i]);
        if (!img) continue;
        const char *name = p_il2cpp_image_get_name((void *)img);
        if (name && strcmp(name, imageName) == 0) return (void *)img;
    }
    return NULL;
}

inline void *GetClass(const char *imageName, const char *namespaze, const char *className) {
    if (!Attach()) return NULL;
    void *img = GetImage(imageName);
    if (!img) return NULL;
    return p_il2cpp_class_from_name(img, namespaze, className);
}

// Trả về con trỏ hàm THẬT gọi được (đọc thẳng MethodInfo::methodPointer, field đầu tiên của
// struct - xem comment đầu file) - trả NULL nếu không tra được (class/method không tồn tại,
// sai argsCount, hoặc thiếu API il2cpp_* trong binary) - nơi gọi PHẢI tự fallback về RVA cũ.
inline void *GetMethod(const char *imageName, const char *namespaze, const char *className,
                       const char *methodName, int argsCount) {
    void *klass = GetClass(imageName, namespaze, className);
    if (!klass) return NULL;
    void **method = (void **)p_il2cpp_class_get_method_from_name(klass, methodName, argsCount);
    if (!method) return NULL;
    return *method; // MethodInfo::methodPointer - offset 0x0, xem il2cpp.h dòng 117-142
}

// Trả về offset field (byte, tính từ đầu object) - trả 0 nếu không tra được. LƯU Ý: offset 0
// KHÔNG PHÂN BIỆT ĐƯỢC với "field thật nằm ở offset 0" (hiếm khi field user thật nằm ngay đầu
// object vì luôn có klass+monitor header trước - nhưng vẫn nên coi 0 là "thất bại, fallback"
// theo đúng quy ước của toàn bộ hàm trong file này).
inline size_t GetFieldOffset(const char *imageName, const char *namespaze, const char *className,
                             const char *fieldName) {
    void *klass = GetClass(imageName, namespaze, className);
    if (!klass) return 0;
    void *field = p_il2cpp_class_get_field_from_name(klass, fieldName);
    if (!field) return 0;
    return p_il2cpp_field_get_offset(field);
}

} // namespace Il2CppResolve
