#pragma once
#import <Foundation/Foundation.h>
#import <netdb.h>
#import <string.h>
#import <strings.h>
#import <ctype.h> // tolower
#import <sys/socket.h>
#import <netinet/in.h>
#import <arpa/inet.h>
#import <objc/runtime.h> // class_getInstanceMethod/method_setImplementation - swizzle NSURLSessionTask
#import <dns_sd.h> // DNSServiceGetAddrInfo - đường phân giải Network.framework/CFNetwork hiện đại hay dùng, không qua getaddrinfo

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <atomic>

#import "MemoryUtils.h"
#import "fishhook.h"
#import "NetLog.h" // logger mạng thụ động (DNS/TCP/UDP/HTTP) - soi request game gửi lên server

// ===== Thống kê chặn DNS & HTTP =====
static std::atomic<unsigned long long> g_dnsBlockCount{0};  // Tổng số request đã bị chặn
static char g_dnsLastBlocked[256] = {0};                    // Host bị chặn gần nhất

inline unsigned long long DNSBlock_count()   { return g_dnsBlockCount.load(std::memory_order_relaxed); }
inline const char*        DNSBlock_lastHost() { return g_dnsLastBlocked; }

inline void dnsNoteBlocked(const char *host) {
    g_dnsBlockCount.fetch_add(1, std::memory_order_relaxed);
    if (host) {
        strncpy(g_dnsLastBlocked, host, sizeof(g_dnsLastBlocked) - 1);
        g_dnsLastBlocked[sizeof(g_dnsLastBlocked) - 1] = '\0';
    }
}

// 1. Danh sách đen các Domain chặn DNS & HTTP - CHỈ CÒN gin.freefiremobile.com theo yêu cầu, đã
// xoá hết danh sách ad-network/Garena-anti-cheat cũ (xem git history commit thêm lại DNSBlock.h
// nếu cần khôi phục danh sách đầy đủ).
static const char *kJunkDNSDomains[] = {
    "gin.freefiremobile.com",
};

// Suffix-matching trie dựa trên nhãn tên miền, duyệt từ TLD vào trong (đảo ngược nhãn)
struct JunkDomainTrieNode {
    std::unordered_map<std::string, JunkDomainTrieNode *> children;
    bool isBlocked = false;
};

class JunkDomainTrie {
public:
    JunkDomainTrie() : root(new JunkDomainTrieNode()) {}

    void insert(const char *domain) {
        JunkDomainTrieNode *node = root;
        forEachLabelReversed(domain, [&](const std::string &label) {
            JunkDomainTrieNode *&child = node->children[label];
            if (!child) child = new JunkDomainTrieNode();
            node = child;
        });
        node->isBlocked = true;
    }

    bool matches(const char *hostname) const {
        if (!hostname || !hostname[0]) return false;
        JunkDomainTrieNode *node = root;
        bool blocked = false;
        forEachLabelReversed(hostname, [&](const std::string &label) {
            if (blocked || !node) return;
            auto it = node->children.find(label);
            if (it == node->children.end()) { node = NULL; return; }
            node = it->second;
            if (node->isBlocked) blocked = true; 
        });
        return blocked;
    }

private:
    JunkDomainTrieNode *root;

    template <typename Fn>
    static void forEachLabelReversed(const char *host, Fn fn) {
        size_t len = strlen(host);
        size_t end = len;
        size_t i = len;
        while (true) {
            bool atStart = (i == 0);
            bool atDot = !atStart && host[i - 1] == '.';
            if (atStart || atDot) {
                if (end > i) {
                    std::string label(host + i, end - i);
                    for (size_t j = 0; j < label.size(); j++) label[j] = (char)tolower((unsigned char)label[j]);
                    fn(label);
                }
                if (atStart) break;
                end = i - 1;
            }
            i--;
        }
    }
};

// Khởi tạo Trie thread-safe dựa trên chuẩn C++11 "magic statics"
inline JunkDomainTrie &junkDomainTrie() {
    static JunkDomainTrie trie = [] {
        JunkDomainTrie t;
        size_t count = sizeof(kJunkDNSDomains) / sizeof(kJunkDNSDomains[0]);
        for (size_t i = 0; i < count; i++) t.insert(kJunkDNSDomains[i]);
        return t;
    }();
    return trie;
}

inline bool isJunkDNSDomain(const char *hostname) {
    return junkDomainTrie().matches(hostname);
}

