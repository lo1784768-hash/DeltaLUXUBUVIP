#pragma once
// ============================================================================
//  HARDWARE-BREAKPOINT HOOK cho open() - THỬ NGHIỆM, RỦI RO CAO.
//
//  Vì sao cần cái này: fishhook (đang dùng cho open/openat/fopen/access/stat/lstat) hoạt
//  động bằng cách patch con trỏ hàm trong bảng import (__DATA,__la_symbol_ptr) của TỪNG
//  file Mach-O trong tiến trình - kỹ thuật này để lại DẤU VẾT dễ quét: bất kỳ code nào so
//  sánh con trỏ "open" hiện tại với địa chỉ gốc trong libSystem.dylib là phát hiện ra ngay.
//  Soi dylib "Monite.dylib" (một tweak khác, dùng thử nghiệm để đối chiếu) cho thấy nó
//  KHÔNG dùng fishhook mà dùng hardware breakpoint (CPU debug register) + Mach exception
//  port - kỹ thuật này không đụng tới GOT lẫn code của hàm gốc, nên né được kiểu quét trên.
//  Không thể dùng inline hook (MSHookFunction) thay thế vì libc nằm trong dyld shared cache
//  read-only, không patch code được - xem memory mshookfunction-shared-cache-limit.
//
//  CƠ CHẾ (bản v4 - BẢNG ĐĂNG KÝ NHIỀU BREAKPOINT, đối chiếu TRỰC TIẾP với Monite.dylib qua
//  Ghidra decompiler, không còn suy đoán qua assembly tay): đặt breakpoint CPU (ARM64
//  DBGBCRn/DBGBVRn qua thread_set_state) tại ĐỊA CHỈ THẬT của hàm cần chặn. Khi CPU chạm
//  tới, kernel gửi Mach exception EXC_BREAKPOINT tới port mình đăng ký VỚI BEHAVIOR =
//  EXCEPTION_STATE - handler tra BẢNG (không hardcode 1 địa chỉ) tìm entry có __bvr khớp PC
//  hiện tại, memcpy NGUYÊN state gốc, CHỈ sửa đúng field PC (offset 0x100 trong
//  arm_thread_state64_t) thành trampoline đã đăng ký cho entry đó, kernel áp dụng nguyên tử
//  lúc reply. KHÔNG đụng tới x0/x1/x2 (đối số) trong handler - khác bản v3 cũ (đã tự tính
//  path redirect ngay trong handler). Lý do đổi: decompile Monite.dylib (MoniteAnalysis/,
//  hàm tương đương FUN_000c14c8) cho thấy handler CỦA HỌ hoàn toàn không chạm tới đối số,
//  chỉ đổi PC - toàn bộ logic ứng dụng (redirect path, gọi hàm thật) nằm trong TRAMPOLINE
//  (chạy như 1 hàm C bình thường TRÊN CHÍNH thread vừa chạm breakpoint, sau khi exception đã
//  reply xong) - handler dùng chung cho MỌI entry càng đơn giản càng ít rủi ro trong ngữ
//  cảnh Mach exception vốn nhạy cảm; logic ứng dụng thật (gọi redirectAllTrafficPath, có thể
//  cấp phát/copy chuỗi...) chạy ở ngữ cảnh hàm C bình thường, an toàn hơn hẳn.
//
//  TẠI SAO ĐỔI TỪ SINGLE-STEP (v1) SANG TRAMPOLINE (v2/v3, giữ nguyên ở v4): bản v1 dùng
//  behavior=EXCEPTION_DEFAULT (có thread/task port), sau khi sửa x0 thì tắt breakpoint + bật
//  single-step (MDSCR_EL1.SS) qua 1 cặp thread_get_state/thread_set_state RIÊNG, cho đúng 1
//  lệnh gốc chạy, bắt exception single-step tiếp theo, rồi tắt single-step + bật lại
//  breakpoint. Qua nhiều vòng test trên máy thật, app luôn treo cứng sau 1 số lần chặn
//  open() không cố định (75, 178, 889, 1354...) - "nhịp tim" độc lập ở main thread
//  (CADisplayLink) xác nhận CHÍNH main thread bị treo thật. Đổi hẳn sang code MIG chuẩn của
//  Apple (thay bộ khung tự viết tay) vẫn bị treo Y HỆT - loại trừ khả năng lỗi nằm ở giao
//  thức message, dồn nghi vấn về đúng chỗ single-step/debug-register dance.
//
//  Soi kỹ Monite.dylib phát hiện: nó đăng ký exception port với behavior=EXCEPTION_STATE
//  (KHÔNG PHẢI EXCEPTION_DEFAULT), và toàn bộ luồng xử lý exception của nó (đã decompile xác
//  nhận) không hề gọi thread_get_state/thread_set_state - CHỈ memcpy state rồi sửa 1 field.
//  Với EXCEPTION_STATE, callback không hề nhận được thread/task port - CHỈ nhận/trả thẳng
//  state thanh ghi (bao gồm cả PC) qua chính request/reply, kernel tự áp dụng khi reply -
//  tức về mặt kỹ thuật KHÔNG THỂ single-step (không có thread port để gọi thread_get_state/
//  set_state cho debug register). Cách duy nhất còn lại để breakpoint không tự chạm lại ngay
//  là ĐỔI THẲNG PC - đúng cách cả v3 lẫn Monite đều làm.
//
//  TẠI SAO v2 (mmap 1 trang RX rồi tự chép lệnh máy vào đó) THẤT BẠI, ĐỔI SANG v3: v2 bị kill
//  ngay lần test đầu trên máy thật - EXC_BAD_ACCESS/KERN_PROTECTION_FAILURE, termination reason
//  "CODESIGNING: Invalid Page" (crash log FreeFire-2026-07-20-080014.ips), PC/FAR trùng khớp
//  100% với địa chỉ trang mmap. Đây là giới hạn cứng của iOS không jailbreak: KHÔNG THỂ thực thi
//  code trên bộ nhớ tự cấp phát runtime dù đã mprotect(PROT_EXEC) - cần entitlement JIT đặc biệt
//  mà 1 dylib chèn vào sideloaded app không có. v3 né hoàn toàn giới hạn này bằng cách dùng 1 HÀM
//  C THẬT, nằm trong chính __TEXT của dylib, được ký hợp lệ cùng lúc với cả gói lúc build - không
//  có bộ nhớ thực thi runtime nào được tạo ra cả. v4 giữ nguyên cách này.
//
//  BẢN V4 KHÁC V3 Ở ĐÂU (đối chiếu Monite.dylib qua Ghidra decompiler, xem MoniteAnalysis/):
//    1. Bảng đăng ký (address, trampolineTarget) tối đa HWBREAK_MAX_ENTRIES thay vì 1 địa chỉ
//       hardcode - hàm cài đặt giờ TỔNG QUÁT (hwbreakRegisterEntry), open() chỉ là 1 lần gọi
//       cụ thể của hàm tổng quát đó. Sẵn sàng thêm hàm khác (fopen/access/stat/lstat/openat -
//       hiện vẫn dùng fishhook) sau này mà không cần viết lại cơ chế lõi.
//    2. Handler KHÔNG còn tính redirect/sửa x0 - chỉ tra bảng + đổi PC. Việc tính
//       redirectAllTrafficPath() dời hẳn vào hwbreakOpenTrampoline (đã chạy như hàm C bình
//       thường, có toàn quyền gọi hàm khác, không còn ở ngữ cảnh Mach exception nhạy cảm).
//
//  AN TOÀN: có bước TỰ KIỂM TRA (self-test) trước khi dùng thật - tự gọi open() giả từ 1
//  thread riêng (không phải thread constructor, để nếu treo cũng không treo cả app), đợi
//  tối đa 500ms xem cơ chế có hoạt động không. KHÔNG hoạt động trong thời gian đó -> coi như
//  thất bại, dọn dẹp, để open() tiếp tục dùng fishhook như cũ (AssetRedirect.h tự thêm lại
//  "open" vào danh sách fishhook nếu hàm HWBreakHook_tryInstallForOpen() dưới đây trả về false).
//
//  GIAO THỨC MACH EXCEPTION: dùng code do `mig` (công cụ chuẩn của Apple) SINH RA từ
//  mach_exc.defs của chính SDK trên máy build (xem bước "Generate MIG exception server stubs"
//  trong .github/workflows/build.yml và Source/Includes/Generated/), KHÔNG tự viết tay layout
//  message. `mach_msg_server()` (hàm chuẩn của Apple) tự lo toàn bộ vòng nhận/dispatch/reply
//  đúng cách, chỉ cần mình cung cấp 3 hàm catch_mach_exception_raise* theo đúng chữ ký mà code
//  sinh ra yêu cầu - chỉ catch_mach_exception_raise_state là hàm thật (khớp EXCEPTION_STATE),
//  2 hàm còn lại chỉ là stub cho đủ liên kết (kernel không bao giờ gọi tới vì mình không đăng ký
//  behavior tương ứng). Monite.dylib dùng đúng cùng 1 kiểu MIG-generated dispatch (xác nhận qua
//  decompile FUN_000c1f34: bảng con trỏ hàm index theo msgh_id, đúng layout chuẩn của mig).
// ============================================================================
#import <mach/mach.h>
#import <mach/mach_error.h>
#import <mach/task.h>
#import <mach/thread_act.h>
#import <mach/exception_types.h>
#import <mach/thread_status.h>
#import <mach/arm/thread_status.h>
#import <pthread.h>
#import <dlfcn.h>
#import <unistd.h>
#include <atomic>

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

