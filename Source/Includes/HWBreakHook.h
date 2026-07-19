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
//  CƠ CHẾ: đặt breakpoint CPU (ARM64 DBGBCRn/DBGBVRn qua thread_set_state) ngay tại địa chỉ
//  hàm open() thật. Khi CPU chạm tới, kernel gửi Mach exception EXC_BREAKPOINT tới port mình
//  đăng ký. Bắt exception đó, sửa thẳng thanh ghi x0 (đối số đầu tiên = path) thành đường
//  dẫn đã redirect, rồi cho hàm gốc CHẠY TIẾP BÌNH THƯỜNG với đối số đã đổi - không cần tự
//  viết lại logic open(), không đụng tới GOT.
//
//  CHỖ NGUY HIỂM NHẤT: sau khi breakpoint chạm, PC vẫn đứng NGUYÊN tại địa chỉ đó - nếu chỉ
//  reply KERN_SUCCESS mà không xử lý gì thêm, CPU chạy lại đúng chỗ đó, chạm breakpoint lần
//  nữa NGAY LẬP TỨC -> lặp vô hạn -> tiến trình treo cứng. Cách xử lý chuẩn: tắt breakpoint
//  đó, bật single-step (MDSCR_EL1.SS), cho đúng 1 lệnh gốc chạy (lúc này x0 đã đổi), bắt
//  exception single-step tiếp theo, tắt single-step + bật lại breakpoint, rồi mới cho chạy
//  tiếp bình thường.
//
//  AN TOÀN: có bước TỰ KIỂM TRA (self-test) trước khi dùng thật - tự gọi open() giả từ 1
//  thread riêng (không phải thread constructor, để nếu treo cũng không treo cả app), đợi
//  tối đa 500ms xem cơ chế có hoạt động không. KHÔNG hoạt động trong thời gian đó -> coi như
//  thất bại, dọn dẹp, để open() tiếp tục dùng fishhook như cũ (AssetRedirect.h tự thêm lại
//  "open" vào danh sách fishhook nếu hàm HWBreakHook_tryInstallForOpen() dưới đây trả về false).
//
//  Giả định về cấu trúc thanh ghi debug ARM64 (arm_debug_state64_t: __bvr/__bcr/__wvr/__wcr/
//  __mdscr_el1) lấy từ header hệ thống <mach/arm/thread_status.h> - dùng đúng type/hằng số
//  của SDK thay vì tự khai báo, giảm rủi ro sai lệch ABI.
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
#include <atomic>

// ---- Cấu trúc message Mach exception (behavior EXCEPTION_DEFAULT | MACH_EXCEPTION_CODES) ----
// Không có sẵn header MIG-generated trên máy build này (không có Xcode/MIG) nên tự khai báo
// đúng layout wire-format chuẩn của "mach_exc" subsystem (msgh_id 2405 request / 2505 reply) -
// layout này ổn định qua nhiều đời iOS, được nhiều dự án mã nguồn mở tự triển khai tương tự.
#pragma pack(push, 4)
typedef struct {
    mach_msg_header_t Head;
    mach_msg_body_t msgh_body;
    mach_msg_port_descriptor_t thread;
    mach_msg_port_descriptor_t task;
    NDR_record_t NDR;
    exception_type_t exception;
    mach_msg_type_number_t codeCnt;
    int64_t code[2];
} hwbreak_exc_request_t;

typedef struct {
    mach_msg_header_t Head;
    NDR_record_t NDR;
    kern_return_t RetCode;
} hwbreak_exc_reply_t;
#pragma pack(pop)

#define HWBREAK_EXC_RAISE_MSGID 2405

static mach_port_t g_hwbreakExcPort = MACH_PORT_NULL;
static uint64_t g_hwbreakOpenAddr = 0;
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

// ---- Đọc/ghi thanh ghi debug 1 thread ----
static inline bool hwbreakArmThreadState(thread_t thread, arm_debug_state64_t *dbg) {
    mach_msg_type_number_t count = ARM_DEBUG_STATE64_COUNT;
    return thread_get_state(thread, ARM_DEBUG_STATE64, (thread_state_t)dbg, &count) == KERN_SUCCESS;
}