// ===== 2. HOOK DNS RESOLUTION (getaddrinfo + legacy gethostbyname) =====
static int (*orig_getaddrinfo)(const char *, const char *, const struct addrinfo *, struct addrinfo **);

// Định nghĩa đầy đủ ở mục 3 bên dưới - forward declare để hooked_getaddrinfo gọi được.
inline void netBlockLearnHostIPs(const char *host);

inline int hooked_getaddrinfo(const char *hostname, const char *servname, const struct addrinfo *hints, struct addrinfo **res) {
    if (isJunkDNSDomain(hostname)) {
        dnsNoteBlocked(hostname);
        netLogRaw("DNS-BLK", hostname ? hostname : "");
        // Học IP thật của host này ở nền: lỡ game né DNS (nhớ sẵn IP rồi connect() thẳng,
        // không phân giải lại) thì vẫn chặn được ở tầng connect() (mục 3, netBlockCheckConnect).
        // Dispatch async vì orig_getaddrinfo có thể chậm/chặn mạng - không giữ thread gọi hàm này.
        if (hostname) {
            std::string host(hostname);
            dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_BACKGROUND, 0), ^{
                netBlockLearnHostIPs(host.c_str());
            });
        }
        return EAI_NONAME;
    }
    if (hostname) netLogRaw("DNS", hostname);
    return orig_getaddrinfo(hostname, servname, hints, res);
}

static struct hostent *(*orig_gethostbyname)(const char *);
static struct hostent *(*orig_gethostbyname2)(const char *, int);

inline struct hostent *hooked_gethostbyname(const char *name) {
    if (isJunkDNSDomain(name)) {
        dnsNoteBlocked(name);
        netLogRaw("DNS-BLK", name ? name : "");
        h_errno = HOST_NOT_FOUND;
        return NULL;
    }
    if (name) netLogRaw("DNS", name);
    return orig_gethostbyname(name);
}

inline struct hostent *hooked_gethostbyname2(const char *name, int af) {
    if (isJunkDNSDomain(name)) {
        dnsNoteBlocked(name);
        netLogRaw("DNS-BLK", name ? name : "");
        h_errno = HOST_NOT_FOUND;
        return NULL;
    }
    if (name) netLogRaw("DNS", name);
    return orig_gethostbyname2(name, af);
}

// ===== 2b. HOOK DNSServiceGetAddrInfo (libsystem_dnssd/Bonjour - đường Network.framework/
// CFNetwork hiện đại hay dùng, KHÔNG đi qua getaddrinfo/gethostbyname) =====
// Vì sao cần: đã thấy DNS query gin.freefiremobile.com lộ ra ở tầng packet-sniffer dù
// getaddrinfo/gethostbyname đã hook đủ - domain vẫn bị chặn thật ở tầng request (NSURLProtocol/
// swizzle resume) nên không rò dữ liệu, nhưng bản thân query DNS đi qua đường khác. Đây là API
// bất đồng bộ (kết quả trả qua callBack, không phải qua giá trị trả về) - callBack ở đây LUÔN
// là con trỏ hàm THẬT của game/CFNetwork (ta không tự gọi nó), nên khi chặn chỉ cần trả lỗi
// đồng bộ kDNSServiceErr_NoSuchName mà KHÔNG gọi callBack - đúng ngữ nghĩa API: trả lỗi khác 0
// nghĩa là request chưa từng được khởi tạo, sdRef không được set, callBack không bao giờ chạy.
//
// Bản trước (revert vì "làm hỏng log INFO tab") hook luôn cả res_9_query/res_9_search/
// getipnodebyname - lần này CHỈ hook đúng DNSServiceGetAddrInfo (đường khớp với triệu chứng
// quan sát được), giảm diện tích rủi ro, và có guard NULL phòng dlsym không tìm thấy symbol.
static DNSServiceErrorType (*orig_DNSServiceGetAddrInfo)(DNSServiceRef *, DNSServiceFlags, uint32_t,
                                                          DNSServiceProtocol, const char *,
                                                          DNSServiceGetAddrInfoReply, void *);