static mach_port_t g_hwbreakExcPort = MACH_PORT_NULL;

// ============================================================================
//  BẢNG ĐĂNG KÝ BREAKPOINT (v4) - đối chiếu Monite.dylib (decompile FUN_000c1578/FUN_000c14c8
//  qua Ghidra, xem MoniteAnalysis/) cho thấy họ KHÔNG hardcode 1 địa chỉ mà đăng ký hẳn 1 mảng
//  (address, trampoline) rồi tra bảng trong handler. 4 slot - khớp số lượng debug breakpoint
//  register (DBGBVRn/DBGBCRn, n=0..3) tối thiểu ARM64 đảm bảo có (ID_AA64DFR0_EL1.BRPs); hiện
//  chỉ dùng 1 slot (open()) nhưng cơ chế đã sẵn sàng đăng ký thêm hàm khác sau này.
// ============================================================================
#define HWBREAK_MAX_ENTRIES 4

struct HWBreakEntry {
    std::atomic<uint64_t> address{0};           // địa chỉ hàm thật đang bị theo dõi
    std::atomic<uint64_t> trampolineTarget{0};  // PC sẽ đổi sang khi chạm breakpoint này
};
static HWBreakEntry g_hwbreakEntries[HWBREAK_MAX_ENTRIES];
static std::atomic<int> g_hwbreakEntryCount{0};

// Atomic (không chỉ plain bool/uint64_t) - đọc từ NHIỀU thread khác nhau (exception handler,
// rearm-poll thread, self-test thread) sau khi được ghi 1 lần từ thread constructor; dùng
// atomic để loại bỏ hoàn toàn nghi vấn lý thuyết về visibility trên kiến trúc weakly-ordered
// như ARM64, thay vì dựa vào các syscall (pthread_create, mach_port_allocate...) tình cờ đóng
// vai trò memory barrier.
static std::atomic<bool> g_hwbreakActive{false};
static std::atomic<bool> g_hwbreakSelfTestPassed{false};
static std::atomic<bool> g_hwbreakSelfTestDone{false};
static std::atomic<bool> g_hwbreakSelfTestMode{false}; // true trong lúc tự kiểm tra - không redirect thật

