#import <UIKit/UIKit.h>
#import <QuartzCore/QuartzCore.h>
#import <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>
#import <AudioToolbox/AudioToolbox.h>

// Project headers
#import "Includes/Vector3.h"
#import "Includes/Vector2.h"
#import "Includes/Quaternion.h"
#import "Includes/UnityTypes.h"
#import "Includes/MemoryUtils.h"
#import "Includes/ESP.h"
#import "Includes/Encryption.h"
#import "Includes/ModHacks.h"
#import "Includes/DNSBlock.h"
#import "Includes/AssetRedirect.h"
#import "Includes/DylibSpy.h"

#define kWidth  [UIScreen mainScreen].bounds.size.width
#define kHeight [UIScreen mainScreen].bounds.size.height

// Delta color identity - purple/cyan, replacing the old flat red accent
#define COLOR_BG [UIColor colorWithRed:0.03 green:0.02 blue:0.06 alpha:0.93]
#define COLOR_PURPLE [UIColor colorWithRed:0.659 green:0.333 blue:0.969 alpha:1.0]
#define COLOR_CYAN   [UIColor colorWithRed:0.133 green:0.827 blue:0.933 alpha:1.0]
#define COLOR_TEXT [UIColor whiteColor]
#define COLOR_TEXT_DIM [UIColor colorWithWhite:0.62 alpha:1.0]
#define COLOR_BTN_OFF [UIColor colorWithWhite:1.0 alpha:0.05]

// Card surface treatment: a tinted-dark glass panel + hairline border, instead of a
// flat white-alpha overlay - reads as an intentional dark-glass surface rather than a
// generic system control. Cards additionally light up (border/accent/icon go cyan) when
// their switch is ON, via applyCardVisualState: - see addToggleCardWithLocKey:.
#define COLOR_CARD_BG [UIColor colorWithRed:0.09 green:0.075 blue:0.135 alpha:0.6]
#define COLOR_CARD_BORDER [UIColor colorWithWhite:1.0 alpha:0.08]
#define COLOR_ACCENT_IDLE [UIColor colorWithWhite:1.0 alpha:0.14]

// ===== Localization (VI default, EN togglable from the INFO tab) =====
static BOOL isEnglishMode = NO;

static NSDictionary<NSString *, NSArray<NSString *> *> *LocStrings() {
    static NSDictionary *d = nil;
    static dispatch_once_t token;
    dispatch_once(&token, ^{
        d = @{
            // key: @[ vi, en ]
            @"master_switch": @[@"Công Tắc Chính", @"Master Switch"],
            @"box": @[@"Khung", @"Box"],
            @"lines": @[@"Đường Kẻ", @"Lines"],
            @"names": @[@"Tên", @"Names"],
            @"health": @[@"Máu", @"Health"],
            @"distance": @[@"Khoảng Cách", @"Distance"],
            @"skeleton": @[@"Khung Xương", @"Skeleton"],
            @"enemy_count": @[@"Số Địch", @"Enemy Count"],
            @"aim_head": @[@"Ngắm Đầu", @"Aim Head"],
            @"aim_nhe_tam": @[@"Aim Nhẹ Tâm", @"Soft Center Aim"],
            @"show_fov_circle": @[@"Hiện Vòng FOV", @"Show FOV Circle"],
            @"section_display": @[@"Hiển Thị", @"Display"],
            @"section_aim": @[@"Tự Động Ngắm", @"Auto Aim"],
            @"section_boost": @[@"Tăng Cường", @"Boost"],
            @"section_network": @[@"Mạng", @"Network"],
            @"block_udp_ports": @[@"Chặn Cổng UDP 10000-10020", @"Block UDP Ports 10000-10020"],
            @"aim_mode_always": @[@"Luôn Bật", @"Always"],
            @"aim_mode_fire": @[@"Khi Bắn/Ngắm", @"Fire/Scope"],
            @"aim_prefer_low_hp": @[@"Ưu Tiên Máu Vàng/Đỏ (Ngẫu Nhiên)", @"Prefer Yellow/Red HP (Random)"],
            @"aim_magnet": @[@"Aim Từ Tính", @"Aim Magnet"],
            @"antena": @[@"Antena", @"Antena"],
            @"speed_x2": @[@"Tốc Độ x2", @"Speed x2"],
            @"speed_x8": @[@"Tốc Độ x8", @"Speed x8"],
            @"no_recoil": @[@"Chống Giật", @"No Recoil"],
            @"spin_bot": @[@"Xoay Nhân Vật (SpinBot)", @"Character Spin (SpinBot)"],
            @"action": @[@"Hành Động", @"Action"],
            @"status": @[@"Trạng thái", @"Status"],
            @"activated": @[@"Đã kích hoạt", @"Activated"],
            @"select_base_action": @[@"CHỌN HÀNH ĐỘNG GỐC", @"SELECT BASE ACTION"],
            @"select_mod_action": @[@"CHỌN HÀNH ĐỘNG MOD", @"SELECT MOD ACTION"],
            @"spy_start": @[@"Bắt Đầu Giám Sát (Vá GOT Dylib B)", @"Start Monitoring (Patches Dylib B's GOT)"],
            @"spy_mem_watch": @[@"Giám Sát Bộ Nhớ (Dylib B)", @"Memory Watch (Dylib B)"],
            @"back": @[@"Trở Về", @"Back"],
            @"on": @[@"BẬT", @"ON"],
            @"off": @[@"TẮT", @"OFF"],
        };
    });
    return d;
}

static NSString *LOC(NSString *key) {
    NSArray<NSString *> *pair = LocStrings()[key];
    if (!pair) return key;
    return isEnglishMode ? pair[1] : pair[0];
}

@interface DeltaMenu : NSObject <UITextFieldDelegate, UIGestureRecognizerDelegate>

// First-run extraction flow - declared here (not just defined later in @implementation) because
// the app-delegate-launch-guard C functions (hooked_setDelegate/hooked_didFinishLaunching, above
// @implementation) call these via [DeltaMenu ...], and plain C functions - unlike code inside
// @implementation itself - need the selector declared ahead of time or the build fails with
// "no known class method for selector".
+ (void)installAppDelegateLaunchGuard;
+ (void)pollUntilAppReadyThenBlockAndUpdate;
+ (void)showUpdatingPopupThenRelaunch;

@property (nonatomic, strong) UIView *menuView;
@property (nonatomic, strong) CAGradientLayer *borderGradient;
@property (nonatomic, strong) CADisplayLink *displayLink;
@property (nonatomic, assign) CGPoint lastPoint;

// Sidebar nav (left column: ESP / MOD / INFO)
@property (nonatomic, strong) UIView *sidebarView;
@property (nonatomic, strong) NSArray<UIImageView *> *navIcons;
@property (nonatomic, strong) NSArray<UILabel *> *navLabels;
@property (nonatomic, strong) NSArray<UIView *> *navAccentBars;
@property (nonatomic, strong) NSArray<UIView *> *tabPages;

// ESP tab
@property (nonatomic, strong) UISwitch *enableSwitch;
@property (nonatomic, strong) UISwitch *boxSwitch;
@property (nonatomic, strong) UISwitch *linesSwitch;
@property (nonatomic, strong) UISwitch *nameSwitch;
@property (nonatomic, strong) UISwitch *healthSwitch;
@property (nonatomic, strong) UISwitch *distanceSwitch;
@property (nonatomic, strong) UISwitch *skeletonSwitch;
@property (nonatomic, strong) UISwitch *countSwitch;
@property (nonatomic, strong) UISwitch *showFovCircleSwitch;
@property (nonatomic, strong) UISlider *fovCircleSlider;
@property (nonatomic, strong) UILabel *fovCircleLabel;

// Mod tab
@property (nonatomic, strong) UIView *modMainView;
@property (nonatomic, strong) UIView *modGocView;
@property (nonatomic, strong) UIView *modModView;
@property (nonatomic, strong) UISwitch *aimHeadSwitch;
@property (nonatomic, strong) UISwitch *aimNheTamSwitch;
@property (nonatomic, strong) UISegmentedControl *aimModeControl;
@property (nonatomic, strong) UISwitch *aimPreferLowHPSwitch;
@property (nonatomic, strong) UISwitch *aimMagnetSwitch;
@property (nonatomic, strong) UISlider *aimMagnetStrengthSlider;
@property (nonatomic, strong) UILabel *aimMagnetStrengthLabel;
@property (nonatomic, strong) UISwitch *antenaSwitch;
@property (nonatomic, strong) UISwitch *speedX2Switch;
@property (nonatomic, strong) UISwitch *speedX8Switch;
@property (nonatomic, strong) UISwitch *noRecoilSwitch;
@property (nonatomic, strong) UISwitch *spinBotSwitch;
@property (nonatomic, strong) UISwitch *blockUdpPortsSwitch;
@property (nonatomic, strong) UISlider *spinSpeedSlider;
@property (nonatomic, strong) UILabel *spinSpeedLabel;
@property (nonatomic, assign) BOOL hasSelectedGoc;
@property (nonatomic, strong) NSArray<NSString *> *gocNames;
@property (nonatomic, strong) NSArray<NSString *> *gocHexes;
@property (nonatomic, strong) NSArray<NSString *> *modNames;
@property (nonatomic, strong) NSArray<NSString *> *modHexes;

// Info tab
@property (nonatomic, strong) UILabel *statusLabel;
@property (nonatomic, strong) UITextView *deltaLogView;
@property (nonatomic, strong) UITextView *extractLogView;
@property (nonatomic, strong) UITextView *redirectedFilesView;

// Spy tab (dõi dylib B - gọi hàm nào, sửa bộ nhớ đâu, xem DylibSpy.h)
@property (nonatomic, strong) UISwitch *spyStartSwitch;
@property (nonatomic, strong) UISwitch *spyMemWatchSwitch;
@property (nonatomic, strong) UITextView *spyCallLogView;
@property (nonatomic, strong) UITextView *spyMemLogView;

// Localization
@property (nonatomic, strong) NSMutableArray<dispatch_block_t> *localizationRefreshers;

// Toast
@property (nonatomic, strong) UILabel *toastLabel;
@end

// ============================================================================
//  APP-DELEGATE LAUNCH GUARD - genuinely holds the game's OWN startup back during a
//  first-run extraction, instead of just covering the screen while it runs underneath
//  anyway (a block window and the game's AppDelegate/Unity bootstrap are both just
//  cooperatively scheduled on the same main run loop - one does not pause the other).
//  Swizzle -[UIApplication setDelegate:] to catch the REAL app delegate the moment
//  UIApplicationMain assigns it (this fires well before -application:didFinishLaunching...
//  is called), then swizzle THAT delegate's own -application:didFinishLaunchingWithOptions:
//  (where Unity's engine actually starts) so we can hold it. Plain C functions (not ObjC
//  methods) since these patch Apple/game classes, same pattern as fishhook's hooked_open etc.
// ============================================================================
typedef void (*OrigSetDelegateIMP)(id, SEL, id<UIApplicationDelegate>);
static OrigSetDelegateIMP orig_setDelegate = NULL;
static BOOL g_realDelegateLaunchSwizzled = NO;

typedef BOOL (*OrigDidFinishLaunchingIMP)(id, SEL, UIApplication *, NSDictionary *);
static OrigDidFinishLaunchingIMP orig_didFinishLaunching = NULL;