inline DNSServiceErrorType hooked_DNSServiceGetAddrInfo(DNSServiceRef *sdRef, DNSServiceFlags flags,
                                                          uint32_t interfaceIndex, DNSServiceProtocol protocol,
                                                          const char *hostname, DNSServiceGetAddrInfoReply callBack,
                                                          void *context) {
    if (isJunkDNSDomain(hostname)) {
        dnsNoteBlocked(hostname);
        netLogRaw("DNS-BLK", hostname ? hostname : "");
        return kDNSServiceErr_NoSuchName;
    }
    if (hostname) netLogRaw("DNS", hostname);
    if (!orig_DNSServiceGetAddrInfo) return kDNSServiceErr_ServiceNotRunning; // symbol không tìm thấy lúc dlsym
    return orig_DNSServiceGetAddrInfo(sdRef, flags, interfaceIndex, protocol, hostname, callBack, context);
}

// ===== 3. CHẶN TẦNG CONNECT() THEO IP (khi game bypass DNS, connect thẳng vào IP) =====
// Vì sao cần: chặn DNS chỉ ăn khi game gọi getaddrinfo của libc. Nếu game tự nhớ sẵn IP rồi
// connect() thẳng (không phân giải lại qua libc) thì hook DNS trượt hoàn toàn.
//
// Cách làm AN TOÀN (khác bản trước đã bị revert vì treo game): KHÔNG bao giờ gọi DNS đồng bộ
// trên đường connect() - từng thử gethostbyaddr() đồng bộ ngay trong hook, mỗi connect() phải
// đợi 1 vòng reverse-DNS qua mạng nên game lag/treo. Giờ học IP trước, ở thread nền, rồi
// connect() chỉ so khớp chuỗi IP với registry đã học sẵn (O(1), không I/O).
static std::mutex g_blockedIPsMutex;
static std::unordered_set<std::string> g_blockedIPs;
static std::mutex g_learnedHostsMutex;
static std::unordered_map<std::string, bool> g_learnedHosts; // host đã học IP rồi -> khỏi lặp lại

// Phân giải THẬT 1 host (dùng hàm gốc, không qua hook) rồi nạp từng IP tìm được vào registry
// chặn. Có thể block khi chờ mạng nên LUÔN gọi từ thread nền (dispatch_async) - xem
// installDNSBlockHook() và hooked_getaddrinfo() bên dưới, không bao giờ gọi trực tiếp từ
// đường connect() đang được game chờ.
inline void netBlockLearnHostIPs(const char *host) {
    if (!host || !host[0] || !orig_getaddrinfo) return;
    {
        std::lock_guard<std::mutex> lock(g_learnedHostsMutex);
        if (!g_learnedHosts.emplace(host, true).second) return;
    }
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC; // lấy cả IPv4 lẫn IPv6
    struct addrinfo *res = NULL;
    if (orig_getaddrinfo(host, NULL, &hints, &res) != 0 || !res) return;
    std::lock_guard<std::mutex> lock(g_blockedIPsMutex);
    // Chặn số vòng lặp (phòng addrinfo trả về bất thường/vòng lặp ai_next) - 1 host thật
    // không bao giờ có quá vài chục bản ghi A/AAAA, 64 đã rất dư.
    int guard = 64;
    for (struct addrinfo *ai = res; ai && guard > 0; ai = ai->ai_next, guard--) {
        char ip[INET6_ADDRSTRLEN] = {0};
        if (ai->ai_family == AF_INET) {
            inet_ntop(AF_INET, &((struct sockaddr_in *)ai->ai_addr)->sin_addr, ip, sizeof(ip));
        } else if (ai->ai_family == AF_INET6) {
            inet_ntop(AF_INET6, &((struct sockaddr_in6 *)ai->ai_addr)->sin6_addr, ip, sizeof(ip));
        } else {
            continue;
        }
        if (ip[0]) g_blockedIPs.insert(ip);
    }
    freeaddrinfo(res);
}

