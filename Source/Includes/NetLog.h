#pragma once
// ============================================================================
// NetLog - logger MẠNG kiêm điểm cắm CHẶN tầng thấp (connect/write/send)
// ----------------------------------------------------------------------------
// Mục đích: soi xem game gửi request gì lên server, ở tầng nào - để biết đường
// nào (vd server realtime ggblueshark.com) đi qua đâu mà chặn cho trúng, VÀ chặn
// được cả những request bay tắt qua thư viện mạng riêng của game (không qua
// NSURLSession nên JunkAdURLProtocol/DNSBlock không thấy được).
//
// 8 tầng được ghi:
//   DNS      : hostname xin phân giải (getaddrinfo/gethostbyname...) - do DNSBlock.h gọi vào
//   DNS-BLK  : domain đen bị chặn ngay ở bước phân giải (do DNSBlock.h gọi vào)
//   TCP/UDP  : connect()/connectx()/sendto() -> ip:port (bắt cả khi connect thẳng IP, không qua
//              DNS, và cả kết nối TCP đi qua connectx() thay vì connect() cổ điển - CFNetwork/
//              Network.framework ngày càng dùng connectx() cho Happy Eyeballs/TFO)
//   CONN-BLK : connect()/connectx() bị chặn theo IP (netLogSetBlockCheck) - domain đen đã học
//              IP trước, hoặc IP DNS cố định (8.8.8.8)
//   HTTP     : full URL - do JunkAdURLProtocol gọi vào (NSURLSession), CỘNG với request HTTP
//              thuần không qua NSURLSession bắt được từ write()/send() (xem dưới)
//   HTTP-BLK : request HTTP thuần (không qua NSURLSession) bị chặn theo header Host
//   HTTP-BODY: preview HTTPBody (method + nội dung, ASCII, ký tự không in được thay '.') của 1
//              request NSURLSession SẮP bị chặn (do DNSBlock.h gọi vào, ngay cạnh dòng HTTP-BLK
//              tương ứng) - để biết chính xác game định gửi gì lên chứ không chỉ mỗi URL. Chỉ
//              ghi nếu request có HTTPBody (NSData sẵn trong bộ nhớ, không đọc HTTPBodyStream).
//   TLS-SNI  : tên miền lấy từ ClientHello (SNI) của kết nối HTTPS không qua NSURLSession -
//              game/thư viện TLS riêng (BoringSSL, libcurl tự vendor...) gọi write()/send()
//              thẳng nên JunkAdURLProtocol không thấy được, phải soi gói ClientHello ở tầng
//              socket mới bắt được domain thật.
//   TLS-BLK  : kết nối HTTPS không qua NSURLSession bị chặn theo SNI (write()/send() trả lỗi
//              ECONNRESET ngay gói đầu, KHÔNG gửi ClientHello/request thật ra ngoài)
//   UDP-PEEK : preview nội dung (ASCII, ký tự không in được thay '.') của gói UDP gửi tới dải
//              cổng nghi vấn 10000-10020 (netLogPortInPeekRange) - CHỈ QUAN SÁT, không chặn gì
//              (dải cổng này từng bị chặn nhầm với traffic server thật, xem log "Revert
//              socket-layer UDP/TCP blocking"), để biết chắc nội dung trước khi quyết định chặn.
//              Chỉ ghi log nếu gói có đoạn ký tự in-được liên tục >= 10 (netLogLongestPrintableRun)
//              - lọc bớt noise binary thuần của gameplay netcode (đa số traffic ở dải cổng này).
//   UDP-BLK  : gói UDP bị CHẶN khi công tắc netLogSetUdpPortBlockEnabled(true) đang bật (bật/tắt
//              từ menu) VÀ khớp 1 trong 2 quy tắc của netLogUdpShouldBlock: (1) cổng đích trong
//              dải 10000-10020 VÀ độ dài gói 2000-4000 byte, hoặc (2) cổng đích là 443 hoặc 0
//              (chặn mọi độ dài). Gói bị chặn thì bỏ hẳn nhưng hook vẫn trả về như gửi thành
//              công để không phá logic reliability/retry của game. Log tầng này (và
//              UDP/UDP-PEEK) nằm ở ring buffer riêng, xem NetLog_udpSnapshot() - không lẫn vào
//              NET LOG chính.
//
// Bản thân NetLog không tự quyết định chặn gì - việc chặn (connect theo IP, hay write/send
// theo hostname từ SNI/Host) do module khác (DNSBlock.h) đăng ký qua netLogSetBlockCheck /
// netLogSetHostBlockCheck, NetLog chỉ gọi hộ đúng lúc rồi trả lỗi thay vì gọi hàm gốc.
// Log gom trùng: mỗi chuỗi chỉ ghi 1 lần.
// ============================================================================
#import <Foundation/Foundation.h>
#import <sys/socket.h>
#import <sys/uio.h> // struct iovec - tham số của connectx()
#import <netinet/in.h>
#import <arpa/inet.h>
#import <time.h>
#import <errno.h>
#import <string.h>

#include <string>
#include <unordered_set>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <cstdint>

#import "fishhook.h"

// DeltaVFS_debugLog/DeltaVFS_debugLogf định nghĩa ở AssetRedirect.h - khai báo extern (không
// #import trực tiếp để tránh phụ thuộc vòng, NetLog.h được include TRƯỚC AssetRedirect.h trong
// DNSBlock.h) - cùng translation unit nên vẫn resolve đúng lúc link.
extern void DeltaVFS_debugLog(const char *msg);
extern void DeltaVFS_debugLogf(const char *fmt, ...);

#define NETLOG_MAX_LINES 100
#define NETLOG_LINE_LEN  200