// Đếm số lần open() THẬT (không tính self-test) đã bị chặn và xử lý xong - dùng để phát "nhịp
// tim" định kỳ trong log (xem hwbreakRearmPollThreadFn) mà không cần log từng lần open() một
// (quá dày, game/Unity có thể gọi open() hàng nghìn lần). Nếu số này còn tăng đến sát lúc app
// treo thì HWBreakHook vẫn đang hoạt động bình thường, không phải thủ phạm; nếu dừng tăng từ
// khá lâu trước khi treo thì nghi ngờ chính nó.
static std::atomic<unsigned long long> g_hwbreakInterceptCount{0};

// Đếm số lần hàm trampoline CHẠY XONG HOÀN TOÀN (open() thật đã return, kể cả self-test) - đối
// chiếu với g_hwbreakInterceptCount (đếm ngay LÚC HANDLER redirect PC, TRƯỚC khi trampoline
// chạy) cho biết chính xác có lần chặn nào bị kẹt/crash giữa chừng trong khoảng từ lúc handler
// đổi PC tới lúc trampoline thật sự chạy xong hay không.
static std::atomic<unsigned long long> g_hwbreakTrampolineCompletions{0};

// Bộ đệm xoay vòng cho path đã redirect - tránh cấp phát động (không an toàn để gọi malloc từ
// ngữ cảnh nhạy cảm) và tránh đụng độ giữa các lần gọi liên tiếp. Ở v4 vẫn cần buffer này vì
// hwbreakOpenTrampoline giờ tự gọi redirectAllTrafficPath() (không còn handler làm hộ), nhưng
// trampoline chạy như hàm C bình thường trên chính thread gọi open() nên an toàn hơn hẳn so với
// gọi trong ngữ cảnh Mach exception handler của bản v3 cũ.
//
// 8 CHỖ LÀ QUÁ ÍT: debug.log thực tế cho thấy tốc độ chặn open() có lúc lên tới ~500-2000
// lần/giây (lúc giải nén/load asset dồn dập). Tăng lên 512 slot (512 x 2KB = 1MB tĩnh, chấp
// nhận được) để có biên độ an toàn rộng hơn nhiều so với độ trễ round-trip thực tế.
#define HWBREAK_PATHBUF_SLOTS 512
static char g_hwbreakPathBufs[HWBREAK_PATHBUF_SLOTS][2048];
static std::atomic<unsigned int> g_hwbreakPathBufNext{0};

static inline char *hwbreakNextPathBuf() {
    unsigned int idx = g_hwbreakPathBufNext.fetch_add(1, std::memory_order_relaxed) % HWBREAK_PATHBUF_SLOTS;
    return g_hwbreakPathBufs[idx];
}

// ---- Đọc/ghi thanh ghi debug 1 thread - CHỈ dùng lúc arm/rearm (cài đặt breakpoint), KHÔNG
// còn dùng trong đường xử lý exception nữa (bản v3/v4 dùng trampoline, xem giải thích ở đầu file) ----
static inline bool hwbreakArmThreadState(thread_t thread, arm_debug_state64_t *dbg) {
    mach_msg_type_number_t count = ARM_DEBUG_STATE64_COUNT;
    return thread_get_state(thread, ARM_DEBUG_STATE64, (thread_state_t)dbg, &count) == KERN_SUCCESS;
}

static inline bool hwbreakSetThreadState(thread_t thread, arm_debug_state64_t *dbg) {
    return thread_set_state(thread, ARM_DEBUG_STATE64, (thread_state_t)dbg, ARM_DEBUG_STATE64_COUNT) == KERN_SUCCESS;
}

// BCR: BAS=1111 (khớp đủ 4 byte lệnh) | PMC=10 (chỉ EL0 - userspace) | E=1 (bật)
#define HWBREAK_BCR_VALUE ((0xFu << 5) | (0x2u << 1) | 0x1u)

// Khoá cho hwbreakArmBreakpointOnThread - v3/v4 chỉ còn rearm-poll thread VÀ self-test thread
// (arm chính mình lúc khởi động) cùng có thể gọi hàm này, không còn exception handler đụng vào
// debug register nữa (đã bỏ hẳn single-step) nên rủi ro tranh chấp thấp hơn nhiều bản v1 - vẫn
// giữ mutex cho chắc, chi phí không đáng kể.
static pthread_mutex_t g_hwbreakDbgMutex = PTHREAD_MUTEX_INITIALIZER;

// Arm TẤT CẢ entry đã đăng ký (g_hwbreakEntries[0..count-1]) lên 1 thread, mỗi entry chiếm 1
// slot DBGBVRn/DBGBCRn riêng (n = chỉ số entry) - khác bản v3 chỉ có 1 slot cố định.
static bool hwbreakArmBreakpointOnThread(thread_t thread) {
    pthread_mutex_lock(&g_hwbreakDbgMutex);
    arm_debug_state64_t dbg;
    memset(&dbg, 0, sizeof(dbg));
    bool ok = hwbreakArmThreadState(thread, &dbg);
    if (ok) {
        int count = g_hwbreakEntryCount.load(std::memory_order_relaxed);
        if (count > HWBREAK_MAX_ENTRIES) count = HWBREAK_MAX_ENTRIES;
        for (int i = 0; i < count; i++) {
            dbg.__bvr[i] = g_hwbreakEntries[i].address.load(std::memory_order_relaxed);
            dbg.__bcr[i] = HWBREAK_BCR_VALUE;
        }
        ok = hwbreakSetThreadState(thread, &dbg);
    }
    pthread_mutex_unlock(&g_hwbreakDbgMutex);
    return ok;
}

// Chỉ bật/tắt ĐÚNG 1 slot (dùng trong trampoline - xem giải thích ở hwbreakOpenTrampoline) mà
// không đụng tới các slot khác đang theo dõi hàm khác (nếu sau này đăng ký thêm).
static bool hwbreakSetSlotEnabledOnSelf(int slotIndex, bool enabled) {
    thread_t self = mach_thread_self();
    pthread_mutex_lock(&g_hwbreakDbgMutex);
    arm_debug_state64_t dbg;
    memset(&dbg, 0, sizeof(dbg));
    bool ok = hwbreakArmThreadState(self, &dbg);
    if (ok) {
        if (enabled) dbg.__bcr[slotIndex] |= 0x1u;
        else dbg.__bcr[slotIndex] &= ~0x1u;
        ok = hwbreakSetThreadState(self, &dbg);
    }
    pthread_mutex_unlock(&g_hwbreakDbgMutex);
    mach_port_deallocate(mach_task_self(), self); // mach_thread_self() cấp 1 send right mới mỗi lần gọi
    return ok;
}