// Callback đăng ký với NetLog.h (netLogSetBlockCheck) - NetLog gọi hộ ngay trước MỌI connect()
// thật. Phải cực nhanh, không I/O/mạng: chỉ so khớp IP với registry đã học sẵn ở trên.
inline bool netBlockCheckConnect(const char *ip, uint16_t port, char *reasonOut, size_t reasonLen) {
    if (!ip || !ip[0]) return false;
    // Chặn toàn bộ loopback (127.0.0.0/8 và ::1): game/anti-cheat hay dò cổng local kiểu
    // 127.0.0.1:22, :2222 (SSH/Dropbear - dấu hiệu jailbreak), :27042 (cổng mặc định Frida) để
    // phát hiện máy jailbreak/đang bị hook. Chặn ở đây làm connect() fail y hệt máy sạch không
    // có gì chạy ở các cổng đó (ECONNREFUSED tự nhiên, không phải hành vi lạ).
    if (strncmp(ip, "127.", 4) == 0 || strcmp(ip, "::1") == 0) {
        dnsNoteBlocked(ip);
        snprintf(reasonOut, reasonLen, "%s:%d (loopback probe)", ip, port);
        return true;
    }
    if (strcmp(ip, "8.8.8.8") == 0) {
        dnsNoteBlocked("8.8.8.8");
        snprintf(reasonOut, reasonLen, "8.8.8.8:%d", port);
        return true;
    }
    std::lock_guard<std::mutex> lock(g_blockedIPsMutex);
    if (g_blockedIPs.count(ip)) {
        dnsNoteBlocked(ip);
        snprintf(reasonOut, reasonLen, "%s:%d (IP-bypass)", ip, port);
        return true;
    }
    return false;
}

// Callback đăng ký với NetLog.h (netLogSetHostBlockCheck) - NetLog gọi hộ khi soi được tên
// miền thật từ ClientHello (SNI) hoặc header Host của 1 request HTTP thuần không qua
// NSURLSession (nên JunkAdURLProtocol không thấy được). Dùng lại đúng trie chặn domain hiện
// có, để danh sách đen chỉ cần khai báo 1 chỗ (kJunkDNSDomains) cho mọi tầng chặn.
inline bool netBlockCheckHost(const char *host) {
    if (!host || !host[0] || !isJunkDNSDomain(host)) return false;
    dnsNoteBlocked(host);
    return true;
}

// Callback đăng ký với NetLog.h (netLogSetLearnBlockedIP) - khi 1 kết nối bị chặn theo SNI/Host
// (tức chỉ phát hiện được sau khi đã connect() xong), nạp luôn IP đó vào registry chặn
// connect() để lần sau gặp lại IP này bị chặn ngay từ connect(), khỏi phải soi SNI lại.
inline void netBlockLearnIP(const char *ip) {
    if (!ip || !ip[0]) return;
    std::lock_guard<std::mutex> lock(g_blockedIPsMutex);
    g_blockedIPs.insert(ip);
}

// Preview nội dung HTTPBody của 1 request SẮP bị chặn - để biết CHÍNH XÁC game định gửi gì
// lên server đen (không chỉ mỗi URL/query string, vốn đã thấy được ở dòng "HTTP-BLK"). Chỉ đọc
// request.HTTPBody (NSData sẵn có trong bộ nhớ) - KHÔNG đọc HTTPBodyStream vì đọc stream sẽ
// tiêu thụ nó mất, lỡ request này thật ra không bị chặn (gọi nhầm) thì sẽ hỏng luôn upload thật.
// Trả nil nếu không có body (GET thường không có, hoặc request dùng stream) - caller bỏ qua,
// khỏi ghi 1 dòng log rỗng vô nghĩa.
inline NSString *httpBlockedBodyPreview(NSData *body) {
    if (!body || body.length == 0) return nil;
    NSUInteger n = body.length < 120 ? body.length : 120;
    const unsigned char *bytes = (const unsigned char *)body.bytes;
    NSMutableString *preview = [NSMutableString stringWithCapacity:n];
    for (NSUInteger i = 0; i < n; i++) {
        unsigned char c = bytes[i];
        [preview appendFormat:@"%c", (c >= 0x20 && c < 0x7f) ? c : '.'];
    }
    return [NSString stringWithFormat:@"len=%lu | %@", (unsigned long)body.length, preview];
}

// ===== 4. NSURLPROTOCOL (Cấp độ HTTP/HTTPS của iOS Cocoa) =====
// Khai báo trước category NSString dùng để so khớp từ khóa method trong path (vd "logevent")
// khi request không có host - dùng bên dưới, implement ở cuối file.
@interface NSString (JunkKeywordMatch)
- (BOOL)containsJunkKeywordUTF8:(const char *)str;
@end

@interface JunkAdURLProtocol : NSURLProtocol
@end

@implementation JunkAdURLProtocol