// Ring buffer dùng chung cho cả log chính (DNS/TCP/HTTP/TLS...) LẪN log UDP riêng - tách ra
// struct để không phải chép 2 lần y hệt logic ghi/dedup/snapshot cho 2 buffer độc lập.
struct NetLogRing {
    char lines[NETLOG_MAX_LINES][NETLOG_LINE_LEN];
    int head = 0;
    std::atomic<unsigned int> total{0};
    std::mutex mutex;
    std::unordered_set<std::string> seen; // gom trùng: chuỗi nào ghi rồi thì bỏ
};
static NetLogRing g_netLogMain;
// Tầng UDP/UDP-PEEK/UDP-BLK ghi RIÊNG vào đây, không lẫn vào log chính - traffic UDP ở dải
// cổng 10000-10020 dày đặc hơn hẳn DNS/HTTP, nếu gộm chung sẽ đẩy trôi mất các dòng chặn hiếm
// (DNS-BLK/HTTP-BLK...) ra khỏi ring buffer trước khi kịp xem trong tab INFO.
static NetLogRing g_netLogUdp;

inline bool netLogLayerIsUdp(const char *layer) {
    return layer && strncmp(layer, "UDP", 3) == 0; // khớp UDP / UDP-PEEK / UDP-BLK
}

// Ghi 1 dòng "[HH:MM:SS] [TẦNG] chi_tiết" vào đúng ring (chính hoặc UDP riêng) - ring buffer vẫn
// gộp trùng để không trôi mất tín hiệu hiếm trong tab INFO (100 dòng có hạn) - TRỪ *-BLK luôn
// ghi, lý do cũ: game hay retry đúng 1 URL nhiều lần, gộp trùng sẽ làm mất dấu vết chặn thật.
//
// debug.log (DeltaVFS_debugLogf bên dưới) thì KHÔNG gộp trùng nữa - user chỉ gửi debug.log để
// xem lại (không chụp tab INFO), cần thấy MỌI request kể cả lặp lại y hệt nhau để biết chính xác
// tần suất/nhịp gói tin quanh 1 mốc thời gian nghi vấn (vd lúc "vào trận"), không chỉ lần đầu
// tiên nhìn thấy 1 endpoint.
inline void netLogRaw(const char *layer, const char *detail) {
    if (!layer || !detail) return;
    size_t layerLen = strlen(layer);
    bool isBlockEvent = layerLen >= 4 && strcmp(layer + layerLen - 4, "-BLK") == 0;
    NetLogRing &ring = netLogLayerIsUdp(layer) ? g_netLogUdp : g_netLogMain;

    // Ghi debug.log TRƯỚC, không phụ thuộc gom-trùng của ring buffer (mục đích khác nhau: ring
    // buffer chỉ phục vụ hiển thị gọn trong tab INFO, debug.log cần đầy đủ tuyệt đối).
    DeltaVFS_debugLogf("NetLog[%s]: %s", layer, detail);

    std::lock_guard<std::mutex> lock(ring.mutex);
    if (!isBlockEvent) {
        std::string key = std::string(layer) + "|" + detail;
        if (!ring.seen.insert(key).second) return; // đã có -> bỏ khỏi RING (debug.log đã ghi ở trên rồi)
    }

    time_t t = time(NULL);
    struct tm tmv;
    localtime_r(&t, &tmv);
    snprintf(ring.lines[ring.head], NETLOG_LINE_LEN, "%02d:%02d:%02d [%s] %s",
             tmv.tm_hour, tmv.tm_min, tmv.tm_sec, layer, detail);
    ring.head = (ring.head + 1) % NETLOG_MAX_LINES;
    ring.total.fetch_add(1, std::memory_order_relaxed);
}

inline unsigned int NetLog_count() { return g_netLogMain.total.load(std::memory_order_relaxed); }
inline unsigned int NetLog_udpCount() { return g_netLogUdp.total.load(std::memory_order_relaxed); }

// Bản chụp log để tab INFO hiển thị - MỚI NHẤT ở trên cùng (đỡ phải cuộn).
inline NSString *netLogRingSnapshot(NetLogRing &ring) {
    std::lock_guard<std::mutex> lock(ring.mutex);
    unsigned int total = ring.total.load(std::memory_order_relaxed);
    unsigned int n = total < NETLOG_MAX_LINES ? total : NETLOG_MAX_LINES;
    if (n == 0) return @"(chưa bắt được request nào)";
    NSMutableString *s = [NSMutableString string];
    int last = (ring.head - 1 + NETLOG_MAX_LINES) % NETLOG_MAX_LINES;
    for (unsigned int i = 0; i < n; i++) {
        int idx = (last - (int)i + NETLOG_MAX_LINES * 2) % NETLOG_MAX_LINES;
        [s appendFormat:@"%s\n", ring.lines[idx]];
    }
    return s;
}
inline NSString *NetLog_snapshot() { return netLogRingSnapshot(g_netLogMain); }
inline NSString *NetLog_udpSnapshot() { return netLogRingSnapshot(g_netLogUdp); }

// ===== Điểm cắm chặn tuỳ chọn cho connect() (DNSBlock.h đăng ký qua netLogSetBlockCheck) =====
// NetLog tự nó luôn chỉ quan sát; việc CHẶN (nếu có) do module khác quyết định, NetLog chỉ
// gọi hộ ngay trước khi connect() thật sự diễn ra - tránh phải hook "connect" hai lần
// (fishhook rebind lần sau sẽ đè mất lần trước, hai hook cùng tên trong 1 TU thì không build được).
typedef bool (*NetBlockCheckFn)(const char *ip, uint16_t port, char *reasonOut, size_t reasonLen);
static NetBlockCheckFn g_netBlockCheck = NULL;
inline void netLogSetBlockCheck(NetBlockCheckFn fn) { g_netBlockCheck = fn; }

// sockaddr -> ip riêng + port riêng. Chỉ nhận IPv4/IPv6, bỏ AF_UNIX... (trả false).
inline bool netLogSplitSockaddr(const struct sockaddr *sa, char *ipOut, size_t ipOutLen, uint16_t *portOut) {
    if (!sa) return false;
    if (sa->sa_family == AF_INET) {
        const struct sockaddr_in *s4 = (const struct sockaddr_in *)sa;
        inet_ntop(AF_INET, &s4->sin_addr, ipOut, ipOutLen);
        *portOut = ntohs(s4->sin_port);
    } else if (sa->sa_family == AF_INET6) {
        const struct sockaddr_in6 *s6 = (const struct sockaddr_in6 *)sa;
        inet_ntop(AF_INET6, &s6->sin6_addr, ipOut, ipOutLen);
        *portOut = ntohs(s6->sin6_port);
    } else {
        return false;
    }
    return true;
}