static inline bool hwbreakSetThreadState(thread_t thread, arm_debug_state64_t *dbg) {
    return thread_set_state(thread, ARM_DEBUG_STATE64, (thread_state_t)dbg, ARM_DEBUG_STATE64_COUNT) == KERN_SUCCESS;
}

// BCR: BAS=1111 (khớp đủ 4 byte lệnh) | PMC=10 (chỉ EL0 - userspace) | E=1 (bật)
#define HWBREAK_BCR_VALUE ((0xFu << 5) | (0x2u << 1) | 0x1u)

// Khoá chung giữa thread poll (hwbreakRearmPollThreadFn, quét TẤT CẢ thread mỗi 200ms) và
// thread xử lý exception (hwbreakHandleException) - cả 2 đều đọc/ghi arm_debug_state64_t của
// cùng 1 thread. KHÔNG khoá theo từng thread_t (mảng/bảng khoá theo cầu port name) vì port
// name có thể bị tái sử dụng - xem lý do đã né hoàn toàn kiểu bug đó ở hwbreakHandleException.
// Dùng 1 mutex TOÀN CỤC duy nhất, không định danh theo thread, nên không có rủi ro đó: chấp
// nhận đánh đổi là quét rearm bị chặn trong lúc 1 exception đang được xử lý (rất ngắn, vài
// lệnh Mach RPC) - rẻ hơn nhiều so với việc để 2 bên ghi đè debug register của nhau.
//
// Đây chính là nguyên nhân treo phát hiện qua crash log FreeFire-2026-07-20-044938.ips (watchdog
// 0x8BADF00D "scene-update", CPU app ~0% lúc bị kill - tức có thread bị KẸT CHỜ chứ không chạy
// vòng lặp): rearm-poll thread có thể chen vào ĐÚNG lúc handler vừa tắt breakpoint + bật
// single-step để cho 1 lệnh gốc chạy nhưng CHƯA kịp reply - poll thread đọc lại state (thấy
// __bcr đang tắt, tưởng là "thread chưa được arm") rồi ghi đè __bcr về ENABLED trong khi PC vẫn
// còn đứng ở đúng địa chỉ open() và single-step vẫn đang bật -> thread không bao giờ thoát khỏi
// vòng lặp chạm-breakpoint dù chưa từng spin CPU thật sự (mỗi lần đều đi vào lại kernel chờ
// exception tiếp theo). Thread đó (rất có thể là thread khởi tạo Firebase Crashlytics, đang gọi
// open() để đọc file) không bao giờ hoàn tất dispatch_once, kéo theo main thread kẹt chờ vô hạn.
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

static void hwbreakArmAllExistingThreads(uint64_t addr) {
    thread_act_array_t threads = NULL;
    mach_msg_type_number_t threadCount = 0;
    if (task_threads(mach_task_self(), &threads, &threadCount) != KERN_SUCCESS) return;
    for (mach_msg_type_number_t i = 0; i < threadCount; i++) {
        hwbreakArmBreakpointOnThread(threads[i], addr);
        mach_port_deallocate(mach_task_self(), threads[i]);
    }
    vm_deallocate(mach_task_self(), (vm_address_t)threads, threadCount * sizeof(thread_act_t));
}