+ (BOOL)canInitWithRequest:(NSURLRequest *)request {
    NSString *host = request.URL.host;
    if (!host) {
        // Kiểm tra chặn chuỗi method đặc biệt xuất hiện trong URL Path
        NSString *absUrl = request.URL.absoluteString;
        if (absUrl) {
            for (const char* junk : kJunkDNSDomains) {
                if (strchr(junk, '.') == NULL) { // Nếu là từ khóa method, không phải domain
                    if ([absUrl containsJunkKeywordUTF8:junk]) {
                        netLogRaw("HTTP-BLK", [absUrl UTF8String]);
                        return YES;
                    }
                }
            }
        }
        return NO;
    }

    NSString *url = request.URL.absoluteString;
    if (url) netLogRaw("HTTP", [url UTF8String]);
    if (isJunkDNSDomain([host UTF8String])) {
        // Ghi rõ CÓ chặn (khác với dòng "HTTP" ở trên chỉ là "đã thấy request") - không có
        // dòng này nghĩa là canInitWithRequest không được gọi cho request đó (vd session
        // background/ephemeral riêng không consult NSURLProtocol đăng ký ở app), không phải
        // do trie chặn domain sai.
        if (url) netLogRaw("HTTP-BLK", [url UTF8String]);
        NSString *bodyPreview = httpBlockedBodyPreview(request.HTTPBody);
        if (bodyPreview) netLogRaw("HTTP-BODY", [[NSString stringWithFormat:@"%@ %@", request.HTTPMethod ?: @"?", bodyPreview] UTF8String]);
        return YES;
    }
    return NO;
}

+ (NSURLRequest *)canonicalRequestForRequest:(NSURLRequest *)request {
    return request;
}

- (void)startLoading {
    dnsNoteBlocked([self.request.URL.host UTF8String]);
    NSError *error = [NSError errorWithDomain:NSURLErrorDomain code:NSURLErrorCannotFindHost userInfo:nil];
    [self.client URLProtocol:self didFailWithError:error];
}

- (void)stopLoading {
}

@end

// Trợ giúp kiểm tra chuỗi NSString chứa UTF8 C-String nhanh
@implementation NSString (JunkKeywordMatch)
- (BOOL)containsJunkKeywordUTF8:(const char *)str {
    return [self rangeOfString:[NSString stringWithUTF8String:str] options:NSCaseInsensitiveSearch].location != NSNotFound;
}
@end

// ===== 4b. SWIZZLE -[NSURLSessionTask resume] (chặn HTTP/2 mà NSURLProtocol bỏ sót) =====
// Vì sao cần: NSURLProtocol (canInitWithRequest ở trên) được biết là KHÔNG đáng tin cậy với
// HTTP/2 - nhiều request HTTP/2 (đặc biệt khi CFNetwork tái dùng connection/multiplex) không
// bao giờ được hỏi qua canInitWithRequest, nên trie chặn domain đúng nhưng không có cơ hội
// chạy. Swizzle -resume chặn ở tầng Objective-C (luôn đi qua objc_msgSend, không phụ thuộc
// NSURLProtocol lẫn việc symbol có nằm trong dyld shared cache hay không như các hook libc ở
// NetLog.h) - kiểm tra URL ngay trước khi task thật sự chạy, [task cancel] thay vì gọi resume
// gốc nếu domain nằm trong danh sách đen.
static IMP g_origTaskResumeIMP = NULL;

inline void hookedTaskResume(id self, SEL _cmd) {
    NSURLSessionTask *task = (NSURLSessionTask *)self;
    NSURL *url = task.currentRequest.URL ?: task.originalRequest.URL;
    NSString *host = url.host;
    if (host && isJunkDNSDomain([host UTF8String])) {
        NSString *urlStr = url.absoluteString;
        netLogRaw("HTTP-BLK", urlStr ? [urlStr UTF8String] : [host UTF8String]);
        NSData *body = task.currentRequest.HTTPBody ?: task.originalRequest.HTTPBody;
        NSString *bodyPreview = httpBlockedBodyPreview(body);
        if (bodyPreview) {
            NSString *method = task.currentRequest.HTTPMethod ?: task.originalRequest.HTTPMethod ?: @"?";
            netLogRaw("HTTP-BODY", [[NSString stringWithFormat:@"%@ %@", method, bodyPreview] UTF8String]);
        }
        dnsNoteBlocked([host UTF8String]);
        [task cancel]; // KHÔNG gọi resume gốc - request thật sự không bao giờ chạy
        return;
    }
    NSString *urlStr = url.absoluteString;
    if (urlStr) netLogRaw("HTTP", [urlStr UTF8String]);
    ((void (*)(id, SEL))g_origTaskResumeIMP)(self, _cmd);
}