// sockaddr -> "ip:port". Chỉ nhận IPv4/IPv6, bỏ AF_UNIX... (trả false).
inline bool netLogFormatSockaddr(const struct sockaddr *sa, char *out, size_t outLen) {
    char ip[INET6_ADDRSTRLEN] = {0};
    uint16_t port = 0;
    if (!netLogSplitSockaddr(sa, ip, sizeof(ip), &port)) return false;
    snprintf(out, outLen, "%s:%d", ip, port);
    return true;
}

// ===== Soi nội dung UDP gửi tới dải cổng nghi vấn (10000-10020) - CHỈ QUAN SÁT =====
// Vì sao cần: dải cổng này trước đây từng bị chặn nhầm lẫn với traffic server thật (xem revert
// "block ALL UDP..."), nên giờ chỉ soi xem có gói nào chứa nội dung dạng chữ (JSON/HTTP/tracking
// string...) lộ ra không, không đổi hành vi mạng - để biết chắc trước khi quyết định chặn gì.
inline bool netLogPortInPeekRange(uint16_t port) { return port >= 10000 && port <= 10020; }

// ===== Công tắc bật/tắt chặn UDP (menu) - 2 quy tắc ĐỘC LẬP, gộp OR =====
// Vì sao: người dùng điều khiển trực tiếp từ 1 switch trong menu (Menu.mm gọi
// netLogSetUdpPortBlockEnabled) - không còn phụ thuộc đồng hồ hệ thống như bản điều tiết theo
// thời gian trước đó. Khi bật, 1 gói UDP bị bỏ (không thật sự ra khỏi máy) nếu khớp 1 trong 2
// quy tắc sau (netLogUdpShouldBlock gộp OR):
//   1) Cổng đích nằm trong dải nghi vấn 10000-10020 (netLogPortInPeekRange) VÀ độ dài gói nằm
//      trong [NETLOG_UDP_BLOCK_LEN2_MIN, NETLOG_UDP_BLOCK_LEN2_MAX] byte - dải cổng này còn lẫn
//      traffic gameplay thật (xem log "Revert socket-layer UDP/TCP blocking") nên lọc thêm
//      theo len để tránh chặn nhầm gói không phải mục tiêu.
//   2) Cổng đích là 443 hoặc 0 (netLogPortInHardBlockSet) - chặn MỌI kích thước, không cần nằm
//      trong dải 10000-10020 (443 = QUIC/HTTP3 qua UDP, 0 = cổng không hợp lệ/dò quét).
// Hook vẫn trả về đúng số byte như gửi thành công cho gói bị bỏ, để không phá logic
// reliability/retry riêng của game.
#define NETLOG_UDP_BLOCK_LEN2_MIN 2000
#define NETLOG_UDP_BLOCK_LEN2_MAX 4000
static std::atomic<bool> g_udpPortBlockEnabled{false};
inline void netLogSetUdpPortBlockEnabled(bool enabled) { g_udpPortBlockEnabled.store(enabled, std::memory_order_relaxed); }
inline bool netLogUdpPortBlockEnabled() { return g_udpPortBlockEnabled.load(std::memory_order_relaxed); }
inline bool netLogUdpLenInBlockRange(size_t len) {
    return len >= NETLOG_UDP_BLOCK_LEN2_MIN && len <= NETLOG_UDP_BLOCK_LEN2_MAX;
}
// Cổng chặn cứng: LUÔN chặn (mọi độ dài) khi công tắc đang bật, không cần nằm trong dải peek.
inline bool netLogPortInHardBlockSet(uint16_t port) { return port == 443 || port == 0; }
// Điểm quyết định chặn DUY NHẤT - dùng ở cả đường sendto() lẫn write()/send() trên fd đã
// connect(), để 2 đường không lệch quy tắc nhau.
inline bool netLogUdpShouldBlock(uint16_t port, size_t len) {
    if (!netLogUdpPortBlockEnabled()) return false;
    if (netLogPortInPeekRange(port) && netLogUdpLenInBlockRange(len)) return true;
    if (netLogPortInHardBlockSet(port)) return true;
    return false;
}

// Đổi buffer thành preview: ký tự in được giữ nguyên, còn lại thay '.', cắt ngắn cho vừa 1 dòng
// log - không phải giải mã, chỉ để mắt người nhìn ra có chữ/JSON lộ trong payload hay không.
inline void netLogPreviewPayload(const void *buf, size_t len, std::string &out) {
    const unsigned char *p = (const unsigned char *)buf;
    size_t n = len < 48 ? len : 48;
    out.resize(n);
    for (size_t i = 0; i < n; i++) {
        unsigned char c = p[i];
        out[i] = (c >= 0x20 && c < 0x7f) ? (char)c : '.';
    }
}

// Độ dài đoạn ký tự in-được LIÊN TỤC dài nhất trong buffer - đa số gói UDP gameplay (vị trí,
// input...) là dữ liệu nhị phân thuần nên toàn dấu chấm rời rạc; chỉ đoạn chữ liên tục ĐỦ DÀI
// mới đáng ngờ là chuỗi/JSON/tracking string thật. Dùng để lọc bớt noise binary khỏi log.
inline size_t netLogLongestPrintableRun(const void *buf, size_t len) {
    const unsigned char *p = (const unsigned char *)buf;
    size_t best = 0, cur = 0;
    for (size_t i = 0; i < len; i++) {
        if (p[i] >= 0x20 && p[i] < 0x7f) {
            cur++;
            if (cur > best) best = cur;
        } else {
            cur = 0;
        }
    }
    return best;
}