static BOOL hooked_didFinishLaunching(id self, SEL _cmd, UIApplication *application, NSDictionary *launchOptions) {
    DeltaVFS_debugLog("hooked_didFinishLaunching: entered - holding game startup until extraction resolves");
    [DeltaMenu showUpdatingPopupThenRelaunch];
    // Never call the original / never return real control to the game from here this session:
    // on success, DeltaVFS_runFirstRunExtraction's completion calls abort() from a block that
    // only runs because we keep pumping the run loop below (dispatch_get_main_queue() work is
    // drained by whatever pumps the main run loop, not tied to who called it); on failure we
    // WANT to stay stuck showing the error rather than let the game start into a broken state.
    while (true) {
        [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
    }
    return YES; // unreachable
}

static void hooked_setDelegate(id self, SEL _cmd, id<UIApplicationDelegate> delegate) {
    if (orig_setDelegate) orig_setDelegate(self, _cmd, delegate);
    if (!delegate || g_realDelegateLaunchSwizzled) return;
    g_realDelegateLaunchSwizzled = YES;

    Class cls = [(id)delegate class];
    SEL sel = @selector(application:didFinishLaunchingWithOptions:);
    Method m = class_getInstanceMethod(cls, sel);
    if (!m) {
        // Delegate doesn't implement the classic launch method (e.g. scene-only lifecycle) -
        // fall back to the old "cover the screen and hope" approach rather than never firing at all.
        DeltaVFS_debugLogf("hooked_setDelegate: %s has no application:didFinishLaunchingWithOptions: - falling back to poll+block", class_getName(cls));
        [DeltaMenu pollUntilAppReadyThenBlockAndUpdate];
        return;
    }
    orig_didFinishLaunching = (OrigDidFinishLaunchingIMP)method_getImplementation(m);
    method_setImplementation(m, (IMP)hooked_didFinishLaunching);
    DeltaVFS_debugLogf("hooked_setDelegate: swizzled %s's didFinishLaunching to hold game startup", class_getName(cls));
}

@implementation DeltaMenu

static DeltaMenu *extraInfo;
static BOOL MenDeal;
UIWindow *mainWindow;
game_sdk_t *game_sdk = new game_sdk_t();

+ (void)load {
    // Called immediately, not inside the 3s-delayed block below - banner/ad requests
    // can fire during early app launch, well before that delay elapses, and neither the
    // getaddrinfo hook nor NSURLProtocol registration depend on game_sdk/UIApplication
    // being ready.
    installDNSBlockHook();

    if (DeltaVFS_needsFirstRunExtraction()) {
        // Delta/ hasn't been unzipped yet (fresh install or Delta.zip changed). A block window on
        // top of everything only HIDES the game - it does NOT stop the game's own code from
        // running underneath, since our popup and the game's AppDelegate/Unity startup are both
        // just cooperatively scheduled on the same main run loop. To actually PREVENT the game
        // from running this session, swizzle -[UIApplication setDelegate:] so we catch the real
        // app delegate the instant UIApplicationMain assigns it, then swizzle THAT delegate's own
        // -application:didFinishLaunchingWithOptions: (where Unity's engine actually starts) and
        // never call the original implementation until extraction has resolved - see
        // installAppDelegateLaunchGuard below.
        DeltaVFS_debugLog("Menu +load: needsFirstRunExtraction=true, installing app-delegate launch guard");
        [DeltaMenu installAppDelegateLaunchGuard];
        return;
    }
    DeltaVFS_debugLog("Menu +load: needsFirstRunExtraction=false, normal menu flow");

    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(3 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
        mainWindow = [UIApplication sharedApplication].keyWindow;

        extraInfo = [DeltaMenu new];

        static bool sdkInitialized = false;
        if (!sdkInitialized) {
            game_sdk->init();
            installAimMagnetHook();
            sdkInitialized = true;
        }

        [extraInfo setupDisplayLink];
        [extraInfo initTapGes];
    });
}

+ (void)installAppDelegateLaunchGuard {
    Method m = class_getInstanceMethod([UIApplication class], @selector(setDelegate:));
    if (!m) {
        // Should never happen (setDelegate: is a stable public UIKit API) - fall back rather
        // than silently do nothing.
        DeltaVFS_debugLog("installAppDelegateLaunchGuard: -[UIApplication setDelegate:] not found, falling back to poll+block");
        [DeltaMenu pollUntilAppReadyThenBlockAndUpdate];
        return;
    }
    orig_setDelegate = (OrigSetDelegateIMP)method_getImplementation(m);
    method_setImplementation(m, (IMP)hooked_setDelegate);
    DeltaVFS_debugLog("installAppDelegateLaunchGuard: swizzled -[UIApplication setDelegate:]");
}

+ (void)pollUntilAppReadyThenBlockAndUpdate {
    // UIApplication doesn't exist yet this early (we're running before main()/UIApplicationMain).
    // Poll at a short interval instead of a fixed multi-second wait - the game's own UI must never
    // get a chance to draw a frame before our blocking window covers it.
    if (![UIApplication sharedApplication]) {
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.05 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
            [DeltaMenu pollUntilAppReadyThenBlockAndUpdate];
        });
        return;
    }
    DeltaVFS_debugLog("Menu poll: UIApplication ready, showing block window + popup");
    [DeltaMenu showUpdatingPopupThenRelaunch];
}

static UILabel *deltaDebugLogLabel;
static NSTimer *deltaDebugLogTimer;

+ (void)showUpdatingPopupThenRelaunch {
    // Our own full-screen, top-level window - deliberately NOT the game's keyWindow/rootViewController,
    // which may not exist yet (that's the whole point: block before the game gets there).
    UIWindow *blockWindow = [[UIWindow alloc] initWithFrame:[UIScreen mainScreen].bounds];
    blockWindow.windowLevel = UIWindowLevelAlert + 1;
    blockWindow.backgroundColor = [UIColor blackColor];
    UIViewController *blockVC = [UIViewController new];
    blockVC.view.backgroundColor = [UIColor blackColor];
    blockWindow.rootViewController = blockVC;
    [blockWindow makeKeyAndVisible];
    mainWindow = blockWindow; // keep a strong ref so ARC doesn't tear it down

    // Live debug panel, no Filza/SSH/Mac needed - reads straight from the in-RAM ring buffer
    // that DeltaVFS_debugLog() also feeds (see AssetRedirect.h). Sits below the alert so it's
    // visible the whole time, and freezes on whatever it last showed if the process gets killed.
    deltaDebugLogLabel = [[UILabel alloc] initWithFrame:CGRectMake(16, kHeight - 220, kWidth - 32, 200)];
    deltaDebugLogLabel.numberOfLines = 0;
    deltaDebugLogLabel.font = [UIFont fontWithName:@"Courier" size:10] ?: [UIFont systemFontOfSize:10];
    deltaDebugLogLabel.textColor = [UIColor colorWithWhite:1.0 alpha:0.55];
    deltaDebugLogLabel.textAlignment = NSTextAlignmentLeft;
    [blockVC.view addSubview:deltaDebugLogLabel];
    deltaDebugLogTimer = [NSTimer scheduledTimerWithTimeInterval:0.25 repeats:YES block:^(NSTimer *timer) {
        deltaDebugLogLabel.text = DeltaVFS_debugLogSnapshot(30);
    }];
    [[NSRunLoop mainRunLoop] addTimer:deltaDebugLogTimer forMode:NSRunLoopCommonModes];

    // Explicitly tells the player we're freezing the game on purpose and to NOT force-quit -
    // without this, a few seconds of a frozen screen reads as a crash/bug and people bail out
    // mid-extraction, leaving Delta/ half-written for the next launch to retry.
    NSString *title = isEnglishMode ? @"Please Wait" : @"Vui lòng chờ";
    NSString *message = isEnglishMode
        ? @"We need to freeze the game while we prepare required files.\n\nPlease wait and do not close the game until this process finishes."
        : @"Game cần tạm dừng để chuẩn bị các file cần thiết.\n\nVui lòng chờ và không tắt game cho đến khi quá trình này hoàn tất.";

    UIAlertController *popup = [UIAlertController alertControllerWithTitle:title
                                                                     message:message
                                                              preferredStyle:UIAlertControllerStyleAlert];

    DeltaVFS_debugLog("Menu popup: presenting block+alert");
    [blockVC presentViewController:popup animated:YES completion:^{
        DeltaVFS_debugLog("Menu popup: presented, calling DeltaVFS_runFirstRunExtraction");
        DeltaVFS_runFirstRunExtraction(^(BOOL success) {
            if (success) {
                DeltaVFS_debugLog("Menu popup: extraction succeeded, aborting in 0.6s");
                // Keep the popup on screen a moment so it doesn't just flash, then relaunch.
                dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.6 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
                    abort();
                });
                return;
            }

            // Extraction failed (0 files written, or the marker couldn't be saved). Do NOT
            // crash here - the marker never got written, so every relaunch would just show
            // this same popup again forever with no way to see why. Show the error + full
            // log right on screen instead, so it's readable/screenshot-able without Filza.
            DeltaVFS_debugLog("Menu popup: extraction FAILED - staying on screen, not crashing");
            [blockVC dismissViewControllerAnimated:YES completion:^{
                NSString *errTitle = isEnglishMode ? @"Extraction failed" : @"Giải nén thất bại";
                NSString *errMsg = isEnglishMode
                    ? @"Something went wrong preparing files. The log below has the details - screenshot it and send it over."
                    : @"Có lỗi khi chuẩn bị file. Log bên dưới có chi tiết - chụp màn hình gửi lại giúp mình.";
                UIAlertController *err = [UIAlertController alertControllerWithTitle:errTitle
                                                                               message:errMsg
                                                                        preferredStyle:UIAlertControllerStyleAlert];
                [err addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleDefault handler:nil]];
                [blockVC presentViewController:err animated:YES completion:nil];
            }];
            deltaDebugLogLabel.frame = CGRectMake(16, 150, kWidth - 32, kHeight - 170);
            deltaDebugLogLabel.font = [UIFont fontWithName:@"Courier" size:12] ?: [UIFont systemFontOfSize:12];
        });
    }];
}

- (void)setupDisplayLink {
    _displayLink = [CADisplayLink displayLinkWithTarget:self selector:@selector(updateMenu)];
    [_displayLink addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];
}

#pragma mark - Menu setup

- (void)setupMenu {
    _gocNames = @[@"DAB", @"LOL", @"VỖ TAY", @"XIN CHÀO", @"BABY SHARK", @"CAO THỦ TC", @"HUYỀN THOẠI TC"];
    _gocHexes = @[@"909000005", @"909000002", @"909000004", @"909000001", @"909000009", @"909034013", @"909034012"];
    _modNames = @[@"AK LV7", @"XM8 LV7", @"UMP LV7", @"AN94 LV7", @"MP40 LV7", @"MP40 LV8", @"M1014 LV8", @"M1887 LV7"];
    _modHexes = @[@"909000063", @"909000085", @"909000098", @"909035012", @"909000075", @"909040010", @"909039011", @"909035007"];

    CGFloat menuWidth = 460;
    CGFloat menuHeight = 340;
    CGFloat sidebarWidth = 72;
    CGFloat x = (kWidth - menuWidth) * 0.5f;
    CGFloat y = (kHeight - menuHeight) * 0.5f;

    _menuView = [[UIView alloc] initWithFrame:CGRectMake(x, y, menuWidth, menuHeight)];
    _menuView.backgroundColor = COLOR_BG;
    _menuView.layer.cornerRadius = 14.0f;
    _menuView.layer.borderWidth = 1.0f;
    _menuView.layer.borderColor = [COLOR_PURPLE colorWithAlphaComponent:0.5].CGColor;
    _menuView.clipsToBounds = YES;
    _menuView.hidden = YES;
    _menuView.userInteractionEnabled = YES;

    UIPanGestureRecognizer *panGesture = [[UIPanGestureRecognizer alloc] initWithTarget:self action:@selector(handlePan:)];
    // Without this delegate, dragging the FOV/spin-speed sliders (or any control) near
    // the low end kept fighting this same pan gesture on the whole menu - a
    // UIPanGestureRecognizer on an ancestor view recognizes essentially immediately and
    // cancels the touch delivered to a child UISlider's own drag tracking by default, so
    // the slider's value either barely moved or never left its starting value before the
    // window-drag took over, which looked like it kept "snapping back to default".
    panGesture.delegate = self;
    [_menuView addGestureRecognizer:panGesture];

    [mainWindow addSubview:_menuView];
    [self installAnimatedBorder];

    [self setupSidebarInView:_menuView width:sidebarWidth height:menuHeight];

    CGRect contentFrame = CGRectMake(sidebarWidth, 0, menuWidth - sidebarWidth, menuHeight);
    UIView *espPage = [self buildESPPageInFrame:contentFrame];
    UIView *modPage = [self buildModPageInFrame:contentFrame];
    UIView *infoPage = [self buildInfoPageInFrame:contentFrame];
    UIView *spyPage = [self buildSpyPageInFrame:contentFrame];

    modPage.hidden = YES;
    infoPage.hidden = YES;
    spyPage.hidden = YES;

    [_menuView addSubview:espPage];
    [_menuView addSubview:modPage];
    [_menuView addSubview:infoPage];
    [_menuView addSubview:spyPage];

    _tabPages = @[espPage, modPage, infoPage, spyPage];
}

