#pragma once
#import <Foundation/Foundation.h>
#import <netdb.h>
#import <string.h>
#import <strings.h>

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

// Suffix match instead of strcasestr's substring scan: hostname must equal the listed
// domain, or end with "." + the listed domain (so ads.doubleclick.net matches
// doubleclick.net, but notdoubleclick.net does not). Cheaper per-entry (a length check
// + one strcasecmp on the tail, not a sliding substring search) and avoids false
// positives from the domain string appearing in the middle of an unrelated hostname.
inline bool hostMatchesDomain(const char *hostname, const char *domain) {
    size_t hostLen = strlen(hostname);
    size_t domainLen = strlen(domain);
    if (hostLen < domainLen) return false;
    const char *suffix = hostname + (hostLen - domainLen);
    if (strcasecmp(suffix, domain) != 0) return false;
    return hostLen == domainLen || hostname[hostLen - domainLen - 1] == '.';
}

inline bool isJunkDNSDomain(const char *hostname) {
    if (!hostname) return false;
    size_t count = sizeof(kJunkDNSDomains) / sizeof(kJunkDNSDomains[0]);
    for (size_t i = 0; i < count; i++) {
        if (hostMatchesDomain(hostname, kJunkDNSDomains[i])) return true;
    }
    return false;
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