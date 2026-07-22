#pragma once
// ============================================================================
//  LỊCH SỬ: file này TỪNG là 1 thử nghiệm hook open() bằng hardware breakpoint (CPU debug
//  register + Mach exception port) để né dấu vết fishhook để lại trên GOT - lấy cảm hứng từ việc
//  soi Monite.dylib thấy nó dùng đúng kỹ thuật exception-port này.
//
//  ĐÃ BỎ HẲN sau khi đối chứng thật trên máy (build fishhook sạch, build HWBreakHook lỗi
//  "hotfix: SaveFailed", lặp lại nhiều lần): đo được mỗi lần chặn open() qua breakpoint mất
//  ~250-460us (đỉnh gần 900us) - CHẬM HƠN fishhook ~100-300 LẦN do chi phí nội tại CPU-trap ->
//  Mach IPC 2 chiều -> resume, không có cách nào giảm bằng code (đã thử tăng QoS thread xử lý
//  exception, không đủ). Quan trọng hơn: đào sâu lại chính Monite.dylib bằng Ghidra (xem
//  MoniteAnalysis/README.md mục 3c) xác nhận HỌ CŨNG KHÔNG dùng kỹ thuật này để hook open() - dù
//  code của họ CÓ 1 utility cài hardware-breakpoint y hệt cấu trúc tham khảo (hook/hook.c), nó chỉ
//  dùng cho hàm gameplay tần suất THẤP (giống hook.c), còn redirect file thật của họ vẫn là
//  swizzle Cocoa/NSBundle (rẻ, không qua Mach IPC) - tức đây KHÔNG PHẢI kỹ thuật hợp lý cho 1 hàm
//  bị gọi hàng trăm/nghìn lần/giây như open(), dù là Monite hay chính mình dùng.
//
//  GIỮ LẠI: đúng hạ tầng Mach exception-port đó, dùng cho 1 việc HỢP LÝ hơn - crash logger tự dump
//  thanh ghi + symbol lúc app crash thật (EXC_BAD_ACCESS/BAD_INSTRUCTION/ARITHMETIC), lấy cảm hứng
//  trực tiếp từ format string "Crash=true"/"x0=%s x1=%s..."/"File: %s\nBase Address: %p\nSymbol
//  Name: %s\nSymbol Address: %p\n" giải mã được từ chính code Monite (cùng mục 3c) - xác nhận đây
//  đúng là 1 module crash-reporter họ tự viết. Các loại exception này RẤT HIẾM khi xảy ra (chỉ lúc
//  crash thật), không có vấn đề tần suất/độ trễ như open().
//
//  GIAO THỨC MACH EXCEPTION: dùng code do `mig` (công cụ chuẩn của Apple) SINH RA từ
//  mach_exc.defs của chính SDK trên máy build (xem bước "Generate MIG exception server stubs"
//  trong .github/workflows/build.yml và Source/Includes/Generated/), KHÔNG tự viết tay layout
//  message. `mach_msg_server()` (hàm chuẩn của Apple) tự lo toàn bộ vòng nhận/dispatch/reply đúng
//  cách, chỉ cần cung cấp 3 hàm catch_mach_exception_raise* theo đúng chữ ký mà code sinh ra yêu
//  cầu - chỉ catch_mach_exception_raise_state là hàm thật (khớp EXCEPTION_STATE), 2 hàm còn lại
//  chỉ là stub cho đủ liên kết (kernel không bao giờ gọi tới vì không đăng ký behavior tương ứng).
// ============================================================================
#import <mach/mach.h>
#import <mach/mach_error.h>
#import <mach/task.h>
#import <mach/thread_act.h>
#import <mach/exception_types.h>
#import <mach/thread_status.h>
#import <mach/arm/thread_status.h>
#import <pthread.h>
#import <pthread/qos.h>
#import <dlfcn.h>
#import <unistd.h>
#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>

