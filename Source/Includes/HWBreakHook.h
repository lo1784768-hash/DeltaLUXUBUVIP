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
//  CƠ CHẾ (bản v3 - TRAMPOLINE LÀ 1 HÀM C BIÊN DỊCH SẴN, KHÔNG single-step, KHÔNG cấp phát bộ
//  nhớ thực thi runtime): đặt breakpoint CPU (ARM64 DBGBCRn/DBGBVRn qua thread_set_state) ngay
//  tại địa chỉ hàm open() thật. Khi CPU chạm tới, kernel gửi Mach exception EXC_BREAKPOINT tới
//  port mình đăng ký VỚI BEHAVIOR = EXCEPTION_STATE (xem giải thích dưới) - handler sửa thẳng x0
//  (path đã redirect) VÀ pc (trỏ sang hwbreakOpenTrampoline - 1 hàm C bình thường, biên dịch/ký
//  sẵn cùng dylib) ngay trong state trả về, kernel áp dụng nguyên tử lúc reply. Vì x0/x1/x2 lúc
//  đó đang giữ đúng path/flags/mode - khớp thẳng với 3 đối số đầu 1 lệnh gọi hàm C chuẩn AAPCS64
//  - hàm trampoline chỉ cần gọi lại open() thật với đúng các đối số đó, rồi return bình thường
//  (Clang tự sinh epilogue nhảy về đúng LR, chưa từng bị đụng tới - trả kết quả đúng chỗ gọi
//  open() ban đầu).
//
//  TẠI SAO ĐỔI TỪ SINGLE-STEP (v1) SANG TRAMPOLINE: bản v1 dùng behavior=EXCEPTION_DEFAULT (có
//  thread/task port), sau khi sửa x0 thì tắt breakpoint + bật single-step (MDSCR_EL1.SS) qua 1
//  cặp thread_get_state/thread_set_state RIÊNG, cho đúng 1 lệnh gốc chạy, bắt exception single-
//  step tiếp theo, rồi tắt single-step + bật lại breakpoint. Qua nhiều vòng test trên máy thật,
//  app luôn treo cứng sau 1 số lần chặn open() không cố định (75, 178, 889, 1354...) - "nhịp
//  tim" độc lập ở main thread (CADisplayLink) xác nhận CHÍNH main thread bị treo thật. Đổi hẳn
//  sang code MIG chuẩn của Apple (thay bộ khung tự viết tay) vẫn bị treo Y HỆT - loại trừ khả
//  năng lỗi nằm ở giao thức message, dồn nghi vấn về đúng chỗ single-step/debug-register dance.
//
//  Soi kỹ Monite.dylib (dùng capstone + tự parse chained-fixups để dò call site) phát hiện: nó
//  đăng ký exception port với behavior=EXCEPTION_STATE (KHÔNG PHẢI EXCEPTION_DEFAULT), và
//  _thread_set_state chỉ xuất hiện ĐÚNG 1 lần trong cả binary - ở đúng chỗ cài breakpoint ban
//  đầu, KHÔNG hề gọi trong lúc xử lý exception thật. Với EXCEPTION_STATE, callback không hề nhận
//  được thread/task port - CHỈ nhận/trả thẳng state thanh ghi (bao gồm cả PC) qua chính request/
//  reply, kernel tự áp dụng khi reply - tức về mặt kỹ thuật KHÔNG THỂ single-step (không có
//  thread port để gọi thread_get_state/set_state cho debug register). Cách duy nhất còn lại để
//  breakpoint không tự chạm lại ngay là ĐỔI THẲNG PC.
//
//  TẠI SAO v2 (mmap 1 trang RX rồi tự chép lệnh máy vào đó) THẤT BẠI, ĐỔI SANG v3: v2 bị kill
//  ngay lần test đầu trên máy thật - EXC_BAD_ACCESS/KERN_PROTECTION_FAILURE, termination reason
//  "CODESIGNING: Invalid Page" (crash log FreeFire-2026-07-20-080014.ips), PC/FAR trùng khớp
//  100% với địa chỉ trang mmap. Đây là giới hạn cứng của iOS không jailbreak: KHÔNG THỂ thực thi
//  code trên bộ nhớ tự cấp phát runtime dù đã mprotect(PROT_EXEC) - cần entitlement JIT đặc biệt
//  mà 1 dylib chèn vào sideloaded app không có. v3 né hoàn toàn giới hạn này bằng cách dùng 1 HÀM
//  C THẬT, nằm trong chính __TEXT của dylib, được ký hợp lệ cùng lúc với cả gói lúc build - không
//  có bộ nhớ thực thi runtime nào được tạo ra cả.
//
//  Bản v3 này bỏ hẳn: mọi thao tác đọc/ghi ARM_DEBUG_STATE64 trong đường xử lý mỗi lần chặn,
//  mutex giữa exception handler và rearm-poll thread (không còn 2 bên cùng đụng debug register
//  nữa - chỉ còn rearm-poll thread chạm tới), toàn bộ logic phân biệt "chạm breakpoint thật" với
//  "vừa single-step xong" (không còn khái niệm single-step, MỌI lần chặn đều xử lý y hệt nhau),
//  VÀ mọi mmap/mprotect/sys_icache_invalidate (không còn bộ nhớ thực thi runtime nào nữa).
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
//  behavior tương ứng).
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
// Atomic (không chỉ plain uint64_t như trước) - đọc từ NHIỀU thread khác nhau (exception
// handler, rearm-poll thread, self-test thread) sau khi được ghi 1 lần từ thread constructor;
// dùng atomic để loại bỏ hoàn toàn nghi vấn lý thuyết về visibility trên kiến trúc weakly-
// ordered như ARM64, thay vì dựa vào các syscall (pthread_create, mach_port_allocate...) tình
// cờ đóng vai trò memory barrier.
static std::atomic<uint64_t> g_hwbreakOpenAddr{0};
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