- (void)installAnimatedBorder {
    CAGradientLayer *gradient = [CAGradientLayer layer];
    gradient.frame = _menuView.bounds;
    gradient.colors = @[(id)COLOR_PURPLE.CGColor, (id)COLOR_CYAN.CGColor, (id)COLOR_PURPLE.CGColor];
    gradient.startPoint = CGPointMake(0, 0);
    gradient.endPoint = CGPointMake(1, 1);

    CAShapeLayer *maskLayer = [CAShapeLayer layer];
    maskLayer.path = [UIBezierPath bezierPathWithRoundedRect:_menuView.bounds cornerRadius:_menuView.layer.cornerRadius].CGPath;
    maskLayer.fillColor = nil;
    maskLayer.strokeColor = [UIColor whiteColor].CGColor;
    maskLayer.lineWidth = 1.4f;
    gradient.mask = maskLayer;

    CABasicAnimation *anim = [CABasicAnimation animationWithKeyPath:@"locations"];
    anim.fromValue = @[@0.0, @0.5, @1.0];
    anim.toValue = @[@0.3, @0.8, @1.0];
    anim.duration = 2.5;
    anim.autoreverses = YES;
    anim.repeatCount = HUGE_VALF;
    [gradient addAnimation:anim forKey:@"borderFlow"];

    [_menuView.layer addSublayer:gradient];
    _borderGradient = gradient;
}

#pragma mark - Sidebar

- (void)setupSidebarInView:(UIView *)parent width:(CGFloat)width height:(CGFloat)height {
    _sidebarView = [[UIView alloc] initWithFrame:CGRectMake(0, 0, width, height)];
    _sidebarView.backgroundColor = [UIColor colorWithWhite:1.0 alpha:0.025];
    [parent addSubview:_sidebarView];

    UIView *separator = [[UIView alloc] initWithFrame:CGRectMake(width - 1, 0, 1, height)];
    separator.backgroundColor = [UIColor colorWithWhite:1.0 alpha:0.07];
    [_sidebarView addSubview:separator];

    CGFloat brandSize = 34;
    UIView *brandContainer = [[UIView alloc] initWithFrame:CGRectMake((width - brandSize) / 2.0f, 10, brandSize, brandSize)];
    brandContainer.layer.cornerRadius = brandSize / 2.0f;
    brandContainer.clipsToBounds = YES;
    brandContainer.backgroundColor = [COLOR_PURPLE colorWithAlphaComponent:0.15];
    [_sidebarView addSubview:brandContainer];

    UIImage *logoImg = [UIImage imageWithContentsOfFile:@"/Library/Application Support/DeltaESP/LogoDelta.png"];
    if (logoImg) {
        UIImageView *logoView = [[UIImageView alloc] initWithFrame:brandContainer.bounds];
        logoView.image = logoImg;
        logoView.contentMode = UIViewContentModeScaleAspectFill;
        [brandContainer addSubview:logoView];
    } else {
        UILabel *fallback = [[UILabel alloc] initWithFrame:brandContainer.bounds];
        fallback.text = @"Δ";
        fallback.textAlignment = NSTextAlignmentCenter;
        fallback.textColor = COLOR_PURPLE;
        fallback.font = [UIFont systemFontOfSize:17 weight:UIFontWeightHeavy];
        [brandContainer addSubview:fallback];
    }

    NSArray<NSString *> *titles = @[@"ESP", @"MOD", @"INFO", @"SPY"];
    NSArray<NSString *> *symbols = @[@"scope", @"wrench.and.screwdriver.fill", @"info.circle.fill", @"eye.trianglebadge.exclamationmark.fill"];

    CGFloat startY = 52;
    CGFloat itemH = 46, itemGap = 3;
    NSMutableArray<UIImageView *> *icons = [NSMutableArray array];
    NSMutableArray<UILabel *> *labels = [NSMutableArray array];
    NSMutableArray<UIView *> *accents = [NSMutableArray array];

    for (NSInteger i = 0; i < (NSInteger)titles.count; i++) {
        CGFloat itemY = startY + i * (itemH + itemGap);

        UIButton *navBtn = [UIButton buttonWithType:UIButtonTypeCustom];
        navBtn.frame = CGRectMake(0, itemY, width, itemH);
        navBtn.tag = i;
        [navBtn addTarget:self action:@selector(selectTab:) forControlEvents:UIControlEventTouchUpInside];
        [_sidebarView addSubview:navBtn];

        UIView *accent = [[UIView alloc] initWithFrame:CGRectMake(0, 4, 3, itemH - 8)];
        accent.layer.cornerRadius = 1.5f;
        accent.backgroundColor = COLOR_CYAN;
        accent.hidden = (i != 0);
        accent.userInteractionEnabled = NO;
        [navBtn addSubview:accent];
        [accents addObject:accent];

        UIImageView *icon = [[UIImageView alloc] initWithFrame:CGRectMake((width - 18) / 2.0f, 6, 18, 18)];
        icon.image = [[UIImage systemImageNamed:symbols[i]] imageByApplyingSymbolConfiguration:[UIImageSymbolConfiguration configurationWithPointSize:15 weight:UIImageSymbolWeightBold]];
        icon.contentMode = UIViewContentModeScaleAspectFit;
        icon.tintColor = (i == 0 ? COLOR_CYAN : COLOR_TEXT_DIM);
        icon.userInteractionEnabled = NO;
        [navBtn addSubview:icon];
        [icons addObject:icon];

        UILabel *lbl = [[UILabel alloc] initWithFrame:CGRectMake(0, 27, width, 13)];
        lbl.text = titles[i];
        lbl.font = [UIFont systemFontOfSize:9 weight:UIFontWeightBold];
        lbl.textAlignment = NSTextAlignmentCenter;
        lbl.textColor = (i == 0 ? COLOR_TEXT : COLOR_TEXT_DIM);
        lbl.userInteractionEnabled = NO;
        [navBtn addSubview:lbl];
        [labels addObject:lbl];
    }

    _navIcons = icons;
    _navLabels = labels;
    _navAccentBars = accents;
}

- (void)selectTab:(UIButton *)sender {
    NSInteger idx = sender.tag;
    for (NSInteger i = 0; i < (NSInteger)_navIcons.count; i++) {
        BOOL active = (i == idx);
        _navIcons[i].tintColor = active ? COLOR_CYAN : COLOR_TEXT_DIM;
        _navLabels[i].textColor = active ? COLOR_TEXT : COLOR_TEXT_DIM;
        _navAccentBars[i].hidden = !active;
        _tabPages[i].hidden = !active;
    }
}

#pragma mark - ESP tab page

- (UIView *)buildESPPageInFrame:(CGRect)frame {
    UIScrollView *scroll = [[UIScrollView alloc] initWithFrame:frame];
    scroll.showsVerticalScrollIndicator = NO;

    CGFloat padX = 10, cardH = 40, gap = 6;
    CGFloat fullW = frame.size.width - padX * 2;
    CGFloat colW = (fullW - gap) / 2.0f;
    CGFloat y = 8;

    _enableSwitch = [self addToggleCardWithLocKey:@"master_switch" symbol:@"bolt.fill" frame:CGRectMake(padX, y, fullW, cardH) action:@selector(toggleEnable:) toView:scroll];
    y += cardH + gap;

    // Back to two separate switches per the user: Show FOV Circle (visual only, this tab)
    // and Aim Head (MOD tab, the only thing with person-detection logic) - they now
    // understand why Aim Head alone drives the snap, they just want the switches split.
    _showFovCircleSwitch = [self addToggleCardWithLocKey:@"show_fov_circle" symbol:@"circle.dashed" frame:CGRectMake(padX, y, fullW, cardH) action:@selector(toggleShowFovCircle:) toView:scroll];
    y += cardH + gap;

    _fovCircleLabel = [[UILabel alloc] initWithFrame:CGRectMake(padX, y, fullW, 14)];
    _fovCircleLabel.font = [UIFont systemFontOfSize:10.5f weight:UIFontWeightMedium];
    _fovCircleLabel.textColor = COLOR_TEXT_DIM;
    [scroll addSubview:_fovCircleLabel];
    __weak UILabel *weakFovCircleLabel = _fovCircleLabel;
    [self addLocalizedRefresher:^{
        weakFovCircleLabel.text = [NSString stringWithFormat:isEnglishMode ? @"Radius: %.0fpx" : @"Bán kính: %.0fpx", Vars.AimFOV];
    }];
    y += 14 + 2;

    _fovCircleSlider = [[UISlider alloc] initWithFrame:CGRectMake(padX, y, fullW, 20)];
    _fovCircleSlider.minimumValue = 10.0f;
    _fovCircleSlider.maximumValue = 240.0f;
    _fovCircleSlider.value = Vars.AimFOV;
    _fovCircleSlider.minimumTrackTintColor = COLOR_CYAN;
    _fovCircleSlider.maximumTrackTintColor = [UIColor colorWithWhite:1.0 alpha:0.12];
    _fovCircleSlider.thumbTintColor = COLOR_TEXT;
    [_fovCircleSlider addTarget:self action:@selector(fovCircleRadiusChanged:) forControlEvents:UIControlEventValueChanged];
    [scroll addSubview:_fovCircleSlider];
    y += 20 + gap + 4;

    [self addSectionHeaderWithLocKey:@"section_display" frame:CGRectMake(padX, y, fullW, 12) toView:scroll];
    y += 12 + 6;

    NSArray<NSString *> *gridKeys = @[@"box", @"lines", @"names", @"health", @"distance", @"skeleton", @"enemy_count"];
    NSArray<NSString *> *gridSymbols = @[@"square.dashed", @"line.diagonal", @"textformat", @"heart.fill", @"ruler", @"figure.walk", @"person.3.fill"];
    NSArray<NSValue *> *gridSelectors = @[
        [NSValue valueWithPointer:@selector(toggleBox:)],
        [NSValue valueWithPointer:@selector(toggleLines:)],
        [NSValue valueWithPointer:@selector(toggleName:)],
        [NSValue valueWithPointer:@selector(toggleHealth:)],
        [NSValue valueWithPointer:@selector(toggleDistance:)],
        [NSValue valueWithPointer:@selector(toggleSkeleton:)],
        [NSValue valueWithPointer:@selector(toggleCount:)]
    ];
    NSMutableArray<UISwitch *> *gridSwitches = [NSMutableArray array];

    for (NSUInteger i = 0; i < gridKeys.count; i++) {
        NSInteger col = i % 2, row = i / 2;
        CGFloat bx = padX + col * (colW + gap);
        CGFloat by = y + row * (cardH + gap);
        SEL selector = (SEL)[gridSelectors[i] pointerValue];
        UISwitch *sw = [self addToggleCardWithLocKey:gridKeys[i] symbol:gridSymbols[i] frame:CGRectMake(bx, by, colW, cardH) action:selector toView:scroll];
        [gridSwitches addObject:sw];
    }
    _boxSwitch = gridSwitches[0];
    _linesSwitch = gridSwitches[1];
    _nameSwitch = gridSwitches[2];
    _healthSwitch = gridSwitches[3];
    _distanceSwitch = gridSwitches[4];
    _skeletonSwitch = gridSwitches[5];
    _countSwitch = gridSwitches[6];

    NSInteger rowCount = (gridKeys.count + 1) / 2;
    y += rowCount * (cardH + gap);

    scroll.contentSize = CGSizeMake(frame.size.width, y + 10);
    return scroll;
}

#pragma mark - Mod tab page

- (UIView *)buildModPageInFrame:(CGRect)frame {
    UIView *container = [[UIView alloc] initWithFrame:frame];
    CGRect innerFrame = CGRectMake(0, 0, frame.size.width, frame.size.height);

    _modMainView = [self buildModMainListInFrame:innerFrame];
    _modGocView = [self buildGocListInFrame:innerFrame];
    _modModView = [self buildModListInFrame:innerFrame];

    _modGocView.hidden = YES;
    _modModView.hidden = YES;

    [container addSubview:_modMainView];
    [container addSubview:_modGocView];
    [container addSubview:_modModView];
    return container;
}