// Thread chạy mach_msg_server() (xử lý exception) - KHÔNG BAO GIỜ được arm breakpoint, xem giải
// thích chi tiết ở hwbreakArmAllExistingThreads bên dưới.
static std::atomic<thread_t> g_hwbreakServerMachThread{MACH_PORT_NULL};

static void hwbreakArmAllExistingThreads() {
    thread_act_array_t threads = NULL;
    mach_msg_type_number_t threadCount = 0;
    if (task_threads(mach_task_self(), &threads, &threadCount) != KERN_SUCCESS) return;
    thread_t serverThread = g_hwbreakServerMachThread.load(std::memory_order_relaxed);
    for (mach_msg_type_number_t i = 0; i < threadCount; i++) {
        // BẮT BUỘC loại trừ chính thread server (chạy mach_msg_server(), xử lý exception) -
        // AssetRedirect.h's ar_extractOneEntryIfNeeded (gọi từ redirectAllTrafficPath, mà
        // hwbreakOpenTrampoline gọi TRÊN THREAD VỪA CHẠM BREAKPOINT, không phải thread server -
        // nhưng vẫn giữ loại trừ này làm lớp phòng thủ) có thể tự gọi open() thật để ghi file
        // giải nén ra đĩa. Đối chiếu Monite.dylib (decompile FUN_000c1578) cho thấy HỌ KHÔNG
        // loại trừ thread nào cả khi arm - nhưng đó là vì handler của họ (và giờ cả trampoline
        // của mình, sau khi dời logic redirect sang đây) không có đường nào gọi lại chính hàm
        // đang bị breakpoint từ BÊN TRONG lúc xử lý exception. Giữ loại trừ này lại (rẻ, không
        // tốn gì đáng kể) làm lưới an toàn phòng trường hợp code sau này lại đổi (VD: quay lại
        // lazy extraction) mà quên xét lại rủi ro deadlock - xem thêm phân tích trong
        // MoniteAnalysis/.
        if (threads[i] != serverThread) {
            hwbreakArmBreakpointOnThread(threads[i]);
        }
        mach_port_deallocate(mach_task_self(), threads[i]);
    }
    vm_deallocate(mach_task_self(), (vm_address_t)threads, threadCount * sizeof(thread_act_t));
}

// Unity/game tạo thread mới liên tục - debug register là TÀI NGUYÊN RIÊNG CỦA TỪNG THREAD,
// không có cách nào "set 1 lần cho cả tiến trình". Poll định kỳ để bắt thread mới thay vì cố
// hook điểm tạo thread (đơn giản hơn, đủ dùng cho bản thử nghiệm).
//
// CHỜ 1 NHỊP TRƯỚC KHI ARM LẦN ĐẦU: (giữ nguyên từ bản v1) app từng treo cứng ngay trong lúc
// dyld vẫn còn đang nạp nốt các thư viện còn lại (constructor này chạy TRƯỚC main()/
// UIApplicationMain). Không có cách nào biết chắc chắn dyld đã nạp xong hay chưa (không có API
// công khai), nên dùng 1 khoảng chờ cố định trước khi arm THẬT lần đầu tiên.
static void *hwbreakRearmPollThreadFn(void *ctx) {
    usleep(400 * 1000); // chờ 400ms trước lần arm đầu tiên - xem giải thích ở trên
    int tick = 0;
    while (g_hwbreakActive.load(std::memory_order_relaxed)) {
        hwbreakArmAllExistingThreads();
        // "Nhịp tim" mỗi ~1s (5 x 200ms) - bằng chứng trực tiếp HWBreakHook có còn đang xử lý
        // open() thật hay không ngay trước lúc app treo, không cần chờ đoán qua log của chỗ
        // khác. Xem giải thích ở g_hwbreakInterceptCount.
        tick++;
        if (tick % 5 == 0) {
            // 2 số này PHẢI luôn sát nhau (completions chỉ trễ hơn 1 chút do độ trễ round-trip
            // Mach RPC bình thường). Nếu "chặn" tăng nhưng "xong" đứng yên/tụt lại xa - bằng
            // chứng trực tiếp có thread đang kẹt/crash NGAY GIỮA lúc handler đổi PC và lúc
            // trampoline thực thi xong - khoanh vùng chính xác chỗ lỗi cho lần debug tiếp theo.
            DeltaVFS_debugLogf("HWBreakHook heartbeat: đã chặn=%llu đã xong=%llu",
                                g_hwbreakInterceptCount.load(std::memory_order_relaxed),
                                g_hwbreakTrampolineCompletions.load(std::memory_order_relaxed));
        }
        usleep(200 * 1000); // 200ms
    }
    return NULL;
}