// Bộ đệm xoay vòng cho path đã redirect - tránh cấp phát động trong exception handler (không
// an toàn để gọi malloc từ ngữ cảnh này) và tránh đụng độ giữa các lần gọi liên tiếp.
//
// 8 CHỖ LÀ QUÁ ÍT: debug.log thực tế cho thấy tốc độ chặn open() có lúc lên tới ~500-2000
// lần/giây (lúc giải nén/load asset dồn dập). Thread bị chặn breakpoint chỉ THẬT SỰ đọc x0 (đọc
// nội dung buffer) sau khi mình reply xong VÀ kernel lập lịch cho nó chạy tiếp - có độ trễ thật
// (dù nhỏ). Với tốc độ trên, chỉ cần trễ vài trăm micro-giây là đủ để 8 slot bị ghi đè vòng qua
// trước khi thread gốc kịp đọc - nó sẽ mở NHẦM file hoặc đọc phải chuỗi đang bị ghi dở dang (dữ
// liệu rác). Tăng lên 512 slot (512 x 2KB = 1MB tĩnh, chấp nhận được) để có biên độ an toàn rộng
// hơn nhiều so với độ trễ round-trip thực tế của 1 lần exception.
#define HWBREAK_PATHBUF_SLOTS 512
static char g_hwbreakPathBufs[HWBREAK_PATHBUF_SLOTS][2048];
static std::atomic<unsigned int> g_hwbreakPathBufNext{0};

static inline char *hwbreakNextPathBuf() {
    unsigned int idx = g_hwbreakPathBufNext.fetch_add(1, std::memory_order_relaxed) % HWBREAK_PATHBUF_SLOTS;
    return g_hwbreakPathBufs[idx];
}

