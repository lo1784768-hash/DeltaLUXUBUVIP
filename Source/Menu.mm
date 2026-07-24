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
#import "Includes/NetLog.h"
#import "Includes/DNSBlock.h"
#import "Includes/AssetRedirect.h"
#import "Includes/Il2CppResolve.h"
#import "Includes/AntiReportSpoof.h"
#import "Includes/PacketCapture.h"
#import "Includes/CheckHackerPatch.h"
#import "Includes/MatchClientInfoPatch.h"
#import "Includes/FFAntiFlagsPatch.h"
#import "Includes/FFAntiObserve.h"
#import "Includes/DylibSpy.h"

#define kWidth  [UIScreen mainScreen].bounds.size.width
#define kHeight [UIScreen mainScreen].bounds.size.height

// Monite-identity retheme (xem MoniteAnalysis/README.md mục 3f-3l + demo HTML dựng lại UI của họ):
// nền navy gần đen + 1 accent cam duy nhất, thay cho bộ purple/cyan "Delta" cũ. COLOR_PURPLE/
// COLOR_CYAN CỐ Ý giữ nguyên TÊN macro (đỡ phải sửa hàng chục call site khắp file - border
// gradient, section header, switch onTint...) nhưng trỏ về CÙNG 1 giá trị cam - Monite thật chỉ
// dùng đúng 1 accent, không phải 2 màu tách biệt như thiết kế cũ.
#define COLOR_BG [UIColor colorWithRed:0.09 green:0.11 blue:0.15 alpha:0.97]
#define COLOR_PURPLE [UIColor colorWithRed:0.949 green:0.388 blue:0.165 alpha:1.0]
#define COLOR_CYAN   [UIColor colorWithRed:0.949 green:0.388 blue:0.165 alpha:1.0]
#define COLOR_TEXT [UIColor whiteColor]
#define COLOR_TEXT_DIM [UIColor colorWithWhite:0.62 alpha:1.0]
#define COLOR_BTN_OFF [UIColor colorWithWhite:1.0 alpha:0.05]

// Sidebar riêng đậm hơn hẳn panel nội dung - đúng 2 tông đậm/nhạt thấy trong ảnh chụp màn hình
// thật (mục 3l), không phải cùng 1 màu duy nhất như bản Delta cũ.
#define COLOR_SIDEBAR_BG [UIColor colorWithRed:0.055 green:0.07 blue:0.10 alpha:1.0]

// Card surface treatment: a tinted-dark glass panel + hairline border, instead of a
// flat white-alpha overlay - reads as an intentional dark-glass surface rather than a
// generic system control. Cards additionally light up (border/accent/icon go orange) when
// their switch is ON, via applyCardVisualState: - see addToggleCardWithLocKey:.
#define COLOR_CARD_BG [UIColor colorWithRed:0.125 green:0.15 blue:0.20 alpha:0.85]
#define COLOR_CARD_BORDER [UIColor colorWithWhite:1.0 alpha:0.07]
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
            @"no_recoil": @[@"Không giật", @"No Recoil"],
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

            // ===== Khung 5 tab kiểu Monite (xem MoniteAnalysis mục 3l) - CHỈ layout/chữ, chưa
            // gắn logic thật, xem comment ở buildCaiDatPageInFrame/buildKhacPageInFrame =====
            @"stream_mode": @[@"Chế độ Stream", @"Stream Mode"],
            @"accent_color": @[@"Màu nhấn", @"Accent Color"],
            @"menu_opacity": @[@"Độ trong suốt menu", @"Menu Opacity"],
            @"language_label": @[@"Ngôn ngữ", @"Language"],
            @"interface_style": @[@"Kiểu giao diện", @"Interface Style"],
            @"scroll_drag_button": @[@"Nút kéo cuộn", @"Scroll Drag Button"],
            @"save_settings": @[@"Lưu cài đặt", @"Save Settings"],
            @"restore_settings": @[@"Khôi phục cài đặt", @"Restore Settings"],
            @"unsafe_warning": @[@"Một số tùy chọn trong mục này có thể không hoàn toàn an toàn. Dùng với thận trọng và tự chịu trách nhiệm.",
                                  @"Some options in this section may not be completely safe. Use with caution and at your own risk."],
            @"khac_doi_sung_nhanh": @[@"Đổi súng nhanh", @"Quick Weapon Switch"],
            @"khac_nap_dan_nhanh": @[@"Nạp đạn nhanh", @"Quick Reload"],
            @"khac_mau_an_do": @[@"Bật máu ở Ấn Độ", @"Enable Blood in India"],
            @"khac_keo_gian": @[@"Kéo giãn màn hình", @"Stretch Screen"],
            @"khac_120fps": @[@"Ép 120 FPS", @"Force 120 FPS"],
            @"khac_dat_lai_khach": @[@"Đặt lại khách", @"Reset Guest"],
            @"empty_tab_note": @[@"Chưa gắn tính năng - sẽ thêm ở bước sau", @"No features wired yet - coming in a follow-up step"],
        };
    });
    return d;
}

static NSString *LOC(NSString *key) {
    NSArray<NSString *> *pair = LocStrings()[key];
    if (!pair) return key;
    return isEnglishMode ? pair[1] : pair[0];
}

// ============================================================================
//  Checkbox vuông bo góc - đúng kiểu công tắc Monite dùng cho gần như mọi tính năng (khác 2 mục
//  "Chế độ Stream"/"Màu nhấn" ở tab Cài Đặt, vẫn dùng UISwitch tròn thật, xem
//  addPillToggleCardWithLocKey: bên dưới). Đặt tên property/method giống hệt UISwitch
//  (on/isOn/setOn:animated:) CỐ Ý - toàn bộ action handler cũ (toggleBox:/toggleAimHead:/...) chỉ
//  cần đổi kiểu tham số sang UIControl* + đọc/ghi qua [(id)sender isOn]/[(id)sender setOn:...]
//  (bracket message send, KHÔNG dùng cú pháp .on trên id - bản Clang của Theos không tự tìm
//  property theo tên trên id như đã tưởng, xem lỗi build "property 'on' not found on type 'id'"),
//  KHÔNG cần viết lại logic bên trong mỗi action handler.
// ============================================================================
@interface DeltaCheckbox : UIControl
@property (nonatomic, getter=isOn) BOOL on;
- (void)setOn:(BOOL)on animated:(BOOL)animated;
@end

@implementation DeltaCheckbox {
    CALayer *_fillLayer;
    CAShapeLayer *_checkLayer;
}

- (instancetype)initWithFrame:(CGRect)frame {
    self = [super initWithFrame:frame];
    if (self) {
        self.backgroundColor = [UIColor clearColor];

        _fillLayer = [CALayer layer];
        _fillLayer.cornerRadius = 6.0f;
        _fillLayer.borderWidth = 1.5f;
        [self.layer addSublayer:_fillLayer];

        _checkLayer = [CAShapeLayer layer];
        _checkLayer.fillColor = nil;
        _checkLayer.strokeColor = [UIColor colorWithWhite:0.08 alpha:1.0].CGColor;
        _checkLayer.lineWidth = 2.2f;
        _checkLayer.lineCap = kCALineCapRound;
        _checkLayer.lineJoin = kCALineJoinRound;
        [self.layer addSublayer:_checkLayer];

        [self addTarget:self action:@selector(handleTap) forControlEvents:UIControlEventTouchUpInside];
        [self updateAppearanceAnimated:NO];
    }
    return self;
}

- (void)layoutSubviews {
    [super layoutSubviews];
    _fillLayer.frame = self.bounds;

    CGFloat w = self.bounds.size.width, h = self.bounds.size.height;
    UIBezierPath *check = [UIBezierPath bezierPath];
    [check moveToPoint:CGPointMake(w * 0.24f, h * 0.54f)];
    [check addLineToPoint:CGPointMake(w * 0.42f, h * 0.72f)];
    [check addLineToPoint:CGPointMake(w * 0.78f, h * 0.28f)];
    _checkLayer.path = check.CGPath;
}

- (void)setOn:(BOOL)on {
    [self setOn:on animated:NO];
}

- (void)setOn:(BOOL)on animated:(BOOL)animated {
    _on = on;
    [self updateAppearanceAnimated:animated];
}

- (void)updateAppearanceAnimated:(BOOL)animated {
    __weak DeltaCheckbox *weakSelf = self;
    void (^apply)(void) = ^{
        __strong DeltaCheckbox *self2 = weakSelf;
        if (!self2) return;
        self2->_fillLayer.backgroundColor = (self2.isOn ? COLOR_CYAN : [UIColor clearColor]).CGColor;
        self2->_fillLayer.borderColor = (self2.isOn ? COLOR_CYAN : [UIColor colorWithWhite:1.0 alpha:0.3]).CGColor;
        self2->_checkLayer.opacity = self2.isOn ? 1.0f : 0.0f;
    };
    [CATransaction begin];
    if (!animated) [CATransaction setDisableActions:YES];
    apply();
    [CATransaction commit];
}

- (void)handleTap {
    [self setOn:!self.isOn animated:YES];
    [self sendActionsForControlEvents:UIControlEventValueChanged];
}

@end

@interface DeltaMenu : NSObject <UITextFieldDelegate, UIGestureRecognizerDelegate>

// First-run extraction flow - declared here (not just defined later in @implementation) vì các
// hàm C thuần (hooked_setDelegate/hooked_didFinishLaunching, khai báo bên dưới, trước
// @implementation) gọi các hàm này qua [DeltaMenu ...] - không như code TRONG @implementation,
// chúng cần selector khai báo trước nếu không build sẽ lỗi "no known class method for selector".
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

// ESP tab - UIControl* thay UISwitch* (chứa DeltaCheckbox vuông, xem addToggleCardWithLocKey:)
@property (nonatomic, strong) UIControl *enableSwitch;
@property (nonatomic, strong) UIControl *boxSwitch;
@property (nonatomic, strong) UIControl *linesSwitch;
@property (nonatomic, strong) UIControl *nameSwitch;
@property (nonatomic, strong) UIControl *healthSwitch;
@property (nonatomic, strong) UIControl *distanceSwitch;
@property (nonatomic, strong) UIControl *skeletonSwitch;
@property (nonatomic, strong) UIControl *countSwitch;
@property (nonatomic, strong) UIControl *showFovCircleSwitch;
@property (nonatomic, strong) UISlider *fovCircleSlider;
@property (nonatomic, strong) UILabel *fovCircleLabel;

// Mod tab
@property (nonatomic, strong) UIView *modMainView;
@property (nonatomic, strong) UIView *modGocView;
@property (nonatomic, strong) UIView *modModView;
@property (nonatomic, strong) UIControl *aimHeadSwitch;
@property (nonatomic, strong) UIControl *aimNheTamSwitch;
@property (nonatomic, strong) UISegmentedControl *aimModeControl;
@property (nonatomic, strong) UIControl *aimPreferLowHPSwitch;
@property (nonatomic, strong) UIControl *aimMagnetSwitch;
@property (nonatomic, strong) UISlider *aimMagnetStrengthSlider;
@property (nonatomic, strong) UILabel *aimMagnetStrengthLabel;
@property (nonatomic, strong) UIControl *antenaSwitch;
@property (nonatomic, strong) UIControl *speedX2Switch;
@property (nonatomic, strong) UIControl *speedX8Switch;
@property (nonatomic, strong) UIControl *noRecoilSwitch;
@property (nonatomic, strong) UIControl *spinBotSwitch;
@property (nonatomic, strong) UIControl *blockUdpPortsSwitch;
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
@property (nonatomic, strong) UITextView *redirectedFilesView;
@property (nonatomic, strong) UITextView *missedFilesView;

// Spy tab (dõi dylib B - gọi hàm nào, sửa bộ nhớ đâu, xem DylibSpy.h)
@property (nonatomic, strong) UIControl *spyStartSwitch;
@property (nonatomic, strong) UIControl *spyMemWatchSwitch;
@property (nonatomic, strong) UITextView *spyCallLogView;
@property (nonatomic, strong) UITextView *spyMemLogView;