// Payload ngắn (24-34 byte) + byte ngẫu nhiên/mã hoá có ~37% khả năng rơi vào dải in-được mỗi
// byte, nên ngưỡng 10 vẫn lọt khá nhiều chuỗi trùng hợp (đặc biệt khi có 1 đoạn magic-byte cố
// định kiểu ";8:*:" lặp lại ở đầu mọi gói - không phải chuỗi bí mật, chỉ là header giao thức).
// Nâng ngưỡng lên 16 cho chắc hơn: 1 đoạn 16+ ký tự in-được liên tục khó xảy ra ngẫu nhiên
// (~37%^16 ≈ 1 phần triệu mỗi gói), nên nếu vẫn thấy thì gần như chắc chắn là chuỗi thật.
#define NETLOG_UDP_PEEK_MIN_TEXT_RUN 16

// fd UDP đã connect() tới 1 peer "đáng quan tâm" (dải cổng nghi vấn 10000-10020 HOẶC cổng nằm
// trong netLogPortInHardBlockSet) -> port thật (để log/soi/chặn write()/send() sau đó, khác
// sniPendingFds: không phải "chỉ 1 lần", vì mỗi gói UDP là 1 đơn vị độc lập, không phải 1 bắt
// tay như TLS). netLogRaw tự gom trùng nếu payload giống hệt, nên không lo log tràn.
static std::mutex g_udpPeekMutex;
static std::unordered_map<int, uint16_t> g_udpPeekFds;
static std::atomic<int> g_udpPeekCount{0};

inline void udpPeekMark(int fd, uint16_t port) {
    std::lock_guard<std::mutex> lock(g_udpPeekMutex);
    if (g_udpPeekFds.size() > 256) { g_udpPeekFds.clear(); g_udpPeekCount.store(0, std::memory_order_relaxed); }
    if (g_udpPeekFds.emplace(fd, port).second) g_udpPeekCount.fetch_add(1, std::memory_order_relaxed);
}

// Trả cổng đã lưu cho fd nếu đang được soi, hoặc -1 nếu không (fd không nằm trong danh sách
// soi). LƯU Ý: trả int32_t chứ không phải uint16_t như trước - cổng 0 giờ là 1 cổng CÓ THỂ bị
// theo dõi thật (netLogPortInHardBlockSet), nên không thể dùng 0 làm sentinel "không tìm thấy"
// nữa (sẽ đụng độ với "fd đang theo dõi cổng 0 thật").
inline int32_t udpPeekPort(int fd) {
    if (g_udpPeekCount.load(std::memory_order_relaxed) == 0) return -1; // hot-path bail, không lock
    std::lock_guard<std::mutex> lock(g_udpPeekMutex);
    auto it = g_udpPeekFds.find(fd);
    return it == g_udpPeekFds.end() ? -1 : (int32_t)it->second;
}

// fd TCP vừa connect() xong, đang đợi write()/send() đầu tiên để soi ClientHello (SNI)
// hoặc header HTTP thuần - xem phần "SNI / plaintext-HTTP sniffer" bên dưới.
//
// write() bị hook CHO MỌI fd của cả process (file, pipe, asset...), không riêng socket, nên
// hot path phải né khoá mutex khi rõ ràng không có gì để soi: g_sniPendingCount là gợi ý
// nhanh (không cần chính xác tuyệt đối) để hooked_write/hooked_send bail ngay không cần lock.
static std::mutex g_sniPendingMutex;
static std::unordered_set<int> g_sniPendingFds;
static std::atomic<int> g_sniPendingCount{0};

inline void sniMarkPending(int fd) {
    std::lock_guard<std::mutex> lock(g_sniPendingMutex);
    // fd không hook close() nên không dọn được lúc đóng socket - tự dọn nếu phình to,
    // rơi rớt vài fd không sao vì đây chỉ là gợi ý "soi 1 lần", không phải state bắt buộc đúng.
    if (g_sniPendingFds.size() > 1024) { g_sniPendingFds.clear(); g_sniPendingCount.store(0, std::memory_order_relaxed); }
    if (g_sniPendingFds.insert(fd).second) g_sniPendingCount.fetch_add(1, std::memory_order_relaxed);
}

inline bool sniTakePending(int fd) {
    if (g_sniPendingCount.load(std::memory_order_relaxed) == 0) return false; // hot-path bail, không lock
    std::lock_guard<std::mutex> lock(g_sniPendingMutex);
    if (g_sniPendingFds.erase(fd) == 0) return false;
    g_sniPendingCount.fetch_sub(1, std::memory_order_relaxed);
    return true;
}

// ===== TCP PEEK - soi CẢ HAI CHIỀU (gửi/nhận) trên MỌI gói của 1 kết nối TCP đã connect() xong,
// không chỉ gói ĐẦU TIÊN như sniPendingFds (dùng để bắt SNI/HTTP header, xong việc thì bỏ) =====
// Vì sao cần: nghi ngờ message FFANTI (EProtocol.Proto FFANTI=29 trong dump.cs, cùng nhóm với
// MATCHMAKING/GROUP/MAIL...) đi qua kết nối TCP lobby ĐÃ MỞ SẴN từ lúc đăng nhập (connect() chỉ
// gọi 1 lần, rất lâu trước lúc vào trận) - trước đây NetLog KHÔNG có cách nào thấy traffic
// CHIỀU NHẬN (chưa hook read()/recv() chút nào) lẫn traffic gửi/nhận SAU gói đầu tiên trên 1 fd
// TCP đã tồn tại từ trước, nên toàn bộ traffic thật của FFANTI (nếu đúng đi qua đây) là vô hình.
static std::mutex g_tcpPeekMutex;
static std::unordered_map<int, uint16_t> g_tcpPeekFds;
static std::atomic<int> g_tcpPeekCount{0};

inline void tcpPeekMark(int fd, uint16_t port) {
    std::lock_guard<std::mutex> lock(g_tcpPeekMutex);
    if (g_tcpPeekFds.size() > 256) { g_tcpPeekFds.clear(); g_tcpPeekCount.store(0, std::memory_order_relaxed); }
    if (g_tcpPeekFds.emplace(fd, port).second) g_tcpPeekCount.fetch_add(1, std::memory_order_relaxed);
}