// ---- Đọc/ghi thanh ghi debug 1 thread - CHỈ dùng lúc arm/rearm (cài đặt breakpoint), KHÔNG
// còn dùng trong đường xử lý exception nữa (bản v3 dùng trampoline, xem giải thích ở đầu file) ----
static inline bool hwbreakArmThreadState(thread_t thread, arm_debug_state64_t *dbg) {
    mach_msg_type_number_t count = ARM_DEBUG_STATE64_COUNT;
    return thread_get_state(thread, ARM_DEBUG_STATE64, (thread_state_t)dbg, &count) == KERN_SUCCESS;
}

static inline bool hwbreakSetThreadState(thread_t thread, arm_debug_state64_t *dbg) {
    return thread_set_state(thread, ARM_DEBUG_STATE64, (thread_state_t)dbg, ARM_DEBUG_STATE64_COUNT) == KERN_SUCCESS;
}

// BCR: BAS=1111 (khớp đủ 4 byte lệnh) | PMC=10 (chỉ EL0 - userspace) | E=1 (bật)
#define HWBREAK_BCR_VALUE ((0xFu << 5) | (0x2u << 1) | 0x1u)

// Khoá cho hwbreakArmBreakpointOnThread - bản v3 chỉ còn rearm-poll thread VÀ self-test thread
// (arm chính mình lúc khởi động) cùng có thể gọi hàm này, không còn exception handler đụng vào
// debug register nữa (đã bỏ hẳn single-step) nên rủi ro tranh chấp thấp hơn nhiều bản v1 - vẫn
// giữ mutex cho chắc, chi phí không đáng kể.
static pthread_mutex_t g_hwbreakDbgMutex = PTHREAD_MUTEX_INITIALIZER;

static bool hwbreakArmBreakpointOnThread(thread_t thread, uint64_t addr) {
    pthread_mutex_lock(&g_hwbreakDbgMutex);
    arm_debug_state64_t dbg;
    memset(&dbg, 0, sizeof(dbg));
    bool ok = hwbreakArmThreadState(thread, &dbg);
    if (ok) {
        dbg.__bvr[0] = addr;
        dbg.__bcr[0] = HWBREAK_BCR_VALUE;
        ok = hwbreakSetThreadState(thread, &dbg);
    }
    pthread_mutex_unlock(&g_hwbreakDbgMutex);
    return ok;
}

// Thread chạy mach_msg_server() (xử lý exception) - KHÔNG BAO GIỜ được arm breakpoint, xem giải
// thích chi tiết ở hwbreakArmAllExistingThreads bên dưới.
static std::atomic<thread_t> g_hwbreakServerMachThread{MACH_PORT_NULL};