// Lớp runtime thật thi hành -resume là 1 private cluster class (vd __NSCFURLSessionTask),
// KHÔNG phải NSURLSessionTask/NSURLSessionDataTask public - tên lớp private đổi qua từng bản
// iOS nên không hardcode, mà tạo 1 task nháp (không resume - không gọi mạng thật) rồi soi
// object_getClass() để lấy đúng lớp cần swizzle, làm việc ổn định qua mọi phiên bản iOS.
inline void installTaskResumeSwizzle() {
    NSURLSession *tmpSession = [NSURLSession sessionWithConfiguration:[NSURLSessionConfiguration defaultSessionConfiguration]];
    NSURLSessionDataTask *tmpTask = [tmpSession dataTaskWithURL:[NSURL URLWithString:@"https://a.a"]];
    Class taskCls = object_getClass(tmpTask);
    tmpTask = nil;
    [tmpSession invalidateAndCancel];
    if (!taskCls) return;

    Method m = class_getInstanceMethod(taskCls, @selector(resume));
    if (!m) return;
    g_origTaskResumeIMP = method_getImplementation(m);
    method_setImplementation(m, (IMP)hookedTaskResume);
}

// ===== 5. KHỞI CHẠY HOOK =====
inline void installDNSBlockHook() {
    orig_getaddrinfo    = (int (*)(const char *, const char *, const struct addrinfo *, struct addrinfo **))dlsym((void *)RTLD_DEFAULT, "getaddrinfo");
    orig_gethostbyname  = (struct hostent *(*)(const char *))dlsym((void *)RTLD_DEFAULT, "gethostbyname");
    orig_gethostbyname2 = (struct hostent *(*)(const char *, int))dlsym((void *)RTLD_DEFAULT, "gethostbyname2");
    orig_DNSServiceGetAddrInfo = (DNSServiceErrorType (*)(DNSServiceRef *, DNSServiceFlags, uint32_t,
                                                           DNSServiceProtocol, const char *,
                                                           DNSServiceGetAddrInfoReply, void *))dlsym((void *)RTLD_DEFAULT, "DNSServiceGetAddrInfo");

    struct rebinding dnsRebindings[] = {
        {"getaddrinfo",           (void *)hooked_getaddrinfo,           (void **)&orig_getaddrinfo},
        {"gethostbyname",         (void *)hooked_gethostbyname,         (void **)&orig_gethostbyname},
        {"gethostbyname2",        (void *)hooked_gethostbyname2,        (void **)&orig_gethostbyname2},
        {"DNSServiceGetAddrInfo", (void *)hooked_DNSServiceGetAddrInfo, (void **)&orig_DNSServiceGetAddrInfo},
    };
    rebind_symbols(dnsRebindings, sizeof(dnsRebindings) / sizeof(dnsRebindings[0]));

    [NSURLProtocol registerClass:[JunkAdURLProtocol class]];
    installTaskResumeSwizzle();

    // NetLog.h là nơi DUY NHẤT hook "connect"/"write"/"send" (fishhook rebind ai gọi sau cùng
    // thì thắng, hai hook cùng tên thì không build được) - đăng ký callback chặn để nó gọi hộ:
    // theo IP lúc connect(), và theo hostname (SNI/Host) lúc write()/send() gói đầu tiên - chặn
    // được cả HTTPS/HTTP đi bằng thư viện mạng riêng của game, không qua NSURLSession.
    netLogSetBlockCheck(netBlockCheckConnect);
    netLogSetHostBlockCheck(netBlockCheckHost);
    netLogSetLearnBlockedIP(netBlockLearnIP);
    installNetLogHook();

    // KHÔNG preload/resolve toàn bộ ~200 host đen ở đây: installDNSBlockHook() chạy trong
    // +[DeltaMenu load], tức là NGAY khi dylib nạp - còn trước cả lúc dyld chạy xong initializer
    // của Foundation/CoreFoundation cho tiến trình (đã tận mắt thấy crash: dispatch_async 1 vòng
    // lặp 200 lần gọi getaddrinfo() + insert vào std::unordered_map/set ở background queue lúc
    // này làm hỏng state của container -> std::__next_prime ném overflow_error -> abort() ngay
    // lúc mở app). IP của từng host đen giờ chỉ được học LAZY, từng cái một, ngay tại
    // hooked_getaddrinfo() khi game thật sự query domain đó - lúc đó app đã chạy ổn định.
}
