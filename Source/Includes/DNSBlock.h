#pragma once
#import <Foundation/Foundation.h>
#import <netdb.h>
#import <string.h>
#import <strings.h>
#import <ctype.h> // tolower

#include <string>
#include <unordered_map>
#include <atomic>

#import "MemoryUtils.h"
#import "fishhook.h"
#import "NetLog.h" // logger mạng thụ động (DNS/TCP/UDP/HTTP) - soi request game gửi lên server

// ===== Thống kê chặn DNS (để tab INFO soi được có chặn thật không) =====
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

// 1. Danh sách đen các Domain chặn DNS & HTTP
static const char *kJunkDNSDomains[] = {
    // --- Google ad / analytics ---
    "doubleclick.net",
    "googlesyndication.com",
    "googleadservices.com",
    "google-analytics.com",
    "app-measurement.com",
    "googletagmanager.com",
    "googletagservices.com",
    "adservice.google.com",
    "googleads.g.doubleclick.net",
    "securetoken.googleapis.com",
    
    // --- Mobile ad networks / mediation ---
    "applovin.com",
    "unityads.unity3d.com",
    "unity3d.com",
    "vungle.com",
    "ironsrc.com",
    "supersonicads.com",
    "mopub.com",
    "chartboost.com",
    "inmobi.com",
    "tapjoy.com",
    "adcolony.com",
    "pangle.io",
    "pangleglobal.com",
    "mintegral.com",
    "startapp.com",
    "fyber.com",
    "smaato.com",
    
    // --- Attribution / mobile analytics ---
    "adjust.com",
    "adjust.io",
    "appsflyer.com",
    "appsflyersdk.com",
    "flurry.com",
    "branch.io",
    "kochava.com",
    "tenjin.io",
    "singular.net",
    "mixpanel.com",
    "amplitude.com",
    "segment.io",
    
    // --- Web ad exchanges / trackers ---
    "adnxs.com",
    "casalemedia.com",
    "openx.net",
    "pubmatic.com",
    "rubiconproject.com",
    "smartadserver.com",
    "media.net",
    "criteo.com",
    "taboola.com",
    "outbrain.com",
    "scorecardresearch.com",
    "moatads.com",
    "adsrvr.org",
    "amazon-adsystem.com",
    "bidswitch.net",
    "y5en.com",
    "ggblueshark.com",
    "vnevent.ggblueshark.com",
    "clientbp.ggblueshark.com",
    "www.chenxiapp.com",
    "version.common.redflamenco.com",
    "bdversion.ggbluefox.com",

    // --- Garena / Free Fire Security & Monitoring Subdomains ---
    "network-monitoring-tools-service.ff-garena.com",
    "security-checks-service-logs.ff-garena.com",
    "fraud-detection-risk-platform.garena.com",
    "malware-detection-tools-service.ff-garena.com",
    "cheat-prevention-platform.garena.com",
    "user-activity-monitoring-logs.garena.com",
    "security-tools-platform.ff-garena.com",
    "integrity-monitoring-checks.garena.com",
    "fraud-detection-policy.ff-garena.com",
    "cheat-detection-tools-analysis.garena.com",
    "user-behavior-monitoring-service.ff-garena.com",
    "security-check-logs-analysis.garena.com",
    "network-security-logs-platform.ff-garena.com",
    "fraud-monitoring-service.garena.com",
    "malware-detection-platform.garena.com",
    "cheat-detection-system-checks.garena.com",
    "security-detection-tools.ff-garena.com",
    "real-time-activity-monitor.ff-garena.com",
    "fraud-detection-logs-platform.garena.com",
    "user-monitoring-tools.ff-garena.com",
    "malware-protection-logs.ff-garena.com",
    "cheat-prevention-monitoring.ff-garena.com",
    "integrity-check-logs-analysis.garena.com",
    "security-logs-analysis.ff-garena.com",
    "network-security-checks-service.ff-garena.com",
    "cheat-detection-reports-audit.ff-garena.com",
    "user-behavior-audit.ff-garena.com",
    "fraud-detection-alerts.garena.com",
    "malware-detection-audit.ff-garena.com",
    "security-policy-checks.ff-garena.com",
    "real-time-cheat-detection.ff-garena.com",
    "cheat-detection-audit-service.garena.com",
    "fraud-detection-platform-logs.ff-garena.com",
    "user-activity-monitoring-service.garena.com",
    "integrity-monitoring-tools.ff-garena.com",
    "security-checks-reports.garena.com",
    "cheat-prevention-tools.ff-garena.com",
    "fraud-detection-analysis-logs.garena.com",
    "malware-protection-service.ff-garena.com",
    "cheat-detection-policies.garena.com",
    "user-monitoring-service.ff-garena.com",
    "security-analysis-tools.garena.com",
    "fraud-detection-service-logs.ff-garena.com",
    "cheat-detection-tools-service.garena.com",
    "network-monitoring-logs.ff-garena.com",
    "malware-analysis-logs.ff-garena.com",
    "integrity-monitoring-platform.garena.com",
    "cheat-prevention-analytics.ff-garena.com",
    "security-checks-logs.ff-garena.com",
    "fraud-detection-risk-analysis.garena.com",
    "user-activity-analysis.ff-garena.com",
    "security-logs-service.garena.com",
    "cheat-detection-risk-assessment.ff-garena.com",
    "malware-scanning-tool.garena.com",
    "fraud-risk-detection.ff-garena.com",
    "cheat-monitoring-logs.ff-garena.com",
    "security-audit-reports.garena.com",
    "user-behavior-detection.ff-garena.com",
    "fraud-detection-tools.garena.com",
    "cheat-detection-analysis-tool.ff-garena.com",
    "security-risk-monitoring.garena.com",
    "network-security-tool.ff-garena.com",
    "integrity-analysis-logs.ff-garena.com",
    "cheat-detection-policy.garena.com",
    "fraud-detection-checks.ff-garena.com",
    "security-monitoring-reports.garena.com",
    "real-time-monitoring-service.ff-garena.com",
    "cheat-analysis-logs.garena.com",
    "user-activity-logs.ff-garena.com",
    "fraud-detection-platform.ff-garena.com",
    "malware-detection-reports.garena.com",
    "security-detection-reports.ff-garena.com",
    "cheat-prevention-analytics.garena.com",
    "fraud-detection-logs-service.ff-garena.com",
    "integrity-check-logs.garena.com",
    "network-monitoring-service.ff-garena.com",
    "cheat-detection-risk-analysis.garena.com",
    "security-audit-logs.ff-garena.com",
    "user-behavior-analysis.ff-garena.com",
    "malware-analysis-service.ff-garena.com",
    "fraud-detection-research.ff-garena.com",
    "cheat-detection-risk.garena.com",
    "security-checks-platform.ff-garena.com",
    "network-security-analysis.garena.com",
    "fraud-analysis-logs.ff-garena.com",
    "cheat-detection-service-logs.garena.com",
    "integrity-monitoring-logs.ff-garena.com",
    "security-tools-service.garena.com",
    "user-monitoring-logs.ff-garena.com",
    "cheat-detection-audit.garena.com",
    "malware-detection-analytics.ff-garena.com",
    "fraud-detection-gateway.garena.com",
    "security-checks-service.ff-garena.com",
    "real-time-security-monitor.ff-garena.com",
    "cheat-prevention-logs.garena.com",
    "fraud-detection-reports.ff-garena.com",
    "security-detection-platform.garena.com",
    "integrity-analysis.ff-garena.com",
    "cheat-monitoring-tool.ff-garena.com",
    "security-risk-assessment.garena.com",
    "network-monitoring.ff-garena.com",
    "malware-analysis-tool.ff-garena.com",
    "fraud-detection-analysis.ff-garena.com",
    "cheat-detection-logs-service.garena.com",
    "user-behavior-monitoring.ff-garena.com",
    "security-analysis-service.garena.com",
    "real-time-fraud-detection.ff-garena.com",
    "game-security-check.ff-garena.com",
    "cheat-detection-research.garena.com",
    "security-logs.ff-garena.com",
    "fraud-analysis-service.garena.com",
    "integrity-checks-service.ff-garena.com",
    "cheat-prevention-tool.garena.com",
    "malware-detection-tool.ff-garena.com",
    "security-threat-analysis.ff-garena.com",
    "cheat-detection-monitoring.garena.com",
    "network-security-checks.ff-garena.com",
    "fraud-risk-assessment.ff-garena.com",
    "security-tools.garena.com",
    "behavior-analysis.ff-garena.com",
    "integrity-monitoring-system.ff-garena.com",
    "cheat-detection-analytics.garena.com",
    "exploit-detection.ff-garena.com",
    "security-risk-management.ff-garena.com",
    "malware-detection-system.garena.com",
    "user-activity-monitor.ff-garena.com",
    "data-security-checks.ff-garena.com",
    "cheat-monitoring-service.garena.com",
    "security-analysis.ff-garena.com",
    "fraud-detection-system.ff-garena.com",
    "anti-cheat-service.ff-garena.com",
    "anti-fraud.ff.garena.com",
    "security-services.garena.com",
    "analytics.ff-garena.com",
    "gameprotection.garena.com",
    "integrity.ff.garena.com",
    "monitor.ff.garena.com",
    "cheat-prevention.ff.garena.com",
    "fraud-detection.garena.com",
    "hack-detection.ff.garena.com",
    "integrity-monitoring-service.ff-garena.com",
    "security-data-analysis.garena.com",
    "real-time-security-service.ff-garena.com",
    "cheat-detection-reports.ff-garena.com",
    "malware-scanning-service.garena.com",
    "security-audit-service.ff-garena.com",
    "risk-monitoring.ff.garena.com",
    "game-integrity-logs.garena.com",
    "cheat-detection-gateway.ff-garena.com",
    "security-detection-logs.garena.com",
    "fraud-analysis-tool.ff-garena.com",
    "cheat-detection-platform.garena.com",
    "security-monitoring-tool.ff-garena.com",
    "cheat-prevention-service.garena.com",
    "integrity-checks.ff.garena.com",
    "fraud-detection-tool.ff-garena.com",
    "security-detection-service.garena.com",
    "cheat-prevention-system.ff-garena.com",
    "security-audit-logs.garena.com",
    "behavioral-analysis-service.ff-garena.com",
    "cheat-detection-engine.garena.com",
    "fraud-monitoring-logs.ff-garena.com",
    "game-security-analysis.garena.com",
    "malware-protection.ff-garena.com",
    "cheat-detection-tool.garena.com",
    "real-time-monitor.ff-garena.com",
    "cheat-detection-reports.garena.com",
    "integrity-service.ff-garena.com",
    "security-policies.ff-garena.com",
    "fraud-detection-analytics.ff-garena.com",
    "risk-assessment.garena.com",
    "security-monitoring-logs.ff-garena.com",
    "gin.freefiremobile.com",
    "sukien1024.ff.garena.vn",

    // --- Core IP / DNS Utilities & Method Interceptions ---
    "8.8.8.8",
    "logevent",
    "cheat-analytics-service",
    "updateguideofnewplayer",
    "initplayercsrankinginfo",
    "getmatchmakingblacklist"
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

// ===== 2. HOOK DNS RESOLUTION (getaddrinfo + legacy gethostbyname/gethostbyname2) =====
static int (*orig_getaddrinfo)(const char *, const char *, const struct addrinfo *, struct addrinfo **);

inline int hooked_getaddrinfo(const char *hostname, const char *servname, const struct addrinfo *hints, struct addrinfo **res) {
    if (isJunkDNSDomain(hostname)) {
        dnsNoteBlocked(hostname);
        netLogRaw("DNS-BLK", hostname ? hostname : "");
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

// ===== 3. NSURLPROTOCOL (Cấp độ HTTP/HTTPS) =====
@interface JunkAdURLProtocol : NSURLProtocol
@end

@implementation JunkAdURLProtocol

+ (BOOL)canInitWithRequest:(NSURLRequest *)request {
    NSString *host = request.URL.host;
    if (!host) return NO;
    
    NSString *url = request.URL.absoluteString;
    if (url) netLogRaw("HTTP", [url UTF8String]);
    if (isJunkDNSDomain([host UTF8String])) {
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

// ===== 4. KHỞI CHẠY HOOK =====
inline void installDNSBlockHook() {
    orig_getaddrinfo    = (int (*)(const char *, const char *, const struct addrinfo *, struct addrinfo **))dlsym((void *)RTLD_DEFAULT, "getaddrinfo");
    orig_gethostbyname  = (struct hostent *(*)(const char *))dlsym((void *)RTLD_DEFAULT, "gethostbyname");
    orig_gethostbyname2 = (struct hostent *(*)(const char *, int))dlsym((void *)RTLD_DEFAULT, "gethostbyname2");

    struct rebinding dnsRebindings[] = {
        {"getaddrinfo",    (void *)hooked_getaddrinfo,    (void **)&orig_getaddrinfo},
        {"gethostbyname",  (void *)hooked_gethostbyname,  (void **)&orig_gethostbyname},
        {"gethostbyname2", (void *)hooked_gethostbyname2, (void **)&orig_gethostbyname2},
    };
    rebind_symbols(dnsRebindings, sizeof(dnsRebindings) / sizeof(dnsRebindings[0]));

    [NSURLProtocol registerClass:[JunkAdURLProtocol class]];
    installNetLogHook();
}