// ============================================================================
//  TRAMPOLINE cho open() - HÀM C BIÊN DỊCH SẴN, KHÔNG cấp phát bộ nhớ thực thi lúc runtime.
//
//  v2 (mmap 1 trang RW rồi mprotect sang RX, chép lệnh gốc + LDR/BR vào đó) BỊ KILL NGAY LẦN
//  TEST ĐẦU: crash log FreeFire-2026-07-20-080014.ips cho thấy EXC_BAD_ACCESS/KERN_PROTECTION_
//  FAILURE, termination reason "CODESIGNING: Invalid Page", PC/FAR trùng khớp 100% với địa chỉ
//  trang mmap đó (vmRegionInfo xác nhận đây là VM_ALLOCATE do chính mình tạo). Đây là giới hạn
//  cứng của iOS không jailbreak: KHÔNG THỂ thực thi code trên bộ nhớ tự cấp phát lúc runtime dù
//  đã mprotect(PROT_EXEC) - cần entitlement JIT đặc biệt (com.apple.security.cs.allow-jit) mà 1
//  dylib chèn vào sideloaded app không có và không thể tự cấp cho mình.
//
//  CÁCH SỬA: dùng 1 HÀM C BÌNH THƯỜNG, biên dịch/ký cùng lúc với cả dylib (không phải bộ nhớ
//  cấp phát runtime) làm trampoline. Khi breakpoint chạm, handler đổi PC thẳng vào ĐỊA CHỈ HÀM
//  NÀY. Vì lúc CPU nhảy tới đây x0/x1/x2 đang giữ NGUYÊN giá trị gốc lúc gọi open() (handler v4
//  không còn sửa x0 nữa, xem giải thích lớn ở đầu file) - KHỚP CHÍNH XÁC với đối số 1 lệnh gọi
//  hàm C bình thường (chuẩn AAPCS64: x0-x2 = 3 đối số đầu) - nên hàm này tự tính redirect (gọi
//  redirectAllTrafficPath) RỒI MỚI gọi open() THẬT, coi như vừa được CPU "gọi" bình thường.
//  Chạy TRÊN CHÍNH thread vừa chạm breakpoint (không phải thread xử lý exception) nên errno
//  cũng được set đúng vào TLS của đúng thread đó. Khi hàm return, epilogue chuẩn của Clang tự
//  nhảy về đúng địa chỉ LR (chưa từng bị đụng tới) - trả kết quả về đúng chỗ gọi open() ban đầu.
static std::atomic<int (*)(const char *, int, ...)> g_hwbreakRealOpen{nullptr};
#define HWBREAK_OPEN_SLOT 0 // chỉ số entry của open() trong g_hwbreakEntries - dùng để bật/tắt đúng slot trong trampoline

__attribute__((noinline))
static int hwbreakOpenTrampoline(const char *path, int flags, int mode) {
    // Log chi tiết CHỈ trong self-test (xem giải thích ở catch_mach_exception_raise_state) -
    // nếu HÀM NÀY không hề chạy (không thấy dòng "trampoline bắt đầu" trong debug.log dù handler
    // đã log "đã đổi PC xong") thì chứng tỏ chính việc CPU nhảy PC sang địa chỉ này mới là chỗ
    // crash, không phải bản thân lệnh gọi open() thật bên trong.
    bool selfTestLog = g_hwbreakSelfTestMode.load(std::memory_order_relaxed);
    if (selfTestLog) DeltaVFS_debugLogf("HWBreakHook: [self-test] trampoline bắt đầu chạy, path=%s", path ? path : "(null)");

    // v4: tự tính redirect NGAY TẠI ĐÂY (không còn handler làm hộ - xem giải thích lớn ở đầu
    // file, đối chiếu Monite.dylib không hề sửa x0 trong handler của họ). Bỏ qua trong lúc
    // self-test (path là "/dev/null" giả, không cần/không nên redirect).
    const char *effectivePath = path;
    if (!selfTestLog && path) {
        const char *redirected = redirectAllTrafficPath(path);
        if (redirected && redirected != path) {
            char *buf = hwbreakNextPathBuf();
            strncpy(buf, redirected, 2047);
            buf[2047] = '\0';
            effectivePath = buf;
        }
    }
    if (!selfTestLog) g_hwbreakInterceptCount.fetch_add(1, std::memory_order_relaxed);

    // LỖI ĐÃ XÁC NHẬN QUA debug.log Ở BẢN v3 (không phải đoán): gọi thẳng fn() (địa chỉ open()
    // thật) ở đây mà KHÔNG tắt breakpoint trước sẽ lập tức CHẠM LẠI ĐÚNG breakpoint đó (open()
    // thật và địa chỉ đang bị theo dõi LÀ MỘT) - handler/trampoline gọi nhau vô hạn. Phải tắt
    // ĐÚNG SLOT của open() (HWBREAK_OPEN_SLOT) TRÊN CHÍNH THREAD NÀY trước khi gọi, rồi bật lại
    // ngay sau - luôn AN TOÀN vì đang thao tác debug register của chính thread đang chạy đoạn
    // code này. Chỉ tắt/bật ĐÚNG slot của open(), không đụng slot của hàm khác (nếu sau này có
    // đăng ký thêm) - khác bản v3 (chỉ có 1 slot nên không cần phân biệt).
    if (selfTestLog) DeltaVFS_debugLog("HWBreakHook: [self-test] sắp tắt breakpoint slot của open() trên chính thread này");
    bool disarmed = hwbreakSetSlotEnabledOnSelf(HWBREAK_OPEN_SLOT, false);

    int (*fn)(const char *, int, ...) = g_hwbreakRealOpen.load(std::memory_order_relaxed);
    if (selfTestLog) DeltaVFS_debugLogf("HWBreakHook: [self-test] trampoline sắp gọi open() thật (đã tắt breakpoint), fn=%p", (void *)fn);

    int result = fn ? fn(effectivePath, flags, mode) : -1;
    if (selfTestLog) DeltaVFS_debugLogf("HWBreakHook: [self-test] trampoline gọi open() thật XONG, result=%d errno=%d", result, errno);

    if (disarmed) {
        hwbreakSetSlotEnabledOnSelf(HWBREAK_OPEN_SLOT, true); // bật lại cho lần open() tiếp theo
    }

    g_hwbreakTrampolineCompletions.fetch_add(1, std::memory_order_relaxed);
    return result;
}