inline int32_t tcpPeekPort(int fd) {
    if (g_tcpPeekCount.load(std::memory_order_relaxed) == 0) return -1; // hot-path bail, không lock
    std::lock_guard<std::mutex> lock(g_tcpPeekMutex);
    auto it = g_tcpPeekFds.find(fd);
    return it == g_tcpPeekFds.end() ? -1 : (int32_t)it->second;
}

// Log preview 1 chiều (SEND/RECV) cho fd TCP đang theo dõi - dùng chung cho write()/send()
// (chiều gửi, gọi SAU sniInspectFirstWrite để không log trùng gói đầu) và read()/recv() (chiều
// nhận, hoàn toàn MỚI - trước đây không có).
inline void tcpPeekLog(const char *direction, int fd, const void *buf, size_t count) {
    if (!buf || count == 0) return;
    int32_t port = tcpPeekPort(fd);
    if (port < 0) return;
    std::string preview;
    netLogPreviewPayload(buf, count, preview);
    char detail[180];
    snprintf(detail, sizeof(detail), "%s fd=%d port=%d len=%zu | %s", direction, fd, (int)port, count, preview.c_str());
    netLogRaw("TCP-PEEK", detail);
}

// ===== HOOK connect() - bắt cả TCP lẫn UDP, kể cả connect thẳng vào IP không qua DNS =====
static int (*orig_connect)(int, const struct sockaddr *, socklen_t);

inline int hooked_connect(int fd, const struct sockaddr *addr, socklen_t len) {
    char ip[INET6_ADDRSTRLEN] = {0};
    uint16_t port = 0;
    bool haveAddr = addr && netLogSplitSockaddr(addr, ip, sizeof(ip), &port);
    int type = 0;
    socklen_t tl = sizeof(type);
    bool isTcp = getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &tl) == 0 && type == SOCK_STREAM;

    if (haveAddr && g_netBlockCheck) {
        char reason[256];
        if (g_netBlockCheck(ip, port, reason, sizeof(reason))) {
            netLogRaw("CONN-BLK", reason);
            errno = ECONNREFUSED;
            return -1;
        }
    }

    if (haveAddr) {
        const char *proto = isTcp ? "TCP" : (type == SOCK_DGRAM ? "UDP" : "SOCK");
        char ep[96];
        snprintf(ep, sizeof(ep), "%s:%d", ip, port);
        netLogRaw(proto, ep);
    }

    int ret = orig_connect(fd, addr, len);
    // ClientHello (nếu có) là gói ĐẦU TIÊN client gửi sau khi kết nối TCP xong - đánh dấu fd
    // này để hook write()/send() bên dưới soi đúng gói đầu, không soi nhầm data sau đó.
    if (isTcp && (ret == 0 || errno == EINPROGRESS)) {
        sniMarkPending(fd);
        // Theo dõi CẢ kết nối TCP này lâu dài (không chỉ gói đầu như sniMarkPending) - để soi
        // được traffic 2 chiều SAU bắt tay TLS, vd nghi ngờ FFANTI (protocol=29) đi qua đây.
        tcpPeekMark(fd, port);
    }
    // Theo dõi TOÀN BỘ fd UDP (không giới hạn dải cổng nghi vấn nữa) - user yêu cầu thấy MỌI
    // request, kể cả cổng match/gameplay thật thay đổi mỗi trận, không đoán trước được port.
    if (!isTcp && ret == 0) udpPeekMark(fd, port);
    return ret;
}

// ===== HOOK connectx() - biến thể connect() hỗ trợ Multipath TCP/TCP Fast Open =====
// Vì sao cần: CFNetwork/NSURLSession và Network.framework (NWConnection) ngày càng dùng
// connectx() thay vì connect() cổ điển cho TCP (Happy Eyeballs, TFO...) - 1 kết nối "realtime"
// dài hạn (vd server sự kiện ggblueshark.com) có thể đi thẳng qua đường này, không lộ ra ở
// TCP/DNS log dù connect() đã hook đủ. Chung logic block-check/log/đánh dấu SNI với connect().
static int (*orig_connectx)(int, const sa_endpoints_t *, sae_associd_t, unsigned int,
                             const struct iovec *, unsigned int, size_t *, sae_connid_t *);

inline int hooked_connectx(int fd, const sa_endpoints_t *endpoints, sae_associd_t associd, unsigned int flags,
                            const struct iovec *iov, unsigned int iovcnt, size_t *len, sae_connid_t *connid) {
    const struct sockaddr *dst = endpoints ? endpoints->sae_dstaddr : NULL;
    char ip[INET6_ADDRSTRLEN] = {0};
    uint16_t port = 0;
    bool haveAddr = dst && netLogSplitSockaddr(dst, ip, sizeof(ip), &port);
    int type = 0;
    socklen_t tl = sizeof(type);
    bool isTcp = getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &tl) == 0 && type == SOCK_STREAM;

    if (haveAddr && g_netBlockCheck) {
        char reason[256];
        if (g_netBlockCheck(ip, port, reason, sizeof(reason))) {
            netLogRaw("CONN-BLK", reason);
            errno = ECONNREFUSED;
            return -1;
        }
    }

    if (haveAddr) {
        const char *proto = isTcp ? "TCP" : (type == SOCK_DGRAM ? "UDP" : "SOCK");
        char ep[96];
        snprintf(ep, sizeof(ep), "%s:%d", ip, port);
        netLogRaw(proto, ep);
    }

    int ret = orig_connectx(fd, endpoints, associd, flags, iov, iovcnt, len, connid);
    if (isTcp && (ret == 0 || errno == EINPROGRESS)) {
        sniMarkPending(fd);
        // Theo dõi CẢ kết nối TCP này lâu dài (không chỉ gói đầu như sniMarkPending) - để soi
        // được traffic 2 chiều SAU bắt tay TLS, vd nghi ngờ FFANTI (protocol=29) đi qua đây.
        tcpPeekMark(fd, port);
    }
    // Theo dõi TOÀN BỘ fd UDP (không giới hạn dải cổng nghi vấn nữa) - user yêu cầu thấy MỌI
    // request, kể cả cổng match/gameplay thật thay đổi mỗi trận, không đoán trước được port.
    if (!isTcp && ret == 0) udpPeekMark(fd, port);
    return ret;
}