// Localization
@property (nonatomic, strong) NSMutableArray<dispatch_block_t> *localizationRefreshers;

// Toast
@property (nonatomic, strong) UILabel *toastLabel;
@end

// ============================================================================
//  APP-DELEGATE LAUNCH GUARD - thật sự giữ KHÔNG cho code của game (Unity bootstrap) chạy trong
//  lúc giải nén lần đầu, thay vì chỉ che màn hình trong khi nó vẫn chạy ngầm bên dưới (1 cửa sổ
//  che và AppDelegate/Unity bootstrap của game đều chỉ là 2 việc được lên lịch hợp tác trên CÙNG
//  1 main run loop - cái này không hề tạm dừng cái kia). Swizzle -[UIApplication setDelegate:] để
//  bắt đúng app delegate THẬT ngay khoảnh khắc UIApplicationMain gán nó (xảy ra rất lâu trước khi
//  -application:didFinishLaunching... được gọi), rồi swizzle tiếp
//  -application:didFinishLaunchingWithOptions: của CHÍNH delegate đó (nơi Unity thật sự khởi
//  động) để giữ lại. Dùng hàm C thuần (không phải ObjC method) vì đang patch class của
//  Apple/game, giống kiểu hooked_open... của fishhook.
//
//  Lý do khôi phục cơ chế này (đã từng bị xoá, xem git log): quan sát Monite.dylib (đối thủ) trên
//  máy thật cho thấy nó hiện popup "Please Wait", giải nén xong THÌ CRASH, người dùng tự mở lại
//  app lần 2 mới chơi được - tức là không có custom hook nào (kể cả HWBreakHook, xem
//  AssetRedirect.h) cần sống sót qua đúng lúc I/O giải nén dồn dập nhất. HWBreakHook giờ chỉ được
//  thử kích hoạt ở process THỨ HAI (đã giải nén sẵn, marker khớp) - process ĐẦU chỉ lo giải nén +
//  popup rồi abort(), không bao giờ chạm tới game thật.
// ============================================================================
typedef void (*OrigSetDelegateIMP)(id, SEL, id<UIApplicationDelegate>);
static OrigSetDelegateIMP orig_setDelegate = NULL;
static BOOL g_realDelegateLaunchSwizzled = NO;
// Set NGAY từ đầu +showUpdatingPopupThenRelaunch - cho timer an toàn 2s của
// installAppDelegateLaunchGuard biết liệu nhánh swizzle đã thực sự hiện popup hay chưa.
static std::atomic<bool> g_firstRunPopupShown{false};

typedef BOOL (*OrigWillFinishLaunchingIMP)(id, SEL, UIApplication *, NSDictionary *);
static OrigWillFinishLaunchingIMP orig_willFinishLaunching = NULL;

// Chặn thêm application:willFinishLaunchingWithOptions: - phòng thân (defense-in-depth) cho
// trường hợp UnityAppController/CustomUnityAppController khởi tạo engine hoặc bắt đầu load data
// NGAY TỪ ĐÂY thay vì ở didFinishLaunchingWithOptions: bên dưới. Không gọi hàm gốc, y hệt lý do
// ở hooked_didFinishLaunching - game tuyệt đối không được chạy tới bất kỳ đâu trong process đang
// giải nén lần đầu này.
static BOOL hooked_willFinishLaunching(id self, SEL _cmd, UIApplication *application, NSDictionary *launchOptions) {
    DeltaVFS_debugLog("hooked_willFinishLaunching: entered - holding, not forwarding to real implementation");
    return YES;
}

typedef BOOL (*OrigDidFinishLaunchingIMP)(id, SEL, UIApplication *, NSDictionary *);
static OrigDidFinishLaunchingIMP orig_didFinishLaunching = NULL;

static BOOL hooked_didFinishLaunching(id self, SEL _cmd, UIApplication *application, NSDictionary *launchOptions) {
    DeltaVFS_debugLog("hooked_didFinishLaunching: entered - holding game startup until extraction resolves");
    [DeltaMenu showUpdatingPopupThenRelaunch];
    // KHÔNG gọi orig_didFinishLaunching thật (đây mới là chỗ Unity thật sự khởi động) - game vẫn
    // không bao giờ chạy trong lần launch này. NHƯNG PHẢI return NGAY thay vì tự bơm run loop vô
    // hạn: launch screen tĩnh của hệ thống chỉ được gỡ xuống SAU KHI didFinishLaunching thật sự
    // return và UIApplicationMain hoàn tất chuỗi khởi động - giữ hàm này chạy mãi khiến UIKit
    // không bao giờ coi launch là "xong", nên launch screen cứ nằm đè lên popup của mình suốt.
    // Return sớm để UIKit gỡ launch screen bình thường; các NSTimer/dispatch_after đã lên lịch
    // (deltaDebugLogTimer, extraction completion...) vẫn chạy đúng như thường lệ ngay khi main
    // run loop chuẩn của UIApplicationMain bắt đầu quay.
    return YES;
}

static void hooked_setDelegate(id self, SEL _cmd, id<UIApplicationDelegate> delegate) {
    if (orig_setDelegate) orig_setDelegate(self, _cmd, delegate);
    if (!delegate || g_realDelegateLaunchSwizzled) return;
    g_realDelegateLaunchSwizzled = YES;

    Class cls = [(id)delegate class];
    SEL sel = @selector(application:didFinishLaunchingWithOptions:);
    Method m = class_getInstanceMethod(cls, sel);
    if (!m) {
        // Delegate không implement launch method cổ điển (VD chỉ dùng scene lifecycle) - fallback
        // qua kiểu "che màn hình rồi hy vọng" thay vì không bao giờ chạy gì cả.
        DeltaVFS_debugLogf("hooked_setDelegate: %s has no application:didFinishLaunchingWithOptions: - falling back to poll+block", class_getName(cls));
        [DeltaMenu pollUntilAppReadyThenBlockAndUpdate];
        return;
    }
    orig_didFinishLaunching = (OrigDidFinishLaunchingIMP)method_getImplementation(m);
    method_setImplementation(m, (IMP)hooked_didFinishLaunching);
    DeltaVFS_debugLogf("hooked_setDelegate: swizzled %s's didFinishLaunching to hold game startup", class_getName(cls));

    // Phòng thân: nếu delegate CÓ implement willFinishLaunchingWithOptions: (nơi 1 số bản Unity
    // export bắt đầu khởi tạo engine sớm hơn cả didFinishLaunching), chặn luôn cả nó - không để
    // lọt đường nào cho game load data trong lúc đang giải nén lần đầu.
    SEL willSel = @selector(application:willFinishLaunchingWithOptions:);
    Method wm = class_getInstanceMethod(cls, willSel);
    if (wm) {
        orig_willFinishLaunching = (OrigWillFinishLaunchingIMP)method_getImplementation(wm);
        method_setImplementation(wm, (IMP)hooked_willFinishLaunching);
        DeltaVFS_debugLogf("hooked_setDelegate: swizzled %s's willFinishLaunching too (defense-in-depth)", class_getName(cls));
    }
}

// Cài launch guard NGAY TỪ __attribute__((constructor)) thay vì đợi tới +[DeltaMenu load] như
// trước - lý do: dyld chạy TOÀN BỘ __attribute__((constructor))/static-initializer của TẤT CẢ
// ảnh (image) đã nạp TRƯỚC KHI gọi bất kỳ +load nào, trên toàn hệ thống (không riêng gì file
// này) - xem AssetRedirect.h's initDeltaAllTrafficVFS. Nếu chỉ cài guard trong +load như cũ, bất
// kỳ SDK bên thứ 3 nào (Facebook/Firebase/DataDome/...) có +load riêng chạy SỚM HƠN +[DeltaMenu
// load] (thứ tự +load giữa các class phụ thuộc link order, không do mình kiểm soát) vẫn có thể đã
// kịp tạo/ghi cache (contentcache, ImageCache, Workshop...) TRƯỚC KHI guard kịp cài - xác nhận qua
// ảnh chụp Files app thật: các folder cache đó đã có nội dung dù màn "Vui lòng chờ" còn đang hiện.
// Constructor này đóng đúng lỗ hổng thứ tự đó - class UIApplication (system framework, luôn nạp
// trước bất kỳ dylib nào của app) và class DeltaMenu (cùng ảnh, đã được objc runtime "realize" lúc
// map ảnh, TRƯỚC khi bất kỳ constructor nào trong ảnh đó chạy) đều sẵn sàng dùng được ở đây.
__attribute__((constructor))
static void installFirstRunLaunchGuardEarly() {
    if (DeltaVFS_needsFirstRunExtraction()) {
        DeltaVFS_debugLog("installFirstRunLaunchGuardEarly: needsFirstRunExtraction=true - cài launch guard NGAY từ constructor (trước mọi +load trong process, kể cả của SDK bên thứ 3)");
        [DeltaMenu installAppDelegateLaunchGuard];
    }
}

@implementation DeltaMenu

static DeltaMenu *extraInfo;
static BOOL MenDeal;
UIWindow *mainWindow;
game_sdk_t *game_sdk = new game_sdk_t();