// Unity/game tạo thread mới liên tục - debug register là TÀI NGUYÊN RIÊNG CỦA TỪNG THREAD,
// không có cách nào "set 1 lần cho cả tiến trình". Poll định kỳ để bắt thread mới thay vì cố
// hook điểm tạo thread (đơn giản hơn, đủ dùng cho bản thử nghiệm).
//
// CHỜ 1 NHỊP TRƯỚC KHI ARM LẦN ĐẦU: log FreeFire-2026-07-20-*.debug.log (2 lần liên tiếp, cùng
// 1 chỗ) cho thấy app treo cứng NGAY sau dòng "ĐÃ KÍCH HOẠT", không còn log gì thêm - tức là
// treo ngay trong lúc dyld vẫn còn đang nạp nốt các thư viện còn lại (constructor này chạy TRƯỚC
// main()/UIApplicationMain). Nếu arm breakpoint cho main thread ngay lập tức (như bản cũ), 1 lệnh
// open() của CHÍNH dyld gọi để nạp thư viện tiếp theo - rất có thể đang được gọi trong lúc dyld
// giữ khoá nội bộ riêng của nó - sẽ bị chặn lại chờ reply từ exception handler; nếu handler đó
// cần bất kỳ thứ gì (VD: lazy-bind 1 hàm Foundation lần đầu) mà cũng cần đúng khoá dyld đang bị
// giữ bởi chính thread đang bị treo -> deadlock vĩnh viễn, không crash, không log thêm - khớp
// chính xác với triệu chứng quan sát được. Không có cách nào biết chắc chắn dyld đã nạp xong hay
// chưa (không có API công khai), nên dùng 1 khoảng chờ cố định trước khi arm THẬT lần đầu tiên -
// giảm mạnh khả năng trúng đúng lúc dyld đang giữ khoá, nhưng KHÔNG đảm bảo tuyệt đối 100%.
static void *hwbreakRearmPollThreadFn(void *ctx) {
    usleep(400 * 1000); // chờ 400ms trước lần arm đầu tiên - xem giải thích ở trên
    int tick = 0;
    while (g_hwbreakActive.load(std::memory_order_relaxed)) {
        if (g_hwbreakOpenAddr != 0) {
            hwbreakArmAllExistingThreads(g_hwbreakOpenAddr);
        }
        // "Nhịp tim" mỗi ~1s (5 x 200ms) - bằng chứng trực tiếp HWBreakHook có còn đang xử lý
        // open() thật hay không ngay trước lúc app treo, không cần chờ đoán qua log của chỗ
        // khác. Xem giải thích ở g_hwbreakInterceptCount.
        tick++;
        if (tick % 5 == 0) {
            DeltaVFS_debugLogf("HWBreakHook heartbeat: đã chặn %llu lần open() thật",
                                g_hwbreakInterceptCount.load(std::memory_order_relaxed));
        }
        usleep(200 * 1000); // 200ms
    }
    return NULL;
}