// ---- 3 hàm callback mà mach_exc_server() (demux do mig sinh ra) gọi ngược lại - PHẢI đúng tên
// + đúng chữ ký mà Source/Includes/Generated/mach_exc_server.h khai báo (extern "C", linkage
// ngoài, vì mach_excServer.c là 1 translation unit RIÊNG chỉ tham chiếu extern tới các hàm này).
// Đăng ký task_set_exception_ports với behavior=EXCEPTION_STATE (không phải EXCEPTION_DEFAULT
// như bản v1) nên kernel CHỈ BAO GIỜ gọi catch_mach_exception_raise_state - 2 hàm còn lại vẫn
// PHẢI định nghĩa (dù trả KERN_FAILURE) vì mach_exc_server tham chiếu extern tới cả 3, thiếu 1
// cái là lỗi link ngay lúc build (an toàn - phát hiện ở CI, không phải lúc chạy trên máy thật).
extern "C" kern_return_t catch_mach_exception_raise(
    mach_port_t exception_port,
    mach_port_t thread,
    mach_port_t task,
    exception_type_t exception,
    mach_exception_data_t code,
    mach_msg_type_number_t codeCnt)
{
    // Không dùng - đã đăng ký EXCEPTION_STATE nên kernel không bao giờ gọi biến thể này (biến
    // thể này ứng với EXCEPTION_DEFAULT, có kèm thread/task port - đã bỏ ở bản v2). Chỉ tồn tại
    // để thoả mãn liên kết với mach_exc_server().
    if (MACH_PORT_VALID(thread)) mach_port_deallocate(mach_task_self(), thread);
    if (MACH_PORT_VALID(task)) mach_port_deallocate(mach_task_self(), task);
    return KERN_FAILURE;
}

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
    // ---- HÀM XỬ LÝ THẬT DUY NHẤT - chạy mỗi lần 1 trong các entry đã đăng ký bị chạm
    // breakpoint (self-test lẫn thật). v4: TRA BẢNG thay vì so 1 địa chỉ hardcode, và KHÔNG
    // đụng tới x0/x1/x2 nữa - đúng cách Monite.dylib làm (xác nhận qua decompile
    // FUN_000c14c8, xem MoniteAnalysis/) - toàn bộ logic ứng dụng (redirect path) dời sang
    // hwbreakOpenTrampoline, hàm này chỉ còn đúng 1 việc: tìm entry khớp PC, copy state, sửa
    // field PC. Càng ít việc trong ngữ cảnh Mach exception handler càng ít rủi ro. ----
    // EXCEPTION_STATE KHÔNG cấp thread/task port - kernel chỉ đưa thẳng state hiện tại
    // (old_state) và mong nhận lại state mới (new_state) để áp dụng NGUYÊN TỬ ngay khi reply,
    // không cần (và không thể) gọi thread_get_state/thread_set_state riêng như bản v1.
    memcpy(new_state, old_state, (size_t)old_stateCnt * sizeof(natural_t));
    *new_stateCnt = old_stateCnt;
    if (flavor) *flavor = ARM_THREAD_STATE64;

    const arm_thread_state64_t *inState = (const arm_thread_state64_t *)old_state;
    arm_thread_state64_t *outState = (arm_thread_state64_t *)new_state;

    // Lưu ý: field pc trong arm_thread_state64_t là opaque (có thể mang PAC) trên SDK thật -
    // PHẢI dùng arm_thread_state64_get_pc()/set_pc_fptr() thay vì đọc/ghi field thô.
    uint64_t pc = (uint64_t)arm_thread_state64_get_pc(*inState);

    int count = g_hwbreakEntryCount.load(std::memory_order_relaxed);
    if (count > HWBREAK_MAX_ENTRIES) count = HWBREAK_MAX_ENTRIES;
    int matchedIdx = -1;
    for (int i = 0; i < count; i++) {
        if (g_hwbreakEntries[i].address.load(std::memory_order_relaxed) == pc) { matchedIdx = i; break; }
    }
    if (matchedIdx < 0) {
        // Không khớp entry nào đã đăng ký (không nên xảy ra - chỉ arm đúng các địa chỉ đã đăng
        // ký). AN TOÀN HƠN là để nguyên state (breakpoint có thể tự chạm lại) còn hơn nhảy PC
        // vào 1 địa chỉ chưa chắc hợp lệ.
        return KERN_SUCCESS;
    }
    uint64_t trampolineAddr = g_hwbreakEntries[matchedIdx].trampolineTarget.load(std::memory_order_relaxed);
    if (trampolineAddr == 0) return KERN_SUCCESS;

    bool selfTestMode = g_hwbreakSelfTestMode.load(std::memory_order_relaxed);
    if (selfTestMode) {
        // Tự kiểm tra: chỉ cần biết breakpoint có chạm được không, KHÔNG đụng gì tới đối số -
        // vẫn phải đổi PC sang trampoline bên dưới, nếu không chính thread tự-kiểm-tra (gọi
        // open("/dev/null",...) thật) sẽ bị breakpoint chặn lại ngay lập tức, không bao giờ
        // hoàn tất được lệnh gọi thử.
        DeltaVFS_debugLog("HWBreakHook: [self-test] handler reached, breakpoint chạm đúng địa chỉ đã đăng ký");
        g_hwbreakSelfTestPassed.store(true, std::memory_order_relaxed);
    }

    // Dùng biến thể "_presigned_fptr" thay vì "_set_pc_fptr" thường - đã kiểm tra header thật
    // của SDK (Xcode 26.5, in ra qua bước CI riêng): nhánh có ptrauth dùng
    // ptrauth_auth_and_resign() cho "_set_pc_fptr", đòi hỏi con trỏ đưa vào PHẢI đã được ký PAC
    // hợp lệ từ trước - trampolineAddr chỉ là địa chỉ hàm thô (build ARCHS=arm64 thường, không
    // tự ký PAC khi lấy địa chỉ hàm), nếu bản build chọn đúng nhánh ptrauth thì "resign" 1 con
    // trỏ chưa từng ký sẽ auth thất bại ngay lập tức -> crash. "_presigned_fptr" bỏ qua hẳn
    // bước auth+resign, chỉ gán thẳng giá trị - an toàn ở MỌI nhánh.
    if (selfTestMode) {
        DeltaVFS_debugLogf("HWBreakHook: [self-test] sắp đổi PC sang trampoline addr=0x%llx", (unsigned long long)trampolineAddr);
    }
    arm_thread_state64_set_pc_presigned_fptr(*outState, (void *)trampolineAddr);
    if (selfTestMode) {
        DeltaVFS_debugLog("HWBreakHook: [self-test] đã đổi PC xong, chuẩn bị reply KERN_SUCCESS");
    }
    return KERN_SUCCESS;
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
    // Không dùng - lý do như catch_mach_exception_raise ở trên (biến thể này ứng với
    // EXCEPTION_STATE_IDENTITY, không đăng ký).
    if (MACH_PORT_VALID(thread)) mach_port_deallocate(mach_task_self(), thread);
    if (MACH_PORT_VALID(task)) mach_port_deallocate(mach_task_self(), task);
    return KERN_FAILURE;
}