+ (void)load {
    // installDNSBlockHook() cài luôn cả socket-layer hook (connect/connectx/sendto/write/send) ở
    // bên trong nó - hook đó cũng là thứ đứng sau MOD tab's "Chặn UDP Port" toggle
    // (netLogSetUdpPortBlockEnabled) và NetLog's passive traffic logging, không chỉ riêng DNS.
    //
    // Called immediately, not inside the 3s-delayed block below - banner/ad requests can fire
    // during early app launch, well before that delay elapses, and neither the getaddrinfo hook
    // nor NSURLProtocol registration depend on game_sdk/UIApplication being ready.
    installDNSBlockHook();

    if (DeltaVFS_needsFirstRunExtraction()) {
        // Guard đã được installFirstRunLaunchGuardEarly() (__attribute__((constructor)) phía trên,
        // chạy TRƯỚC +load này) cài rồi - không cài lại ở đây, chỉ return sớm để không chạy tiếp
        // phần setup menu "bình thường" bên dưới (game process này sẽ không bao giờ chạy tới đó).
        DeltaVFS_debugLog("Menu +load: needsFirstRunExtraction=true (guard đã cài từ constructor sớm hơn), bỏ qua setup menu");
        return;
    }
    DeltaVFS_debugLog("Menu +load: needsFirstRunExtraction=false, normal menu flow, scheduling setup in 3s");

    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(3 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
        // Checkpoint - nếu dòng này KHÔNG xuất hiện trong debug.log, nghĩa là main thread bị
        // kẹt (deadlock/treo) từ TRƯỚC mốc 3s này rồi, rất có thể ngay trong/ngay sau lúc
        // HWBreakHook kích hoạt như nghi ngờ. Nếu dòng này CÓ xuất hiện, main thread vẫn chạy
        // bình thường qua mốc 3s - chỗ treo thật sự nằm ở đâu đó SAU đây (game_sdk init,
        // AimMagnet hook, hoặc chính trong game/Unity), không phải do HWBreakHook nữa.
        DeltaVFS_debugLog("Menu +load: 3s dispatch_after fired, bắt đầu setup menu");

        mainWindow = [UIApplication sharedApplication].keyWindow;

        extraInfo = [DeltaMenu new];

        static bool sdkInitialized = false;
        if (!sdkInitialized) {
            DeltaVFS_debugLog("Menu +load: gọi game_sdk->init()");
            game_sdk->init();
            // installAimMagnetHook() TẠM TẮT ĐỂ TEST - dùng MSHookFunction (trampoline/inline-hook
            // cổ điển) hook thẳng vào GetAimAssistDampCoefficient (RVA 0x545567C), CÀI VÔ ĐIỀU KIỆN
            // ở đây dù người dùng có bật tính năng Aim Magnet hay không - nghi ngờ đây là thứ bị
            // PMS_HOOK (case 7 trong dispatcher OnMsgMatchMaking đã tìm thấy trước đó) phát hiện,
            // khớp với việc user báo "không hề kích hoạt gì vẫn bị đá". Tắt thử để xác nhận.
            // DeltaVFS_debugLog("Menu +load: gọi installAimMagnetHook()");
            // installAimMagnetHook();
            // installAntiReportSpoof() ĐÃ BỎ GỌI LẠI - test lần 2 (VFS quay về fishhook) vẫn
            // crash y hệt ở firebase::crashlytics::Frame::__vdeallocate, 2/3 lần chạy, kể cả khi
            // KHÔNG hề vào trận (crash ngay lúc ngồi ở menu ~15-20s sau khi hook cài xong) - xác
            // nhận chắc chắn nguyên nhân là DobbyHook patch vào GetMatchClientInfo(), không liên
            // quan gì tới VFS/Cocoa-swizzle/fishhook. Giữ nguyên định nghĩa trong
            // AntiReportSpoof.h để tham khảo, không gọi installAntiReportSpoof() nữa.
            // installPacketCapture() VẪN ĐANG TẮT - gây crash-loop ngay từ đầu (xem PacketCapture.h).
            //
            // installCheckHackerPatch() ĐÃ TẮT HẲN - đã bisect trên máy thật 2 lần: bản patch nguyên
            // 8 byte đầu hàm VÀ bản đã sửa chỉ patch đúng 1 lệnh đọc giá trị (RVA 0x4DDCE48, giữ
            // nguyên phần đảm bảo static constructor) ĐỀU gây crash sớm ngay lúc logo hiện, trong khi
            // installMatchClientInfoPatch() một mình chạy sạch 20s+ không crash. Vậy vấn đề không nằm
            // ở CÁCH patch (byte nào bị đổi) mà rất có thể ép GameConfig.CheckHacker về false tự nó
            // gây ra hệ quả logic ở 1 nơi khác chưa lần ra được (có thể còn nơi gọi get_CheckHacker()
            // ngoài 2 chỗ đã quét được qua bl trực tiếp - vd gọi gián tiếp qua delegate/reflection).
            // Không đáng risk tiếp tục dùng patch này. Giữ nguyên định nghĩa trong CheckHackerPatch.h
            // để tham khảo, không gọi installCheckHackerPatch() nữa.
            //
            // installMatchClientInfoPatch() - vá byte TĨNH qua vm_remap (xem file .h), không
            // trampoline/hook nên không có rủi ro adrp-relocation/Crashlytics như AntiReportSpoof gặp
            // phải. Có memcmp byte gốc trước khi ghi - game update lệch offset thì tự huỷ, không ghi
            // bậy. Đã xác nhận qua bisect: chạy MỘT MÌNH không crash, nhưng CHƯA test xem có thật sự
            // giải quyết được việc bị đá sau ~11s khi vào trận hay không - test vào trận thật cho
            // thấy VẪN bị đá nhưng timing đổi thành ~23s (gấp đôi) - xác nhận có patch có tác dụng
            // 1 phần nhưng còn 1 cơ chế phát hiện ĐỘC LẬP khác (SDK "ffantihack" riêng biệt, không
            // liên quan HackerPoolCdt/CheckHacker) - xem FFAntiFlagsPatch.h.
            // DeltaVFS_debugLog("Menu +load: gọi installCheckHackerPatch()");
            // installCheckHackerPatch();
            DeltaVFS_debugLog("Menu +load: gọi installMatchClientInfoPatch()");
            installMatchClientInfoPatch();
            // installFFAntiFlagsPatch() KHÔNG gọi ở đây nữa - 2 lần thử trước đều gọi NGAY lúc
            // +load (~3s sau khi mở app), tức TRƯỚC KHI class MFHPGMELLCC chạy xong static
            // constructor (FFAntiObserve.h cho thấy phải retry ~23-26 lần/giây mới có static field
            // data). Giả thuyết mới: sửa code BÊN TRONG 1 class trong lúc nó CHƯA init xong mới là
            // lý do crash, không phải nội dung patch. Giờ trì hoãn: gọi trong updateMenu (xem bên
            // dưới) NGAY KHI FFAntiObserve::IsReady() báo class đã init xong, thay vì gọi cứng ở
            // +load. Xem đoạn "FFAntiObserve::IsReady()" trong updateMenu.
            DeltaVFS_debugLog("Menu +load: game_sdk + AimMagnet hook xong");
            sdkInitialized = true;
        }

        [extraInfo setupDisplayLink];
        [extraInfo initTapGes];
        DeltaVFS_debugLog("Menu +load: setup menu hoàn tất");
    });
}

+ (void)installAppDelegateLaunchGuard {
    Method m = class_getInstanceMethod([UIApplication class], @selector(setDelegate:));
    if (!m) {
        // Không nên bao giờ xảy ra (setDelegate: là public UIKit API ổn định) - fallback thay vì
        // im lặng không làm gì.
        DeltaVFS_debugLog("installAppDelegateLaunchGuard: -[UIApplication setDelegate:] not found, falling back to poll+block");
        [DeltaMenu pollUntilAppReadyThenBlockAndUpdate];
        return;
    }
    orig_setDelegate = (OrigSetDelegateIMP)method_getImplementation(m);
    method_setImplementation(m, (IMP)hooked_setDelegate);
    DeltaVFS_debugLog("installAppDelegateLaunchGuard: swizzled -[UIApplication setDelegate:]");

    // An toàn: swizzle -setDelegate: không đảm bảo 100% bắt được lúc UIApplicationMain gán
    // delegate trên MỌI phiên bản iOS/cấu hình app. Nếu nhánh swizzle chưa hiện popup trong 2s,
    // fallback qua poll+block thay vì người chơi không thấy gì cả và game chạy không bị chặn.
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(2.0 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
        if (!g_firstRunPopupShown.load(std::memory_order_relaxed)) {
            DeltaVFS_debugLog("installAppDelegateLaunchGuard: safety-net fired - swizzle never showed the popup, falling back to poll+block");
            [DeltaMenu pollUntilAppReadyThenBlockAndUpdate];
        }
    });
}

+ (void)pollUntilAppReadyThenBlockAndUpdate {
    // UIApplication chưa tồn tại lúc này (đang chạy trước main()/UIApplicationMain). Poll thay vì
    // đợi cố định vài giây - UI của game tuyệt đối không được có cơ hội vẽ 1 frame nào trước khi
    // cửa sổ che của mình phủ lên.
    if (![UIApplication sharedApplication]) {
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.05 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
            [DeltaMenu pollUntilAppReadyThenBlockAndUpdate];
        });
        return;
    }
    DeltaVFS_debugLog("Menu poll: UIApplication ready, showing block window + popup");
    [DeltaMenu showUpdatingPopupThenRelaunch];
}

+ (void)showUpdatingPopupThenRelaunch {
    // Set NGAY, đồng bộ, trước bất kỳ việc async/animation nào - timer an toàn 2s của
    // installAppDelegateLaunchGuard kiểm tra cờ này để biết còn cần fallback poll+block không.
    if (g_firstRunPopupShown.exchange(true, std::memory_order_relaxed)) {
        DeltaVFS_debugLog("showUpdatingPopupThenRelaunch: already shown once this run, ignoring duplicate call");
        return;
    }

    // Cửa sổ full-screen, top-level của riêng mình - CỐ Ý không phải keyWindow/rootViewController
    // của game (có thể còn chưa tồn tại - đúng ý đồ: chặn trước khi game kịp tới đó).
    UIWindow *blockWindow = [[UIWindow alloc] initWithFrame:[UIScreen mainScreen].bounds];
    blockWindow.windowLevel = UIWindowLevelAlert + 1;
    blockWindow.backgroundColor = [UIColor blackColor];
    UIViewController *blockVC = [UIViewController new];
    blockVC.view.backgroundColor = [UIColor blackColor];
    blockWindow.rootViewController = blockVC;
    [blockWindow makeKeyAndVisible];
    mainWindow = blockWindow; // giữ strong ref để ARC không dọn nó đi

    // Nói rõ cho người chơi biết đang CỐ Ý tạm dừng game, đừng force-quit - không có dòng này vài
    // giây màn hình đứng im giống crash/bug, dễ khiến người dùng thoát giữa chừng, để lại Delta/
    // giải nén dở cho lần mở sau retry.
    NSString *title = isEnglishMode ? @"Please Wait" : @"Vui lòng chờ";
    NSString *message = isEnglishMode
        ? @"We need to freeze the game while we prepare required files.\n\nPlease wait and do not close the game until this process finishes."
        : @"Game cần tạm dừng để chuẩn bị các file cần thiết.\n\nVui lòng chờ và không tắt game cho đến khi quá trình này hoàn tất.";

    // UIAlertController THẬT của Apple (không tự vẽ như trước) - không gắn action nào cả nên
    // người dùng không bấm tắt được, đúng ý "chặn" trong lúc giải nén.
    UIAlertController *waitAlert = [UIAlertController alertControllerWithTitle:title
                                                                        message:message
                                                                 preferredStyle:UIAlertControllerStyleAlert];
    [blockVC presentViewController:waitAlert animated:YES completion:nil];

    DeltaVFS_debugLog("Menu popup: đã hiện UIAlertController thật, gọi DeltaVFS_runFirstRunExtraction");
    DeltaVFS_runFirstRunExtraction(^(BOOL success) {
        if (success) {
            DeltaVFS_debugLog("Menu popup: extraction succeeded, aborting in 0.6s");
            // Giữ popup trên màn hình 1 chút để không chỉ lóe lên rồi tắt, rồi relaunch.
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.6 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
                abort();
            });
            return;
        }

        // Giải nén thất bại (0 file được ghi, hoặc marker không ghi được). KHÔNG crash ở đây -
        // marker chưa ghi thì relaunch nào cũng lại hiện đúng popup này mãi mà không cách nào
        // biết vì sao. Đổi sang UIAlertController báo lỗi kèm đường dẫn debug.log thật (đã ghi đầy
        // đủ ra đĩa từ trước qua DeltaVFS_debugLog, xem AssetRedirect.h) để đọc qua Files app -
        // UIAlertController thật tự vẽ lớp scrim che mất mọi subview thêm phía sau nó nên không
        // hiện log sống trên màn hình theo kiểu cũ được nữa.
        DeltaVFS_debugLog("Menu popup: extraction FAILED - staying on screen, not crashing");
        NSString *errTitle = isEnglishMode ? @"Extraction failed" : @"Giải nén thất bại";
        NSString *logPath = DeltaVFS_debugLogPath();
        NSString *errMsg = isEnglishMode
            ? [NSString stringWithFormat:@"Something went wrong preparing files. Open this file via the Files app and send it over:\n%@", logPath]
            : [NSString stringWithFormat:@"Có lỗi khi chuẩn bị file. Mở file này qua Files app rồi gửi lại giúp mình:\n%@", logPath];
        [waitAlert dismissViewControllerAnimated:YES completion:^{
            UIAlertController *errAlert = [UIAlertController alertControllerWithTitle:errTitle
                                                                                message:errMsg
                                                                         preferredStyle:UIAlertControllerStyleAlert];
            [errAlert addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleDefault handler:nil]];
            [blockVC presentViewController:errAlert animated:YES completion:nil];
        }];
    });
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

    // Kích thước/tỉ lệ rộng hơn hẳn bản 460x340 cũ - gần với dáng panel ngang thấy trong ảnh
    // chụp màn hình thật của Monite (mục 3l), thay vì 1 khung vuông nhỏ kiểu popup tiện ích.
    CGFloat menuWidth = 560;
    CGFloat menuHeight = 360;
    CGFloat sidebarWidth = 84;
    CGFloat x = (kWidth - menuWidth) * 0.5f;
    CGFloat y = (kHeight - menuHeight) * 0.5f;

    _menuView = [[UIView alloc] initWithFrame:CGRectMake(x, y, menuWidth, menuHeight)];
    _menuView.backgroundColor = COLOR_BG;
    _menuView.layer.cornerRadius = 14.0f;
    // KHÔNG còn viền/glow animated (installAnimatedBorder bên dưới, không gọi nữa) - ảnh chụp
    // màn hình thật của Monite là 1 panel phẳng, không viền sáng kiểu "Delta" cũ.
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
    // installAnimatedBorder (định nghĩa bên dưới, KHÔNG xoá) không còn được gọi - viền gradient
    // động là nhận diện "Delta" cũ, Monite thật không có hiệu ứng này.

    [self setupSidebarInView:_menuView width:sidebarWidth height:menuHeight];

    // Khung 5 tab kiểu Monite (Aimbot/Hiển thị/Khác/Cài Đặt/Tài Khoản) - CHỈ dựng bố cục/màu/icon
    // theo đúng cấu trúc đã phân tích (xem MoniteAnalysis/README.md + demo HTML), CHƯA gắn tính
    // năng ESP/MOD thật vào - đó là bước "sau" người dùng đã nói rõ. Cài Đặt/Khác có nội dung
    // MẪU khớp ảnh chụp màn hình thật của Monite (mục 3l); Aimbot/Hiển thị/Tài Khoản để trống vì
    // chưa có ảnh/tính năng nào xác nhận được cho các tab đó.
    //
    // buildESPPageInFrame/buildModPageInFrame/buildInfoPageInFrame/buildSpyPageInFrame (định
    // nghĩa bên dưới, KHÔNG xoá) tạm thời không còn được gọi - toàn bộ tính năng cũ (ESP box/
    // lines/..., MOD aim/speed/no-recoil..., INFO log, SPY dylib B) vẫn còn nguyên trong code,
    // chỉ chưa có chỗ hiển thị trong sidebar mới. "Không giật" (noRecoilSwitch, trong modPage cũ)
    // đã có sẵn logic thật, khớp thẳng "Không giật" ở tab Khác mới - ứng viên đầu tiên nên nối
    // lại khi làm bước "gắn tính năng".
    CGRect contentFrame = CGRectMake(sidebarWidth, 0, menuWidth - sidebarWidth, menuHeight);
    UIView *aimbotPage = [self buildAimbotPageInFrame:contentFrame];
    UIView *hienThiPage = [self buildHienThiPageInFrame:contentFrame];
    UIView *khacPage = [self buildKhacPageInFrame:contentFrame];
    UIView *caiDatPage = [self buildCaiDatPageInFrame:contentFrame];
    UIView *taiKhoanPage = [self buildTaiKhoanPageInFrame:contentFrame];
    UIView *infoTabPage = [self buildInfoTabPageInFrame:contentFrame];

    hienThiPage.hidden = YES;
    khacPage.hidden = YES;
    caiDatPage.hidden = YES;
    taiKhoanPage.hidden = YES;
    infoTabPage.hidden = YES;

    [_menuView addSubview:aimbotPage];
    [_menuView addSubview:hienThiPage];
    [_menuView addSubview:khacPage];
    [_menuView addSubview:caiDatPage];
    [_menuView addSubview:taiKhoanPage];
    [_menuView addSubview:infoTabPage];

    _tabPages = @[aimbotPage, hienThiPage, khacPage, caiDatPage, taiKhoanPage, infoTabPage];
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
    _sidebarView.backgroundColor = COLOR_SIDEBAR_BG;
    [parent addSubview:_sidebarView];

    UIView *separator = [[UIView alloc] initWithFrame:CGRectMake(width - 1, 0, 1, height)];
    separator.backgroundColor = [UIColor colorWithWhite:1.0 alpha:0.07];
    [_sidebarView addSubview:separator];

    // KHÔNG còn logo/brand tròn ở đầu sidebar (bỏ luôn LogoDelta.png/chữ "Δ") - ảnh chụp màn hình
    // thật của Monite không có logo riêng, sidebar bắt đầu ngay bằng 6 mục tab (xem startY bên
    // dưới, kéo lên sát mép trên thay vì chừa chỗ cho logo như trước).

    // 5 tab đúng theo cấu trúc sidebar Monite đã phân tích (xem MoniteAnalysis/README.md mục
    // 3g.1/3h/3j và demo HTML dựng lại UI của họ) - "scope" thay cho icon crosshair riêng của họ
    // (không dùng lại chính PNG trích xuất được từ Monite.dylib, chỉ mô phỏng đúng Ý TƯỞNG icon).
    // Các tab ESP/MOD/INFO/SPY cũ (buildESPPageInFrame/buildModPageInFrame/buildInfoPageInFrame/
    // buildSpyPageInFrame bên dưới) TẠM THỜI không còn nối vào sidebar nữa - code vẫn còn nguyên,
    // chưa xoá gì, chỉ chưa gọi tới. Xem comment ở setupMenu.
    // "Info" thêm riêng ở CUỐI sidebar theo yêu cầu - KHÔNG thuộc 5 tab gốc của Monite, đây là
    // bảng chẩn đoán/debug của chính mình (log VFS, redirected/missed files) - xem
    // buildInfoTabPageInFrame bên dưới (bọc lại buildInfoPageInFrame cũ, không đổi nội dung).
    NSArray<NSString *> *titles = @[@"Aimbot", @"Hiển thị", @"Khác", @"Cài Đặt", @"Tài Khoản", @"Info"];
    NSArray<NSString *> *symbols = @[@"scope", @"eye.fill", @"shippingbox.fill", @"gearshape.fill", @"person.fill", @"info.circle.fill"];

    CGFloat startY = 14;
    CGFloat itemH = 50, itemGap = 3;
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