// ---- Xử lý 1 exception đã nhận được - dùng chung cho cả server thật lẫn self-test ----
static void hwbreakHandleException(const hwbreak_exc_request_t *req) {
    thread_t faultingThread = req->thread.name;

    // Phân biệt "vừa chạm breakpoint thật" với "vừa hoàn tất single-step" bằng cách đọc PC
    // hiện tại và so trực tiếp với địa chỉ open() - KHÔNG dùng mảng tự quản lý theo thread_t
    // nữa (đã gây crash EXC_BAD_ACCESS trên máy thật, log FreeFire-2026-07-20-042329.ips):
    // Mach thread port name có thể bị hệ thống tái sử dụng cho 1 thread mới ngay sau khi
    // thread cũ chết, khiến mảng tra theo thread_t trả về sai trạng thái (VD: coi nhầm 1 lần
    // chạm breakpoint thật là "đang single-step", bỏ qua việc đọc/sửa x0, rồi sau đó dùng x0
    // rác đi strncpy -> SIGSEGV). So PC là tự-xác-thực từ trạng thái CPU thật, không phụ
    // thuộc sổ sách riêng nên né được lớp lỗi này hoàn toàn.
    arm_thread_state64_t state;
    memset(&state, 0, sizeof(state));
    mach_msg_type_number_t stateCount = ARM_THREAD_STATE64_COUNT;
    bool gotState = thread_get_state(faultingThread, ARM_THREAD_STATE64, (thread_state_t)&state, &stateCount) == KERN_SUCCESS;
    // Lưu ý: field pc trong arm_thread_state64_t là opaque (__opaque_pc, có thể mang PAC) trên
    // SDK thật - PHẢI dùng arm_thread_state64_get_pc() để lấy địa chỉ thô đã strip PAC, khác
    // với x0-x28 (state.__x[i]) là GPR thường, không opaque, đọc thẳng field được (xem chỗ dùng
    // state.__x[0] bên dưới).
    bool atBreakpointAddr = gotState && g_hwbreakOpenAddr != 0 &&
        (uint64_t)arm_thread_state64_get_pc(state) == g_hwbreakOpenAddr;

    if (atBreakpointAddr) {
        // Chạm breakpoint thật lần đầu - đây là lúc sửa đối số.
        if (g_hwbreakSelfTestMode.load(std::memory_order_relaxed)) {
            // Tự kiểm tra: không đụng gì tới đối số thật, chỉ cần biết breakpoint có
            // chạm được không.
            g_hwbreakSelfTestPassed.store(true, std::memory_order_relaxed);
        } else {
            g_hwbreakInterceptCount.fetch_add(1, std::memory_order_relaxed);
            // Truy cập trực tiếp state.__x[0] (x0 = đối số đầu tiên = path) - KHÔNG dùng
            // các hàm accessor arm_thread_state64_get_pc()/... vì những hàm đó chỉ tồn
            // tại (và chỉ cần thiết) cho pc/lr do Pointer Authentication (PAC) - thanh ghi
            // x0-x28 không bị PAC đóng gói, đọc/ghi thẳng field __x là đúng ABI chuẩn.
            const char *origPath = (const char *)state.__x[0];
            const char *redirected = origPath ? redirectAllTrafficPath(origPath) : origPath;
            if (redirected && redirected != origPath) {
                char *buf = hwbreakNextPathBuf();
                strncpy(buf, redirected, 2047);
                buf[2047] = '\0';
                state.__x[0] = (uint64_t)buf;
                thread_set_state(faultingThread, ARM_THREAD_STATE64, (thread_state_t)&state, ARM_THREAD_STATE64_COUNT);
            }
        }

        // Tắt breakpoint này, bật single-step để cho đúng 1 lệnh (lệnh gốc, với x0 đã đổi)
        // chạy trước khi bắt lại. Khoá g_hwbreakDbgMutex trong lúc đọc-sửa-ghi để rearm-poll
        // thread không chen vào ghi đè __bcr về ENABLED giữa chừng (xem giải thích ở khai báo
        // g_hwbreakDbgMutex phía trên - đây là nguyên nhân treo đã xác định từ crash log
        // FreeFire-2026-07-20-044938.ips).
        pthread_mutex_lock(&g_hwbreakDbgMutex);
        arm_debug_state64_t dbg;
        memset(&dbg, 0, sizeof(dbg));
        if (hwbreakArmThreadState(faultingThread, &dbg)) {
            dbg.__bcr[0] &= ~0x1u;   // tắt Enable
            dbg.__mdscr_el1 |= 0x1u; // bật Single Step
            hwbreakSetThreadState(faultingThread, &dbg);
        }
        pthread_mutex_unlock(&g_hwbreakDbgMutex);
    } else {
        // Đây là exception single-step (đã chạy xong đúng 1 lệnh gốc) - bật lại breakpoint,
        // tắt single-step, để thread chạy tiếp bình thường từ lệnh thứ 2 trở đi. Khoá tương tự.
        pthread_mutex_lock(&g_hwbreakDbgMutex);
        arm_debug_state64_t dbg;
        memset(&dbg, 0, sizeof(dbg));
        if (hwbreakArmThreadState(faultingThread, &dbg)) {
            dbg.__bcr[0] |= 0x1u;    // bật lại Enable
            dbg.__mdscr_el1 &= ~0x1u; // tắt Single Step
            hwbreakSetThreadState(faultingThread, &dbg);
        }
        pthread_mutex_unlock(&g_hwbreakDbgMutex);
    }
}

