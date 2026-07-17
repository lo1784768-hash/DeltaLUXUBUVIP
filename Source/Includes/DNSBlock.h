#pragma once
#import <Foundation/Foundation.h>
#import <netdb.h>
#import <string.h>
#import <strings.h> // strcasestr is declared here on Darwin, not string.h

#import "MemoryUtils.h"

// In-process ad/tracker DNS blocker (NextDNS-style curated blocklist, but scoped to
// this process only): hooks getaddrinfo, the libc DNS resolution entry point almost
// everything - NSURLSession, third-party SDKs, raw sockets - goes through, and fails
// the lookup for known ad/analytics/tracking domains. Always active once installed
// (see installDNSBlockHook() in +load) - no menu switch, runs continuously alongside
// everything else.
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

static int (*orig_getaddrinfo)(const char *, const char *, const struct addrinfo *, struct addrinfo **);

// Diagnostics - the user reported the block having no effect at all (banner still
// loads); before guessing further, these let us see from the Info tab whether
// getaddrinfo is even being called for the banner domains, or whether the game/its ad
// SDK resolves DNS some other way entirely (would mean this whole approach needs to
// move to a different interception point, e.g. NSURLProtocol, instead of getaddrinfo).
static unsigned long g_dnsCallCount = 0;
static unsigned long g_dnsBlockedCount = 0;
static char g_lastDNSHostname[256] = "";

inline int hooked_getaddrinfo(const char *hostname, const char *servname, const struct addrinfo *hints, struct addrinfo **res) {
    g_dnsCallCount++;
    if (hostname) strncpy(g_lastDNSHostname, hostname, sizeof(g_lastDNSHostname) - 1);
    if (isJunkDNSDomain(hostname)) {
        g_dnsBlockedCount++;
        return EAI_NONAME; // fail resolution, as if the domain simply doesn't exist
    }
    return orig_getaddrinfo(hostname, servname, hints, res);
}

inline void installDNSBlockHook() {
    HOOKSYM("getaddrinfo", hooked_getaddrinfo, orig_getaddrinfo);
}

// "DNS calls: 0" on-device proved getaddrinfo is never even invoked in this process -
// modern NSURLSession/CFNetwork resolves hostnames through a private system-level path
// that doesn't go through the public getaddrinfo symbol, so hooking it was the wrong
// interception point entirely for whatever loads the banner images. This blocks at the
// URL Loading System level instead, which is what NSURLSession/NSURLConnection (and
// very likely Unity's own iOS networking backend, which is built on NSURLSession)
// actually route through - registering a class here lets it intercept every request
// before any connection is attempted, regardless of how DNS would have been resolved.
static unsigned long g_urlBlockCallCount = 0;
static unsigned long g_urlBlockedCount = 0;

// Small ring buffers instead of a single "last host" slot - a single slot gets
// overwritten by whatever request happens to come in next, which made it impossible to
// tell whether a *specific* domain (like gin.freefiremobile.com) was ever actually seen
// by canInitWithRequest: at all, as opposed to just not being the most recent one.
#define kURLLogSize 8
static char g_checkedURLHosts[kURLLogSize][256];
static char g_blockedURLHosts[kURLLogSize][256];

inline void appendToURLLog(char log[][256], unsigned long countAfterIncrement, NSString *host) {
    int idx = (int)((countAfterIncrement - 1) % kURLLogSize);
    const char *c = host ? [host UTF8String] : NULL;
    strncpy(log[idx], c ? c : "", 255);
    log[idx][255] = '\0';
}

inline NSString *joinURLLog(char log[][256], unsigned long count) {
    NSMutableArray<NSString *> *parts = [NSMutableArray array];
    int n = (int)MIN(count, (unsigned long)kURLLogSize);
    for (int i = 0; i < n; i++) {
        if (log[i][0]) [parts addObject:[NSString stringWithUTF8String:log[i]]];
    }
    return [parts componentsJoinedByString:@", "];
}

@interface JunkAdURLProtocol : NSURLProtocol
@end

@implementation JunkAdURLProtocol

+ (BOOL)canInitWithRequest:(NSURLRequest *)request {
    NSString *host = request.URL.host;
    if (!host) return NO;
    g_urlBlockCallCount++;
    appendToURLLog(g_checkedURLHosts, g_urlBlockCallCount, host);
    if (isJunkDNSDomain([host UTF8String])) {
        g_urlBlockedCount++;
        appendToURLLog(g_blockedURLHosts, g_urlBlockedCount, host);
        return YES; // claim it so -startLoading below can fail it
    }
    return NO; // let every other request through untouched
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

inline void installJunkAdURLProtocolHook() {
    [NSURLProtocol registerClass:[JunkAdURLProtocol class]];
}