#pragma mark - Monite-style 5 tab pages (khung mới - xem setupMenu, CHƯA gắn tính năng thật)

// Header nhỏ (icon + tiêu đề in hoa) đầu mỗi trang - khớp panel-head thấy trong ảnh chụp màn hình
// thật của Monite (MoniteAnalysis/README.md mục 3l). Không có nút đóng/dark-mode riêng ở đây vì
// menu đã có cơ chế đóng ở tầng ngoài rồi (closeMenu, chạm ra ngoài menuView).
- (void)addPageHeaderWithSymbol:(NSString *)symbolName title:(NSString *)titleText toView:(UIView *)parent width:(CGFloat)width {
    UIImageView *icon = [[UIImageView alloc] initWithFrame:CGRectMake(10, 10, 16, 16)];
    icon.image = [[UIImage systemImageNamed:symbolName] imageByApplyingSymbolConfiguration:[UIImageSymbolConfiguration configurationWithPointSize:14 weight:UIImageSymbolWeightBold]];
    icon.tintColor = COLOR_CYAN;
    icon.contentMode = UIViewContentModeScaleAspectFit;
    [parent addSubview:icon];

    UILabel *title = [[UILabel alloc] initWithFrame:CGRectMake(32, 9, width - 42, 18)];
    title.font = [UIFont systemFontOfSize:13 weight:UIFontWeightBold];
    title.textColor = COLOR_CYAN;
    title.text = [titleText uppercaseString];
    [parent addSubview:title];
}

// Dùng cho mọi control CHƯA nối logic thật (Cài Đặt/Khác) - vẫn nhận target để card sáng lên khi
// bật (applyCardVisualState:, xem refreshCardVisualState:) nhưng không đụng gì tới Vars/game.
- (void)placeholderControlChanged:(id)sender {}

// Hàng dạng dropdown (Ngôn ngữ/Kiểu giao diện) - CHỈ hiển thị, chưa có picker thật đằng sau.
- (void)addSelectRowWithTitle:(NSString *)titleText valueText:(NSString *)valueText frame:(CGRect)frame toView:(UIView *)parent {
    UIView *card = [[UIView alloc] initWithFrame:frame];
    card.backgroundColor = COLOR_CARD_BG;
    card.layer.cornerRadius = 10.0f;
    [parent addSubview:card];

    UILabel *label = [[UILabel alloc] initWithFrame:CGRectMake(12, 0, frame.size.width * 0.4f, frame.size.height)];
    label.font = [UIFont systemFontOfSize:11.5f weight:UIFontWeightMedium];
    label.textColor = COLOR_TEXT_DIM;
    label.text = titleText;
    [card addSubview:label];

    UILabel *value = [[UILabel alloc] initWithFrame:CGRectMake(frame.size.width * 0.4f, 0, frame.size.width * 0.6f - 34, frame.size.height)];
    value.font = [UIFont systemFontOfSize:12 weight:UIFontWeightSemibold];
    value.textColor = COLOR_TEXT;
    value.textAlignment = NSTextAlignmentRight;
    value.text = valueText;
    [card addSubview:value];

    UIImageView *chevron = [[UIImageView alloc] initWithFrame:CGRectMake(frame.size.width - 24, (frame.size.height - 12) / 2.0f, 12, 12)];
    chevron.image = [[UIImage systemImageNamed:@"chevron.down"] imageByApplyingSymbolConfiguration:[UIImageSymbolConfiguration configurationWithPointSize:10 weight:UIImageSymbolWeightSemibold]];
    chevron.tintColor = COLOR_TEXT_DIM;
    chevron.contentMode = UIViewContentModeScaleAspectFit;
    [card addSubview:chevron];
}

// Slider có nhãn + giá trị số phía trên, cùng quy ước với _fovCircleSlider/_spinSpeedSlider hiện
// có (không bọc card riêng, giữ nhất quán phong cách sẵn của file này).
- (UISlider *)addPlaceholderSliderWithLabel:(NSString *)labelText valueText:(NSString *)valueText frame:(CGRect)frame toView:(UIView *)parent {
    UILabel *top = [[UILabel alloc] initWithFrame:CGRectMake(frame.origin.x, frame.origin.y, frame.size.width, 14)];
    top.font = [UIFont systemFontOfSize:11 weight:UIFontWeightMedium];
    top.textColor = COLOR_TEXT_DIM;
    top.text = labelText;
    [parent addSubview:top];

    UILabel *val = [[UILabel alloc] initWithFrame:CGRectMake(frame.origin.x, frame.origin.y, frame.size.width, 14)];
    val.font = [UIFont monospacedDigitSystemFontOfSize:11 weight:UIFontWeightSemibold];
    val.textColor = COLOR_CYAN;
    val.textAlignment = NSTextAlignmentRight;
    val.text = valueText;
    [parent addSubview:val];

    UISlider *slider = [[UISlider alloc] initWithFrame:CGRectMake(frame.origin.x, frame.origin.y + 16, frame.size.width, 20)];
    slider.minimumValue = 0.0f;
    slider.maximumValue = 1.0f;
    slider.value = 1.0f;
    slider.minimumTrackTintColor = COLOR_CYAN;
    slider.maximumTrackTintColor = [UIColor colorWithWhite:1.0 alpha:0.12];
    slider.thumbTintColor = COLOR_TEXT;
    [parent addSubview:slider];
    return slider;
}

// Nút hành động dạng card (Lưu cài đặt/Khôi phục cài đặt) - chưa nối action thật.
- (void)addActionButtonWithTitle:(NSString *)titleText symbol:(NSString *)symbolName frame:(CGRect)frame toView:(UIView *)parent {
    UIView *card = [[UIView alloc] initWithFrame:frame];
    card.backgroundColor = COLOR_CARD_BG;
    card.layer.cornerRadius = 10.0f;
    [parent addSubview:card];

    CGFloat iconSize = 15;
    UIImageView *icon = [[UIImageView alloc] initWithFrame:CGRectMake(12, (frame.size.height - iconSize) / 2.0f, iconSize, iconSize)];
    icon.image = [[UIImage systemImageNamed:symbolName] imageByApplyingSymbolConfiguration:[UIImageSymbolConfiguration configurationWithPointSize:13 weight:UIImageSymbolWeightSemibold]];
    icon.tintColor = COLOR_TEXT_DIM;
    icon.contentMode = UIViewContentModeScaleAspectFit;
    [card addSubview:icon];

    UILabel *label = [[UILabel alloc] initWithFrame:CGRectMake(12 + iconSize + 8, 0, frame.size.width - (12 + iconSize + 8) - 10, frame.size.height)];
    label.font = [UIFont systemFontOfSize:12 weight:UIFontWeightSemibold];
    label.textColor = COLOR_TEXT;
    label.text = titleText;
    [card addSubview:label];
}