// ===== HOOK sendto() - UDP connectionless (netcode game hay bắn UDP không connect()) =====
// Gọi cực nhiều lần/giây nhưng netLogRaw gom trùng nên mỗi đích chỉ ghi 1 lần -> nhẹ.
static ssize_t (*orig_sendto)(int, const void *, size_t, int, const struct sockaddr *, socklen_t);

inline ssize_t hooked_sendto(int fd, const void *buf, size_t len, int flags,
                             const struct sockaddr *dest, socklen_t dlen) {
    if (dest) {
        char ip[INET6_ADDRSTRLEN] = {0};
        uint16_t port = 0;
        if (netLogSplitSockaddr(dest, ip, sizeof(ip), &port)) {
            char ep[96];
            snprintf(ep, sizeof(ep), "%s:%d", ip, port);
            netLogRaw("UDP", ep);
            // Preview MỌI gói có nội dung, không chỉ khi có đoạn chữ dài hay cổng nằm trong dải
            // nghi vấn - user muốn thấy hết, kể cả preview toàn dấu '.' (nội dung mã hoá/nhị
            // phân) để biết ít nhất kích cỡ/tần suất gói, không chỉ khi may mắn có chữ lộ ra.
            if (buf && len > 0) {
                std::string preview;
                netLogPreviewPayload(buf, len, preview);
                char detail[160];
                snprintf(detail, sizeof(detail), "%s len=%zu | %s", ep, len, preview.c_str());
                netLogRaw("UDP-PEEK", detail);
            }
            if (netLogUdpShouldBlock(port, len)) {
                netLogRaw("UDP-BLK", ep);
                return (ssize_t)len; // giả vờ gửi thành công, thực ra bỏ gói - công tắc đang bật + khớp quy tắc
            }
        }
    }
    return orig_sendto(fd, buf, len, flags, dest, dlen);
}

// ===== SNI / plaintext-HTTP sniffer =====
// Vì sao cần: JunkAdURLProtocol chỉ soi được request đi qua NSURLSession/NSURLConnection.
// Game hay thư viện mạng riêng (BoringSSL/OpenSSL/libcurl tự vendor, socket thuần) gọi
// write()/send() thẳng, không đụng NSURLProtocol -> tầng đó vô hình với NetLog. ClientHello
// (bước đầu tiên của bắt tay TLS) và request-line HTTP thuần đều đi ở dạng RÕ trên chính gói
// write() ĐẦU TIÊN sau khi connect() xong, nên chặn đúng gói đó là bắt được tên miền HTTPS/HTTP
// thật sự đang gọi, không cần giải mã TLS.

// Đọc extension server_name (SNI) từ 1 gói TLS ClientHello (RFC 6066 §3). Trả false nếu
// buffer không phải ClientHello hợp lệ hoặc không có SNI (vd nối thẳng bằng IP).
inline bool tlsParseSNI(const unsigned char *p, size_t len, std::string &outHost) {
    if (len < 43 || p[0] != 0x16 /* Handshake */) return false;
    const unsigned char *hs = p + 5;
    size_t hsLen = len - 5;
    if (hsLen < 39 || hs[0] != 0x01 /* ClientHello */) return false;

    size_t pos = 4 + 2 + 32; // handshake header(4) + client_version(2) + random(32)
    if (pos >= hsLen) return false;
    size_t sidLen = hs[pos]; pos += 1 + sidLen;
    if (pos + 2 > hsLen) return false;
    size_t csLen = ((size_t)hs[pos] << 8) | hs[pos + 1]; pos += 2 + csLen;
    if (pos + 1 > hsLen) return false;
    size_t cmLen = hs[pos]; pos += 1 + cmLen;
    if (pos + 2 > hsLen) return false;
    size_t extTotalLen = ((size_t)hs[pos] << 8) | hs[pos + 1]; pos += 2;
    size_t extEnd = pos + extTotalLen;
    if (extEnd > hsLen) extEnd = hsLen;

    while (pos + 4 <= extEnd) {
        unsigned int extType = ((unsigned int)hs[pos] << 8) | hs[pos + 1];
        unsigned int extLen  = ((unsigned int)hs[pos + 2] << 8) | hs[pos + 3];
        pos += 4;
        if (pos + extLen > extEnd) break;
        if (extType == 0x0000 /* server_name */ && extLen >= 5) {
            size_t sp = pos + 2; // bỏ server_name_list length(2)
            size_t listEnd = pos + extLen;
            if (sp + 3 <= listEnd && hs[sp] == 0x00 /* host_name */) {
                size_t nameLen = ((size_t)hs[sp + 1] << 8) | hs[sp + 2];
                sp += 3;
                if (sp + nameLen <= listEnd) {
                    outHost.assign((const char *)hs + sp, nameLen);
                    return true;
                }
            }
        }
        pos += extLen;
    }
    return false;
}

// Nhận diện request HTTP thuần (không TLS) từ gói write() đầu tiên: "METHOD /path HTTP/1.1"
// + header "Host:". Trả false nếu không giống HTTP (vd giao thức nhị phân riêng của game).
// outHost rỗng nếu request không có header Host (vẫn coi là HTTP để log, nhưng không chặn được
// theo domain vì không biết host).
inline bool httpParsePlaintext(const char *p, size_t len, std::string &outHost, std::string &outSummary) {
    static const char *kMethods[] = {"GET ", "POST ", "PUT ", "HEAD ", "DELETE ", "OPTIONS ", "PATCH "};
    bool looksHttp = false;
    for (size_t i = 0; i < sizeof(kMethods) / sizeof(kMethods[0]); i++) {
        size_t mlen = strlen(kMethods[i]);
        if (len >= mlen && memcmp(p, kMethods[i], mlen) == 0) { looksHttp = true; break; }
    }
    if (!looksHttp) return false;

    const char *lineEnd = (const char *)memchr(p, '\r', len);
    std::string reqLine = lineEnd ? std::string(p, lineEnd - p) : std::string(p, len < 200 ? len : 200);

    outHost.clear();
    const void *hp = memmem(p, len, "Host:", 5);
    if (hp) {
        const char *hostStart = (const char *)hp + 5;
        const char *bufEnd = p + len;
        while (hostStart < bufEnd && *hostStart == ' ') hostStart++;
        const char *hostEnd = (const char *)memchr(hostStart, '\r', bufEnd - hostStart);
        if (hostEnd) outHost.assign(hostStart, hostEnd - hostStart);
    }
    outSummary = outHost.empty() ? reqLine : ("http://" + outHost + " | " + reqLine);
    return true;
}