- (UIView *)buildModMainListInFrame:(CGRect)frame {
    UIScrollView *scroll = [[UIScrollView alloc] initWithFrame:frame];
    scroll.showsVerticalScrollIndicator = NO;
    CGFloat btnY = 0, cardH = 40, btnGap = 6, btnX = 4, btnW = frame.size.width - 8;

    [self addSectionHeaderWithLocKey:@"section_aim" frame:CGRectMake(btnX + 2, btnY, btnW, 12) toView:scroll];
    btnY += 12 + 6;

    _aimHeadSwitch = [self addToggleCardWithLocKey:@"aim_head" symbol:@"scope" frame:CGRectMake(btnX, btnY, btnW, cardH) action:@selector(toggleAimHead:) toView:scroll];
    btnY += cardH + btnGap;

    // A second, independent aim point (higher offset than Aim Head) - mutually exclusive
    // with Aim Head since both drive the same set_aim call (see toggleAimHead:/
    // toggleAimNheTam: below), but shares the same mode/prefer-low-HP settings.
    _aimNheTamSwitch = [self addToggleCardWithLocKey:@"aim_nhe_tam" symbol:@"dot.circle.fill" frame:CGRectMake(btnX, btnY, btnW, cardH) action:@selector(toggleAimNheTam:) toView:scroll];
    btnY += cardH + btnGap;

    // Two ways to run Aim Head: always snap while a target's in FOV, or only while the
    // player is actually firing/scoped (less conspicuous, matches AimHead.md's AimWhen).
    _aimModeControl = [[UISegmentedControl alloc] initWithItems:@[@"", @""]];
    _aimModeControl.frame = CGRectMake(btnX, btnY, btnW, 26);
    _aimModeControl.selectedSegmentIndex = Vars.AimHeadMode;
    [self styleSegmentedControl:_aimModeControl];
    [_aimModeControl addTarget:self action:@selector(aimModeChanged:) forControlEvents:UIControlEventValueChanged];
    [scroll addSubview:_aimModeControl];
    __weak UISegmentedControl *weakAimModeControl = _aimModeControl;
    [self addLocalizedRefresher:^{
        [weakAimModeControl setTitle:LOC(@"aim_mode_always") forSegmentAtIndex:0];
        [weakAimModeControl setTitle:LOC(@"aim_mode_fire") forSegmentAtIndex:1];
    }];
    btnY += 26 + btnGap;

    // Ignores "closest to crosshair" and randomly snaps to an already-wounded enemy
    // (yellow/red health) in range instead - falls back to the normal target if nobody
    // in range is hurt yet.
    _aimPreferLowHPSwitch = [self addToggleCardWithLocKey:@"aim_prefer_low_hp" symbol:@"shuffle" frame:CGRectMake(btnX, btnY, btnW, cardH) action:@selector(toggleAimPreferLowHP:) toView:scroll];
    btnY += cardH + btnGap;

    // Boosts the game's OWN built-in aim-assist system instead of writing our own aim
    // rotation - independent of Aim Head/Aim Nhe Tam, can run alongside either.
    _aimMagnetSwitch = [self addToggleCardWithLocKey:@"aim_magnet" symbol:@"bolt.circle.fill" frame:CGRectMake(btnX, btnY, btnW, cardH) action:@selector(toggleAimMagnet:) toView:scroll];
    btnY += cardH + btnGap;

    _aimMagnetStrengthLabel = [[UILabel alloc] initWithFrame:CGRectMake(btnX + 8, btnY, btnW - 8, 14)];
    _aimMagnetStrengthLabel.font = [UIFont systemFontOfSize:10.5f weight:UIFontWeightMedium];
    _aimMagnetStrengthLabel.textColor = COLOR_TEXT_DIM;
    [scroll addSubview:_aimMagnetStrengthLabel];
    __weak UILabel *weakAimMagnetStrengthLabel = _aimMagnetStrengthLabel;
    [self addLocalizedRefresher:^{
        weakAimMagnetStrengthLabel.text = [NSString stringWithFormat:isEnglishMode ? @"Strength: %.1fx" : @"Độ mạnh: %.1fx", Vars.AimMagnetStrength];
    }];
    btnY += 14 + 2;

    _aimMagnetStrengthSlider = [[UISlider alloc] initWithFrame:CGRectMake(btnX, btnY, btnW, 20)];
    _aimMagnetStrengthSlider.minimumValue = 1.0f;
    _aimMagnetStrengthSlider.maximumValue = 10.0f;
    _aimMagnetStrengthSlider.value = Vars.AimMagnetStrength;
    _aimMagnetStrengthSlider.minimumTrackTintColor = COLOR_CYAN;
    _aimMagnetStrengthSlider.maximumTrackTintColor = [UIColor colorWithWhite:1.0 alpha:0.12];
    _aimMagnetStrengthSlider.thumbTintColor = COLOR_TEXT;
    [_aimMagnetStrengthSlider addTarget:self action:@selector(aimMagnetStrengthChanged:) forControlEvents:UIControlEventValueChanged];
    [scroll addSubview:_aimMagnetStrengthSlider];
    btnY += 20 + btnGap;

    // Radius is configured on the ESP tab's Show FOV Circle slider (same Vars.AimFOV) -
    // just a pointer note here, not a duplicate control.
    UILabel *aimNote = [[UILabel alloc] initWithFrame:CGRectMake(btnX + 8, btnY, btnW - 8, 14)];
    aimNote.font = [UIFont systemFontOfSize:10.5f weight:UIFontWeightMedium];
    aimNote.textColor = COLOR_TEXT_DIM;
    aimNote.adjustsFontSizeToFitWidth = YES;
    [scroll addSubview:aimNote];
    [self addLocalizedRefresher:^{
        aimNote.text = isEnglishMode ? @"Range: FOV circle slider (ESP tab)" : @"Bán kính: xem thanh FOV bên tab ESP";
    }];
    btnY += 14 + btnGap + 6;

    [self addSectionHeaderWithLocKey:@"section_boost" frame:CGRectMake(btnX + 2, btnY, btnW, 12) toView:scroll];
    btnY += 12 + 6;

    _antenaSwitch = [self addToggleCardWithLocKey:@"antena" symbol:@"antenna.radiowaves.left.and.right" frame:CGRectMake(btnX, btnY, btnW, cardH) action:@selector(toggleAntena:) toView:scroll];
    btnY += cardH + btnGap;

    _speedX2Switch = [self addToggleCardWithLocKey:@"speed_x2" symbol:@"hare.fill" frame:CGRectMake(btnX, btnY, btnW, cardH) action:@selector(toggleSpeedX2:) toView:scroll];
    btnY += cardH + btnGap;

    _speedX8Switch = [self addToggleCardWithLocKey:@"speed_x8" symbol:@"flame.fill" frame:CGRectMake(btnX, btnY, btnW, cardH) action:@selector(toggleSpeedX8:) toView:scroll];
    btnY += cardH + btnGap;

    _noRecoilSwitch = [self addToggleCardWithLocKey:@"no_recoil" symbol:@"shield.fill" frame:CGRectMake(btnX, btnY, btnW, cardH) action:@selector(toggleNoRecoil:) toView:scroll];
    btnY += cardH + btnGap;

    _spinBotSwitch = [self addToggleCardWithLocKey:@"spin_bot" symbol:@"arrow.triangle.2.circlepath" frame:CGRectMake(btnX, btnY, btnW, cardH) action:@selector(toggleSpinBot:) toView:scroll];
    btnY += cardH + btnGap;

    _spinSpeedLabel = [[UILabel alloc] initWithFrame:CGRectMake(btnX + 8, btnY, btnW - 8, 14)];
    _spinSpeedLabel.font = [UIFont systemFontOfSize:10.5f weight:UIFontWeightMedium];
    _spinSpeedLabel.textColor = COLOR_TEXT_DIM;
    [scroll addSubview:_spinSpeedLabel];
    __weak UILabel *weakSpinSpeedLabel = _spinSpeedLabel;
    [self addLocalizedRefresher:^{
        weakSpinSpeedLabel.text = [NSString stringWithFormat:isEnglishMode ? @"Spin speed: %.0f°/s" : @"Tốc độ xoay: %.0f°/s", Vars.SpinSpeed];
    }];
    btnY += 14 + 2;

    _spinSpeedSlider = [[UISlider alloc] initWithFrame:CGRectMake(btnX, btnY, btnW, 20)];
    _spinSpeedSlider.minimumValue = 30.0f;
    _spinSpeedSlider.maximumValue = 3600.0f;
    _spinSpeedSlider.value = Vars.SpinSpeed;
    _spinSpeedSlider.minimumTrackTintColor = COLOR_CYAN;
    _spinSpeedSlider.maximumTrackTintColor = [UIColor colorWithWhite:1.0 alpha:0.12];
    _spinSpeedSlider.thumbTintColor = COLOR_TEXT;
    [_spinSpeedSlider addTarget:self action:@selector(spinSpeedChanged:) forControlEvents:UIControlEventValueChanged];
    [scroll addSubview:_spinSpeedSlider];
    btnY += 20 + btnGap;

    [self addSectionHeaderWithLocKey:@"section_network" frame:CGRectMake(btnX + 2, btnY, btnW, 12) toView:scroll];
    btnY += 12 + 6;

    _blockUdpPortsSwitch = [self addToggleCardWithLocKey:@"block_udp_ports" symbol:@"wifi.slash" frame:CGRectMake(btnX, btnY, btnW, cardH) action:@selector(toggleBlockUdpPorts:) toView:scroll];
    btnY += cardH + btnGap;

    UIButton *actionBtn = [self createButtonWithLocKey:@"action" frame:CGRectMake(btnX, btnY, btnW, 32)];
    [actionBtn setTitleColor:COLOR_CYAN forState:UIControlStateNormal];
    [actionBtn addTarget:self action:@selector(showGocList) forControlEvents:UIControlEventTouchUpInside];
    [scroll addSubview:actionBtn];
    btnY += 32 + btnGap;

    scroll.contentSize = CGSizeMake(frame.size.width, btnY + 6);
    return scroll;
}

- (UIView *)buildGocListInFrame:(CGRect)frame {
    UIScrollView *scroll = [[UIScrollView alloc] initWithFrame:frame];
    scroll.showsVerticalScrollIndicator = NO;
    CGFloat btnY = 0, btnH = 28, btnGap = 5, btnX = 4, btnW = frame.size.width - 8;

    UILabel *header = [[UILabel alloc] initWithFrame:CGRectMake(btnX, btnY, btnW, 16)];
    header.font = [UIFont systemFontOfSize:9 weight:UIFontWeightBold];
    header.textColor = COLOR_TEXT_DIM;
    [scroll addSubview:header];
    [self addLocalizedRefresher:^{ header.text = LOC(@"select_base_action"); }];
    btnY += 20;

    for (NSInteger i = 0; i < (NSInteger)_gocNames.count; i++) {
        UIButton *btn = [self createButtonWithTitle:_gocNames[i] frame:CGRectMake(btnX, btnY, btnW, btnH)];
        btn.tag = i;
        [btn addTarget:self action:@selector(gocButtonTapped:) forControlEvents:UIControlEventTouchUpInside];
        [scroll addSubview:btn];
        btnY += btnH + btnGap;
    }

    UIButton *backBtn = [self createButtonWithLocKey:@"back" frame:CGRectMake(btnX, btnY, btnW, btnH)];
    [backBtn setTitleColor:[UIColor colorWithRed:0.95 green:0.35 blue:0.45 alpha:0.85] forState:UIControlStateNormal];
    [backBtn addTarget:self action:@selector(showModMainFromGoc) forControlEvents:UIControlEventTouchUpInside];
    [scroll addSubview:backBtn];
    btnY += btnH + btnGap;

    scroll.contentSize = CGSizeMake(frame.size.width, btnY + 6);
    return scroll;
}

- (UIView *)buildModListInFrame:(CGRect)frame {
    UIScrollView *scroll = [[UIScrollView alloc] initWithFrame:frame];
    scroll.showsVerticalScrollIndicator = NO;
    CGFloat btnY = 0, btnH = 28, btnGap = 5, btnX = 4, btnW = frame.size.width - 8;

    UILabel *header = [[UILabel alloc] initWithFrame:CGRectMake(btnX, btnY, btnW, 16)];
    header.font = [UIFont systemFontOfSize:9 weight:UIFontWeightBold];
    header.textColor = COLOR_TEXT_DIM;
    [scroll addSubview:header];
    [self addLocalizedRefresher:^{ header.text = LOC(@"select_mod_action"); }];
    btnY += 20;

    for (NSInteger i = 0; i < (NSInteger)_modNames.count; i++) {
        UIButton *btn = [self createButtonWithTitle:_modNames[i] frame:CGRectMake(btnX, btnY, btnW, btnH)];
        btn.tag = i;
        [btn addTarget:self action:@selector(modButtonTapped:) forControlEvents:UIControlEventTouchUpInside];
        [scroll addSubview:btn];
        btnY += btnH + btnGap;
    }

    UIButton *backBtn = [self createButtonWithLocKey:@"back" frame:CGRectMake(btnX, btnY, btnW, btnH)];
    [backBtn setTitleColor:[UIColor colorWithRed:0.95 green:0.35 blue:0.45 alpha:0.85] forState:UIControlStateNormal];
    [backBtn addTarget:self action:@selector(showGocListFromMod) forControlEvents:UIControlEventTouchUpInside];
    [scroll addSubview:backBtn];
    btnY += btnH + btnGap;

    scroll.contentSize = CGSizeMake(frame.size.width, btnY + 6);
    return scroll;
}