// Header do mig sinh ra KHÔNG tự bọc extern "C" cho người dùng C++ (thấy rõ qua lỗi build:
// "declaration of 'catch_mach_exception_raise' has a different language linkage" - Clang coi
// khai báo trong header này là C++ mangled vì file .mm include nó không có gì báo là C). Nhưng
// Source/Includes/Generated/mach_excServer.c lại là 1 file .c THUẦN, biên dịch với linkage C
// bình thường - nếu 2 bên linkage khác nhau, code trong mach_excServer.c gọi catch_mach_exception_raise
// sẽ gọi tới tên đã mangle sai (hoặc ngược lại), lỗi lúc link. Tự bọc extern "C" quanh #import để
// mọi khai báo trong header này (và định nghĩa tương ứng bên dưới) đều là linkage C thống nhất,
// khớp với mach_excServer.c.
//
// Header này include lại 1 loạt header hệ thống (<string.h>, <mach/port.h>, ...) - dưới
// -fmodules (Clang modules, mặc định bật khi build cho iOS), các include đó được dịch thành
// "@import module" ngầm, mà Clang KHÔNG cho phép 1 "@import" nằm bên trong khối extern "C" (lỗi
// -Wmodule-import-in-extern-c). Đây chỉ là hạn chế CÚ PHÁP - các module vẫn được import đúng và
// hợp lệ về mặt ngữ nghĩa, Clang chỉ cảnh báo vì cách viết này khác thường - nên tắt riêng đúng
// cảnh báo đó quanh #import thay vì tìm cách viết lại (không sửa được nội dung file do mig tự
// sinh ra mỗi lần build).
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmodule-import-in-extern-c"
extern "C" {
#import "Generated/mach_exc_server.h"
}
#pragma clang diagnostic pop

// mach_msg_server() là hàm chuẩn của libsystem (chạy vòng lặp nhận/dispatch/reply đúng giao
// thức MIG cho mình, gọi callback "demux" - ở đây là mach_exc_server() do mig sinh ra) - không
// tự chắc chắn header hệ thống nào khai báo sẵn tên này trên SDK cụ thể đang build, nên khai báo
// thẳng ở đây theo đúng chữ ký ổn định nhiều đời của Apple, tránh phụ thuộc 1 header cụ thể có
// thể khác nhau giữa các phiên bản SDK.
extern "C" mach_msg_return_t mach_msg_server(
    boolean_t (*demux)(mach_msg_header_t *, mach_msg_header_t *),
    mach_msg_size_t max_size,
    mach_port_t rcv_name,
    mach_msg_options_t options);

static mach_port_t g_crashLogExcPort = MACH_PORT_NULL;

// ---- 3 hàm callback mà mach_exc_server() (demux do mig sinh ra) gọi ngược lại - PHẢI đúng tên
// + đúng chữ ký mà Source/Includes/Generated/mach_exc_server.h khai báo (extern "C", linkage
// ngoài, vì mach_excServer.c là 1 translation unit RIÊNG chỉ tham chiếu extern tới các hàm này).
// Đăng ký task_swap_exception_ports với behavior=EXCEPTION_STATE (không phải EXCEPTION_DEFAULT)
// nên kernel CHỈ BAO GIỜ gọi catch_mach_exception_raise_state - 2 hàm còn lại vẫn PHẢI định nghĩa
// (dù trả KERN_FAILURE) vì mach_exc_server tham chiếu extern tới cả 3, thiếu 1 cái là lỗi link
// ngay lúc build (an toàn - phát hiện ở CI, không phải lúc chạy trên máy thật).
extern "C" kern_return_t catch_mach_exception_raise(
    mach_port_t exception_port,
    mach_port_t thread,
    mach_port_t task,
    exception_type_t exception,
    mach_exception_data_t code,
    mach_msg_type_number_t codeCnt)
{
    if (MACH_PORT_VALID(thread)) mach_port_deallocate(mach_task_self(), thread);
    if (MACH_PORT_VALID(task)) mach_port_deallocate(mach_task_self(), task);
    return KERN_FAILURE;
}

