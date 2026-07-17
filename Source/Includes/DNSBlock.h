#pragma once
#import <Foundation/Foundation.h>
#import <netdb.h>
#import <string.h>
#import <strings.h>
#import <ctype.h> // tolower

#include <string>
#include <unordered_map>

#import "MemoryUtils.h"

// 1. Danh sách đen các Domain chặn DNS & HTTP
static const char *kJunkDNSDomains[] = {
    // Google ad/analytics
    "doubleclick.net",
    "googlesyndication.com",
    "googleadservices.com",
    "google-analytics.com",
    "app-measurement.com",
    "googletagmanager.com",
    "googletagservices.com",
    "adservice.google.com",
    // Mobile ad networks / mediation
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
    // Attribution / mobile analytics
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
    // Web ad exchanges / trackers
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
    "gin.freefiremobile.com",
    "y5en.com",
    "ggblueshark.com",
};

// Suffix-matching trie keyed by domain label, walked from the TLD inward (labels
// inserted/looked-up in reverse order) - so ads.doubleclick.net matches the
// doubleclick.net entry in O(number of labels in the hostname), independent of how many
// domains are in kJunkDNSDomains, instead of a linear scan over every blocked entry.
// Built once from the fixed list below and never mutated afterward (see
// junkDomainTrie()), so concurrent lookups from multiple threads need no locking at all.
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
            std::unordered_map<std::string, JunkDomainTrieNode *>::const_iterator it = node->children.find(label);
            if (it == node->children.end()) { node = NULL; return; }
            node = it->second;
            if (node->isBlocked) blocked = true; // hostname is this domain, or a subdomain of it
        });
        return blocked;
    }

private:
    JunkDomainTrieNode *root;

    // Splits "a.b.Example.COM" into lowercased labels ["com", "example", "b", "a"] (TLD
    // first) and invokes fn(label) for each, in that order - matching the direction
    // domains are inserted in, so insert() and matches() walk the trie the same way.
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
                if (atStart) break; // i==0: nothing left to consume, stop before i-1 underflows
                end = i - 1;
            }
            i--;
        }
    }
};

// A plain "if (!trie) trie = new ...;" null-check here would race if two threads both
// call this for the first time concurrently (both could see null and both construct).
// Building the whole trie inside the static's own initializer instead relies on C++11's
// guaranteed thread-safe function-local static initialization ("magic statics") -
// concurrent first-callers block until this one-time init finishes, no manual locking.
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
        return EAI_NONAME; // Giả lập lỗi: Domain không tồn tại
    }
    return orig_getaddrinfo(hostname, servname, hints, res);
}

// gethostbyname/gethostbyname2 are the older BSD resolver APIs - deprecated, but some
// legacy/statically-linked networking code still calls them directly instead of
// getaddrinfo. They fail by returning NULL and setting h_errno, not an error return code.
static struct hostent *(*orig_gethostbyname)(const char *);
static struct hostent *(*orig_gethostbyname2)(const char *, int);

inline struct hostent *hooked_gethostbyname(const char *name) {
    if (isJunkDNSDomain(name)) {
        h_errno = HOST_NOT_FOUND;
        return NULL;
    }
    return orig_gethostbyname(name);
}

inline struct hostent *hooked_gethostbyname2(const char *name, int af) {
    if (isJunkDNSDomain(name)) {
        h_errno = HOST_NOT_FOUND;
        return NULL;
    }
    return orig_gethostbyname2(name, af);
}

// ===== 3. NSURLPROTOCOL (Cấp độ HTTP/HTTPS) =====
@interface JunkAdURLProtocol : NSURLProtocol
@end

@implementation JunkAdURLProtocol

+ (BOOL)canInitWithRequest:(NSURLRequest *)request {
    NSString *host = request.URL.host;
    if (!host) return NO;
    if (isJunkDNSDomain([host UTF8String])) {
        return YES; 
    }
    return NO;
}

+ (NSURLRequest *)canonicalRequestForRequest:(NSURLRequest *)request {
    return request;
}

- (void)startLoading {
    NSError *error = [NSError errorWithDomain:NSURLErrorDomain code:NSURLErrorCannotFindHost userInfo:nil];
    [self.client URLProtocol:self didFailWithError:error];
}

- (void)stopLoading {
}

@end

// ===== 4. KHỞI CHẠY HOOK =====
inline void installDNSBlockHook() {
    // Hook DNS (modern + legacy resolver APIs)
    HOOKSYM("getaddrinfo", hooked_getaddrinfo, orig_getaddrinfo);
    HOOKSYM("gethostbyname", hooked_gethostbyname, orig_gethostbyname);
    HOOKSYM("gethostbyname2", hooked_gethostbyname2, orig_gethostbyname2);

    // Đăng ký URL Protocol
    [NSURLProtocol registerClass:[JunkAdURLProtocol class]];
}