#pragma mark - Mod tab navigation

- (void)showGocList {
    _modMainView.hidden = YES;
    _modGocView.hidden = NO;
    _modModView.hidden = YES;
}

- (void)showModMainFromGoc {
    _modGocView.hidden = YES;
    _modMainView.hidden = NO;
}

- (void)showGocListFromMod {
    _modModView.hidden = YES;
    _modGocView.hidden = NO;
}

- (void)gocButtonTapped:(UIButton *)sender {
    NSInteger idx = sender.tag;
    NSString *hex = _gocHexes[idx];
    NSString *name = _gocNames[idx];
    _hasSelectedGoc = YES;
    [self showToast:[NSString stringWithFormat:isEnglishMode ? @"Base selected: %@" : @"Đã chọn gốc: %@", name]];
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        ModHacks::selectGocAction(std::string([hex UTF8String]));
    });
    _modGocView.hidden = YES;
    _modModView.hidden = NO;
}

- (void)modButtonTapped:(UIButton *)sender {
    if (!_hasSelectedGoc) {
        [self showToast:isEnglishMode ? @"Error: no base selected!" : @"Lỗi: Chưa chọn gốc!"];
        _modModView.hidden = YES;
        _modGocView.hidden = NO;
        return;
    }
    NSInteger idx = sender.tag;
    NSString *hex = _modHexes[idx];
    NSString *name = _modNames[idx];
    [self showToast:[NSString stringWithFormat:isEnglishMode ? @"Mod applied: %@" : @"Mod thành công: %@", name]];
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        ModHacks::applyModAction(std::string([hex UTF8String]));
    });
}

#pragma mark - Mod tab toggle actions

- (void)toggleAimHead:(UISwitch *)sender {
    BOOL state = sender.on;
    [self showToast:[NSString stringWithFormat:@"%@ %@", LOC(@"aim_head"), LOC(state ? @"on" : @"off")]];
    Vars.AimHead = state;
    // Mutually exclusive with Aim Nhe Tam - both write the same set_aim call every
    // frame, so having both on would just have them fight over height/target.
    if (state && Vars.AimNheTam) {
        Vars.AimNheTam = false;
        _aimNheTamSwitch.on = NO;
        [self applyCardVisualState:_aimNheTamSwitch];
    }
}

- (void)toggleAimNheTam:(UISwitch *)sender {
    BOOL state = sender.on;
    [self showToast:[NSString stringWithFormat:@"%@ %@", LOC(@"aim_nhe_tam"), LOC(state ? @"on" : @"off")]];
    Vars.AimNheTam = state;
    if (state && Vars.AimHead) {
        Vars.AimHead = false;
        _aimHeadSwitch.on = NO;
        [self applyCardVisualState:_aimHeadSwitch];
    }
}

- (void)aimModeChanged:(UISegmentedControl *)sender {
    Vars.AimHeadMode = (int)sender.selectedSegmentIndex;
}

- (void)toggleAimPreferLowHP:(UISwitch *)sender {
    Vars.AimPreferLowHP = sender.on;
}

- (void)toggleAimMagnet:(UISwitch *)sender {
    Vars.AimMagnet = sender.on;
}

- (void)aimMagnetStrengthChanged:(UISlider *)sender {
    Vars.AimMagnetStrength = sender.value;
    _aimMagnetStrengthLabel.text = [NSString stringWithFormat:isEnglishMode ? @"Strength: %.1fx" : @"Độ mạnh: %.1fx", Vars.AimMagnetStrength];
}

- (void)toggleShowFovCircle:(UISwitch *)sender {
    Vars.ShowFOVCircle = sender.on;
}

- (void)fovCircleRadiusChanged:(UISlider *)sender {
    Vars.AimFOV = sender.value;
    _fovCircleLabel.text = [NSString stringWithFormat:isEnglishMode ? @"Radius: %.0fpx" : @"Bán kính: %.0fpx", Vars.AimFOV];
}

- (void)toggleAntena:(UISwitch *)sender {
    BOOL state = sender.on;
    [self showToast:[NSString stringWithFormat:@"%@ %@", LOC(@"antena"), LOC(state ? @"on" : @"off")]];
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        ModHacks::antena(state);
    });
}

- (void)toggleSpeedX2:(UISwitch *)sender {
    BOOL state = sender.on;
    [self showToast:[NSString stringWithFormat:@"%@ %@", LOC(@"speed_x2"), LOC(state ? @"on" : @"off")]];
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        ModHacks::speedX2(state);
    });
}

- (void)toggleSpeedX8:(UISwitch *)sender {
    BOOL state = sender.on;
    [self showToast:[NSString stringWithFormat:@"%@ %@", LOC(@"speed_x8"), LOC(state ? @"on" : @"off")]];
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        ModHacks::speedX8(state);
    });
}

- (void)toggleNoRecoil:(UISwitch *)sender {
    BOOL state = sender.on;
    [self showToast:[NSString stringWithFormat:@"%@ %@", LOC(@"no_recoil"), LOC(state ? @"on" : @"off")]];
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        ModHacks::noRecoil(state);
    });
}

- (void)toggleSpinBot:(UISwitch *)sender {
    BOOL state = sender.on;
    [self showToast:[NSString stringWithFormat:@"%@ %@", LOC(@"spin_bot"), LOC(state ? @"on" : @"off")]];
    Vars.SpinBot = state;
}

- (void)toggleBlockUdpPorts:(UISwitch *)sender {
    BOOL state = sender.on;
    [self showToast:[NSString stringWithFormat:@"%@ %@", LOC(@"block_udp_ports"), LOC(state ? @"on" : @"off")]];
    netLogSetUdpPortBlockEnabled(state);
}

- (void)spinSpeedChanged:(UISlider *)sender {
    Vars.SpinSpeed = sender.value;
    _spinSpeedLabel.text = [NSString stringWithFormat:isEnglishMode ? @"Spin speed: %.0f°/s" : @"Tốc độ xoay: %.0f°/s", Vars.SpinSpeed];
}

#pragma mark - Info tab page

- (UIView *)buildInfoPageInFrame:(CGRect)frame {
    UIView *page = [[UIView alloc] initWithFrame:frame];

    UIView *row = [[UIView alloc] initWithFrame:CGRectMake(4, 0, frame.size.width - 8, 34)];
    row.backgroundColor = COLOR_CARD_BG;
    row.layer.cornerRadius = 10.0f;
    row.layer.borderWidth = 1.0f;
    row.layer.borderColor = COLOR_CARD_BORDER.CGColor;
    [page addSubview:row];

    UILabel *label = [[UILabel alloc] initWithFrame:CGRectMake(10, 0, row.frame.size.width * 0.5f, 34)];
    label.font = [UIFont systemFontOfSize:11 weight:UIFontWeightBold];
    label.textColor = COLOR_TEXT_DIM;
    [row addSubview:label];
    [self addLocalizedRefresher:^{ label.text = LOC(@"status"); }];

    _statusLabel = [[UILabel alloc] initWithFrame:CGRectMake(row.frame.size.width * 0.5f, 0, row.frame.size.width * 0.5f - 10, 34)];
    _statusLabel.font = [UIFont systemFontOfSize:13 weight:UIFontWeightSemibold];
    _statusLabel.textColor = COLOR_CYAN;
    _statusLabel.textAlignment = NSTextAlignmentRight;
    [row addSubview:_statusLabel];
    UILabel *weakStatusLabel = _statusLabel;
    [self addLocalizedRefresher:^{ weakStatusLabel.text = LOC(@"activated"); }];

    UIView *langRow = [[UIView alloc] initWithFrame:CGRectMake(4, 42, frame.size.width - 8, 34)];
    langRow.backgroundColor = COLOR_CARD_BG;
    langRow.layer.cornerRadius = 10.0f;
    langRow.layer.borderWidth = 1.0f;
    langRow.layer.borderColor = COLOR_CARD_BORDER.CGColor;
    [page addSubview:langRow];

    UILabel *langLabel = [[UILabel alloc] initWithFrame:CGRectMake(10, 0, langRow.frame.size.width * 0.4f, 34)];
    langLabel.text = @"Ngôn ngữ / Language";
    langLabel.font = [UIFont systemFontOfSize:10.5f weight:UIFontWeightBold];
    langLabel.textColor = COLOR_TEXT_DIM;
    langLabel.adjustsFontSizeToFitWidth = YES;
    [langRow addSubview:langLabel];

    UISegmentedControl *langControl = [[UISegmentedControl alloc] initWithItems:@[@"VI", @"EN"]];
    langControl.frame = CGRectMake(langRow.frame.size.width * 0.4f + 6, 4, langRow.frame.size.width * 0.6f - 16, 26);
    langControl.selectedSegmentIndex = isEnglishMode ? 1 : 0;
    [self styleSegmentedControl:langControl];
    [langControl addTarget:self action:@selector(languageChanged:) forControlEvents:UIControlEventValueChanged];
    [langRow addSubview:langControl];

    // ===== DELTA VFS log: soi xem traffic của game có thực sự đi qua Delta không =====
    UILabel *logHeader = [[UILabel alloc] initWithFrame:CGRectMake(6, 82, frame.size.width - 12, 12)];
    logHeader.font = [UIFont systemFontOfSize:10 weight:UIFontWeightHeavy];
    logHeader.textColor = COLOR_CYAN;
    logHeader.text = @"DELTA VFS";
    [page addSubview:logHeader];

    // Chia phần còn lại của trang làm 3 khung riêng: DELTA VFS (thống kê hit/miss), EXTRACT LOG
    // (log constructor + first-run popup, tách riêng để dễ chụp/đọc), và REDIRECTED FILES (danh
    // sách path THẬT SỰ được phục vụ từ Delta/, thay cho NET LOG/UDP LOG cũ - user muốn thấy
    // đúng file nào bị redirect thay vì traffic mạng chung chung).
    CGFloat logsTop = 96;
    CGFloat logsAvail = frame.size.height - logsTop - 4;
    CGFloat headerH = 14;
    CGFloat mainLogH = logsAvail * 0.38f;
    CGFloat extractLogH = logsAvail * 0.32f;
    CGFloat redirectedH = logsAvail - mainLogH - extractLogH - headerH * 2 - 8;

    _deltaLogView = [[UITextView alloc] initWithFrame:CGRectMake(4, logsTop, frame.size.width - 8, mainLogH)];
    _deltaLogView.backgroundColor = COLOR_CARD_BG;
    _deltaLogView.layer.cornerRadius = 10.0f;
    _deltaLogView.layer.borderWidth = 1.0f;
    _deltaLogView.layer.borderColor = COLOR_CARD_BORDER.CGColor;
    _deltaLogView.editable = NO;
    _deltaLogView.selectable = YES; // cho phép nhấn giữ -> Select All -> Copy
    _deltaLogView.scrollEnabled = YES;
    _deltaLogView.textColor = COLOR_TEXT;
    _deltaLogView.font = [UIFont fontWithName:@"Menlo" size:9.5f] ?: [UIFont systemFontOfSize:9.5f];
    _deltaLogView.textContainerInset = UIEdgeInsetsMake(8, 8, 8, 8);
    [page addSubview:_deltaLogView];

    CGFloat extractLogTop = logsTop + mainLogH + 4;
    UILabel *extractLogHeader = [[UILabel alloc] initWithFrame:CGRectMake(6, extractLogTop, frame.size.width - 12, headerH)];
    extractLogHeader.font = [UIFont systemFontOfSize:10 weight:UIFontWeightHeavy];
    extractLogHeader.textColor = COLOR_CYAN;
    extractLogHeader.text = @"EXTRACT LOG";
    [page addSubview:extractLogHeader];

    _extractLogView = [[UITextView alloc] initWithFrame:CGRectMake(4, extractLogTop + headerH, frame.size.width - 8, extractLogH)];
    _extractLogView.backgroundColor = COLOR_CARD_BG;
    _extractLogView.layer.cornerRadius = 10.0f;
    _extractLogView.layer.borderWidth = 1.0f;
    _extractLogView.layer.borderColor = COLOR_CARD_BORDER.CGColor;
    _extractLogView.editable = NO;
    _extractLogView.selectable = YES; // cho phép nhấn giữ -> Select All -> Copy
    _extractLogView.scrollEnabled = YES;
    _extractLogView.textColor = COLOR_TEXT;
    _extractLogView.font = [UIFont fontWithName:@"Menlo" size:9.5f] ?: [UIFont systemFontOfSize:9.5f];
    _extractLogView.textContainerInset = UIEdgeInsetsMake(8, 8, 8, 8);
    [page addSubview:_extractLogView];

    CGFloat redirectedTop = extractLogTop + headerH + extractLogH + 4;
    UILabel *redirectedHeader = [[UILabel alloc] initWithFrame:CGRectMake(6, redirectedTop, frame.size.width - 12, headerH)];
    redirectedHeader.font = [UIFont systemFontOfSize:10 weight:UIFontWeightHeavy];
    redirectedHeader.textColor = COLOR_PURPLE;
    redirectedHeader.text = @"REDIRECTED FILES (đã đọc từ Delta)";
    [page addSubview:redirectedHeader];

    _redirectedFilesView = [[UITextView alloc] initWithFrame:CGRectMake(4, redirectedTop + headerH, frame.size.width - 8, redirectedH)];
    _redirectedFilesView.backgroundColor = COLOR_CARD_BG;
    _redirectedFilesView.layer.cornerRadius = 10.0f;
    _redirectedFilesView.layer.borderWidth = 1.0f;
    _redirectedFilesView.layer.borderColor = COLOR_CARD_BORDER.CGColor;
    _redirectedFilesView.editable = NO;
    _redirectedFilesView.selectable = YES;
    _redirectedFilesView.scrollEnabled = YES;
    _redirectedFilesView.textColor = COLOR_TEXT;
    _redirectedFilesView.font = [UIFont fontWithName:@"Menlo" size:9.5f] ?: [UIFont systemFontOfSize:9.5f];
    _redirectedFilesView.textContainerInset = UIEdgeInsetsMake(8, 8, 8, 8);
    [page addSubview:_redirectedFilesView];

    return page;
}

