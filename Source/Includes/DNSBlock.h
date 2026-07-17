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

inline bool isJunkDNSDomain(const char *hostname) {
    if (!hostname) return false;
    size_t count = sizeof(kJunkDNSDomains) / sizeof(kJunkDNSDomains[0]);
    for (size_t i = 0; i < count; i++) {
        if (strcasestr(hostname, kJunkDNSDomains[i])) return true;
    }
    return false;
}

// ===== 2. HOOK GETADDRINFO (Cấp độ DNS) =====
static int (*orig_getaddrinfo)(const char *, const char *, const struct addrinfo *, struct addrinfo **);

inline int hooked_getaddrinfo(const char *hostname, const char *servname, const struct addrinfo *hints, struct addrinfo **res) {
    if (isJunkDNSDomain(hostname)) {
        return EAI_NONAME; // Giả lập lỗi: Domain không tồn tại
    }
    return orig_getaddrinfo(hostname, servname, hints, res);
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
    // Hook DNS
    HOOKSYM("getaddrinfo", hooked_getaddrinfo, orig_getaddrinfo);

    // Đăng ký URL Protocol
    [NSURLProtocol registerClass:[JunkAdURLProtocol class]];
}