static void *hwbreakServerThreadFn(void *ctx) {
    // Buffer nhận LỚN HƠN struct thật - kernel có thể kèm trailer (audit/security info) sau
    // phần message chính; nếu buffer nhận đúng bằng sizeof(request), thiếu chỗ cho trailer sẽ
    // ra lỗi MACH_RCV_TOO_LARGE và message coi như mất - thread đứng chờ reply mãi mãi (treo).
    union {
        hwbreak_exc_request_t req;
        char raw[sizeof(hwbreak_exc_request_t) + 512];
    } msgBuf;

    while (true) {
        memset(&msgBuf, 0, sizeof(msgBuf));
        msgBuf.req.Head.msgh_local_port = g_hwbreakExcPort;
        msgBuf.req.Head.msgh_size = sizeof(msgBuf);
        mach_msg_return_t mr = mach_msg(&msgBuf.req.Head, MACH_RCV_MSG, 0, sizeof(msgBuf),
                                         g_hwbreakExcPort, MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
        if (mr != MACH_MSG_SUCCESS) {
            // Trước đây im lặng bỏ qua - nếu đây từng là nguyên nhân treo (thread gốc mất
            // message, không bao giờ được reply) thì hoàn toàn không có dấu vết gì trong log.
            DeltaVFS_debugLogf("HWBreakHook: mach_msg RCV thất bại mr=0x%x, bỏ qua message này", mr);
            continue;
        }
        hwbreak_exc_request_t &req = msgBuf.req;
        if (req.Head.msgh_id != HWBREAK_EXC_RAISE_MSGID) {
            // QUAN TRỌNG: nếu đây là 1 exception message THẬT (không phải rác) mà msgh_id khác
            // giá trị mình đoán (2405) - VD do 1 phiên bản iOS nào đó tạo message theo dạng khác
            // - thì thread gốc sẽ bị bỏ rơi VĨNH VIỄN vì không bao giờ nhận được reply, và trước
            // bản vá này thì không có log nào ghi lại việc đó cả. Log lại để biết chắc nếu đây là
            // nguyên nhân.
            DeltaVFS_debugLogf("HWBreakHook: nhận message msgh_id=%d (khác %d mong đợi) - BỎ QUA KHÔNG REPLY, thread gốc có thể bị treo vĩnh viễn nếu đây là exception thật",
                                req.Head.msgh_id, HWBREAK_EXC_RAISE_MSGID);
            continue;
        }

        hwbreakHandleException(&req);

        // Message exception mang theo SEND RIGHT cho thread port và task port của thread bị
        // chạm breakpoint (mach_msg_port_descriptor_t) - đây là quyền sở hữu thật, PHẢI
        // mach_port_deallocate sau khi dùng xong, nếu không sẽ RÒ RỈ PORT mỗi lần open() bị
        // chạm breakpoint. open() được gọi rất nhiều lần trong lúc app khởi động (đọc Info.plist,
        // load framework, Firebase Crashlytics init, v.v.) nên rò rỉ này tích luỹ rất nhanh.
        if (MACH_PORT_VALID(req.thread.name)) {
            mach_port_deallocate(mach_task_self(), req.thread.name);
        }
        if (MACH_PORT_VALID(req.task.name)) {
            mach_port_deallocate(mach_task_self(), req.task.name);
        }

        hwbreak_exc_reply_t reply;
        memset(&reply, 0, sizeof(reply));
        reply.Head.msgh_bits = MACH_MSGH_BITS(MACH_MSGH_BITS_REMOTE(req.Head.msgh_bits), 0);
        reply.Head.msgh_remote_port = req.Head.msgh_remote_port;
        reply.Head.msgh_size = sizeof(reply);
        reply.Head.msgh_local_port = MACH_PORT_NULL;
        reply.Head.msgh_id = req.Head.msgh_id + 100;
        reply.NDR = req.NDR;
        reply.RetCode = KERN_SUCCESS;
        mach_msg_return_t sendResult = mach_msg(&reply.Head, MACH_SEND_MSG, sizeof(reply), 0, MACH_PORT_NULL, MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
        if (sendResult != MACH_MSG_SUCCESS) {
            // TRƯỚC ĐÂY KHÔNG KIỂM TRA GIÁ TRỊ TRẢ VỀ Ở ĐÂY - nếu gửi reply thất bại (VD sai
            // định dạng message, quyền gửi không hợp lệ, v.v.) thì thread gốc vẫn bị kernel giữ
            // suspended VĨNH VIỄN chờ 1 reply không bao giờ tới đúng cách, mà mình hoàn toàn
            // không biết gì cả vì code cứ lặng lẽ loop tiếp - khớp CHÍNH XÁC với triệu chứng quan
            // sát được qua debug.log (bộ đếm g_hwbreakInterceptCount dừng tăng đột ngột, không
            // crash, không log gì thêm). Log lại đây để xác nhận/loại trừ.
            DeltaVFS_debugLogf("HWBreakHook: GỬI REPLY THẤT BẠI mr=0x%x cho thread=%u - thread đó rất có thể bị treo vĩnh viễn từ đây",
                                sendResult, (unsigned)req.thread.name);
        }
    }
    return NULL;
}

// Thread riêng để tự gọi open() thử - KHÔNG chạy trên thread constructor, để nếu cơ chế
// treo (vòng lặp single-step không thoát được) thì chỉ thread thử nghiệm này bị kẹt, không
// kéo theo cả app không mở được.
static void *hwbreakSelfTestCallerThreadFn(void *ctx) {
    hwbreakArmBreakpointOnThread(mach_thread_self(), g_hwbreakOpenAddr);
    int (*realOpen)(const char *, int, ...) = (int (*)(const char *, int, ...))dlsym(RTLD_DEFAULT, "open");
    if (realOpen) {
        int fd = realOpen("/dev/null", O_RDONLY);
        if (fd >= 0) close(fd);
    }
    g_hwbreakSelfTestDone.store(true, std::memory_order_relaxed);
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
    g_hwbreakOpenAddr = (uint64_t)openSym;

    kern_return_t kr = mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE, &g_hwbreakExcPort);
    if (kr != KERN_SUCCESS) {
        DeltaVFS_debugLogf("HWBreakHook: mach_port_allocate thất bại kr=%d, huỷ", kr);
        return false;
    }
    mach_port_insert_right(mach_task_self(), g_hwbreakExcPort, g_hwbreakExcPort, MACH_MSG_TYPE_MAKE_SEND);

    kr = task_set_exception_ports(mach_task_self(), EXC_MASK_BREAKPOINT, g_hwbreakExcPort,
                                   (exception_behavior_t)(EXCEPTION_DEFAULT | MACH_EXCEPTION_CODES),
                                   ARM_THREAD_STATE64);
    if (kr != KERN_SUCCESS) {
        DeltaVFS_debugLogf("HWBreakHook: task_set_exception_ports thất bại kr=%d, huỷ", kr);
        mach_port_deallocate(mach_task_self(), g_hwbreakExcPort);
        g_hwbreakExcPort = MACH_PORT_NULL;
        return false;
    }

    pthread_t serverThread;
    if (pthread_create(&serverThread, NULL, hwbreakServerThreadFn, NULL) != 0) {
        DeltaVFS_debugLog("HWBreakHook: tạo server thread thất bại, huỷ");
        return false;
    }
    pthread_detach(serverThread);

    // ---- TỰ KIỂM TRA: gọi open() thử từ 1 thread riêng, đợi tối đa 500ms ----
    g_hwbreakSelfTestMode.store(true, std::memory_order_relaxed);
    pthread_t testThread;
    if (pthread_create(&testThread, NULL, hwbreakSelfTestCallerThreadFn, NULL) != 0) {
        DeltaVFS_debugLog("HWBreakHook: tạo test thread thất bại, huỷ");
        return false;
    }
    pthread_detach(testThread);

    int waited = 0;
    while (!g_hwbreakSelfTestPassed.load(std::memory_order_relaxed) && waited < 500) {
        usleep(10 * 1000);
        waited += 10;
    }
    g_hwbreakSelfTestMode.store(false, std::memory_order_relaxed);

    bool passed = g_hwbreakSelfTestPassed.load(std::memory_order_relaxed);
    DeltaVFS_debugLogf("HWBreakHook: tự kiểm tra %s sau %dms", passed ? "THÀNH CÔNG" : "THẤT BẠI", waited);

    if (!passed) {
        // KHÔNG dùng cho open() thật - nhưng KHÔNG thu hồi exception port/server thread vì
        // test thread có thể vẫn đang kẹt giữa chừng (nếu single-step không hoạt động đúng) -
        // gỡ exception port lúc này có thể khiến test thread đó chết theo kiểu khó lường hơn
        // là cứ để nó tự kẹt (vô hại, chỉ 1 thread mồ côi, không ảnh hưởng phần còn lại của app).
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