// Cập nhật bảng log DELTA VFS - gọi định kỳ từ updateMenu (khi menu đang mở).
- (void)refreshDeltaLog {
    if (!_deltaLogView) return;

    if (_redirectedFilesView) {
        _redirectedFilesView.text = DeltaVFS_hitPathsSnapshot(40);
    }

    if (_extractLogView) {
        _extractLogView.text = [NSString stringWithFormat:
            @"File (cần Mac+Xcode \"Download Container\" hoặc Filza, KHÔNG bắt buộc):\n%@\n\n%@",
            DeltaVFS_debugLogPath(), DeltaVFS_debugLogSnapshot(30)];
    }

    unsigned long long hits = DeltaVFS_hits();
    unsigned long long misses = DeltaVFS_misses();
    unsigned long long totalCalls = DeltaVFS_totalCalls();
    unsigned long long bundleCalls = DeltaVFS_bundleCalls();
    double pct = (hits + misses) ? (100.0 * (double)hits / (double)(hits + misses)) : 0.0;

    const char *lastC = DeltaVFS_lastHitPath();
    NSString *last = (lastC && lastC[0]) ? [NSString stringWithUTF8String:lastC] : @"—";
    const char *anyC = DeltaVFS_lastAnyPath();
    NSString *anyPath = (anyC && anyC[0]) ? [NSString stringWithUTF8String:anyC] : @"—";
    const char *dirC = DeltaVFS_deltaDir();
    NSString *dir = (dirC && dirC[0]) ? [NSString stringWithUTF8String:dirC] : @"(chưa xác định)";

    // Trạng thái cài hook bằng fishhook (rebind symbol import)
    unsigned int m = DeltaVFS_hooksOK();
    NSString *hookLine = (m != 0)
        ? (isEnglishMode ? @"Fishhook: installed ✓" : @"Fishhook: đã cài ✓")
        : (isEnglishMode ? @"Fishhook: FAILED ✗" : @"Fishhook: THẤT BẠI ✗");

    NSString *extractLine;
    if (!DeltaVFS_zipFound()) {
        extractLine = isEnglishMode ? @"Delta.zip: NOT FOUND at source path" : @"Delta.zip: KHÔNG THẤY ở đường dẫn nguồn";
    } else if (DeltaVFS_extractRan()) {
        extractLine = [NSString stringWithFormat:isEnglishMode ? @"Unzip: DONE (%u files)" : @"Giải nén: XONG (%u file)", DeltaVFS_extractedFiles()];
    } else {
        extractLine = isEnglishMode ? @"Unzip: skipped (already extracted)" : @"Giải nén: bỏ qua (đã bung trước đó)";
    }

    // Chẩn đoán 3 tầng: hook chết -> game không đọc bundle -> đọc bundle nhưng thiếu file trong Delta
    NSString *verdict;
    if (m == 0) {
        verdict = isEnglishMode ? @"❌ HOOKS DEAD - fishhook failed" : @"❌ HOOK CHẾT - fishhook thất bại";
    } else if (totalCalls == 0) {
        verdict = isEnglishMode ? @"❌ Hooked but 0 file calls (fishhook not biting?)" : @"❌ Đã cài nhưng 0 lời gọi (fishhook chưa ăn?)";
    } else if (bundleCalls == 0) {
        verdict = isEnglishMode ? @"⚠️ Game reads OUTSIDE .app (not the bundle)" : @"⚠️ Game đọc NGOÀI .app (không phải trong bundle)";
    } else if (hits > 0) {
        verdict = isEnglishMode ? @"✅ Game IS reading through Delta" : @"✅ Game ĐANG đọc qua Delta";
    } else {
        verdict = isEnglishMode ? @"⚠️ Bundle calls seen, but ALL missing in Delta -> now failing (ENOENT), not reading original" : @"⚠️ Có gọi bundle nhưng Delta thiếu HẾT -> giờ LỖI (ENOENT), không đọc bản gốc";
    }

    // Thống kê chặn DNS/mạng
    unsigned long long dnsBlocked = DNSBlock_count();
    const char *dnsHostC = DNSBlock_lastHost();
    NSString *dnsHost = (dnsHostC && dnsHostC[0]) ? [NSString stringWithUTF8String:dnsHostC] : @"—";

    // Chữ ký: Delta.zip / folder Delta có được ký vào app không (đọc CodeResources)
    NSString *signInfo = DeltaVFS_signatureSummary();

    NSString *text = [NSString stringWithFormat:
        @"%@\n\n"
         "%@\n"
         "%@\n"
         "Delta: %@\n\n"
         "Tổng lời gọi file: %llu\n"
         "Trong bundle: %llu\n"
         "Hits  (đọc từ Delta): %llu\n"
         "Miss  (Delta thiếu -> LỖI, không đọc gốc): %llu\n"
         "Tỉ lệ qua Delta: %.1f%%\n\n"
         "Path bất kỳ gần nhất:\n%@\n"
         "File Delta gần nhất:\n%@\n\n"
         "── ABHOTUPDATES (icon/texture CDN, OVERLAY) ──\n"
         "Hits (từ Delta): %llu\n"
         "Miss (đọc bản gốc - bình thường, Delta chỉ chứa vài icon custom): %llu\n\n"
         "── CHỮ KÝ DELTA ──\n"
         "%@\n\n"
         "── CHẶN DNS ──\n"
         "Đã chặn: %llu request\n"
         "Host chặn gần nhất:\n%@",
        verdict, hookLine, extractLine, dir,
        totalCalls, bundleCalls, hits, misses, pct, anyPath, last,
        DeltaVFS_abHotUpdatesHits(), DeltaVFS_abHotUpdatesMisses(),
        signInfo,
        dnsBlocked, dnsHost];

    _deltaLogView.text = text;
}

#pragma mark - Spy tab page (dõi dylib B - xem DylibSpy.h)

- (UIView *)buildSpyPageInFrame:(CGRect)frame {
    UIView *page = [[UIView alloc] initWithFrame:frame];
    CGFloat w = frame.size.width;

    // Bắt Đầu Giám Sát TÁCH RIÊNG khỏi Giám Sát Bộ Nhớ, mặc định TẮT cả hai -
    // bật cái này mới thật sự vá GOT của Monite.dylib (dlopen/dlsym/mmap/
    // mprotect/vm_protect/vm_write), xem ghi chú an toàn ở g_spyMonitoringRequested
    // trong DylibSpy.h. Trước khi bật, tab chỉ ĐỌC dyld image list (vô hại).
    _spyStartSwitch = [self addToggleCardWithLocKey:@"spy_start" symbol:@"play.circle.fill" frame:CGRectMake(4, 0, w - 8, 34) action:@selector(toggleSpyStart:) toView:page];
    _spyMemWatchSwitch = [self addToggleCardWithLocKey:@"spy_mem_watch" symbol:@"waveform.path.ecg.rectangle" frame:CGRectMake(4, 38, w - 8, 34) action:@selector(toggleSpyMemWatch:) toView:page];

    UILabel *callHeader = [[UILabel alloc] initWithFrame:CGRectMake(6, 80, w - 12, 12)];
    callHeader.font = [UIFont systemFontOfSize:10 weight:UIFontWeightHeavy];
    callHeader.textColor = COLOR_CYAN;
    callHeader.text = @"CALL TRACE";
    [page addSubview:callHeader];

    // Chia phần còn lại làm 2: CALL TRACE (dlopen/dlsym/mmap/mprotect/vm_protect/
    // vm_write mà dylib B gọi) ở trên, MEM DIFF (vùng __TEXT bị ghi đè) ở dưới -
    // cùng cách chia INFO tab đang dùng cho DELTA VFS / UDP LOG.
    CGFloat logsTop = 94;
    CGFloat logsAvail = frame.size.height - logsTop - 4;
    CGFloat callLogH = logsAvail * 0.6f;
    CGFloat memHeaderH = 14;
    CGFloat memLogH = logsAvail - callLogH - memHeaderH - 4;

    _spyCallLogView = [[UITextView alloc] initWithFrame:CGRectMake(4, logsTop, w - 8, callLogH)];
    _spyCallLogView.backgroundColor = COLOR_CARD_BG;
    _spyCallLogView.layer.cornerRadius = 10.0f;
    _spyCallLogView.layer.borderWidth = 1.0f;
    _spyCallLogView.layer.borderColor = COLOR_CARD_BORDER.CGColor;
    _spyCallLogView.editable = NO;
    _spyCallLogView.selectable = NO;
    _spyCallLogView.scrollEnabled = YES;
    _spyCallLogView.textColor = COLOR_TEXT;
    _spyCallLogView.font = [UIFont fontWithName:@"Menlo" size:9.5f] ?: [UIFont systemFontOfSize:9.5f];
    _spyCallLogView.textContainerInset = UIEdgeInsetsMake(8, 8, 8, 8);
    [page addSubview:_spyCallLogView];

    UILabel *memHeader = [[UILabel alloc] initWithFrame:CGRectMake(6, logsTop + callLogH + 4, w - 12, memHeaderH)];
    memHeader.font = [UIFont systemFontOfSize:10 weight:UIFontWeightHeavy];
    memHeader.textColor = COLOR_PURPLE;
    memHeader.text = @"MEM DIFF";
    [page addSubview:memHeader];

    _spyMemLogView = [[UITextView alloc] initWithFrame:CGRectMake(4, logsTop + callLogH + memHeaderH + 4, w - 8, memLogH)];
    _spyMemLogView.backgroundColor = COLOR_CARD_BG;
    _spyMemLogView.layer.cornerRadius = 10.0f;
    _spyMemLogView.layer.borderWidth = 1.0f;
    _spyMemLogView.layer.borderColor = COLOR_CARD_BORDER.CGColor;
    _spyMemLogView.editable = NO;
    _spyMemLogView.selectable = NO;
    _spyMemLogView.scrollEnabled = YES;
    _spyMemLogView.textColor = COLOR_TEXT;
    _spyMemLogView.font = [UIFont fontWithName:@"Menlo" size:9.5f] ?: [UIFont systemFontOfSize:9.5f];
    _spyMemLogView.textContainerInset = UIEdgeInsetsMake(8, 8, 8, 8);
    [page addSubview:_spyMemLogView];

    return page;
}