// Trạng thái trống trung thực cho Aimbot/Hiển thị/Tài Khoản - CHƯA có ảnh chụp màn hình hay
// tính năng nào xác nhận được cho các tab này (khác Cài Đặt/Khác, xem
// buildCaiDatPageInFrame/buildKhacPageInFrame), nên không tự bịa nội dung.
- (UIView *)buildEmptyTabPageInFrame:(CGRect)frame symbol:(NSString *)symbolName title:(NSString *)titleText {
    UIView *page = [[UIView alloc] initWithFrame:frame];
    [self addPageHeaderWithSymbol:symbolName title:titleText toView:page width:frame.size.width];

    CGFloat bigIconSize = 30;
    UIImageView *bigIcon = [[UIImageView alloc] initWithFrame:CGRectMake((frame.size.width - bigIconSize) / 2.0f, frame.size.height / 2.0f - 44, bigIconSize, bigIconSize)];
    bigIcon.image = [[UIImage systemImageNamed:symbolName] imageByApplyingSymbolConfiguration:[UIImageSymbolConfiguration configurationWithPointSize:26 weight:UIImageSymbolWeightRegular]];
    bigIcon.tintColor = [COLOR_TEXT_DIM colorWithAlphaComponent:0.5];
    bigIcon.contentMode = UIViewContentModeScaleAspectFit;
    [page addSubview:bigIcon];

    UILabel *note = [[UILabel alloc] initWithFrame:CGRectMake(16, CGRectGetMaxY(bigIcon.frame) + 8, frame.size.width - 32, 34)];
    note.numberOfLines = 0;
    note.textAlignment = NSTextAlignmentCenter;
    note.font = [UIFont systemFontOfSize:10.5f weight:UIFontWeightMedium];
    note.textColor = COLOR_TEXT_DIM;
    [page addSubview:note];
    __weak UILabel *weakNote = note;
    [self addLocalizedRefresher:^{ weakNote.text = LOC(@"empty_tab_note"); }];

    return page;
}

// Nội dung THẬT của tab "Aimbot" - toàn bộ section "Tự Động Ngắm" chuyển nguyên từ MOD tab cũ
// (buildModMainListInFrame) sang đây, khớp đúng tên tab Monite. Logic/action y hệt cũ
// (toggleAimHead:/toggleAimNheTam:/aimModeChanged:/toggleAimPreferLowHP:/toggleAimMagnet:/
// aimMagnetStrengthChanged:, xem "Mod tab toggle actions" bên dưới) - chỉ đổi chỗ hiển thị.
- (UIView *)buildAimbotPageInFrame:(CGRect)frame {
    UIView *page = [[UIView alloc] initWithFrame:frame];
    [self addPageHeaderWithSymbol:@"scope" title:@"Aimbot" toView:page width:frame.size.width];

    CGFloat headerH = 30;
    CGRect scrollFrame = CGRectMake(0, headerH, frame.size.width, frame.size.height - headerH);
    UIScrollView *scroll = [[UIScrollView alloc] initWithFrame:scrollFrame];
    scroll.showsVerticalScrollIndicator = NO;
    [page addSubview:scroll];

    CGFloat btnX = 4, cardH = 40, btnGap = 6, btnW = scrollFrame.size.width - 8;
    CGFloat btnY = 6;

    _aimHeadSwitch = [self addToggleCardWithLocKey:@"aim_head" symbol:@"scope" frame:CGRectMake(btnX, btnY, btnW, cardH) action:@selector(toggleAimHead:) toView:scroll];
    btnY += cardH + btnGap;

    _aimNheTamSwitch = [self addToggleCardWithLocKey:@"aim_nhe_tam" symbol:@"dot.circle.fill" frame:CGRectMake(btnX, btnY, btnW, cardH) action:@selector(toggleAimNheTam:) toView:scroll];
    btnY += cardH + btnGap;

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

    _aimPreferLowHPSwitch = [self addToggleCardWithLocKey:@"aim_prefer_low_hp" symbol:@"shuffle" frame:CGRectMake(btnX, btnY, btnW, cardH) action:@selector(toggleAimPreferLowHP:) toView:scroll];
    btnY += cardH + btnGap;

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

    UILabel *aimNote = [[UILabel alloc] initWithFrame:CGRectMake(btnX + 8, btnY, btnW - 8, 28)];
    aimNote.font = [UIFont systemFontOfSize:10.5f weight:UIFontWeightMedium];
    aimNote.textColor = COLOR_TEXT_DIM;
    aimNote.numberOfLines = 2;
    [scroll addSubview:aimNote];
    [self addLocalizedRefresher:^{
        aimNote.text = isEnglishMode ? @"Range: FOV circle slider (Hiển thị tab)" : @"Bán kính: xem thanh FOV bên tab Hiển thị";
    }];
    btnY += 28 + btnGap;

    scroll.contentSize = CGSizeMake(scrollFrame.size.width, btnY + 6);
    return page;
}