static void *hwbreakServerThreadFn(void *ctx) {
    // mach_msg_server() tự lo TOÀN BỘ vòng nhận/dispatch(mach_exc_server)/reply đúng giao thức
    // MIG chuẩn. Hàm này block vĩnh viễn (như vòng while(true) cũ), chỉ return nếu có lỗi Mach
    // nghiêm trọng không tự phục hồi được - không mong đợi xảy ra.
    mach_msg_return_t mr = mach_msg_server(mach_exc_server, 4096, g_hwbreakExcPort, MACH_MSG_OPTION_NONE);
    DeltaVFS_debugLogf("HWBreakHook: mach_msg_server() thoát bất thường mr=0x%x (không nên xảy ra)", mr);
    return NULL;
}

// Thread riêng để tự gọi open() thử - KHÔNG chạy trên thread constructor, để nếu cơ chế
// treo thì chỉ thread thử nghiệm này bị kẹt, không kéo theo cả app không mở được.
static void *hwbreakSelfTestCallerThreadFn(void *ctx) {
    DeltaVFS_debugLog("HWBreakHook: [self-test] thread bắt đầu, chuẩn bị arm breakpoint cho chính mình");
    bool armed = hwbreakArmBreakpointOnThread(mach_thread_self());
    DeltaVFS_debugLogf("HWBreakHook: [self-test] arm breakpoint cho chính mình: %s", armed ? "OK" : "THẤT BẠI");

    int (*realOpen)(const char *, int, ...) = (int (*)(const char *, int, ...))dlsym(RTLD_DEFAULT, "open");
    if (realOpen) {
        // Với bản trampoline (v2/v3/v4), lệnh gọi này giờ THẬT SỰ hoàn tất bình thường (không
        // còn kẹt giữa chừng chờ single-step) - fd trả về hợp lệ, đóng lại được.
        DeltaVFS_debugLog("HWBreakHook: [self-test] chuẩn bị gọi open(\"/dev/null\",...) thật - lệnh gọi này sẽ chạm breakpoint");
        int fd = realOpen("/dev/null", O_RDONLY);
        DeltaVFS_debugLogf("HWBreakHook: [self-test] open(\"/dev/null\",...) ĐÃ RETURN, fd=%d errno=%d", fd, errno);
        if (fd >= 0) close(fd);
    } else {
        DeltaVFS_debugLog("HWBreakHook: [self-test] dlsym(open) thất bại trong chính test thread");
    }
    g_hwbreakSelfTestDone.store(true, std::memory_order_relaxed);
    DeltaVFS_debugLog("HWBreakHook: [self-test] thread kết thúc, đã set Done=true");
    return NULL;
}

// Đăng ký 1 entry vào bảng (address, trampolineTarget) - hàm TỔNG QUÁT, dùng chung cho mọi hàm
// muốn hook qua hardware breakpoint sau này (hiện chỉ open() gọi hàm này). Trả về chỉ số slot
// nếu đăng ký thành công, -1 nếu bảng đầy.
static int hwbreakRegisterEntry(uint64_t address, uint64_t trampolineTarget) {
    int idx = g_hwbreakEntryCount.load(std::memory_order_relaxed);
    if (idx >= HWBREAK_MAX_ENTRIES) return -1;
    g_hwbreakEntries[idx].address.store(address, std::memory_order_relaxed);
    g_hwbreakEntries[idx].trampolineTarget.store(trampolineTarget, std::memory_order_relaxed);
    g_hwbreakEntryCount.store(idx + 1, std::memory_order_release);
    return idx;
}

