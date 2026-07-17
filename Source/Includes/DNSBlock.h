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
    if (isJunkDNSDomain(hostname)) {
        return EAI_NONAME; // fail resolution, as if the domain simply doesn't exist
    }
    return orig_getaddrinfo(hostname, servname, hints, res);
}

inline void installDNSBlockHook() {
    HOOKSYM("getaddrinfo", hooked_getaddrinfo, orig_getaddrinfo);
}