// Nội dung THẬT của tab "Hiển thị" - toàn bộ nội dung ESP tab cũ (buildESPPageInFrame) chuyển
// nguyên sang đây, tên tab khớp thẳng "section_display" ("Hiển Thị") đã dùng làm section header
// từ trước. Logic/action y hệt cũ (toggleEnable:/toggleShowFovCircle:/toggleBox:/...).
- (UIView *)buildHienThiPageInFrame:(CGRect)frame {
    UIView *page = [[UIView alloc] initWithFrame:frame];
    [self addPageHeaderWithSymbol:@"eye.fill" title:@"Hiển thị" toView:page width:frame.size.width];

    CGFloat headerH = 30;
    CGRect scrollFrame = CGRectMake(0, headerH, frame.size.width, frame.size.height - headerH);
    UIScrollView *scroll = [[UIScrollView alloc] initWithFrame:scrollFrame];
    scroll.showsVerticalScrollIndicator = NO;
    [page addSubview:scroll];

    CGFloat padX = 10, cardH = 40, gap = 6;
    CGFloat fullW = scrollFrame.size.width - padX * 2;
    CGFloat colW = (fullW - gap) / 2.0f;
    CGFloat y = 6;

    _enableSwitch = [self addToggleCardWithLocKey:@"master_switch" symbol:@"bolt.fill" frame:CGRectMake(padX, y, fullW, cardH) action:@selector(toggleEnable:) toView:scroll];
    y += cardH + gap;

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
    NSMutableArray<UIControl *> *gridSwitches = [NSMutableArray array];

    for (NSUInteger i = 0; i < gridKeys.count; i++) {
        NSInteger col = i % 2, row = i / 2;
        CGFloat bx = padX + col * (colW + gap);
        CGFloat by = y + row * (cardH + gap);
        SEL selector = (SEL)[gridSelectors[i] pointerValue];
        UIControl *sw = [self addToggleCardWithLocKey:gridKeys[i] symbol:gridSymbols[i] frame:CGRectMake(bx, by, colW, cardH) action:selector toView:scroll];
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

    scroll.contentSize = CGSizeMake(scrollFrame.size.width, y + 10);
    return page;
}

- (UIView *)buildTaiKhoanPageInFrame:(CGRect)frame {
    return [self buildEmptyTabPageInFrame:frame symbol:@"person.fill" title:@"Tài Khoản"];
}

// Tab "Info" thêm riêng ở CUỐI sidebar (không thuộc 5 tab gốc Monite) - bọc lại
// buildInfoPageInFrame CŨ nguyên vẹn (log VFS, redirected/missed files, ngôn ngữ) trong 1 header
// nhỏ cùng phong cách các tab kia, không đổi bất kỳ nội dung/logic nào bên trong.
- (UIView *)buildInfoTabPageInFrame:(CGRect)frame {
    UIView *page = [[UIView alloc] initWithFrame:frame];
    [self addPageHeaderWithSymbol:@"info.circle.fill" title:@"Info" toView:page width:frame.size.width];

    CGFloat headerH = 30;
    CGRect innerFrame = CGRectMake(0, headerH, frame.size.width, frame.size.height - headerH);
    UIView *infoContent = [self buildInfoPageInFrame:innerFrame];
    [page addSubview:infoContent];
    return page;
}

// Nội dung khớp đúng ảnh chụp màn hình thật của tab "Cài Đặt" Monite (mục 3l): Chế độ Stream →
// Màu nhấn (khớp thẳng cơ chế đọc RGBA byte÷255.0 đã phân tích ở mục 3i) → Độ trong suốt menu →
// Ngôn ngữ → Kiểu giao diện → Nút kéo cuộn → Lưu/Khôi phục cài đặt. Toàn bộ control ở đây CHƯA nối
// logic thật (đổi màu/độ trong suốt/ngôn ngữ chưa làm gì) - xem placeholderControlChanged:.
- (UIView *)buildCaiDatPageInFrame:(CGRect)frame {
    UIView *page = [[UIView alloc] initWithFrame:frame];
    [self addPageHeaderWithSymbol:@"gearshape.fill" title:@"Cài đặt" toView:page width:frame.size.width];

    CGFloat headerH = 30;
    CGRect scrollFrame = CGRectMake(0, headerH, frame.size.width, frame.size.height - headerH);
    UIScrollView *scroll = [[UIScrollView alloc] initWithFrame:scrollFrame];
    scroll.showsVerticalScrollIndicator = NO;
    [page addSubview:scroll];

    CGFloat padX = 10, rowH = 34, gap = 6;
    CGFloat fullW = scrollFrame.size.width - padX * 2;
    CGFloat y = 6;

    [self addPillToggleCardWithLocKey:@"stream_mode" symbol:@"tv.fill" frame:CGRectMake(padX, y, fullW, rowH) action:@selector(placeholderControlChanged:) toView:scroll];
    y += rowH + gap;
    [self addPillToggleCardWithLocKey:@"accent_color" symbol:@"paintpalette.fill" frame:CGRectMake(padX, y, fullW, rowH) action:@selector(placeholderControlChanged:) toView:scroll];
    y += rowH + gap;

    [self addPlaceholderSliderWithLabel:LOC(@"menu_opacity") valueText:@"1.00" frame:CGRectMake(padX, y, fullW, 36) toView:scroll];
    y += 36 + gap;

    [self addSelectRowWithTitle:LOC(@"language_label") valueText:@"Tiếng Việt" frame:CGRectMake(padX, y, fullW, rowH) toView:scroll];
    y += rowH + gap;
    [self addSelectRowWithTitle:LOC(@"interface_style") valueText:@"Hiện đại" frame:CGRectMake(padX, y, fullW, rowH) toView:scroll];
    y += rowH + gap;

    [self addToggleCardWithLocKey:@"scroll_drag_button" symbol:@"square.grid.3x3.fill" frame:CGRectMake(padX, y, fullW, rowH) action:@selector(placeholderControlChanged:) toView:scroll];
    y += rowH + gap;

    // Chặn cổng UDP - tính năng THẬT đã có sẵn (netLogSetUdpPortBlockEnabled, xem NetLog.h),
    // đưa vào đây vì đây là 1 cài đặt hệ thống/mạng, không phải tính năng gameplay để ở tab Khác.
    [self addSectionHeaderWithLocKey:@"section_network" frame:CGRectMake(padX, y, fullW, 12) toView:scroll];
    y += 12 + 6;
    _blockUdpPortsSwitch = [self addToggleCardWithLocKey:@"block_udp_ports" symbol:@"wifi.slash" frame:CGRectMake(padX, y, fullW, rowH) action:@selector(toggleBlockUdpPorts:) toView:scroll];
    y += rowH + gap;

    [self addActionButtonWithTitle:LOC(@"save_settings") symbol:@"square.and.arrow.down.fill" frame:CGRectMake(padX, y, fullW, rowH) toView:scroll];
    y += rowH + gap;
    [self addActionButtonWithTitle:LOC(@"restore_settings") symbol:@"arrow.counterclockwise" frame:CGRectMake(padX, y, fullW, rowH) toView:scroll];
    y += rowH + gap;

    scroll.contentSize = CGSizeMake(scrollFrame.size.width, y + 6);
    return page;
}

// Tab "Khác": banner cảnh báo Monite (mục 3l) + tính năng THẬT của mình đã có
// (Không giật/Antena/Speed x2/Speed x8/SpinBot+tốc độ, và nút "Hành Động" đổi skin súng gốc/mod -
// nguyên xi từ MOD tab cũ, dùng lại đúng _modMainView/_modGocView/_modModView +
// showGocList/showModMainFromGoc/showGocListFromMod/gocButtonTapped/modButtonTapped bên dưới,
// KHÔNG đổi gì cả) + 6 mục còn thiếu implementation thật (Đổi súng nhanh/Nạp đạn nhanh/Bật máu ở
// Ấn Độ/Kéo giãn màn hình/Ép 120 FPS/Đặt lại khách - vẫn placeholderControlChanged:, ghi rõ
// "(chưa hoạt động)" trong nhãn để không nhầm với tính năng thật).
- (UIView *)buildKhacPageInFrame:(CGRect)frame {
    UIView *page = [[UIView alloc] initWithFrame:frame];
    [self addPageHeaderWithSymbol:@"shippingbox.fill" title:@"Khác" toView:page width:frame.size.width];

    CGFloat headerH = 30;
    CGRect innerFrame = CGRectMake(0, headerH, frame.size.width, frame.size.height - headerH);

    _modMainView = [self buildKhacMainListInFrame:innerFrame];
    _modGocView = [self buildGocListInFrame:innerFrame];
    _modModView = [self buildModListInFrame:innerFrame];

    _modGocView.hidden = YES;
    _modModView.hidden = YES;

    [page addSubview:_modMainView];
    [page addSubview:_modGocView];
    [page addSubview:_modModView];
    return page;
}

- (UIView *)buildKhacMainListInFrame:(CGRect)frame {
    UIScrollView *scroll = [[UIScrollView alloc] initWithFrame:frame];
    scroll.showsVerticalScrollIndicator = NO;

    CGFloat btnX = 4, cardH = 40, btnGap = 6, btnW = frame.size.width - 8;
    CGFloat btnY = 6;

    UILabel *warn = [[UILabel alloc] initWithFrame:CGRectMake(btnX + 4, btnY, btnW - 8, 32)];
    warn.numberOfLines = 0;
    warn.font = [UIFont systemFontOfSize:9.5f weight:UIFontWeightMedium];
    warn.textColor = COLOR_TEXT_DIM;
    [scroll addSubview:warn];
    __weak UILabel *weakWarn = warn;
    [self addLocalizedRefresher:^{ weakWarn.text = LOC(@"unsafe_warning"); }];
    btnY += 32 + btnGap;

    // Tính năng THẬT đã có sẵn - "Không giật" khớp đúng vị trí đầu tiên như tab Khác thật của
    // Monite (mục 3l), cùng logic ModHacks::noRecoil(state) như trước, không đổi gì.
    _noRecoilSwitch = [self addToggleCardWithLocKey:@"no_recoil" symbol:@"shield.fill" frame:CGRectMake(btnX, btnY, btnW, cardH) action:@selector(toggleNoRecoil:) toView:scroll];
    btnY += cardH + btnGap;

    // 6 mục còn lại của Monite CHƯA có implementation thật trong dylib này - để placeholder,
    // KHÔNG tự viết logic game giả (an toàn hơn để trống còn hơn code sai gameplay thật).
    NSArray<NSString *> *khacKeys = @[@"khac_doi_sung_nhanh", @"khac_nap_dan_nhanh", @"khac_mau_an_do", @"khac_keo_gian", @"khac_120fps", @"khac_dat_lai_khach"];
    NSArray<NSString *> *khacSymbols = @[@"arrow.2.squarepath", @"arrow.clockwise", @"drop.fill", @"arrow.left.and.right", @"speedometer", @"arrow.counterclockwise"];
    for (NSUInteger i = 0; i < khacKeys.count; i++) {
        [self addToggleCardWithLocKey:khacKeys[i] symbol:khacSymbols[i] frame:CGRectMake(btnX, btnY, btnW, cardH) action:@selector(placeholderControlChanged:) toView:scroll];
        btnY += cardH + btnGap;
    }

    [self addSectionHeaderWithLocKey:@"section_boost" frame:CGRectMake(btnX + 4, btnY, btnW, 12) toView:scroll];
    btnY += 12 + 6;

    _antenaSwitch = [self addToggleCardWithLocKey:@"antena" symbol:@"antenna.radiowaves.left.and.right" frame:CGRectMake(btnX, btnY, btnW, cardH) action:@selector(toggleAntena:) toView:scroll];
    btnY += cardH + btnGap;

    _speedX2Switch = [self addToggleCardWithLocKey:@"speed_x2" symbol:@"hare.fill" frame:CGRectMake(btnX, btnY, btnW, cardH) action:@selector(toggleSpeedX2:) toView:scroll];
    btnY += cardH + btnGap;

    _speedX8Switch = [self addToggleCardWithLocKey:@"speed_x8" symbol:@"flame.fill" frame:CGRectMake(btnX, btnY, btnW, cardH) action:@selector(toggleSpeedX8:) toView:scroll];
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

    UIButton *actionBtn = [self createButtonWithLocKey:@"action" frame:CGRectMake(btnX, btnY, btnW, 32)];
    [actionBtn setTitleColor:COLOR_CYAN forState:UIControlStateNormal];
    [actionBtn addTarget:self action:@selector(showGocList) forControlEvents:UIControlEventTouchUpInside];
    [scroll addSubview:actionBtn];
    btnY += 32 + btnGap;

    scroll.contentSize = CGSizeMake(frame.size.width, btnY + 6);
    return scroll;
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
    NSMutableArray<UIControl *> *gridSwitches = [NSMutableArray array];

    for (NSUInteger i = 0; i < gridKeys.count; i++) {
        NSInteger col = i % 2, row = i / 2;
        CGFloat bx = padX + col * (colW + gap);
        CGFloat by = y + row * (cardH + gap);
        SEL selector = (SEL)[gridSelectors[i] pointerValue];
        UIControl *sw = [self addToggleCardWithLocKey:gridKeys[i] symbol:gridSymbols[i] frame:CGRectMake(bx, by, colW, cardH) action:selector toView:scroll];
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
    [backBtn setTitleColor:COLOR_TEXT_DIM forState:UIControlStateNormal];
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
    [backBtn setTitleColor:COLOR_TEXT_DIM forState:UIControlStateNormal];
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

- (void)toggleAimHead:(UIControl *)sender {
    BOOL state = [(id)sender isOn];
    [self showToast:[NSString stringWithFormat:@"%@ %@", LOC(@"aim_head"), LOC(state ? @"on" : @"off")]];
    Vars.AimHead = state;
    // Mutually exclusive with Aim Nhe Tam - both write the same set_aim call every
    // frame, so having both on would just have them fight over height/target.
    if (state && Vars.AimNheTam) {
        Vars.AimNheTam = false;
        [(id)_aimNheTamSwitch setOn:NO];
        [self applyCardVisualState:_aimNheTamSwitch];
    }
}

- (void)toggleAimNheTam:(UIControl *)sender {
    BOOL state = [(id)sender isOn];
    [self showToast:[NSString stringWithFormat:@"%@ %@", LOC(@"aim_nhe_tam"), LOC(state ? @"on" : @"off")]];
    Vars.AimNheTam = state;
    if (state && Vars.AimHead) {
        Vars.AimHead = false;
        [(id)_aimHeadSwitch setOn:NO];
        [self applyCardVisualState:_aimHeadSwitch];
    }
}

- (void)aimModeChanged:(UISegmentedControl *)sender {
    Vars.AimHeadMode = (int)sender.selectedSegmentIndex;
}

- (void)toggleAimPreferLowHP:(UIControl *)sender {
    Vars.AimPreferLowHP = [(id)sender isOn];
}

- (void)toggleAimMagnet:(UIControl *)sender {
    Vars.AimMagnet = [(id)sender isOn];
}

- (void)aimMagnetStrengthChanged:(UISlider *)sender {
    Vars.AimMagnetStrength = sender.value;
    _aimMagnetStrengthLabel.text = [NSString stringWithFormat:isEnglishMode ? @"Strength: %.1fx" : @"Độ mạnh: %.1fx", Vars.AimMagnetStrength];
}

- (void)toggleShowFovCircle:(UIControl *)sender {
    Vars.ShowFOVCircle = [(id)sender isOn];
}

- (void)fovCircleRadiusChanged:(UISlider *)sender {
    Vars.AimFOV = sender.value;
    _fovCircleLabel.text = [NSString stringWithFormat:isEnglishMode ? @"Radius: %.0fpx" : @"Bán kính: %.0fpx", Vars.AimFOV];
}

- (void)toggleAntena:(UIControl *)sender {
    BOOL state = [(id)sender isOn];
    [self showToast:[NSString stringWithFormat:@"%@ %@", LOC(@"antena"), LOC(state ? @"on" : @"off")]];
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        ModHacks::antena(state);
    });
}

- (void)toggleSpeedX2:(UIControl *)sender {
    BOOL state = [(id)sender isOn];
    [self showToast:[NSString stringWithFormat:@"%@ %@", LOC(@"speed_x2"), LOC(state ? @"on" : @"off")]];
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        ModHacks::speedX2(state);
    });
}

- (void)toggleSpeedX8:(UIControl *)sender {
    BOOL state = [(id)sender isOn];
    [self showToast:[NSString stringWithFormat:@"%@ %@", LOC(@"speed_x8"), LOC(state ? @"on" : @"off")]];
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        ModHacks::speedX8(state);
    });
}

- (void)toggleNoRecoil:(UIControl *)sender {
    BOOL state = [(id)sender isOn];
    [self showToast:[NSString stringWithFormat:@"%@ %@", LOC(@"no_recoil"), LOC(state ? @"on" : @"off")]];
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        ModHacks::noRecoil(state);
    });
}

- (void)toggleSpinBot:(UIControl *)sender {
    BOOL state = [(id)sender isOn];
    [self showToast:[NSString stringWithFormat:@"%@ %@", LOC(@"spin_bot"), LOC(state ? @"on" : @"off")]];
    Vars.SpinBot = state;
}

- (void)toggleBlockUdpPorts:(UIControl *)sender {
    BOOL state = [(id)sender isOn];
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

    // Chia phần còn lại của trang làm 3 khung: DELTA VFS (thống kê hit/miss), REDIRECTED FILES
    // (path THẬT SỰ được phục vụ từ Delta/) và MISSED FILES (path KHÔNG có trong Delta.zip, đọc
    // bản gốc) - EXTRACT LOG (log constructor/first-run popup) đã bị bỏ theo yêu cầu user từ trước.
    CGFloat logsTop = 96;
    CGFloat logsAvail = frame.size.height - logsTop - 4;
    CGFloat headerH = 14;
    CGFloat mainLogH = logsAvail * 0.24f;
    CGFloat listsAvail = logsAvail - mainLogH - 2 * headerH - 8;
    CGFloat redirectedH = listsAvail * 0.5f;
    CGFloat missedH = listsAvail - redirectedH;

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

    CGFloat redirectedTop = logsTop + mainLogH + 4;
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

    CGFloat missedTop = redirectedTop + headerH + redirectedH + 4;
    UILabel *missedHeader = [[UILabel alloc] initWithFrame:CGRectMake(6, missedTop, frame.size.width - 12, headerH)];
    missedHeader.font = [UIFont systemFontOfSize:10 weight:UIFontWeightHeavy];
    missedHeader.textColor = COLOR_TEXT_DIM;
    missedHeader.text = @"MISSED FILES (không có trong Delta, đọc bản gốc)";
    [page addSubview:missedHeader];

    _missedFilesView = [[UITextView alloc] initWithFrame:CGRectMake(4, missedTop + headerH, frame.size.width - 8, missedH)];
    _missedFilesView.backgroundColor = COLOR_CARD_BG;
    _missedFilesView.layer.cornerRadius = 10.0f;
    _missedFilesView.layer.borderWidth = 1.0f;
    _missedFilesView.layer.borderColor = COLOR_CARD_BORDER.CGColor;
    _missedFilesView.editable = NO;
    _missedFilesView.selectable = YES;
    _missedFilesView.scrollEnabled = YES;
    _missedFilesView.textColor = COLOR_TEXT;
    _missedFilesView.font = [UIFont fontWithName:@"Menlo" size:9.5f] ?: [UIFont systemFontOfSize:9.5f];
    _missedFilesView.textContainerInset = UIEdgeInsetsMake(8, 8, 8, 8);
    [page addSubview:_missedFilesView];

    return page;
}

