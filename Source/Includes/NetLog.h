#pragma once
// ============================================================================
// NetLog - logger MẠNG THỤ ĐỘNG (chỉ quan sát, KHÔNG chặn)
// ----------------------------------------------------------------------------
// Mục đích: soi xem game gửi request gì lên server, ở tầng nào - để biết đường
// nào (vd server realtime ggblueshark.com) đi qua đâu mà chặn cho trúng.
//
// 4 tầng được ghi:
//   DNS  : hostname xin phân giải (getaddrinfo/gethostbyname...) - do DNSBlock.h gọi vào
//   TCP  : connect() tới socket SOCK_STREAM  -> ip:port
//   UDP  : connect()/sendto() tới socket SOCK_DGRAM -> ip:port (bắt cả khi connect thẳng IP)
//   HTTP : full URL kèm path - do JunkAdURLProtocol gọi vào (xem DNSBlock.h)
//
// TẤT CẢ hook đều gọi hàm gốc rồi trả nguyên kết quả -> không đổi hành vi mạng,
// không làm hỏng luồng như bản blocking trước. Log gom trùng: mỗi chuỗi chỉ ghi 1 lần.
// ============================================================================
#import <Foundation/Foundation.h>
#import <sys/socket.h>
#import <netinet/in.h>
#import <arpa/inet.h>
#import <time.h>

#include <string>
#include <unordered_set>
#include <mutex>
#include <atomic>

#import "fishhook.h"

#define NETLOG_MAX_LINES 100
#define NETLOG_LINE_LEN  200

static char g_netLog[NETLOG_MAX_LINES][NETLOG_LINE_LEN];
static int  g_netLogHead = 0;                       // vị trí ghi kế tiếp (ring buffer)
static std::atomic<unsigned int> g_netLogTotal{0};  // tổng dòng đã ghi (đã trừ trùng)
static std::mutex g_netLogMutex;
static std::unordered_set<std::string> g_netLogSeen; // gom trùng: chuỗi nào ghi rồi thì bỏ

// Ghi 1 dòng "[HH:MM:SS] [TẦNG] chi_tiết". Trùng thì bỏ qua, giữ log gọn & nhiều tín hiệu.
inline void netLogRaw(const char *layer, const char *detail) {
    if (!layer || !detail) return;
    std::string key = std::string(layer) + "|" + detail;
    std::lock_guard<std::mutex> lock(g_netLogMutex);
    if (!g_netLogSeen.insert(key).second) return; // đã có -> bỏ

    time_t t = time(NULL);
    struct tm tmv;
    localtime_r(&t, &tmv);
    snprintf(g_netLog[g_netLogHead], NETLOG_LINE_LEN, "%02d:%02d:%02d [%s] %s",
             tmv.tm_hour, tmv.tm_min, tmv.tm_sec, layer, detail);
    g_netLogHead = (g_netLogHead + 1) % NETLOG_MAX_LINES;
    g_netLogTotal.fetch_add(1, std::memory_order_relaxed);
}

inline unsigned int NetLog_count() { return g_netLogTotal.load(std::memory_order_relaxed); }

// Bản chụp log để tab INFO hiển thị - MỚI NHẤT ở trên cùng (đỡ phải cuộn).
inline NSString *NetLog_snapshot() {
    std::lock_guard<std::mutex> lock(g_netLogMutex);
    unsigned int total = g_netLogTotal.load(std::memory_order_relaxed);
    unsigned int n = total < NETLOG_MAX_LINES ? total : NETLOG_MAX_LINES;
    if (n == 0) return @"(chưa bắt được request nào)";
    NSMutableString *s = [NSMutableString string];
    int last = (g_netLogHead - 1 + NETLOG_MAX_LINES) % NETLOG_MAX_LINES;
    for (unsigned int i = 0; i < n; i++) {
        int idx = (last - (int)i + NETLOG_MAX_LINES * 2) % NETLOG_MAX_LINES;
        [s appendFormat:@"%s\n", g_netLog[idx]];
    }
    return s;
}

// sockaddr -> "ip:port". Chỉ nhận IPv4/IPv6, bỏ AF_UNIX... (trả false).
inline bool netLogFormatSockaddr(const struct sockaddr *sa, char *out, size_t outLen) {
    if (!sa) return false;
    char ip[INET6_ADDRSTRLEN] = {0};
    int port = 0;
    if (sa->sa_family == AF_INET) {
        const struct sockaddr_in *s4 = (const struct sockaddr_in *)sa;
        inet_ntop(AF_INET, &s4->sin_addr, ip, sizeof(ip));
        port = ntohs(s4->sin_port);
    } else if (sa->sa_family == AF_INET6) {
        const struct sockaddr_in6 *s6 = (const struct sockaddr_in6 *)sa;
        inet_ntop(AF_INET6, &s6->sin6_addr, ip, sizeof(ip));
        port = ntohs(s6->sin6_port);
    } else {
        return false;
    }
    snprintf(out, outLen, "%s:%d", ip, port);
    return true;
}

// ===== HOOK connect() - bắt cả TCP lẫn UDP, kể cả connect thẳng vào IP không qua DNS =====
static int (*orig_connect)(int, const struct sockaddr *, socklen_t);

inline int hooked_connect(int fd, const struct sockaddr *addr, socklen_t len) {
    char ep[96];
    if (addr && netLogFormatSockaddr(addr, ep, sizeof(ep))) {
        int type = 0;
        socklen_t tl = sizeof(type);
        // SO_TYPE cho biết SOCK_STREAM (TCP) hay SOCK_DGRAM (UDP)
        const char *proto = "SOCK";
        if (getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &tl) == 0) {
            proto = (type == SOCK_DGRAM) ? "UDP" : (type == SOCK_STREAM ? "TCP" : "SOCK");
        }
        netLogRaw(proto, ep);
    }
    return orig_connect(fd, addr, len);
}

// ===== HOOK sendto() - UDP connectionless (netcode game hay bắn UDP không connect()) =====
// Gọi cực nhiều lần/giây nhưng netLogRaw gom trùng nên mỗi đích chỉ ghi 1 lần -> nhẹ.
static ssize_t (*orig_sendto)(int, const void *, size_t, int, const struct sockaddr *, socklen_t);

inline ssize_t hooked_sendto(int fd, const void *buf, size_t len, int flags,
                             const struct sockaddr *dest, socklen_t dlen) {
    if (dest) {
        char ep[96];
        if (netLogFormatSockaddr(dest, ep, sizeof(ep))) netLogRaw("UDP", ep);
    }
    return orig_sendto(fd, buf, len, flags, dest, dlen);
}

// Cài hook tầng socket. Gọi 1 lần lúc khởi động (từ installDNSBlockHook).
inline void installNetLogHook() {
    orig_connect = (int (*)(int, const struct sockaddr *, socklen_t))dlsym((void *)RTLD_DEFAULT, "connect");
    orig_sendto  = (ssize_t (*)(int, const void *, size_t, int, const struct sockaddr *, socklen_t))dlsym((void *)RTLD_DEFAULT, "sendto");

    struct rebinding netRebindings[] = {
        {"connect", (void *)hooked_connect, (void **)&orig_connect},
        {"sendto",  (void *)hooked_sendto,  (void **)&orig_sendto},
    };
    rebind_symbols(netRebindings, sizeof(netRebindings) / sizeof(netRebindings[0]));
}