// Chỉ XIN BẬT (không tắt lại được - đã vá GOT rồi thì không gỡ ra nữa, gỡ nửa
// chừng còn rủi ro hơn để nguyên). Nhỡ tay bật rồi tắt lại thì switch tự nhảy
// ngay về ON, phản ánh đúng trạng thái thật (đã xin bật thì không rút lại được).
- (void)toggleSpyStart:(UISwitch *)sender {
    if (sender.on) {
        DylibSpy_startMonitoring();
    } else {
        sender.on = DylibSpy_monitoringRequested();
    }
}

- (void)toggleSpyMemWatch:(UISwitch *)sender {
    DylibSpy_setMemWatchEnabled(sender.on);
}

// Cập nhật 2 khung log tab SPY - gọi định kỳ từ updateMenu (khi menu đang mở
// và tab SPY đang hiển thị). Việc quét MEM DIFF thật sự (đọc/checksum bộ nhớ)
// chạy trên hàng đợi nền riêng (xem updateMenu) - hàm này chỉ đọc lại kết quả
// đã có sẵn trong ring buffer, rẻ, an toàn gọi trên main thread.
- (void)refreshSpyLog {
    if (!_spyCallLogView || !_spyMemLogView) return;

    NSString *target = DylibSpy_targetInfo();
    NSString *symbols = DylibSpy_symbolSummary();
    NSString *callSummary = DylibSpy_callTraceSummary();
    NSString *callLog = DylibSpy_callTraceLog();
    // Ring buffer trong RAM mất sạch nếu app crash - mỗi dòng log CŨNG được
    // ghi thẳng (write() syscall, không buffer) vào file này trong App Bundle,
    // sống sót qua crash. Hiện đường dẫn ngay trên UI để biết chỗ lấy bằng
    // Filza/SSH sau khi game văng, không cần đoán.
    NSString *logFile = DylibSpy_logFilePath();

    _spyCallLogView.text = [NSString stringWithFormat:
        @"── TARGET ──\n%@\n\n"
         "── HOOK STATUS ──\n%@\n\n"
         "── FILE LOG (sống sót qua crash) ──\n%@\n\n"
         "── IMPORT/EXPORT ──\n%@\n\n"
         "── LOG ──\n%@",
        target, callSummary, logFile, symbols, callLog];

    _spyMemLogView.text = DylibSpy_memWatchLog();
}

- (BOOL)textFieldShouldReturn:(UITextField *)textField {
    [textField resignFirstResponder];
    return YES;
}

#pragma mark - Toast

- (void)showToast:(NSString *)message {
    if (_toastLabel) {
        [_toastLabel removeFromSuperview];
        _toastLabel = nil;
    }

    UIFont *font = [UIFont systemFontOfSize:13 weight:UIFontWeightMedium];
    CGSize textSize = [message sizeWithAttributes:@{NSFontAttributeName: font}];
    CGFloat paddingH = 16;
    CGFloat paddingV = 10;
    CGFloat toastW = textSize.width + paddingH * 2;
    CGFloat toastH = textSize.height + paddingV * 2;
    CGFloat x = (kWidth - toastW) * 0.5f;
    CGFloat y = kHeight * 0.15f;

    _toastLabel = [[UILabel alloc] initWithFrame:CGRectMake(x, y, toastW, toastH)];
    _toastLabel.text = message;
    _toastLabel.font = font;
    _toastLabel.textColor = COLOR_TEXT;
    _toastLabel.textAlignment = NSTextAlignmentCenter;
    _toastLabel.backgroundColor = COLOR_BG;
    _toastLabel.layer.cornerRadius = 10.0f;
    _toastLabel.clipsToBounds = YES;
    _toastLabel.layer.borderWidth = 1.0f;
    _toastLabel.layer.borderColor = [COLOR_PURPLE colorWithAlphaComponent:0.6].CGColor;
    _toastLabel.alpha = 0.0f;
    _toastLabel.userInteractionEnabled = NO;

    [mainWindow addSubview:_toastLabel];

    __weak __typeof__(self) weakSelf = self;
    [UIView animateWithDuration:0.25 animations:^{
        weakSelf.toastLabel.alpha = 1.0f;
    } completion:^(BOOL finished) {
        [UIView animateWithDuration:0.25 delay:1.5 options:0 animations:^{
            weakSelf.toastLabel.alpha = 0.0f;
        } completion:^(BOOL finished2) {
            [weakSelf.toastLabel removeFromSuperview];
            weakSelf.toastLabel = nil;
        }];
    }];
}

#pragma mark - Shared helpers

- (void)handlePan:(UIPanGestureRecognizer *)pan {
    CGPoint translation = [pan translationInView:mainWindow];
    if (pan.state == UIGestureRecognizerStateBegan) {
        _lastPoint = _menuView.center;
    }
    _menuView.center = CGPointMake(_lastPoint.x + translation.x, _lastPoint.y + translation.y);
}

// Let sliders/switches/buttons/segmented controls/text fields handle their own touches
// - only start dragging the whole menu when the touch begins somewhere else (a card's
// background, the sidebar, empty space), not on an interactive control.
- (BOOL)gestureRecognizer:(UIGestureRecognizer *)gestureRecognizer shouldReceiveTouch:(UITouch *)touch {
    UIView *view = touch.view;
    while (view && view != _menuView) {
        if ([view isKindOfClass:[UISlider class]] ||
            [view isKindOfClass:[UISwitch class]] ||
            [view isKindOfClass:[UIButton class]] ||
            [view isKindOfClass:[UISegmentedControl class]] ||
            [view isKindOfClass:[UITextField class]]) {
            return NO;
        }
        view = view.superview;
    }
    return YES;
}

- (UIButton *)createButtonWithTitle:(NSString *)title frame:(CGRect)frame {
    UIButton *button = [UIButton buttonWithType:UIButtonTypeCustom];
    button.frame = frame;
    button.backgroundColor = COLOR_CARD_BG;
    button.layer.cornerRadius = 8.0f;
    button.layer.borderWidth = 1.0f;
    button.layer.borderColor = COLOR_CARD_BORDER.CGColor;
    [button setTitle:title forState:UIControlStateNormal];
    [button setTitleColor:COLOR_TEXT_DIM forState:UIControlStateNormal];
    button.titleLabel.font = [UIFont systemFontOfSize:12 weight:UIFontWeightBold];
    button.titleLabel.adjustsFontSizeToFitWidth = YES;
    // Tactile press feedback (scale + fade) instead of the dead flat tap most system
    // buttons have here by default - small thing, reads as a considered/premium control.
    [button addTarget:self action:@selector(cardButtonTouchDown:) forControlEvents:UIControlEventTouchDown];
    [button addTarget:self action:@selector(cardButtonTouchUp:) forControlEvents:UIControlEventTouchUpInside | UIControlEventTouchUpOutside | UIControlEventTouchCancel];
    return button;
}

- (void)cardButtonTouchDown:(UIButton *)sender {
    [UIView animateWithDuration:0.08 animations:^{
        sender.transform = CGAffineTransformMakeScale(0.96, 0.96);
        sender.alpha = 0.85;
    }];
}

- (void)cardButtonTouchUp:(UIButton *)sender {
    [UIView animateWithDuration:0.12 animations:^{
        sender.transform = CGAffineTransformIdentity;
        sender.alpha = 1.0;
    }];
}

// Shared dark-glass styling for the few native controls (segmented controls, text
// fields) that otherwise render with their light-theme system defaults and stick out
// against the rest of the purple/cyan UI.
- (void)styleSegmentedControl:(UISegmentedControl *)control {
    control.backgroundColor = COLOR_CARD_BG;
    control.selectedSegmentTintColor = COLOR_CYAN;
    control.layer.cornerRadius = 8.0f;
    control.layer.borderWidth = 1.0f;
    control.layer.borderColor = COLOR_CARD_BORDER.CGColor;
    UIFont *segFont = [UIFont systemFontOfSize:11 weight:UIFontWeightBold];
    [control setTitleTextAttributes:@{NSForegroundColorAttributeName: COLOR_TEXT_DIM, NSFontAttributeName: segFont} forState:UIControlStateNormal];
    [control setTitleTextAttributes:@{NSForegroundColorAttributeName: [UIColor blackColor], NSFontAttributeName: segFont} forState:UIControlStateSelected];
}

- (void)styleDarkTextField:(UITextField *)field {
    field.borderStyle = UITextBorderStyleNone;
    field.backgroundColor = COLOR_CARD_BG;
    field.textColor = COLOR_TEXT;
    field.font = [UIFont systemFontOfSize:13 weight:UIFontWeightMedium];
    field.layer.cornerRadius = 8.0f;
    field.layer.borderWidth = 1.0f;
    field.layer.borderColor = COLOR_CARD_BORDER.CGColor;
    field.leftView = [[UIView alloc] initWithFrame:CGRectMake(0, 0, 10, 1)];
    field.leftViewMode = UITextFieldViewModeAlways;
}

- (UIButton *)createButtonWithLocKey:(NSString *)key frame:(CGRect)frame {
    UIButton *button = [self createButtonWithTitle:LOC(key) frame:frame];
    __weak UIButton *weakBtn = button;
    [self addLocalizedRefresher:^{ [weakBtn setTitle:LOC(key) forState:UIControlStateNormal]; }];
    return button;
}

#pragma mark - Localization

- (void)addLocalizedRefresher:(dispatch_block_t)block {
    if (!_localizationRefreshers) _localizationRefreshers = [NSMutableArray array];
    [_localizationRefreshers addObject:[block copy]];
    block();
}

- (void)refreshLocalization {
    for (dispatch_block_t block in _localizationRefreshers) block();
}

- (void)languageChanged:(UISegmentedControl *)sender {
    isEnglishMode = (sender.selectedSegmentIndex == 1);
    [self refreshLocalization];
}

#pragma mark - Section headers

// Small uppercase, letter-spaced dividers to group related cards (Display / Auto Aim /
// Boost) - the flat, ungrouped list of switches was a big part of what read as "sơ sài".
- (void)addSectionHeaderWithLocKey:(NSString *)key frame:(CGRect)frame toView:(UIView *)parent {
    UILabel *header = [[UILabel alloc] initWithFrame:frame];
    header.font = [UIFont systemFontOfSize:10 weight:UIFontWeightHeavy];
    header.textColor = COLOR_CYAN;
    [parent addSubview:header];
    __weak UILabel *weakHeader = header;
    [self addLocalizedRefresher:^{
        NSString *text = [LOC(key) uppercaseString];
        NSMutableAttributedString *attr = [[NSMutableAttributedString alloc] initWithString:text];
        [attr addAttribute:NSKernAttributeName value:@(1.3) range:NSMakeRange(0, attr.length)];
        weakHeader.attributedText = attr;
    }];
}

#pragma mark - Toggle cards (icon + label + native switch)

// Tag constants so applyCardVisualState: can find the accent bar / icon inside an
// arbitrary card without needing dedicated properties for every single toggle.
static const NSInteger kCardAccentTag = 9001;
static const NSInteger kCardIconTag = 9002;

