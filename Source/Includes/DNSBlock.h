#pragma once
#import <Foundation/Foundation.h>
#import <netdb.h>
#import <string.h>
#import <strings.h> // strcasestr is declared here on Darwin, not string.h

#import "MemoryUtils.h"

// Simple in-process junk-domain blocker: hooks getaddrinfo (the libc DNS resolution
// entry point almost everything - NSURLSession, third-party SDKs, raw sockets - goes
// through) and fails the lookup for known ad/analytics SDK domains bundled inside the
// game. Deliberately NOT a generic "block anything" mechanism - the list below is the
// only thing it can ever block, and it's limited to well-known ad-network/analytics
// endpoints, nothing related to actual gameplay/matchmaking traffic.
struct DNSBlockVars_t {
    bool BlockJunkDNS = false;
} DNSVars;

static const char *kJunkDNSDomains[] = {
    "doubleclick.net",
    "googlesyndication.com",
    "googleadservices.com",
    "google-analytics.com",
    "app-measurement.com",
    "adjust.com",
    "adjust.io",
    "appsflyer.com",
    "applovin.com",
    "unityads.unity3d.com",
    "vungle.com",
    "ironsrc.com",
    "supersonicads.com",
    "mopub.com",
    "chartboost.com",
    "flurry.com",
    "branch.io",
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

inline int hooked_getaddrinfo(const char *hostname, const char *servname, const struct addrinfo *hints, struct addrinfo **res) {
    if (DNSVars.BlockJunkDNS && isJunkDNSDomain(hostname)) {
        return EAI_NONAME; // fail resolution, as if the domain simply doesn't exist
    }
    return orig_getaddrinfo(hostname, servname, hints, res);
}

inline void installDNSBlockHook() {
    HOOKSYM("getaddrinfo", hooked_getaddrinfo, orig_getaddrinfo);
}