// ---- HÀM XỬ LÝ THẬT DUY NHẤT - chạy mỗi khi có 1 trong 3 loại exception đã đăng ký (xem
// CrashLogger_install: EXC_MASK_BAD_ACCESS/BAD_INSTRUCTION/ARITHMETIC, đều là crash thật, RẤT
// HIẾM xảy ra - không có vấn đề tần suất/độ trễ như bản hook open() cũ đã bỏ). ----
// Chỉ LOG (dump thanh ghi x0-x7 + PC + tên hàm/module gần nhất qua dladdr) rồi trả KERN_FAILURE -
// KHÔNG sửa state, KHÔNG nhận xử lý - kernel tự chuyển tiếp cho handler mặc định của OS (Apple vẫn
// tự tạo .ips bình thường, không mất) - đây chỉ "nghe lén" thêm thông tin vào debug.log (sống sót
// qua crash vì ghi thẳng write()), không can thiệp crash flow thật. Tham khảo trực tiếp format
// string "Crash=true"/"x0=%s x1=%s..." giải mã được từ chính code Monite, xem
// MoniteAnalysis/README.md mục 3c.
extern "C" kern_return_t catch_mach_exception_raise_state(
    mach_port_t exception_port,
    exception_type_t exception,
    const mach_exception_data_t code,
    mach_msg_type_number_t codeCnt,
    int *flavor,
    const thread_state_t old_state,
    mach_msg_type_number_t old_stateCnt,
    thread_state_t new_state,
    mach_msg_type_number_t *new_stateCnt)
{
    const arm_thread_state64_t *inState = (const arm_thread_state64_t *)old_state;
    uint64_t crashPc = (uint64_t)arm_thread_state64_get_pc(*inState);
    Dl_info info;
    memset(&info, 0, sizeof(info));
    dladdr((void *)crashPc, &info);
    DeltaVFS_debugLogf(
        "CrashLogger: exc=%d pc=0x%llx File=%s Base=%p Symbol=%s SymAddr=%p "
        "x0=0x%llx x1=0x%llx x2=0x%llx x3=0x%llx x4=0x%llx x5=0x%llx x6=0x%llx x7=0x%llx",
        (int)exception, (unsigned long long)crashPc,
        info.dli_fname ? info.dli_fname : "?", info.dli_fbase,
        info.dli_sname ? info.dli_sname : "?", info.dli_saddr,
        (unsigned long long)inState->__x[0], (unsigned long long)inState->__x[1],
        (unsigned long long)inState->__x[2], (unsigned long long)inState->__x[3],
        (unsigned long long)inState->__x[4], (unsigned long long)inState->__x[5],
        (unsigned long long)inState->__x[6], (unsigned long long)inState->__x[7]);
    // Không đụng new_state/new_stateCnt/flavor - không cần vì trả KERN_FAILURE (kernel bỏ qua
    // new_state trong trường hợp này, chuyển thẳng sang handler tiếp theo trong chuỗi).
    (void)new_state; (void)new_stateCnt; (void)flavor; (void)code; (void)codeCnt; (void)exception_port;
    return KERN_FAILURE;
}

extern "C" kern_return_t catch_mach_exception_raise_state_identity(
    mach_port_t exception_port,
    mach_port_t thread,
    mach_port_t task,
    exception_type_t exception,
    mach_exception_data_t code,
    mach_msg_type_number_t codeCnt,
    int *flavor,
    thread_state_t old_state,
    mach_msg_type_number_t old_stateCnt,
    thread_state_t new_state,
    mach_msg_type_number_t *new_stateCnt)
{
    if (MACH_PORT_VALID(thread)) mach_port_deallocate(mach_task_self(), thread);
    if (MACH_PORT_VALID(task)) mach_port_deallocate(mach_task_self(), task);
    return KERN_FAILURE;
}

static void *crashLoggerServerThreadFn(void *ctx) {
    // Tăng QoS lên mức cao nhất - thread này CHỈ chạy khi có crash thật (cực hiếm), không tốn chi
    // phí gì đáng kể lúc bình thường, nhưng lúc crash cần được lập lịch nhanh để log kịp trước khi
    // tiến trình bị OS dọn dẹp.
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
    mach_msg_return_t mr = mach_msg_server(mach_exc_server, 4096, g_crashLogExcPort, MACH_MSG_OPTION_NONE);
    DeltaVFS_debugLogf("CrashLogger: mach_msg_server() thoát bất thường mr=0x%x (không nên xảy ra)", mr);
    return NULL;
}