- (UISwitch *)addToggleCardWithLocKey:(NSString *)key symbol:(NSString *)symbolName frame:(CGRect)frame action:(SEL)action toView:(UIView *)parent {
    UIView *card = [[UIView alloc] initWithFrame:frame];
    card.backgroundColor = COLOR_CARD_BG;
    card.layer.cornerRadius = 10.0f;
    card.layer.borderWidth = 1.0f;
    card.layer.borderColor = COLOR_CARD_BORDER.CGColor;
    [parent addSubview:card];

    UIView *accent = [[UIView alloc] initWithFrame:CGRectMake(0, 6, 3, frame.size.height - 12)];
    accent.layer.cornerRadius = 1.5f;
    accent.backgroundColor = COLOR_ACCENT_IDLE;
    accent.tag = kCardAccentTag;
    accent.userInteractionEnabled = NO;
    [card addSubview:accent];

    CGFloat iconSize = 15;
    UIImageView *icon = [[UIImageView alloc] initWithFrame:CGRectMake(12, (frame.size.height - iconSize) / 2.0f, iconSize, iconSize)];
    icon.image = [[UIImage systemImageNamed:symbolName] imageByApplyingSymbolConfiguration:[UIImageSymbolConfiguration configurationWithPointSize:13 weight:UIImageSymbolWeightSemibold]];
    icon.contentMode = UIViewContentModeScaleAspectFit;
    icon.tintColor = COLOR_TEXT_DIM;
    icon.tag = kCardIconTag;
    icon.userInteractionEnabled = NO;
    [card addSubview:icon];

    CGFloat labelX = 12 + iconSize + 8;
    UILabel *label = [[UILabel alloc] initWithFrame:CGRectMake(labelX, 0, frame.size.width - labelX - 62, frame.size.height)];
    label.font = [UIFont systemFontOfSize:12 weight:UIFontWeightSemibold];
    label.textColor = COLOR_TEXT;
    label.adjustsFontSizeToFitWidth = YES;
    label.minimumScaleFactor = 0.7f;
    [card addSubview:label];
    __weak UILabel *weakLabel = label;
    [self addLocalizedRefresher:^{ weakLabel.text = LOC(key); }];

    UISwitch *sw = [[UISwitch alloc] init];
    sw.onTintColor = COLOR_CYAN;
    // Stock UISwitch off-state (light grey track, plain white knob) reads as a default
    // system control against this dark glass UI - recolor both states to match.
    sw.tintColor = [UIColor colorWithWhite:1.0 alpha:0.16];
    sw.thumbTintColor = [UIColor colorWithWhite:0.94 alpha:1.0];
    CGSize swSize = sw.frame.size;
    sw.frame = CGRectMake(frame.size.width - swSize.width - 10, (frame.size.height - swSize.height) / 2.0f, swSize.width, swSize.height);
    [sw addTarget:self action:action forControlEvents:UIControlEventValueChanged];
    [sw addTarget:self action:@selector(refreshCardVisualState:) forControlEvents:UIControlEventValueChanged];
    [card addSubview:sw];

    [self applyCardVisualState:sw];
    return sw;
}

- (void)refreshCardVisualState:(UISwitch *)sender {
    [self applyCardVisualState:sender];
}

// Lights up the whole card (border, left accent bar, icon tint) to cyan while its
// switch is ON, instead of only the tiny native switch changing color - makes active
// features scannable at a glance instead of every card looking identical.
- (void)applyCardVisualState:(UISwitch *)sw {
    UIView *card = sw.superview;
    if (!card) return;
    UIView *accent = [card viewWithTag:kCardAccentTag];
    UIImageView *icon = (UIImageView *)[card viewWithTag:kCardIconTag];
    BOOL on = sw.isOn;
    [UIView animateWithDuration:0.2 animations:^{
        card.layer.borderColor = (on ? [COLOR_CYAN colorWithAlphaComponent:0.5] : COLOR_CARD_BORDER).CGColor;
        card.backgroundColor = on ? [COLOR_CYAN colorWithAlphaComponent:0.10] : COLOR_CARD_BG;
        accent.backgroundColor = on ? COLOR_CYAN : COLOR_ACCENT_IDLE;
        icon.tintColor = on ? COLOR_CYAN : COLOR_TEXT_DIM;
    }];
}

- (void)updateMenu {
    _menuView.hidden = !MenDeal;

    get_players();

    if (!MenDeal) return;

    NSArray<UISwitch *> *subSwitches = @[_boxSwitch, _linesSwitch, _nameSwitch, _healthSwitch, _distanceSwitch, _skeletonSwitch, _countSwitch, _showFovCircleSwitch];
    for (UISwitch *sw in subSwitches) {
        sw.enabled = Vars.Enable;
        sw.superview.alpha = Vars.Enable ? 1.0f : 0.4f;
    }

    // Làm mới bảng log DELTA VFS ~2 lần/giây, chỉ khi tab INFO đang hiển thị (tránh phí CPU mỗi frame).
    static NSInteger logTick = 0;
    if (_tabPages.count > 2 && !_tabPages[2].hidden && (++logTick % 30 == 0)) {
        [self refreshDeltaLog];
    }

    // Dò/cài DylibSpy mỗi frame (rẻ - vài atomic bool sau khi đã cài xong, xem
    // DylibSpy_tick). 2 đồng hồ tick TÁCH RIÊNG bên dưới - cố tình không lồng
    // chung 1 biến ++đếm vào điều kiện && (tab hiển thị): nếu lồng chung, việc
    // ++ chỉ chạy khi vế && tab-hiển thị đúng (short-circuit), khiến vòng quét
    // MEM DIFF (chỉ nên phụ thuộc công tắc, không phụ thuộc tab có đang mở hay
    // không) bị đứng hẳn mỗi khi người dùng rời tab SPY dù công tắc vẫn bật.
    DylibSpy_tick();
    static NSInteger spyUiTick = 0;
    if (_tabPages.count > 3 && !_tabPages[3].hidden && (++spyUiTick % 30 == 0)) {
        [self refreshSpyLog];
    }
    static NSInteger spyScanTick = 0;
    // Vòng quét MEM DIFF thật sự thì NẶNG (đọc/checksum tới 32MB bộ nhớ) nên
    // chỉ chạy ~1 lần/3s, trên hàng đợi nền, và chỉ khi công tắc Giám Sát Bộ
    // Nhớ đang bật - tránh phí CPU khi tính năng đang tắt.
    if (DylibSpy_memWatchEnabled() && (++spyScanTick % 90 == 0)) {
        dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_LOW, 0), ^{
            dylibSpyScanForChanges();
        });
    }
}

#pragma mark - ESP Toggle Actions
- (void)toggleEnable:(UISwitch *)sender { Vars.Enable = sender.on; }
- (void)toggleBox:(UISwitch *)sender { Vars.Box = sender.on; }
- (void)toggleLines:(UISwitch *)sender { Vars.lines = sender.on; }
- (void)toggleName:(UISwitch *)sender { Vars.Name = sender.on; }
- (void)toggleHealth:(UISwitch *)sender { Vars.Health = sender.on; }
- (void)toggleDistance:(UISwitch *)sender { Vars.Distance = sender.on; }
- (void)toggleSkeleton:(UISwitch *)sender { Vars.skeleton = sender.on; }
- (void)toggleCount:(UISwitch *)sender { Vars.counts = sender.on; }

- (void)closeMenu { MenDeal = false; }

#pragma mark - Gesture Handling
- (void)initTapGes {
    UITapGestureRecognizer *tap1 = [[UITapGestureRecognizer alloc] initWithTarget:self action:@selector(openMenu)];
    tap1.numberOfTapsRequired = 2;
    tap1.numberOfTouchesRequired = 3;
    [mainWindow addGestureRecognizer:tap1];

    UITapGestureRecognizer *tap2 = [[UITapGestureRecognizer alloc] initWithTarget:self action:@selector(closeMenu)];
    tap2.numberOfTapsRequired = 2;
    tap2.numberOfTouchesRequired = 2;
    [mainWindow addGestureRecognizer:tap2];
}

- (void)openMenu {
    if (!_menuView) [self setupMenu];
    MenDeal = true;
}

- (void)touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    if (!MenDeal) return;
    UITouch *touch = [touches anyObject];
    if (!CGRectContainsPoint(_menuView.frame, [touch locationInView:mainWindow])) [self closeMenu];
}

@end

// OB54 OFFSETS - UnityFramework (dumped from dump.cs via Il2CppDumper)
// Curent_Match      -> COW.GameFacade.CurrentMatch()               (static, 0 args)
// GetLocalPlayer    -> EMKJHAJNPDH(=MatchGame-like).MBEDKMKBFIE()  (instance, 0 visible args -> 1 native arg: match)
// get_position      -> UnityEngine.Transform.get_position()
// Component_GetTransform -> UnityEngine.Component.get_transform()
// get_camera        -> UnityEngine.Camera.get_main()
// WorldToScreenPoint-> UnityEngine.Camera.WorldToScreenPoint(Vector3) (single-arg overload)
// GetForward        -> UnityEngine.Transform.get_forward()
// get_isLocalTeam   -> COW.GameFacade.IsLocalTeammate(Player)       (static, 1 arg)
// get_IsDieing      -> COW.GamePlay.Player.get_IsDieing()
// get_MaxHP         -> COW.GamePlay.Player.get_MaxHP()
// GetHp             -> COW.GamePlay.Player.get_CurHP()
// name              -> COW.GamePlay.Player.get_NickName()
// bone getters      -> COW.GamePlay.Player.Get*TF()/get_*TF()
void game_sdk_t::init()
{
    this->Curent_Match = (void *(*)())getRealOffset(0x55C4DA4);
    this->GetLocalPlayer = (void *(*)(void *))getRealOffset(0x560E3DC);
    this->get_position = (Vector3(*)(void *))getRealOffset(0x91CA56C);
    this->Component_GetTransform = (void *(*)(void *))getRealOffset(0x91B82E4);
    this->get_camera = (void *(*)())getRealOffset(0x915E9E4);
    // Named WorldToScreenPoint but Camera$$WorldToScreen::Regular (ESP.h) actually
    // expects normalized viewport coords (multiplies by screenWidth/Height itself),
    // so this must bind to Camera.WorldToViewportPoint, not the literal ScreenPoint method.
    this->WorldToScreenPoint = (Vector3(*)(void *, Vector3))getRealOffset(0x915E364);
    this->GetForward = (Vector3(*)(void *))getRealOffset(0x91CAF64);
    // Transform.set_forward(Vector3) - same Transform class as get_position/GetForward above (verified via dump.cs).
    // Setting this internally does rotation = Quaternion.LookRotation(value), so no manual quaternion math is needed for aim.
    this->set_forward = (void (*)(void *, Vector3))getRealOffset(0x91CB024);
    // COW.GamePlay.Player.SetAimRotation(Quaternion, bool = true) - non-virtual instance
    // method on Player itself (found next to other Player-specific state methods like
    // get_InFallingState/get_InSwapWeaponCD). The reference in AimHead.md calls the
    // equivalent of this "set_aim(LocalPlayer, Quaternion)" to actually move the aim -
    // writing the camera's Transform directly (set_forward above) never worked for Aim
    // Head, which lines up with the game reading aim from the player's own state instead.
    this->set_aim = (void (*)(void *, Quaternion, bool))getRealOffset(0x53C4534);
    // Player.SetEAimAssitMode(EAimAssist) - non-virtual, used by Aim Magnet to force
    // the game's own built-in aim-assist always on (EAimAssist.AllOn = 0).
    this->set_aim_assist_mode = (void (*)(void *, int))getRealOffset(0x53C1750);
    this->get_isLocalTeam = (bool (*)(void *))getRealOffset(0x55C5AC0);
    this->get_IsDieing = (bool (*)(void *))getRealOffset(0x53AA18C);
    // Player.IsFiring()/get_IsSighting() - both non-virtual (no Slot: in dump.cs), safe
    // to call directly like set_aim. Used to gate Aim Head's "only while firing/scoped" mode.
    this->get_IsFiring = (bool (*)(void *))getRealOffset(0x53ACC9C);
    this->get_IsSighting = (bool (*)(void *))getRealOffset(0x53B769C);
    this->get_MaxHP = (int (*)(void *))getRealOffset(0x5435A3C);
    this->GetHp = (int (*)(void *))getRealOffset(0x543592C);
    this->name = (monoString * (*)(void *player))getRealOffset(0x53BE8E0);

    // GetHeadTF/GetHipTF are virtual (vtable slots 231/232). Player's own override
    // (0x60DDEF0/0x60DDF6C, found next to Player-specific methods like IsLocalTeammate)
    // looked like the theoretically correct fix for HeadY reading waist-height, but calling
    // it crashed the game on-device (reverted). Back to the base class's implementation,
    // which is at least stable - FindAimHeadTarget below no longer trusts its Y for aiming
    // anyway, using the root+1.6m estimate instead.
    this->_GetHeadPositions = (void *(*)(void *))getRealOffset(0x54547E0);
    this->_newHipMods = (void *(*)(void *))getRealOffset(0x5454990);
    this->_GetLeftAnkleTF = (void *(*)(void *))getRealOffset(0x5454DE0);
    this->_GetRightAnkleTF = (void *(*)(void *))getRealOffset(0x5454EEC);
    this->_GetLeftToeTF = (void *(*)(void *))getRealOffset(0x5454FF8);
    this->_GetRightToeTF = (void *(*)(void *))getRealOffset(0x5455104);
    this->_getLeftHandTF = (void *(*)(void *))getRealOffset(0x53C3608);
    this->_getRightHandTF = (void *(*)(void *))getRealOffset(0x53C370C);
    this->_getLeftForeArmTF = (void *(*)(void *))getRealOffset(0x53C3810);
    this->_getRightForeArmTF = (void *(*)(void *))getRealOffset(0x53C3914);
}