// Trả về true nếu breakpoint hoạt động đúng và AN TOÀN để dùng thật cho open() - caller
// (AssetRedirect.h) chỉ nên BỎ "open" ra khỏi danh sách fishhook nếu hàm này trả true.
inline bool HWBreakHook_tryInstallForOpen() {
    DeltaVFS_debugLog("HWBreakHook: bắt đầu tự kiểm tra hardware breakpoint cho open()");

    void *openSym = dlsym(RTLD_DEFAULT, "open");
    if (!openSym) {
        DeltaVFS_debugLog("HWBreakHook: dlsym(open) thất bại, huỷ - dùng fishhook như cũ");
        return false;
    }
    DeltaVFS_debugLogf("HWBreakHook: dlsym(open) OK, addr=0x%llx", (unsigned long long)(uint64_t)openSym);
    g_hwbreakRealOpen.store((int (*)(const char *, int, ...))openSym, std::memory_order_relaxed);

    int slot = hwbreakRegisterEntry((uint64_t)openSym, (uint64_t)(void *)hwbreakOpenTrampoline);
    if (slot != HWBREAK_OPEN_SLOT) {
        // Phải luôn là slot 0 vì hwbreakOpenTrampoline hardcode HWBREAK_OPEN_SLOT khi tắt/bật
        // breakpoint của chính nó - đăng ký open() TRƯỚC bất kỳ entry nào khác trong constructor
        // đảm bảo điều này. Không nên xảy ra vì đây là entry đầu tiên đăng ký, nhưng kiểm tra
        // lại cho chắc thay vì im lặng dùng sai slot.
        DeltaVFS_debugLogf("HWBreakHook: đăng ký open() vào slot %d (mong đợi %d), huỷ", slot, HWBREAK_OPEN_SLOT);
        return false;
    }
    DeltaVFS_debugLogf("HWBreakHook: đăng ký open() vào slot %d, trampoline addr=0x%llx", slot, (unsigned long long)(uint64_t)(void *)hwbreakOpenTrampoline);

    kern_return_t kr = mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &g_hwbreakExcPort);
    if (kr != KERN_SUCCESS) {
        DeltaVFS_debugLogf("HWBreakHook: mach_port_allocate thất bại kr=%d, huỷ", kr);
        return false;
    }
    mach_port_insert_right(mach_task_self(), g_hwbreakExcPort, g_hwbreakExcPort, MACH_MSG_TYPE_MAKE_SEND);
    DeltaVFS_debugLog("HWBreakHook: mach_port_allocate + insert_right OK");

    // EXCEPTION_STATE (không phải EXCEPTION_DEFAULT của bản v1) - xem giải thích lớn ở đầu file.
    kr = task_set_exception_ports(mach_task_self(), EXC_MASK_BREAKPOINT, g_hwbreakExcPort,
                                   (exception_behavior_t)(EXCEPTION_STATE | MACH_EXCEPTION_CODES),
                                   ARM_THREAD_STATE64);
    if (kr != KERN_SUCCESS) {
        DeltaVFS_debugLogf("HWBreakHook: task_set_exception_ports thất bại kr=%d, huỷ", kr);
        mach_port_deallocate(mach_task_self(), g_hwbreakExcPort);
        g_hwbreakExcPort = MACH_PORT_NULL;
        return false;
    }
    DeltaVFS_debugLog("HWBreakHook: task_set_exception_ports OK (EXCEPTION_STATE|MACH_EXCEPTION_CODES)");

    pthread_t serverThread;
    if (pthread_create(&serverThread, NULL, hwbreakServerThreadFn, NULL) != 0) {
        DeltaVFS_debugLog("HWBreakHook: tạo server thread thất bại, huỷ");
        return false;
    }
    // Ghi lại Mach thread port của CHÍNH thread server này để hwbreakArmAllExistingThreads loại
    // trừ vĩnh viễn - xem giải thích chi tiết ở đó.
    g_hwbreakServerMachThread.store(pthread_mach_thread_np(serverThread), std::memory_order_relaxed);
    pthread_detach(serverThread);
    DeltaVFS_debugLog("HWBreakHook: server thread đã tạo, chuẩn bị tự kiểm tra");

    // ---- TỰ KIỂM TRA: gọi open() thử từ 1 thread riêng, đợi tối đa 500ms ----
    g_hwbreakSelfTestMode.store(true, std::memory_order_relaxed);
    pthread_t testThread;
    if (pthread_create(&testThread, NULL, hwbreakSelfTestCallerThreadFn, NULL) != 0) {
        DeltaVFS_debugLog("HWBreakHook: tạo test thread thất bại, huỷ");
        return false;
    }
    pthread_detach(testThread);
    DeltaVFS_debugLog("HWBreakHook: self-test thread đã tạo, bắt đầu chờ (tối đa 500ms)");

    // Chờ đủ 2 mốc, KHÔNG chỉ g_hwbreakSelfTestPassed (chỉ chứng minh breakpoint CHẠM được -
    // set NGAY TRONG handler, TRƯỚC khi trampoline chạy xong) mà còn phải chờ
    // g_hwbreakSelfTestDone (set ở CUỐI hwbreakSelfTestCallerThreadFn, SAU KHI open() thật đã
    // return và fd đã đóng) - phát hiện đúng trường hợp trampoline bị treo (timeout, coi như
    // thất bại, an toàn) - KHÔNG giúp gì nếu trampoline CRASH thẳng (tiến trình chết ngay tại
    // đây, không có gì để bắt - đó là lý do phải kiểm tra kỹ ownership của
    // g_hwbreakTrampolineCompletions ở heartbeat).
    int waited = 0;
    while (!g_hwbreakSelfTestDone.load(std::memory_order_relaxed) && waited < 500) {
        usleep(10 * 1000);
        waited += 10;
    }
    g_hwbreakSelfTestMode.store(false, std::memory_order_relaxed);

    bool hitBreakpoint = g_hwbreakSelfTestPassed.load(std::memory_order_relaxed);
    bool callCompleted = g_hwbreakSelfTestDone.load(std::memory_order_relaxed);
    bool passed = hitBreakpoint && callCompleted;
    DeltaVFS_debugLogf("HWBreakHook: tự kiểm tra %s sau %dms (chạm breakpoint=%d, gọi xong=%d)",
                        passed ? "THÀNH CÔNG" : "THẤT BẠI", waited, hitBreakpoint, callCompleted);

    if (!passed) {
        // KHÔNG dùng cho open() thật - nhưng KHÔNG thu hồi exception port/server thread vì
        // test thread có thể vẫn đang kẹt giữa chừng - gỡ exception port lúc này có thể khiến
        // test thread đó chết theo kiểu khó lường hơn là cứ để nó tự kẹt (vô hại, chỉ 1 thread
        // mồ côi, không ảnh hưởng phần còn lại của app).
        return false;
    }

    // KHÔNG arm tất cả thread (kể cả main thread) ngay tại đây - vẫn đang chạy trong constructor,
    // tức là TRƯỚC main()/UIApplicationMain, đúng lúc dyld có thể vẫn đang nạp nốt các thư viện
    // còn lại. hwbreakRearmPollThreadFn (spawn ngay dưới đây) tự chờ 1 nhịp trước khi thực hiện
    // lần arm đầu tiên - xem giải thích chi tiết ở khai báo hàm đó.
    g_hwbreakActive.store(true, std::memory_order_relaxed);

    pthread_t rearmThread;
    if (pthread_create(&rearmThread, NULL, hwbreakRearmPollThreadFn, NULL) == 0) {
        pthread_detach(rearmThread);
    }

    DeltaVFS_debugLog("HWBreakHook: ĐÃ KÍCH HOẠT cho open() - fishhook sẽ KHÔNG hook open() nữa");
    return true;
}