// ===== Điểm cắm chặn tuỳ chọn theo hostname (DNSBlock.h đăng ký qua netLogSetHostBlockCheck) =====
// Gọi khi soi được tên miền thật từ ClientHello (SNI) hoặc header Host của request HTTP thuần
// - tức là request đã bay tới tận write()/send(), không qua NSURLProtocol nên JunkAdURLProtocol
// không chặn được. Nếu bị chặn, netLogSetLearnBlockedIP (nếu có) được gọi để nạp luôn IP peer
// vào registry chặn connect() - lần sau gặp lại IP này không cần soi SNI nữa.
typedef bool (*NetHostBlockCheckFn)(const char *host);
typedef void (*NetLearnBlockedIPFn)(const char *ip);
static NetHostBlockCheckFn g_netHostBlockCheck = NULL;
static NetLearnBlockedIPFn g_netLearnBlockedIP = NULL;
inline void netLogSetHostBlockCheck(NetHostBlockCheckFn fn) { g_netHostBlockCheck = fn; }
inline void netLogSetLearnBlockedIP(NetLearnBlockedIPFn fn) { g_netLearnBlockedIP = fn; }

inline void netLogLearnPeerIP(int fd) {
    if (!g_netLearnBlockedIP) return;
    struct sockaddr_storage ss;
    socklen_t sl = sizeof(ss);
    if (getpeername(fd, (struct sockaddr *)&ss, &sl) != 0) return;
    char ip[INET6_ADDRSTRLEN] = {0};
    uint16_t port = 0;
    if (netLogSplitSockaddr((struct sockaddr *)&ss, ip, sizeof(ip), &port)) g_netLearnBlockedIP(ip);
}

// Soi gói write()/send() nếu fd đang được đánh dấu "vừa connect() TCP xong, chưa gửi gì".
// Dù có tìm ra gì hay không cũng chỉ soi ĐÚNG 1 LẦN cho fd đó (sniTakePending), tránh phải
// parse mọi byte đi qua mọi kết nối - handshake/HTTP header chỉ nằm ở gói đầu.
// Trả true nếu gói này bị CHẶN (caller không được gọi hàm write/send gốc).
inline bool sniInspectFirstWrite(int fd, const void *buf, size_t count) {
    if (!buf || count == 0 || !sniTakePending(fd)) return false;

    std::string host;
    if (tlsParseSNI((const unsigned char *)buf, count, host)) {
        netLogRaw("TLS-SNI", host.c_str());
        if (!host.empty() && g_netHostBlockCheck && g_netHostBlockCheck(host.c_str())) {
            netLogRaw("TLS-BLK", host.c_str());
            netLogLearnPeerIP(fd);
            return true;
        }
        return false;
    }

    std::string reqHost, summary;
    if (httpParsePlaintext((const char *)buf, count, reqHost, summary)) {
        netLogRaw("HTTP", summary.c_str());
        if (!reqHost.empty() && g_netHostBlockCheck && g_netHostBlockCheck(reqHost.c_str())) {
            netLogRaw("HTTP-BLK", summary.c_str());
            netLogLearnPeerIP(fd);
            return true;
        }
    }
    return false;
}

// Soi payload UDP cho fd đã connect() tới BẤT KỲ peer nào (udpPeekMark giờ đánh dấu mọi fd UDP,
// không riêng dải cổng nghi vấn nữa) - MỌI gói, không phải chỉ gói đầu như TLS/HTTP, vì UDP là
// datagram rời rạc chứ không phải 1 bắt tay liên tục. Trả true nếu gói này đang bị chặn (công
// tắc netLogUdpPortBlockEnabled đang bật + khớp netLogUdpShouldBlock) - caller trả về count giả
// vờ, không gọi write()/send() gốc.
inline bool udpPeekInspectWrite(int fd, const void *buf, size_t count) {
    if (!buf || count == 0) return false;
    int32_t portRaw = udpPeekPort(fd);
    if (portRaw < 0) return false; // fd không nằm trong danh sách theo dõi
    uint16_t port = (uint16_t)portRaw;
    // Preview MỌI gói có nội dung - không lọc theo cổng/độ dài chữ nữa (xem lý do ở hooked_sendto).
    {
        std::string preview;
        netLogPreviewPayload(buf, count, preview);
        char detail[160];
        snprintf(detail, sizeof(detail), "port=%d len=%zu | %s", port, count, preview.c_str());
        netLogRaw("UDP-PEEK", detail);
    }
    if (netLogUdpShouldBlock(port, count)) {
        char detail[64];
        snprintf(detail, sizeof(detail), "port=%d len=%zu", port, count);
        netLogRaw("UDP-BLK", detail);
        return true;
    }
    return false;
}

static ssize_t (*orig_write)(int, const void *, size_t);
inline ssize_t hooked_write(int fd, const void *buf, size_t count) {
    if (sniInspectFirstWrite(fd, buf, count)) { errno = ECONNRESET; return -1; }
    if (udpPeekInspectWrite(fd, buf, count)) return (ssize_t)count;
    tcpPeekLog("SEND", fd, buf, count); // no-op nếu fd không phải TCP đang theo dõi
    return orig_write(fd, buf, count);
}