static void hwbreakArmAllExistingThreads(uint64_t addr) {
    thread_act_array_t threads = NULL;
    mach_msg_type_number_t threadCount = 0;
    if (task_threads(mach_task_self(), &threads, &threadCount) != KERN_SUCCESS) return;
    thread_t serverThread = g_hwbreakServerMachThread.load(std::memory_order_relaxed);
    for (mach_msg_type_number_t i = 0; i < threadCount; i++) {
        // BẮT BUỘC loại trừ chính thread server (chạy mach_msg_server(), xử lý exception) -
        // AssetRedirect.h's ar_extractOneEntryIfNeeded (gọi từ redirectAllTrafficPath, mà
        // catch_mach_exception_raise_state gọi trực tiếp trên CHÍNH thread server này) có thể tự
        // gọi open() thật để ghi file giải nén ra đĩa. Nếu thread server cũng bị arm breakpoint,
        // lệnh gọi open() đó sẽ tự sinh ra 1 exception MỚI cần chính thread server xử lý - nhưng
        // nó đang bị kernel treo (suspended) chờ reply cho exception HIỆN TẠI, không thể đồng
        // thời quay lại vòng lặp mach_msg_server() để nhận exception MỚI của chính mình - TỰ
        // KHOÁ CHẾT (deadlock) vĩnh viễn, không crash, không log gì thêm - khớp chính xác với
        // debug.log: mọi lần khởi động đều dừng lại ngay sau "ĐÃ KÍCH HOẠT", im bặt hoàn toàn.
        if (threads[i] != serverThread) {
            hwbreakArmBreakpointOnThread(threads[i], addr);
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
        uint64_t addr = g_hwbreakOpenAddr.load(std::memory_order_relaxed);
        if (addr != 0) {
            hwbreakArmAllExistingThreads(addr);
        }
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
//  TRAMPOLINE v3 - HÀM C BIÊN DỊCH SẴN, KHÔNG cấp phát bộ nhớ thực thi lúc runtime nữa.
//
//  v2 (mmap 1 trang RW rồi mprotect sang RX, chép lệnh gốc + LDR/BR vào đó) BỊ KILL NGAY LẦN
//  TEST ĐẦU: crash log FreeFire-2026-07-20-080014.ips cho thấy EXC_BAD_ACCESS/KERN_PROTECTION_
//  FAILURE, termination reason "CODESIGNING: Invalid Page", PC/FAR trùng khớp 100% với địa chỉ
//  trang mmap đó (vmRegionInfo xác nhận đây là VM_ALLOCATE do chính mình tạo). Đây là giới hạn
//  cứng của iOS không jailbreak: KHÔNG THỂ thực thi code trên bộ nhớ tự cấp phát lúc runtime dù
//  đã mprotect(PROT_EXEC) - cần entitlement JIT đặc biệt (com.apple.security.cs.allow-jit) mà 1
//  dylib chèn vào sideloaded app không có và không thể tự cấp cho mình. Đây CHÍNH LÀ lý do gốc
//  không thể patch code tĩnh trực tiếp nữa (đã ghi ở memory mshookfunction-shared-cache-limit) -
//  hoá ra cũng chặn luôn cả cách "tạo trampoline runtime" tưởng là né được giới hạn đó.
//
//  CÁCH SỬA: dùng 1 HÀM C BÌNH THƯỜNG, biên dịch/ký cùng lúc với cả dylib (không phải bộ nhớ
//  cấp phát runtime) làm trampoline. Khi breakpoint chạm, đổi PC thẳng vào ĐỊA CHỈ HÀM NÀY (đúng
//  lệnh đầu tiên của nó) thay vì 1 trang RX tự dựng. Vì lúc CPU nhảy tới đây x0/x1/x2 đang giữ
//  đúng nguyên giá trị handler vừa set (path đã redirect nếu có, flags/mode giữ nguyên) - KHỚP
//  CHÍNH XÁC với đối số 1 lệnh gọi hàm C bình thường (chuẩn AAPCS64: x0-x2 = 3 đối số đầu) - nên
//  hàm này chỉ cần gọi lại open() THẬT (qua con trỏ đã dlsym) với đúng x0/x1/x2 đó, coi như vừa
//  được CPU "gọi" bình thường. Chạy TRÊN CHÍNH thread vừa chạm breakpoint (không phải thread xử
//  lý exception) nên errno cũng được set đúng vào TLS của đúng thread đó - không có vấn đề errno
//  bị lẫn sang thread khác. Khi hàm return, epilogue chuẩn của Clang tự nhảy về đúng địa chỉ
//  LR (chưa từng bị đụng tới) - tức trả kết quả về đúng chỗ gọi open() ban đầu, y hệt như open()
//  thật tự chạy xong và return - không cần biết/replay lệnh gốc bị breakpoint đè lên nữa.
static std::atomic<int (*)(const char *, int, ...)> g_hwbreakRealOpen{nullptr};
static std::atomic<uint64_t> g_hwbreakTrampolineAddr{0};

// Đối chiếu hook/hook.c (1 tool hook khác, "TLOI") và cụm exception-port của Monite.dylib (xem
// MoniteAnalysis/README.md mục 3b) cho thấy CẢ 2 không hề có bước tắt/bật breakpoint trong lúc xử
// lý - lý do là handler của họ chỉ đổi PC rồi return ngay, KHÔNG bao giờ tự gọi lại đúng địa chỉ
// đang bị theo dõi. Trampoline của mình khác: gọi lại open() THẬT để lấy kết quả đúng - nhưng nếu
// dlsym được "open$NOCANCEL" (1 entry point THẬT KHÁC trong libsystem_kernel.dylib, không phải
// syscall thô, vẫn đi qua đường libc bình thường - chỉ là biến thể "không cho phép pthread_cancel
// ngắt giữa chừng" của open(), tồn tại song song để phục vụ chính cơ chế cancellation nội bộ của
// libc) thì có thể gọi thẳng qua đó - ĐỊA CHỈ KHÁC HẲN open(), không bao giờ tự chạm lại breakpoint
// - bỏ hẳn được vòng tắt/bật debug register + khoá mutex trong đường nóng. Nếu dlsym thất bại
// (tên có thể không tồn tại trên 1 số SDK/OS version) thì rơi về đúng cách cũ (gọi qua open() thật,
// vẫn cần tắt/bật quanh mỗi lần gọi) - xem hwbreakOpenTrampoline bên dưới.
static std::atomic<bool> g_hwbreakRealOpenNeedsBreakpointDance{true};

__attribute__((noinline))
static int hwbreakOpenTrampoline(const char *path, int flags, int mode) {
    // Log chi tiết CHỈ trong self-test (xem giải thích ở catch_mach_exception_raise_state) -
    // nếu HÀM NÀY không hề chạy (không thấy dòng "trampoline bắt đầu" trong debug.log dù handler
    // đã log "đã đổi PC xong") thì chứng tỏ chính việc CPU nhảy PC sang địa chỉ này mới là chỗ
    // crash, không phải bản thân lệnh gọi open() thật bên trong.
    bool selfTestLog = g_hwbreakSelfTestMode.load(std::memory_order_relaxed);
    if (selfTestLog) DeltaVFS_debugLogf("HWBreakHook: [self-test] trampoline bắt đầu chạy, path=%s", path ? path : "(null)");

    int (*fn)(const char *, int, ...) = g_hwbreakRealOpen.load(std::memory_order_relaxed);
    bool needsDance = g_hwbreakRealOpenNeedsBreakpointDance.load(std::memory_order_relaxed);
    int result;

    if (!needsDance) {
        // CÓ "open$NOCANCEL" - địa chỉ khác hẳn open(), không bao giờ tự chạm lại breakpoint (xem
        // giải thích ở khai báo g_hwbreakRealOpenNeedsBreakpointDance) - gọi thẳng, y hệt cách
        // hook.c/Monite không cần đụng gì tới debug register trong lúc xử lý.
        if (selfTestLog) DeltaVFS_debugLogf("HWBreakHook: [self-test] trampoline sắp gọi open$NOCANCEL (không cần tắt breakpoint), fn=%p", (void *)fn);
        result = fn ? fn(path, flags, mode) : -1;
        if (selfTestLog) DeltaVFS_debugLogf("HWBreakHook: [self-test] trampoline gọi open$NOCANCEL XONG, result=%d errno=%d", result, errno);
    } else {
        // KHÔNG có "open$NOCANCEL" trên SDK/OS này - rơi về cách cũ: fn() ở đây CHÍNH LÀ địa chỉ
        // đang bị breakpoint (open() thật), nên PHẢI tắt breakpoint TRÊN CHÍNH THREAD NÀY trước
        // khi gọi (nếu không sẽ chạm lại đúng breakpoint đó, handler/trampoline gọi nhau vô hạn -
        // lỗi đã xác nhận qua debug.log ở bản trước), rồi bật lại ngay sau - luôn AN TOÀN vì đang
        // thao tác debug register của chính thread đang chạy đoạn code này (không phải thread
        // khác, không cần cross-thread RPC). Khoá g_hwbreakDbgMutex quanh mỗi lần đọc-sửa-ghi để
        // rearm-poll thread (bật lại breakpoint cho MỌI thread mỗi 200ms) không chen vào đúng lúc
        // đang tắt - xem lý do đầy đủ ở khai báo g_hwbreakDbgMutex.
        thread_t self = mach_thread_self();
        arm_debug_state64_t dbg;
        memset(&dbg, 0, sizeof(dbg));

        pthread_mutex_lock(&g_hwbreakDbgMutex);
        bool haveState = hwbreakArmThreadState(self, &dbg);
        if (haveState) {
            dbg.__bcr[0] &= ~0x1u; // tắt breakpoint trên chính thread này
            hwbreakSetThreadState(self, &dbg);
        }
        pthread_mutex_unlock(&g_hwbreakDbgMutex);

        if (selfTestLog) DeltaVFS_debugLogf("HWBreakHook: [self-test] trampoline sắp gọi open() thật (đã tắt breakpoint), fn=%p", (void *)fn);
        result = fn ? fn(path, flags, mode) : -1;
        if (selfTestLog) DeltaVFS_debugLogf("HWBreakHook: [self-test] trampoline gọi open() thật XONG, result=%d errno=%d", result, errno);

        if (haveState) {
            pthread_mutex_lock(&g_hwbreakDbgMutex);
            dbg.__bcr[0] |= 0x1u; // bật lại breakpoint cho lần open() tiếp theo
            hwbreakSetThreadState(self, &dbg);
            pthread_mutex_unlock(&g_hwbreakDbgMutex);
        }
        mach_port_deallocate(mach_task_self(), self); // mach_thread_self() cấp 1 send right mới mỗi lần gọi
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
    // ---- HÀM XỬ LÝ THẬT DUY NHẤT - chạy mỗi lần open() bị chạm breakpoint (self-test lẫn
    // thật) ----
    // EXCEPTION_STATE KHÔNG cấp thread/task port - kernel chỉ đưa thẳng state hiện tại
    // (old_state) và mong nhận lại state mới (new_state) để áp dụng NGUYÊN TỬ ngay khi reply,
    // không cần (và không thể) gọi thread_get_state/thread_set_state riêng như bản v1. Đây là
    // đúng cách Monite.dylib (đối chiếu) đang làm - xem giải thích lớn ở đầu file.
    memcpy(new_state, old_state, (size_t)old_stateCnt * sizeof(natural_t));
    *new_stateCnt = old_stateCnt;
    if (flavor) *flavor = ARM_THREAD_STATE64;

    const arm_thread_state64_t *inState = (const arm_thread_state64_t *)old_state;
    arm_thread_state64_t *outState = (arm_thread_state64_t *)new_state;

    // Lưu ý: field pc trong arm_thread_state64_t là opaque (có thể mang PAC) trên SDK thật -
    // PHẢI dùng arm_thread_state64_get_pc()/set_pc_fptr() thay vì đọc/ghi field thô, khác với
    // x0-x28 (state.__x[i]) là GPR thường, không opaque, đọc thẳng field được (xem chỗ dùng
    // inState->__x[0]/outState->__x[0] bên dưới).
    uint64_t pc = (uint64_t)arm_thread_state64_get_pc(*inState);
    uint64_t trampolineAddr = g_hwbreakTrampolineAddr.load(std::memory_order_relaxed);
    if (pc != g_hwbreakOpenAddr.load(std::memory_order_relaxed) || trampolineAddr == 0) {
        // Không đúng địa chỉ đang theo dõi (không nên xảy ra - chỉ arm đúng 1 địa chỉ) hoặc
        // trampoline chưa dựng xong. AN TOÀN HƠN là để nguyên state (breakpoint có thể tự chạm
        // lại) còn hơn nhảy PC vào 1 địa chỉ chưa chắc hợp lệ.
        return KERN_SUCCESS;
    }

    // Chụp 1 lần, dùng lại suốt hàm - tránh đọc atomic lặp lại, và để chắc chắn nhất quán dù
    // biến toàn cục có gì thay đổi (không thay đổi trong lúc này, nhưng an toàn hơn).
    bool selfTestMode = g_hwbreakSelfTestMode.load(std::memory_order_relaxed);

    if (selfTestMode) {
        // Tự kiểm tra: không đụng gì tới đối số thật, chỉ cần biết breakpoint có chạm được
        // không. VẪN phải đổi PC sang trampoline bên dưới - nếu không, chính thread tự-kiểm-tra
        // (gọi open("/dev/null",...) thật) sẽ bị breakpoint chặn lại ngay lập tức, không bao
        // giờ hoàn tất được lệnh gọi thử.
        //
        // Log chi tiết CHỈ trong self-test (không log mỗi lần chặn open() thật - quá dày) để dù
        // không có crash log (.ips) vẫn biết chính xác app chết ở bước nào trong debug.log, vì
        // debug.log ghi thẳng write() syscall nên sống sót qua crash.
        DeltaVFS_debugLog("HWBreakHook: [self-test] handler reached, breakpoint chạm đúng địa chỉ open()");
        g_hwbreakSelfTestPassed.store(true, std::memory_order_relaxed);
    } else {
        g_hwbreakInterceptCount.fetch_add(1, std::memory_order_relaxed);
        const char *origPath = (const char *)inState->__x[0];
        const char *redirected = origPath ? redirectAllTrafficPath(origPath) : origPath;
        if (redirected && redirected != origPath) {
            char *buf = hwbreakNextPathBuf();
            strncpy(buf, redirected, 2047);
            buf[2047] = '\0';
            outState->__x[0] = (uint64_t)buf;
        }
    }

    // Dùng biến thể "_presigned_fptr" thay vì "_set_pc_fptr" thường - đã kiểm tra header thật
    // của SDK (Xcode 26.5, in ra qua bước CI riêng): nhánh có ptrauth dùng
    // ptrauth_auth_and_resign() cho "_set_pc_fptr", đòi hỏi con trỏ đưa vào PHẢI đã được ký PAC
    // hợp lệ từ trước - g_hwbreakTrampolineAddr chỉ là địa chỉ hàm thô (build ARCHS=arm64
    // thường, không tự ký PAC khi lấy địa chỉ hàm), nếu bản build chọn đúng nhánh ptrauth thì
    // "resign" 1 con trỏ chưa từng ký sẽ auth thất bại ngay lập tức -> crash (rất có thể là
    // nguyên nhân crash mới sau khi đổi sang trampoline). "_presigned_fptr" bỏ qua hẳn bước
    // auth+resign, chỉ gán thẳng giá trị - an toàn ở MỌI nhánh (kể cả các nhánh không-ptrauth,
    // nơi 2 biến thể này giống hệt nhau).
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
    bool armed = hwbreakArmBreakpointOnThread(mach_thread_self(), g_hwbreakOpenAddr.load(std::memory_order_relaxed));
    DeltaVFS_debugLogf("HWBreakHook: [self-test] arm breakpoint cho chính mình: %s", armed ? "OK" : "THẤT BẠI");

    int (*realOpen)(const char *, int, ...) = (int (*)(const char *, int, ...))dlsym(RTLD_DEFAULT, "open");
    if (realOpen) {
        // Với bản trampoline (v2/v3), lệnh gọi này giờ THẬT SỰ hoàn tất bình thường (không còn
        // kẹt giữa chừng chờ single-step) - fd trả về hợp lệ, đóng lại được, là bằng chứng mạnh
        // hơn bản v1 (trước đây chỉ cần chạm breakpoint 1 lần là coi như "qua", không xác nhận
        // được toàn bộ lệnh gọi có hoàn tất đúng hay không).
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
    g_hwbreakOpenAddr.store((uint64_t)openSym, std::memory_order_relaxed);

    // Thử tìm "open$NOCANCEL" - entry point THẬT KHÁC trong libsystem_kernel.dylib (không phải
    // syscall thô, vẫn qua đường libc bình thường), địa chỉ khác hẳn open() nên trampoline gọi
    // qua đây sẽ KHÔNG bao giờ tự chạm lại breakpoint - bỏ được vòng tắt/bật debug register +
    // mutex trong đường nóng (xem giải thích ở khai báo g_hwbreakRealOpenNeedsBreakpointDance).
    // Không đảm bảo tồn tại trên mọi SDK/OS version - dlsym NULL thì rơi về cách cũ (vẫn đúng,
    // chỉ chậm/phức tạp hơn 1 chút), KHÔNG coi là lỗi/huỷ cài đặt.
    void *openNoCancelSym = dlsym(RTLD_DEFAULT, "open$NOCANCEL");
    if (openNoCancelSym) {
        DeltaVFS_debugLogf("HWBreakHook: dlsym(open$NOCANCEL) OK, addr=0x%llx - trampoline sẽ KHÔNG cần tắt/bật breakpoint", (unsigned long long)(uint64_t)openNoCancelSym);
        g_hwbreakRealOpen.store((int (*)(const char *, int, ...))openNoCancelSym, std::memory_order_relaxed);
        g_hwbreakRealOpenNeedsBreakpointDance.store(false, std::memory_order_relaxed);
    } else {
        DeltaVFS_debugLog("HWBreakHook: dlsym(open$NOCANCEL) thất bại - dùng lại open() thật, trampoline vẫn cần tắt/bật breakpoint quanh mỗi lần gọi");
        g_hwbreakRealOpen.store((int (*)(const char *, int, ...))openSym, std::memory_order_relaxed);
        g_hwbreakRealOpenNeedsBreakpointDance.store(true, std::memory_order_relaxed);
    }
    g_hwbreakTrampolineAddr.store((uint64_t)(void *)hwbreakOpenTrampoline, std::memory_order_relaxed);
    DeltaVFS_debugLogf("HWBreakHook: trampoline addr=0x%llx", (unsigned long long)(uint64_t)(void *)hwbreakOpenTrampoline);

    kern_return_t kr = mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &g_hwbreakExcPort);
    if (kr != KERN_SUCCESS) {
        DeltaVFS_debugLogf("HWBreakHook: mach_port_allocate thất bại kr=%d, huỷ", kr);
        return false;
    }
    mach_port_insert_right(mach_task_self(), g_hwbreakExcPort, g_hwbreakExcPort, MACH_MSG_TYPE_MAKE_SEND);
    DeltaVFS_debugLog("HWBreakHook: mach_port_allocate + insert_right OK");

    // EXCEPTION_STATE (không phải EXCEPTION_DEFAULT của bản v1) - xem giải thích lớn ở đầu file
    // về lý do đổi (đối chiếu Monite.dylib, và đây là cách duy nhất tương thích với thiết kế
    // trampoline: không có thread/task port thì cũng không cần, vì không còn gọi
    // thread_get_state/set_state trong đường xử lý exception nữa).
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
    // trừ vĩnh viễn - xem giải thích chi tiết ở đó (tự khoá chết nếu thread server bị arm).
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
    // return và fd đã đóng) - đây là lỗ hổng thật trong bản trước: "tự kiểm tra THÀNH CÔNG" đã
    // từng được log trong debug.log ngay cả những lần sau đó app bị crash, vì flag đó không hề
    // chứng minh trampoline chạy xong trót lọt, chỉ chứng minh exception đã được bắt. Chờ cả 2
    // giúp phát hiện đúng trường hợp trampoline bị treo (timeout, coi như thất bại, an toàn) -
    // KHÔNG giúp gì nếu trampoline CRASH thẳng (tiến trình chết ngay tại đây, không có gì để bắt
    // - đó là lý do phải kiểm tra kỹ ownership của g_hwbreakTrampolineCompletions ở heartbeat).
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