// Cập nhật bảng log DELTA VFS - gọi định kỳ từ updateMenu (khi menu đang mở).
- (void)refreshDeltaLog {
    if (!_deltaLogView) return;

    if (_redirectedFilesView) {
        _redirectedFilesView.text = DeltaVFS_hitPathsSnapshot(120);
    }
    if (_missedFilesView) {
        _missedFilesView.text = DeltaVFS_missPathsSnapshot(120);
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
        verdict = isEnglishMode ? @"⚠️ Bundle calls seen, but ALL missing in Delta -> falling back to original every time" : @"⚠️ Có gọi bundle nhưng Delta thiếu HẾT -> luôn đọc bản gốc (Delta không phát huy tác dụng)";
    }

    // Chữ ký: Delta.zip / folder Delta có được ký vào app không (đọc CodeResources)
    NSString *signInfo = DeltaVFS_signatureSummary();

    // Thống kê chặn DNS/mạng
    unsigned long long dnsBlocked = DNSBlock_count();
    const char *dnsHostC = DNSBlock_lastHost();
    NSString *dnsHost = (dnsHostC && dnsHostC[0]) ? [NSString stringWithUTF8String:dnsHostC] : @"—";

    NSString *text = [NSString stringWithFormat:
        @"%@\n\n"
         "%@\n"
         "%@\n"
         "Delta: %@\n\n"
         "Tổng lời gọi file: %llu\n"
         "Trong bundle: %llu\n"
         "Hits  (đọc từ Delta): %llu\n"
         "Miss  (Delta thiếu -> đọc bản gốc): %llu\n"
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
- (void)toggleSpyStart:(UIControl *)sender {
    if ([(id)sender isOn]) {
        DylibSpy_startMonitoring();
    } else {
        [(id)sender setOn:DylibSpy_monitoringRequested()];
    }
}

- (void)toggleSpyMemWatch:(UIControl *)sender {
    DylibSpy_setMemWatchEnabled([(id)sender isOn]);
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
        [UIView animateWithDuration:0.25 delay:7.0 options:0 animations:^{
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
            [view isKindOfClass:[DeltaCheckbox class]] || // checkbox vuông mới - xem đầu file
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
// Ảnh 1x1 đặc màu - dùng để thay hẳn background/divider mặc định của UISegmentedControl bên
// dưới, vì chỉ đổi backgroundColor/selectedSegmentTintColor KHÔNG đủ để bỏ dải phân cách + bóng
// mờ hệ thống mà iOS vẫn tự vẽ - nhìn vẫn ra "control hệ thống", không phẳng như ảnh chụp thật.
- (UIImage *)imageWithColor:(UIColor *)color {
    CGRect rect = CGRectMake(0, 0, 1, 1);
    UIGraphicsBeginImageContextWithOptions(rect.size, NO, 0);
    CGContextRef ctx = UIGraphicsGetCurrentContext();
    CGContextSetFillColorWithColor(ctx, color.CGColor);
    CGContextFillRect(ctx, rect);
    UIImage *img = UIGraphicsGetImageFromCurrentImageContext();
    UIGraphicsEndImageContext();
    return img;
}

// Viên thuốc cam đặc, không dải phân cách/bóng mờ hệ thống - đúng ảnh chụp màn hình thật của
// Monite (mục 3l: "Luôn Bật"/"Khi Bắn/Ngắm"), khác hẳn UISegmentedControl mặc định của iOS.
- (void)styleSegmentedControl:(UISegmentedControl *)control {
    control.backgroundColor = [UIColor clearColor];
    [control setBackgroundImage:[self imageWithColor:COLOR_CARD_BG] forState:UIControlStateNormal barMetrics:UIBarMetricsDefault];
    [control setBackgroundImage:[self imageWithColor:COLOR_CYAN] forState:UIControlStateSelected barMetrics:UIBarMetricsDefault];
    [control setBackgroundImage:[self imageWithColor:COLOR_CARD_BG] forState:UIControlStateHighlighted barMetrics:UIBarMetricsDefault];
    UIImage *clearDivider = [self imageWithColor:[UIColor clearColor]];
    [control setDividerImage:clearDivider forLeftSegmentState:UIControlStateNormal rightSegmentState:UIControlStateNormal barMetrics:UIBarMetricsDefault];
    [control setDividerImage:clearDivider forLeftSegmentState:UIControlStateSelected rightSegmentState:UIControlStateNormal barMetrics:UIBarMetricsDefault];
    [control setDividerImage:clearDivider forLeftSegmentState:UIControlStateNormal rightSegmentState:UIControlStateSelected barMetrics:UIBarMetricsDefault];

    control.layer.cornerRadius = control.bounds.size.height > 0 ? control.bounds.size.height / 2.0f : 13.0f;
    control.clipsToBounds = YES;

    UIFont *segFont = [UIFont systemFontOfSize:11 weight:UIFontWeightBold];
    [control setTitleTextAttributes:@{NSForegroundColorAttributeName: COLOR_TEXT_DIM, NSFontAttributeName: segFont} forState:UIControlStateNormal];
    [control setTitleTextAttributes:@{NSForegroundColorAttributeName: COLOR_TEXT, NSFontAttributeName: segFont} forState:UIControlStateSelected];
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

// Card PHẲNG, tĩnh - đúng ảnh chụp màn hình thật của Monite (mục 3l): không viền, không thanh
// accent bên trái, nền không đổi màu khi bật/tắt - CHỈ có công tắc/checkbox tự đổi màu. Khác hẳn
// kiểu "cả card sáng lên" của thiết kế Delta cũ (xem applyCardVisualState: bên dưới, giờ chỉ còn
// no-op giữ cho các call site cũ không phải sửa, không còn hiệu ứng thật).
// Checkbox vuông (DeltaCheckbox, xem khai báo đầu file) - kiểu công tắc THẬT SỰ Monite dùng cho
// gần như mọi tính năng. Trả về UIControl* (không phải UISwitch*) - mọi action handler nhận nó
// qua tham số (UIControl *)sender rồi đọc/ghi qua [(id)sender isOn]/[(id)sender setOn:...]
// (bracket message send), xem "Mod tab toggle actions".
- (UIControl *)addToggleCardWithLocKey:(NSString *)key symbol:(NSString *)symbolName frame:(CGRect)frame action:(SEL)action toView:(UIView *)parent {
    UIView *card = [[UIView alloc] initWithFrame:frame];
    card.backgroundColor = COLOR_CARD_BG;
    card.layer.cornerRadius = 10.0f;
    [parent addSubview:card];

    CGFloat iconSize = 15;
    UIImageView *icon = [[UIImageView alloc] initWithFrame:CGRectMake(12, (frame.size.height - iconSize) / 2.0f, iconSize, iconSize)];
    icon.image = [[UIImage systemImageNamed:symbolName] imageByApplyingSymbolConfiguration:[UIImageSymbolConfiguration configurationWithPointSize:13 weight:UIImageSymbolWeightSemibold]];
    icon.contentMode = UIViewContentModeScaleAspectFit;
    icon.tintColor = COLOR_TEXT_DIM;
    icon.tag = kCardIconTag;
    icon.userInteractionEnabled = NO;
    [card addSubview:icon];

    CGFloat labelX = 12 + iconSize + 8;
    UILabel *label = [[UILabel alloc] initWithFrame:CGRectMake(labelX, 0, frame.size.width - labelX - 52, frame.size.height)];
    label.font = [UIFont systemFontOfSize:12 weight:UIFontWeightSemibold];
    label.textColor = COLOR_TEXT;
    label.adjustsFontSizeToFitWidth = YES;
    label.minimumScaleFactor = 0.7f;
    [card addSubview:label];
    __weak UILabel *weakLabel = label;
    [self addLocalizedRefresher:^{ weakLabel.text = LOC(key); }];

    CGFloat boxSize = 22;
    DeltaCheckbox *box = [[DeltaCheckbox alloc] initWithFrame:CGRectMake(frame.size.width - boxSize - 12, (frame.size.height - boxSize) / 2.0f, boxSize, boxSize)];
    [box addTarget:self action:action forControlEvents:UIControlEventValueChanged];
    [card addSubview:box];
    return box;
}

// Switch tròn kiểu iOS THẬT (không phải checkbox) - CHỈ dùng cho 2 mục "Chế độ Stream"/"Màu nhấn"
// ở tab Cài Đặt, đúng ảnh chụp màn hình thật của Monite (mục 3l) - mọi tính năng khác dùng
// checkbox vuông (addToggleCardWithLocKey: ở trên).
- (UISwitch *)addPillToggleCardWithLocKey:(NSString *)key symbol:(NSString *)symbolName frame:(CGRect)frame action:(SEL)action toView:(UIView *)parent {
    UIView *card = [[UIView alloc] initWithFrame:frame];
    card.backgroundColor = COLOR_CARD_BG;
    card.layer.cornerRadius = 10.0f;
    [parent addSubview:card];

    CGFloat iconSize = 15;
    UIImageView *icon = [[UIImageView alloc] initWithFrame:CGRectMake(12, (frame.size.height - iconSize) / 2.0f, iconSize, iconSize)];
    icon.image = [[UIImage systemImageNamed:symbolName] imageByApplyingSymbolConfiguration:[UIImageSymbolConfiguration configurationWithPointSize:13 weight:UIImageSymbolWeightSemibold]];
    icon.contentMode = UIViewContentModeScaleAspectFit;
    icon.tintColor = COLOR_TEXT_DIM;
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
    sw.tintColor = [UIColor colorWithWhite:1.0 alpha:0.3];
    sw.thumbTintColor = [UIColor colorWithWhite:0.94 alpha:1.0];
    CGSize swSize = sw.frame.size;
    sw.frame = CGRectMake(frame.size.width - swSize.width - 10, (frame.size.height - swSize.height) / 2.0f, swSize.width, swSize.height);
    [sw addTarget:self action:action forControlEvents:UIControlEventValueChanged];
    [card addSubview:sw];
    return sw;
}

- (void)refreshCardVisualState:(UIControl *)sender {
    [self applyCardVisualState:sender];
}

// Retheme Monite: card KHÔNG còn sáng lên khi bật nữa (bỏ đổi border/nền/accent/icon) - chỉ còn
// no-op giữ nguyên chữ ký hàm để các call site cũ (toggleAimHead:/toggleAimNheTam: tự tắt lẫn
// nhau...) không cần sửa. Trạng thái ON/OFF giờ CHỈ thể hiện qua chính công tắc/checkbox, đúng
// ảnh chụp màn hình thật của Monite (card luôn 1 màu, không đổi theo trạng thái).
- (void)applyCardVisualState:(UIControl *)sw {
    (void)sw;
}

- (void)updateMenu {
    // Nhịp tim ĐỘC LẬP với HWBreakHook - updateMenu chạy mỗi frame qua CADisplayLink trên main
    // thread, đòi hỏi main run loop vẫn đang bơm bình thường. debug.log gần đây cho thấy bộ đếm
    // HWBreakHook heartbeat dừng tăng rồi app "đứng" - nhưng KHÔNG rõ đó là do HWBreakHook thật
    // sự kẹt, hay đơn giản là hết traffic open() cần chặn (lành) và chỗ treo thật nằm ở nơi khác
    // hoàn toàn (mạng, DataDomeSDK, logic game...). Nếu dòng log này VẪN xuất hiện đều đặn ngay
    // cả sau khi HWBreakHook heartbeat đã dừng tăng, main thread/UI chắc chắn KHÔNG bị treo cứng -
    // tức HWBreakHook không phải thủ phạm, cần tìm chỗ khác. Nếu dòng này CŨNG dừng luôn thì main
    // thread mới thực sự là nạn nhân.
    static int mainHeartbeatTick = 0;
    if (++mainHeartbeatTick % 60 == 0) {
        DeltaVFS_debugLog("Main thread heartbeat: updateMenu vẫn đang chạy (CADisplayLink)");
    }

    // Đánh dấu mốc sảnh <-> trận trong debug.log - KHÔNG hook gì mới, chỉ đọc lại
    // game_sdk->Curent_Match() (hàm đã gọi an toàn sẵn cho Aim/ESP mỗi frame, không phải patch)
    // mỗi frame và chỉ log khi trạng thái THAY ĐỔI. Giúp đối chiếu thời điểm crash/bị đá với
    // đúng lúc vào/rời trận thay vì chỉ có "Main thread heartbeat" chung chung.
    static bool wasInMatch = false;
    bool isInMatch = game_sdk && game_sdk->Curent_Match && (game_sdk->Curent_Match() != NULL);
    if (isInMatch != wasInMatch) {
        DeltaVFS_debugLogf("Match state: %s", isInMatch ? "DA VAO TRAN (Curent_Match != null)" : "DA ROI TRAN / O SANH (Curent_Match == null)");
        wasInMatch = isInMatch;
    }

    // FFAntiObserve: đọc THUẦN TUÝ (không hook/patch) cờ kết quả phát hiện ffantihack.MFHPGMELLCC
    // mỗi frame, chỉ log khi có thay đổi - xem FFAntiObserve.h để biết vì sao chọn đọc thay vì hook.
    FFAntiObserve::CheckAndLog();

    // installFFAntiFlagsPatch() trì hoãn tới khi class ĐÃ init xong (xem giải thích ở +load) -
    // giả thuyết: patch quá sớm (lúc class chưa chạy xong static constructor) mới là lý do crash
    // 2 lần trước, không phải nội dung patch. Chỉ gọi ĐÚNG 1 LẦN.
    static bool ffantiPatchApplied = false;
    if (!ffantiPatchApplied && FFAntiObserve::IsReady()) {
        ffantiPatchApplied = true;
        DeltaVFS_debugLog("updateMenu: FFAntiObserve bao class da san sang, goi installFFAntiFlagsPatch() (tri hoan)");
        installFFAntiFlagsPatch();
    }

    _menuView.hidden = !MenDeal;

    get_players();

    if (!MenDeal) return;

    NSArray<UIControl *> *subSwitches = @[_boxSwitch, _linesSwitch, _nameSwitch, _healthSwitch, _distanceSwitch, _skeletonSwitch, _countSwitch, _showFovCircleSwitch];
    for (UIControl *sw in subSwitches) {
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
- (void)toggleEnable:(UIControl *)sender { Vars.Enable = [(id)sender isOn]; }
- (void)toggleBox:(UIControl *)sender { Vars.Box = [(id)sender isOn]; }
- (void)toggleLines:(UIControl *)sender { Vars.lines = [(id)sender isOn]; }
- (void)toggleName:(UIControl *)sender { Vars.Name = [(id)sender isOn]; }
- (void)toggleHealth:(UIControl *)sender { Vars.Health = [(id)sender isOn]; }
- (void)toggleDistance:(UIControl *)sender { Vars.Distance = [(id)sender isOn]; }
- (void)toggleSkeleton:(UIControl *)sender { Vars.skeleton = [(id)sender isOn]; }
- (void)toggleCount:(UIControl *)sender { Vars.counts = [(id)sender isOn]; }

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
//
// Mỗi hàm dưới đây giờ THỬ tra theo tên trước (Il2CppResolve.h - bền qua các bản update game
// vì không phụ thuộc RVA), NẾU THẤT BẠI (class/method không tồn tại, sai tên/argsCount, hoặc
// thiếu API il2cpp_* trong binary) thì tự động rơi về đúng RVA cứng cũ - KHÔNG có hàm nào bị
// NULL/gãy nếu resolve theo tên sai, an toàn tương đương bản cũ 100%. GetLocalPlayer's class bị
// Garena obfuscate tên ("EMKJHAJNPDH") - thử namespace rỗng trước, nếu sai vẫn rơi về RVA như
// bình thường (không tệ hơn trước khi có Il2CppResolve).
template <typename Fn>
static inline Fn ResolveOrFallback(const char *image, const char *ns, const char *cls,
                                    const char *method, int argc, uint64_t fallbackRVA) {
    void *resolved = Il2CppResolve::GetMethod(image, ns, cls, method, argc);
    if (resolved) {
        DeltaVFS_debugLogf("Il2CppResolve: %s.%s.%s OK theo ten (bo qua RVA 0x%llx cu)", ns, cls, method, (unsigned long long)fallbackRVA);
        return (Fn)resolved;
    }
    return (Fn)getRealOffset(fallbackRVA);
}
#define RESOLVE(FnType, image, ns, cls, method, argc, rva) ResolveOrFallback<FnType>(image, ns, cls, method, argc, rva)
#define IMG_ASM "Assembly-CSharp.dll"
#define IMG_UNITY_CORE "UnityEngine.CoreModule.dll"

void game_sdk_t::init()
{
    this->Curent_Match = RESOLVE(void *(*)(), IMG_ASM, "COW", "GameFacade", "CurrentMatch", 0, 0x55C4DA4);
    this->GetLocalPlayer = RESOLVE(void *(*)(void *), IMG_ASM, "", "EMKJHAJNPDH", "MBEDKMKBFIE", 0, 0x560E3DC);
    this->get_position = RESOLVE(Vector3(*)(void *), IMG_UNITY_CORE, "UnityEngine", "Transform", "get_position", 0, 0x91CA56C);
    this->Component_GetTransform = RESOLVE(void *(*)(void *), IMG_UNITY_CORE, "UnityEngine", "Component", "get_transform", 0, 0x91B82E4);
    this->get_camera = RESOLVE(void *(*)(), IMG_UNITY_CORE, "UnityEngine", "Camera", "get_main", 0, 0x915E9E4);
    // Named WorldToScreenPoint but Camera$$WorldToScreen::Regular (ESP.h) actually
    // expects normalized viewport coords (multiplies by screenWidth/Height itself),
    // so this must bind to Camera.WorldToViewportPoint, not the literal ScreenPoint method.
    this->WorldToScreenPoint = RESOLVE(Vector3(*)(void *, Vector3), IMG_UNITY_CORE, "UnityEngine", "Camera", "WorldToViewportPoint", 1, 0x915E364);
    this->GetForward = RESOLVE(Vector3(*)(void *), IMG_UNITY_CORE, "UnityEngine", "Transform", "get_forward", 0, 0x91CAF64);
    // Transform.set_forward(Vector3) - same Transform class as get_position/GetForward above (verified via dump.cs).
    // Setting this internally does rotation = Quaternion.LookRotation(value), so no manual quaternion math is needed for aim.
    this->set_forward = RESOLVE(void (*)(void *, Vector3), IMG_UNITY_CORE, "UnityEngine", "Transform", "set_forward", 1, 0x91CB024);
    // COW.GamePlay.Player.SetAimRotation(Quaternion, bool = true) - non-virtual instance
    // method on Player itself (found next to other Player-specific state methods like
    // get_InFallingState/get_InSwapWeaponCD). The reference in AimHead.md calls the
    // equivalent of this "set_aim(LocalPlayer, Quaternion)" to actually move the aim -
    // writing the camera's Transform directly (set_forward above) never worked for Aim
    // Head, which lines up with the game reading aim from the player's own state instead.
    this->set_aim = RESOLVE(void (*)(void *, Quaternion, bool), IMG_ASM, "COW.GamePlay", "Player", "SetAimRotation", 2, 0x53C4534);
    // Player.SetEAimAssitMode(EAimAssist) - non-virtual, used by Aim Magnet to force
    // the game's own built-in aim-assist always on (EAimAssist.AllOn = 0).
    this->set_aim_assist_mode = RESOLVE(void (*)(void *, int), IMG_ASM, "COW.GamePlay", "Player", "SetEAimAssitMode", 1, 0x53C1750);
    this->get_isLocalTeam = RESOLVE(bool (*)(void *), IMG_ASM, "COW", "GameFacade", "IsLocalTeammate", 1, 0x55C5AC0);
    this->get_IsDieing = RESOLVE(bool (*)(void *), IMG_ASM, "COW.GamePlay", "Player", "get_IsDieing", 0, 0x53AA18C);
    // Player.IsFiring()/get_IsSighting() - both non-virtual (no Slot: in dump.cs), safe
    // to call directly like set_aim. Used to gate Aim Head's "only while firing/scoped" mode.
    this->get_IsFiring = RESOLVE(bool (*)(void *), IMG_ASM, "COW.GamePlay", "Player", "IsFiring", 0, 0x53ACC9C);
    this->get_IsSighting = RESOLVE(bool (*)(void *), IMG_ASM, "COW.GamePlay", "Player", "get_IsSighting", 0, 0x53B769C);
    this->get_MaxHP = RESOLVE(int (*)(void *), IMG_ASM, "COW.GamePlay", "Player", "get_MaxHP", 0, 0x5435A3C);
    this->GetHp = RESOLVE(int (*)(void *), IMG_ASM, "COW.GamePlay", "Player", "get_CurHP", 0, 0x543592C);
    this->name = RESOLVE(monoString * (*)(void *), IMG_ASM, "COW.GamePlay", "Player", "get_NickName", 0, 0x53BE8E0);

    // GetHeadTF/GetHipTF are virtual (vtable slots 231/232). Player's own override
    // (0x60DDEF0/0x60DDF6C, found next to Player-specific methods like IsLocalTeammate)
    // looked like the theoretically correct fix for HeadY reading waist-height, but calling
    // it crashed the game on-device (reverted). Back to the base class's implementation,
    // which is at least stable - FindAimHeadTarget below no longer trusts its Y for aiming
    // anyway, using the root+1.6m estimate instead.
    this->_GetHeadPositions = RESOLVE(void *(*)(void *), IMG_ASM, "COW.GamePlay", "Player", "GetHeadPositions", 0, 0x54547E0);
    this->_newHipMods = RESOLVE(void *(*)(void *), IMG_ASM, "COW.GamePlay", "Player", "newHipMods", 0, 0x5454990);
    this->_GetLeftAnkleTF = RESOLVE(void *(*)(void *), IMG_ASM, "COW.GamePlay", "Player", "GetLeftAnkleTF", 0, 0x5454DE0);
    this->_GetRightAnkleTF = RESOLVE(void *(*)(void *), IMG_ASM, "COW.GamePlay", "Player", "GetRightAnkleTF", 0, 0x5454EEC);
    this->_GetLeftToeTF = RESOLVE(void *(*)(void *), IMG_ASM, "COW.GamePlay", "Player", "GetLeftToeTF", 0, 0x5454FF8);
    this->_GetRightToeTF = RESOLVE(void *(*)(void *), IMG_ASM, "COW.GamePlay", "Player", "GetRightToeTF", 0, 0x5455104);
    this->_getLeftHandTF = RESOLVE(void *(*)(void *), IMG_ASM, "COW.GamePlay", "Player", "getLeftHandTF", 0, 0x53C3608);
    this->_getRightHandTF = RESOLVE(void *(*)(void *), IMG_ASM, "COW.GamePlay", "Player", "getRightHandTF", 0, 0x53C370C);
    this->_getLeftForeArmTF = RESOLVE(void *(*)(void *), IMG_ASM, "COW.GamePlay", "Player", "getLeftForeArmTF", 0, 0x53C3810);
    this->_getRightForeArmTF = RESOLVE(void *(*)(void *), IMG_ASM, "COW.GamePlay", "Player", "getRightForeArmTF", 0, 0x53C3914);
}
#undef RESOLVE
#undef IMG_ASM
#undef IMG_UNITY_CORE