// Cài đặt crash logger - AN TOÀN để gọi vô điều kiện (không phụ thuộc gì vào open()/fishhook nữa,
// khác hẳn bản HWBreakHook cũ). Dùng task_swap_exception_ports (không phải task_set_exception_
// ports) để biết CÓ ai đã đăng ký crash-handler cho các loại exception này trước mình chưa - nếu
// CÓ (VD DataDomeSDK hoặc chính FreeFire tự có crash-reporter riêng), KHÔNG cài đè lên (phục hồi
// lại nguyên trạng ngay) để tránh giành mất port của thành phần khác - chỉ log lại để biết, không
// cố tự implement forward/chain (phức tạp hơn nhiều, xem Monite làm ở MoniteAnalysis/README.md
// mục 3b - họ tự dựng message Mach exception rồi gọi thẳng mach_msg() để forward, mình chưa cần
// mức đó cho 1 tính năng phụ trợ như crash logger).
inline void CrashLogger_install() {
    kern_return_t kr = mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &g_crashLogExcPort);
    if (kr != KERN_SUCCESS) {
        DeltaVFS_debugLogf("CrashLogger: mach_port_allocate thất bại kr=%d, huỷ", kr);
        return;
    }
    mach_port_insert_right(mach_task_self(), g_crashLogExcPort, g_crashLogExcPort, MACH_MSG_TYPE_MAKE_SEND);

    exception_mask_t oldMasks[EXC_TYPES_COUNT];
    mach_port_t oldPorts[EXC_TYPES_COUNT];
    exception_behavior_t oldBehaviors[EXC_TYPES_COUNT];
    thread_state_flavor_t oldFlavors[EXC_TYPES_COUNT];
    mach_msg_type_number_t oldCount = EXC_TYPES_COUNT;
    exception_mask_t mask = EXC_MASK_BAD_ACCESS | EXC_MASK_BAD_INSTRUCTION | EXC_MASK_ARITHMETIC;

    kr = task_swap_exception_ports(mach_task_self(), mask, g_crashLogExcPort,
                                    (exception_behavior_t)(EXCEPTION_STATE | MACH_EXCEPTION_CODES),
                                    ARM_THREAD_STATE64,
                                    oldMasks, &oldCount, oldPorts, oldBehaviors, oldFlavors);
    if (kr != KERN_SUCCESS) {
        DeltaVFS_debugLogf("CrashLogger: task_swap_exception_ports thất bại kr=%d, huỷ", kr);
        mach_port_deallocate(mach_task_self(), g_crashLogExcPort);
        g_crashLogExcPort = MACH_PORT_NULL;
        return;
    }

    bool conflict = false;
    for (mach_msg_type_number_t i = 0; i < oldCount; i++) {
        if (oldPorts[i] != MACH_PORT_NULL) { conflict = true; break; }
    }
    if (conflict) {
        DeltaVFS_debugLog("CrashLogger: phát hiện handler crash KHÁC đã đăng ký thật trước - phục hồi lại nguyên trạng, KHÔNG cài crash logger để tránh giành mất port của thành phần khác");
        for (mach_msg_type_number_t i = 0; i < oldCount; i++) {
            if (oldMasks[i] != 0) {
                task_set_exception_ports(mach_task_self(), oldMasks[i], oldPorts[i], oldBehaviors[i], oldFlavors[i]);
            }
        }
        mach_port_deallocate(mach_task_self(), g_crashLogExcPort);
        g_crashLogExcPort = MACH_PORT_NULL;
        return;
    }

    pthread_t serverThread;
    if (pthread_create(&serverThread, NULL, crashLoggerServerThreadFn, NULL) != 0) {
        DeltaVFS_debugLog("CrashLogger: tạo server thread thất bại, huỷ");
        mach_port_deallocate(mach_task_self(), g_crashLogExcPort);
        g_crashLogExcPort = MACH_PORT_NULL;
        return;
    }
    pthread_detach(serverThread);
    DeltaVFS_debugLog("CrashLogger: cài đặt OK - không có handler crash nào khác bị ghi đè");
}