static ssize_t (*orig_send)(int, const void *, size_t, int);
inline ssize_t hooked_send(int fd, const void *buf, size_t count, int flags) {
    if (sniInspectFirstWrite(fd, buf, count)) { errno = ECONNRESET; return -1; }
    if (udpPeekInspectWrite(fd, buf, count)) return (ssize_t)count;
    tcpPeekLog("SEND", fd, buf, count); // no-op nếu fd không phải TCP đang theo dõi
    return orig_send(fd, buf, count, flags);
}

// ===== HOOK read()/recv()/recvfrom() - CHIỀU NHẬN, hoàn toàn MỚI (trước đây NetLog chỉ soi
// được chiều gửi) =====
// Vì sao cần: mọi hook trước đó (write/send/sendto) chỉ thấy được data CLIENT GỬI LÊN - hoàn
// toàn không thấy gì SERVER GỬI XUỐNG (packet netcode nhận về, hay message FFANTI/OnRecvMsg nếu
// server chủ động đẩy xuống qua kết nối lobby TCP đã mở sẵn). Gọi orig_* TRƯỚC (lấy dữ liệu
// thật đã nhận), rồi mới soi/log - không đổi hành vi gì, chỉ quan sát thêm.
static ssize_t (*orig_read)(int, void *, size_t);
inline ssize_t hooked_read(int fd, void *buf, size_t count) {
    ssize_t ret = orig_read(fd, buf, count);
    if (ret > 0) {
        tcpPeekLog("RECV", fd, buf, (size_t)ret);
        if (udpPeekPort(fd) >= 0) {
            std::string preview;
            netLogPreviewPayload(buf, (size_t)ret, preview);
            char detail[180];
            snprintf(detail, sizeof(detail), "RECV port=%d len=%zd | %s", (int)udpPeekPort(fd), ret, preview.c_str());
            netLogRaw("UDP-PEEK", detail);
        }
    }
    return ret;
}

static ssize_t (*orig_recv)(int, void *, size_t, int);
inline ssize_t hooked_recv(int fd, void *buf, size_t len, int flags) {
    ssize_t ret = orig_recv(fd, buf, len, flags);
    if (ret > 0) {
        tcpPeekLog("RECV", fd, buf, (size_t)ret);
        if (udpPeekPort(fd) >= 0) {
            std::string preview;
            netLogPreviewPayload(buf, (size_t)ret, preview);
            char detail[180];
            snprintf(detail, sizeof(detail), "RECV port=%d len=%zd | %s", (int)udpPeekPort(fd), ret, preview.c_str());
            netLogRaw("UDP-PEEK", detail);
        }
    }
    return ret;
}

// recvfrom() - UDP connectionless nhận về (đối xứng với hooked_sendto ở trên) - fd chưa chắc đã
// connect() (game có thể dùng 1 fd UDP không-connect để nhận từ nhiều peer), nên log theo đúng
// địa chỉ NGUỒN lấy được từ tham số src, không phụ thuộc bảng theo dõi udpPeekMark.
static ssize_t (*orig_recvfrom)(int, void *, size_t, int, struct sockaddr *, socklen_t *);
inline ssize_t hooked_recvfrom(int fd, void *buf, size_t len, int flags, struct sockaddr *src, socklen_t *srclen) {
    ssize_t ret = orig_recvfrom(fd, buf, len, flags, src, srclen);
    if (ret > 0 && buf) {
        char ip[INET6_ADDRSTRLEN] = {0};
        uint16_t port = 0;
        std::string preview;
        netLogPreviewPayload(buf, (size_t)ret, preview);
        char detail[200];
        if (src && netLogSplitSockaddr(src, ip, sizeof(ip), &port)) {
            snprintf(detail, sizeof(detail), "RECV %s:%d len=%zd | %s", ip, port, ret, preview.c_str());
        } else {
            snprintf(detail, sizeof(detail), "RECV len=%zd | %s", ret, preview.c_str());
        }
        netLogRaw("UDP-PEEK", detail);
    }
    return ret;
}

// Cài hook tầng socket. Gọi 1 lần lúc khởi động (từ +[DeltaMenu load]).
inline void installNetLogHook() {
    orig_connect  = (int (*)(int, const struct sockaddr *, socklen_t))dlsym((void *)RTLD_DEFAULT, "connect");
    orig_connectx = (int (*)(int, const sa_endpoints_t *, sae_associd_t, unsigned int,
                              const struct iovec *, unsigned int, size_t *, sae_connid_t *))dlsym((void *)RTLD_DEFAULT, "connectx");
    orig_sendto   = (ssize_t (*)(int, const void *, size_t, int, const struct sockaddr *, socklen_t))dlsym((void *)RTLD_DEFAULT, "sendto");
    orig_write    = (ssize_t (*)(int, const void *, size_t))dlsym((void *)RTLD_DEFAULT, "write");
    orig_send     = (ssize_t (*)(int, const void *, size_t, int))dlsym((void *)RTLD_DEFAULT, "send");
    orig_read     = (ssize_t (*)(int, void *, size_t))dlsym((void *)RTLD_DEFAULT, "read");
    orig_recv     = (ssize_t (*)(int, void *, size_t, int))dlsym((void *)RTLD_DEFAULT, "recv");
    orig_recvfrom = (ssize_t (*)(int, void *, size_t, int, struct sockaddr *, socklen_t *))dlsym((void *)RTLD_DEFAULT, "recvfrom");

    struct rebinding netRebindings[] = {
        {"connect",  (void *)hooked_connect,  (void **)&orig_connect},
        {"connectx", (void *)hooked_connectx, (void **)&orig_connectx},
        {"sendto",   (void *)hooked_sendto,   (void **)&orig_sendto},
        {"write",    (void *)hooked_write,    (void **)&orig_write},
        {"send",     (void *)hooked_send,     (void **)&orig_send},
        {"read",     (void *)hooked_read,     (void **)&orig_read},
        {"recv",     (void *)hooked_recv,     (void **)&orig_recv},
        {"recvfrom", (void *)hooked_recvfrom, (void **)&orig_recvfrom},
    };
    rebind_symbols(netRebindings, sizeof(netRebindings) / sizeof(netRebindings[0]));
}