// ============================================================================
//  CrashFlag: cờ "Crash=true"/"Crash=false" ghi xuống đĩa - bổ sung cho CrashLogger_install() ở
//  trên. Register-dump ở trên CHỈ bắt được exception ĐỒNG BỘ do chính CPU sinh ra (BAD_ACCESS/
//  BAD_INSTRUCTION/ARITHMETIC) - nó KHÔNG BAO GIỜ thấy được các kiểu "chết" do NGOẠI LỰC: bị
//  jetsam SIGKILL vì hết bộ nhớ, bị iOS watchdog kill vì treo main thread quá lâu, hoặc user tự
//  force-quit - không có exception nào phát sinh trong các trường hợp đó để mà bắt.
//
//  Cách duy nhất phát hiện được những kiểu chết này: ghi 1 cờ "dơ" xuống đĩa ngay khi vào trạng
//  thái "có thể crash" (app đang ở foreground), rồi XOÁ cờ (ghi "sạch") khi vào lại trạng thái an
//  toàn (background/terminate). Lần mở app SAU đó, nếu đọc lại thấy cờ vẫn "dơ" từ phiên trước ->
//  phiên đó không tắt sạch. Định dạng chuỗi "Crash=true"/"Crash=false" lấy trực tiếp từ chính
//  Monite (giải mã được qua Ghidra, xem MoniteAnalysis/README.md mục 3c).
// ============================================================================
inline NSString *CrashFlag_path() {
    NSArray<NSString *> *paths = NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask, YES);
    return [paths.firstObject stringByAppendingPathComponent:@"delta_crash_flag.log"];
}

inline void CrashFlag_write(BOOL crashed) {
    NSString *content = crashed ? @"Crash=true" : @"Crash=false";
    [content writeToFile:CrashFlag_path() atomically:YES encoding:NSUTF8StringEncoding error:nil];
}

// Gọi 1 LẦN DUY NHẤT, càng sớm càng tốt (constructor) - đọc cờ của phiên TRƯỚC, log kết quả, rồi
// "arm" lại (ghi Crash=true) cho phiên hiện tại, và đăng ký NSNotificationCenter để tự hạ cờ
// xuống Crash=false mỗi khi vào background/terminate sạch, và nâng lại Crash=true mỗi khi quay
// lại foreground (vì lúc đó lại bắt đầu "có thể crash" trở lại).
inline void CrashFlag_checkPreviousSessionAndArm() {
    NSString *path = CrashFlag_path();
    NSString *prev = [NSString stringWithContentsOfFile:path encoding:NSUTF8StringEncoding error:nil];
    if ([prev isEqualToString:@"Crash=true"]) {
        DeltaVFS_debugLog("CrashFlag: phiên trước KHÔNG tắt sạch (còn Crash=true) - khả năng đã crash/bị kill bất thường (jetsam/watchdog/force-quit)");
    } else {
        DeltaVFS_debugLog("CrashFlag: phiên trước tắt sạch (Crash=false) hoặc đây là lần mở đầu tiên");
    }
    CrashFlag_write(YES);

    [[NSNotificationCenter defaultCenter] addObserverForName:UIApplicationDidEnterBackgroundNotification
                                                        object:nil
                                                         queue:nil
                                                    usingBlock:^(NSNotification *note) {
        CrashFlag_write(NO);
        DeltaVFS_debugLog("CrashFlag: vào background sạch - ghi Crash=false");
    }];
    [[NSNotificationCenter defaultCenter] addObserverForName:UIApplicationWillTerminateNotification
                                                        object:nil
                                                         queue:nil
                                                    usingBlock:^(NSNotification *note) {
        CrashFlag_write(NO);
        DeltaVFS_debugLog("CrashFlag: app terminate sạch - ghi Crash=false");
    }];
    [[NSNotificationCenter defaultCenter] addObserverForName:UIApplicationDidBecomeActiveNotification
                                                        object:nil
                                                         queue:nil
                                                    usingBlock:^(NSNotification *note) {
        CrashFlag_write(YES);
    }];
}
