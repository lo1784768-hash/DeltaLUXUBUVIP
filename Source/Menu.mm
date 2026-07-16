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

#define kWidth  [UIScreen mainScreen].bounds.size.width
#define kHeight [UIScreen mainScreen].bounds.size.height

// Delta color identity - purple/cyan, replacing the old flat red accent
#define COLOR_BG [UIColor colorWithRed:0.03 green:0.02 blue:0.06 alpha:0.93]
#define COLOR_PURPLE [UIColor colorWithRed:0.659 green:0.333 blue:0.969 alpha:1.0]
#define COLOR_CYAN   [UIColor colorWithRed:0.133 green:0.827 blue:0.933 alpha:1.0]
#define COLOR_TEXT [UIColor whiteColor]
#define COLOR_TEXT_DIM [UIColor colorWithWhite:0.62 alpha:1.0]
#define COLOR_BTN_OFF [UIColor colorWithWhite:1.0 alpha:0.05]

@interface BrazilixMenu : NSObject <UITextFieldDelegate>
@property (nonatomic, strong) UIView *menuView;
@property (nonatomic, strong) UILabel *titleLabel;
@property (nonatomic, strong) CAGradientLayer *borderGradient;
@property (nonatomic, strong) CADisplayLink *displayLink;
@property (nonatomic, assign) CGPoint lastPoint;

// Tabs
@property (nonatomic, strong) UIView *tabBar;
@property (nonatomic, strong) NSArray<UIButton *> *tabButtons;
@property (nonatomic, strong) NSArray<UIView *> *tabUnderlines;
@property (nonatomic, strong) NSArray<UIView *> *tabPages;

// ESP tab
@property (nonatomic, strong) UIButton *enableCheatsButton;
@property (nonatomic, strong) UIButton *boxESPButton;
@property (nonatomic, strong) UIButton *linesESPButton;
@property (nonatomic, strong) UIButton *nameButton;
@property (nonatomic, strong) UIButton *healthButton;
@property (nonatomic, strong) UIButton *distanceButton;
@property (nonatomic, strong) UIButton *skeletonButton;
@property (nonatomic, strong) UIButton *countButton;

// Mod tab
@property (nonatomic, strong) UIView *modMainView;
@property (nonatomic, strong) UIView *modGocView;
@property (nonatomic, strong) UIView *modModView;
@property (nonatomic, strong) UIButton *hsCoButton;
@property (nonatomic, strong) UIButton *antenaButton;
@property (nonatomic, strong) UIButton *speedX2Button;
@property (nonatomic, strong) UIButton *speedX8Button;
@property (nonatomic, strong) UIButton *noRecoilButton;
@property (nonatomic, strong) UIButton *magicBulletButton;
@property (nonatomic, assign) BOOL hsCoOn;
@property (nonatomic, assign) BOOL antenaOn;
@property (nonatomic, assign) BOOL speedX2On;
@property (nonatomic, assign) BOOL speedX8On;
@property (nonatomic, assign) BOOL noRecoilOn;
@property (nonatomic, assign) BOOL magicBulletOn;
@property (nonatomic, assign) BOOL hasSelectedGoc;
@property (nonatomic, strong) NSArray<NSString *> *gocNames;
@property (nonatomic, strong) NSArray<NSString *> *gocHexes;
@property (nonatomic, strong) NSArray<NSString *> *modNames;
@property (nonatomic, strong) NSArray<NSString *> *modHexes;

// Info tab
@property (nonatomic, strong) UILabel *statusLabel;

// Scan tab (manual live search - Cheat Engine style)
@property (nonatomic, strong) UISegmentedControl *scanTypeControl;
@property (nonatomic, strong) UITextField *scanValueField;
@property (nonatomic, strong) UITextField *scanNewValueField;
@property (nonatomic, strong) UILabel *scanResultLabel;
@property (nonatomic, strong) UIButton *scanFindButton;
@property (nonatomic, strong) UIButton *scanNextButton;
@property (nonatomic, strong) UIButton *scanEditButton;
@property (nonatomic, strong) UIButton *scanClearButton;

// Toast
@property (nonatomic, strong) UILabel *toastLabel;
@end

@implementation BrazilixMenu

static BrazilixMenu *extraInfo;
static BOOL MenDeal;
UIWindow *mainWindow;
game_sdk_t *game_sdk = new game_sdk_t();

// Dedicated scanner for the manual Scan tab - separate from ModHacks' own internal
// scanners so the two tools can't clobber each other's in-progress result sets.
static MemScanner searchScanner;

+ (void)load {
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(3 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
        mainWindow = [UIApplication sharedApplication].keyWindow;
        extraInfo = [BrazilixMenu new];

        static bool sdkInitialized = false;
        if (!sdkInitialized) {
            game_sdk->init();
            sdkInitialized = true;
        }

        [extraInfo setupDisplayLink];
        [extraInfo initTapGes];
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

    CGFloat menuWidth = 260;
    CGFloat menuHeight = 380;
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
    [_menuView addGestureRecognizer:panGesture];

    [mainWindow addSubview:_menuView];
    [self installAnimatedBorder];

    _titleLabel = [[UILabel alloc] initWithFrame:CGRectMake(0, 8, menuWidth, 22)];
    _titleLabel.text = @"DELTA";
    _titleLabel.textColor = COLOR_PURPLE;
    _titleLabel.font = [UIFont systemFontOfSize:15 weight:UIFontWeightHeavy];
    _titleLabel.textAlignment = NSTextAlignmentCenter;
    [_menuView addSubview:_titleLabel];

    [self setupTabBarInView:_menuView width:menuWidth];

    CGRect contentFrame = CGRectMake(0, 66, menuWidth, menuHeight - 66 - 8);
    UIView *espPage = [self buildESPPageInFrame:contentFrame];
    UIView *modPage = [self buildModPageInFrame:contentFrame];
    UIView *infoPage = [self buildInfoPageInFrame:contentFrame];
    UIView *scanPage = [self buildScanPageInFrame:contentFrame];

    modPage.hidden = YES;
    infoPage.hidden = YES;
    scanPage.hidden = YES;

    [_menuView addSubview:espPage];
    [_menuView addSubview:modPage];
    [_menuView addSubview:infoPage];
    [_menuView addSubview:scanPage];

    _tabPages = @[espPage, modPage, infoPage, scanPage];
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

#pragma mark - Tab bar

- (void)setupTabBarInView:(UIView *)parent width:(CGFloat)width {
    _tabBar = [[UIView alloc] initWithFrame:CGRectMake(0, 32, width, 30)];
    [parent addSubview:_tabBar];

    NSArray<NSString *> *titles = @[@"ESP", @"MOD", @"INFO", @"SCAN"];
    CGFloat tabW = width / titles.count;
    NSMutableArray<UIButton *> *buttons = [NSMutableArray array];
    NSMutableArray<UIView *> *underlines = [NSMutableArray array];

    for (NSInteger i = 0; i < (NSInteger)titles.count; i++) {
        UIButton *tabBtn = [UIButton buttonWithType:UIButtonTypeCustom];
        tabBtn.frame = CGRectMake(i * tabW, 0, tabW, 30);
        tabBtn.tag = i;
        [tabBtn setTitle:titles[i] forState:UIControlStateNormal];
        tabBtn.titleLabel.font = [UIFont systemFontOfSize:11 weight:UIFontWeightBold];
        [tabBtn setTitleColor:(i == 0 ? COLOR_TEXT : COLOR_TEXT_DIM) forState:UIControlStateNormal];
        [tabBtn addTarget:self action:@selector(selectTab:) forControlEvents:UIControlEventTouchUpInside];
        [_tabBar addSubview:tabBtn];
        [buttons addObject:tabBtn];

        UIView *underline = [[UIView alloc] initWithFrame:CGRectMake(i * tabW + 14, 27, tabW - 28, 2)];
        underline.backgroundColor = COLOR_CYAN;
        underline.hidden = (i != 0);
        [_tabBar addSubview:underline];
        [underlines addObject:underline];
    }

    _tabButtons = buttons;
    _tabUnderlines = underlines;
}

- (void)selectTab:(UIButton *)sender {
    NSInteger idx = sender.tag;
    for (NSInteger i = 0; i < (NSInteger)_tabButtons.count; i++) {
        [_tabButtons[i] setTitleColor:(i == idx ? COLOR_TEXT : COLOR_TEXT_DIM) forState:UIControlStateNormal];
        _tabUnderlines[i].hidden = (i != idx);
        _tabPages[i].hidden = (i != idx);
    }
}

#pragma mark - ESP tab page

- (UIView *)buildESPPageInFrame:(CGRect)frame {
    UIScrollView *scroll = [[UIScrollView alloc] initWithFrame:frame];
    scroll.showsVerticalScrollIndicator = NO;

    CGFloat btnY = 0, btnH = 32, btnGap = 6, btnX = 12, btnW = frame.size.width - 24;

    _enableCheatsButton = [self createButtonWithTitle:@"Master Switch" frame:CGRectMake(btnX, btnY, btnW, btnH)];
    [_enableCheatsButton addTarget:self action:@selector(toggleEnable) forControlEvents:UIControlEventTouchUpInside];
    [scroll addSubview:_enableCheatsButton];
    btnY += btnH + btnGap;

    _boxESPButton = [self createButtonWithTitle:@"Box" frame:CGRectMake(btnX, btnY, btnW, btnH)];
    [_boxESPButton addTarget:self action:@selector(toggleBox) forControlEvents:UIControlEventTouchUpInside];
    [scroll addSubview:_boxESPButton];
    btnY += btnH + btnGap;

    _linesESPButton = [self createButtonWithTitle:@"Lines" frame:CGRectMake(btnX, btnY, btnW, btnH)];
    [_linesESPButton addTarget:self action:@selector(toggleLines) forControlEvents:UIControlEventTouchUpInside];
    [scroll addSubview:_linesESPButton];
    btnY += btnH + btnGap;

    _nameButton = [self createButtonWithTitle:@"Names" frame:CGRectMake(btnX, btnY, btnW, btnH)];
    [_nameButton addTarget:self action:@selector(toggleName) forControlEvents:UIControlEventTouchUpInside];
    [scroll addSubview:_nameButton];
    btnY += btnH + btnGap;

    _healthButton = [self createButtonWithTitle:@"Health" frame:CGRectMake(btnX, btnY, btnW, btnH)];
    [_healthButton addTarget:self action:@selector(toggleHealth) forControlEvents:UIControlEventTouchUpInside];
    [scroll addSubview:_healthButton];
    btnY += btnH + btnGap;

    _distanceButton = [self createButtonWithTitle:@"Distance" frame:CGRectMake(btnX, btnY, btnW, btnH)];
    [_distanceButton addTarget:self action:@selector(toggleDistance) forControlEvents:UIControlEventTouchUpInside];
    [scroll addSubview:_distanceButton];
    btnY += btnH + btnGap;

    _skeletonButton = [self createButtonWithTitle:@"Skeleton" frame:CGRectMake(btnX, btnY, btnW, btnH)];
    [_skeletonButton addTarget:self action:@selector(toggleSkeleton) forControlEvents:UIControlEventTouchUpInside];
    [scroll addSubview:_skeletonButton];
    btnY += btnH + btnGap;

    _countButton = [self createButtonWithTitle:@"Enemy Count" frame:CGRectMake(btnX, btnY, btnW, btnH)];
    [_countButton addTarget:self action:@selector(toggleCount) forControlEvents:UIControlEventTouchUpInside];
    [scroll addSubview:_countButton];
    btnY += btnH + btnGap;

    scroll.contentSize = CGSizeMake(frame.size.width, btnY + 10);
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
    CGFloat btnY = 0, btnH = 30, btnGap = 6, btnX = 4, btnW = frame.size.width - 8;

    _hsCoButton = [self createButtonWithTitle:@"Hs Cổ" frame:CGRectMake(btnX, btnY, btnW, btnH)];
    [_hsCoButton addTarget:self action:@selector(toggleHsCo) forControlEvents:UIControlEventTouchUpInside];
    [scroll addSubview:_hsCoButton];
    btnY += btnH + btnGap;

    _antenaButton = [self createButtonWithTitle:@"Antena" frame:CGRectMake(btnX, btnY, btnW, btnH)];
    [_antenaButton addTarget:self action:@selector(toggleAntena) forControlEvents:UIControlEventTouchUpInside];
    [scroll addSubview:_antenaButton];
    btnY += btnH + btnGap;

    _speedX2Button = [self createButtonWithTitle:@"Speed x2" frame:CGRectMake(btnX, btnY, btnW, btnH)];
    [_speedX2Button addTarget:self action:@selector(toggleSpeedX2) forControlEvents:UIControlEventTouchUpInside];
    [scroll addSubview:_speedX2Button];
    btnY += btnH + btnGap;

    _speedX8Button = [self createButtonWithTitle:@"Speed x8" frame:CGRectMake(btnX, btnY, btnW, btnH)];
    [_speedX8Button addTarget:self action:@selector(toggleSpeedX8) forControlEvents:UIControlEventTouchUpInside];
    [scroll addSubview:_speedX8Button];
    btnY += btnH + btnGap;

    _noRecoilButton = [self createButtonWithTitle:@"No Recoil" frame:CGRectMake(btnX, btnY, btnW, btnH)];
    [_noRecoilButton addTarget:self action:@selector(toggleNoRecoil) forControlEvents:UIControlEventTouchUpInside];
    [scroll addSubview:_noRecoilButton];
    btnY += btnH + btnGap;

    _magicBulletButton = [self createButtonWithTitle:@"Magic Bullet" frame:CGRectMake(btnX, btnY, btnW, btnH)];
    [_magicBulletButton addTarget:self action:@selector(toggleMagicBullet) forControlEvents:UIControlEventTouchUpInside];
    [scroll addSubview:_magicBulletButton];
    btnY += btnH + btnGap;

    UIButton *actionBtn = [self createButtonWithTitle:@"Hành Động" frame:CGRectMake(btnX, btnY, btnW, btnH)];
    [actionBtn setTitleColor:COLOR_CYAN forState:UIControlStateNormal];
    [actionBtn addTarget:self action:@selector(showGocList) forControlEvents:UIControlEventTouchUpInside];
    [scroll addSubview:actionBtn];
    btnY += btnH + btnGap;

    scroll.contentSize = CGSizeMake(frame.size.width, btnY + 6);
    return scroll;
}

- (UIView *)buildGocListInFrame:(CGRect)frame {
    UIScrollView *scroll = [[UIScrollView alloc] initWithFrame:frame];
    scroll.showsVerticalScrollIndicator = NO;
    CGFloat btnY = 0, btnH = 28, btnGap = 5, btnX = 4, btnW = frame.size.width - 8;

    UILabel *header = [[UILabel alloc] initWithFrame:CGRectMake(btnX, btnY, btnW, 16)];
    header.text = @"CHỌN HÀNH ĐỘNG GỐC";
    header.font = [UIFont systemFontOfSize:9 weight:UIFontWeightBold];
    header.textColor = COLOR_TEXT_DIM;
    [scroll addSubview:header];
    btnY += 20;

    for (NSInteger i = 0; i < (NSInteger)_gocNames.count; i++) {
        UIButton *btn = [self createButtonWithTitle:_gocNames[i] frame:CGRectMake(btnX, btnY, btnW, btnH)];
        btn.tag = i;
        [btn addTarget:self action:@selector(gocButtonTapped:) forControlEvents:UIControlEventTouchUpInside];
        [scroll addSubview:btn];
        btnY += btnH + btnGap;
    }

    UIButton *backBtn = [self createButtonWithTitle:@"Trở Về" frame:CGRectMake(btnX, btnY, btnW, btnH)];
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
    header.text = @"CHỌN HÀNH ĐỘNG MOD";
    header.font = [UIFont systemFontOfSize:9 weight:UIFontWeightBold];
    header.textColor = COLOR_TEXT_DIM;
    [scroll addSubview:header];
    btnY += 20;

    for (NSInteger i = 0; i < (NSInteger)_modNames.count; i++) {
        UIButton *btn = [self createButtonWithTitle:_modNames[i] frame:CGRectMake(btnX, btnY, btnW, btnH)];
        btn.tag = i;
        [btn addTarget:self action:@selector(modButtonTapped:) forControlEvents:UIControlEventTouchUpInside];
        [scroll addSubview:btn];
        btnY += btnH + btnGap;
    }

    UIButton *backBtn = [self createButtonWithTitle:@"Trở Về" frame:CGRectMake(btnX, btnY, btnW, btnH)];
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
    [self showToast:[NSString stringWithFormat:@"Đã chọn gốc: %@", name]];
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        ModHacks::selectGocAction(std::string([hex UTF8String]));
    });
    _modGocView.hidden = YES;
    _modModView.hidden = NO;
}

- (void)modButtonTapped:(UIButton *)sender {
    if (!_hasSelectedGoc) {
        [self showToast:@"Lỗi: Chưa chọn gốc!"];
        _modModView.hidden = YES;
        _modGocView.hidden = NO;
        return;
    }
    NSInteger idx = sender.tag;
    NSString *hex = _modHexes[idx];
    NSString *name = _modNames[idx];
    [self showToast:[NSString stringWithFormat:@"Mod thành công: %@", name]];
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        ModHacks::applyModAction(std::string([hex UTF8String]));
    });
}

#pragma mark - Mod tab toggle actions

- (void)toggleHsCo {
    _hsCoOn = !_hsCoOn;
    BOOL state = _hsCoOn;
    [self updateButton:_hsCoButton forState:state];
    [self showToast:state ? @"Hs Cổ ON" : @"Hs Cổ OFF"];
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        ModHacks::hsCo(state);
    });
}

- (void)toggleAntena {
    _antenaOn = !_antenaOn;
    BOOL state = _antenaOn;
    [self updateButton:_antenaButton forState:state];
    [self showToast:state ? @"Antena ON" : @"Antena OFF"];
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        ModHacks::antena(state);
    });
}

- (void)toggleSpeedX2 {
    _speedX2On = !_speedX2On;
    BOOL state = _speedX2On;
    [self updateButton:_speedX2Button forState:state];
    [self showToast:state ? @"Speed x2 ON" : @"Speed x2 OFF"];
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        ModHacks::speedX2(state);
    });
}

- (void)toggleSpeedX8 {
    _speedX8On = !_speedX8On;
    BOOL state = _speedX8On;
    [self updateButton:_speedX8Button forState:state];
    [self showToast:state ? @"Speed x8 ON" : @"Speed x8 OFF"];
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        ModHacks::speedX8(state);
    });
}

- (void)toggleNoRecoil {
    _noRecoilOn = !_noRecoilOn;
    BOOL state = _noRecoilOn;
    [self updateButton:_noRecoilButton forState:state];
    [self showToast:state ? @"No Recoil ON" : @"No Recoil OFF"];
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        ModHacks::noRecoil(state);
    });
}

- (void)toggleMagicBullet {
    _magicBulletOn = !_magicBulletOn;
    BOOL state = _magicBulletOn;
    [self updateButton:_magicBulletButton forState:state];
    [self showToast:state ? @"Magic Bullet ON" : @"Magic Bullet OFF"];
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        ModHacks::magicBullet(state);
    });
}

#pragma mark - Info tab page

- (UIView *)buildInfoPageInFrame:(CGRect)frame {
    UIView *page = [[UIView alloc] initWithFrame:frame];

    UIView *row = [[UIView alloc] initWithFrame:CGRectMake(4, 0, frame.size.width - 8, 34)];
    row.backgroundColor = [UIColor colorWithWhite:1.0 alpha:0.03];
    row.layer.cornerRadius = 6.0f;
    [page addSubview:row];

    UILabel *label = [[UILabel alloc] initWithFrame:CGRectMake(10, 0, row.frame.size.width * 0.5f, 34)];
    label.text = @"Trạng thái";
    label.font = [UIFont systemFontOfSize:11 weight:UIFontWeightBold];
    label.textColor = COLOR_TEXT_DIM;
    [row addSubview:label];

    _statusLabel = [[UILabel alloc] initWithFrame:CGRectMake(row.frame.size.width * 0.5f, 0, row.frame.size.width * 0.5f - 10, 34)];
    _statusLabel.text = @"Đã kích hoạt";
    _statusLabel.font = [UIFont systemFontOfSize:13 weight:UIFontWeightSemibold];
    _statusLabel.textColor = COLOR_CYAN;
    _statusLabel.textAlignment = NSTextAlignmentRight;
    [row addSubview:_statusLabel];

    return page;
}

#pragma mark - Scan tab page

- (UIView *)buildScanPageInFrame:(CGRect)frame {
    UIView *page = [[UIView alloc] initWithFrame:frame];
    CGFloat w = frame.size.width;
    CGFloat y = 0;

    _scanTypeControl = [[UISegmentedControl alloc] initWithItems:@[@"I32", @"I64", @"F32"]];
    _scanTypeControl.frame = CGRectMake(4, y, w - 8, 26);
    _scanTypeControl.selectedSegmentIndex = 0;
    [page addSubview:_scanTypeControl];
    y += 26 + 8;

    _scanValueField = [[UITextField alloc] initWithFrame:CGRectMake(4, y, w - 8, 30)];
    _scanValueField.placeholder = @"Giá trị hiện tại";
    _scanValueField.borderStyle = UITextBorderStyleRoundedRect;
    _scanValueField.keyboardType = UIKeyboardTypeNumbersAndPunctuation;
    _scanValueField.returnKeyType = UIReturnKeyDone;
    _scanValueField.delegate = self;
    [page addSubview:_scanValueField];
    y += 30 + 6;

    CGFloat halfW = (w - 8 - 6) / 2.0f;
    _scanFindButton = [self createButtonWithTitle:@"Tìm" frame:CGRectMake(4, y, halfW, 30)];
    [_scanFindButton addTarget:self action:@selector(scanFindTapped) forControlEvents:UIControlEventTouchUpInside];
    [page addSubview:_scanFindButton];

    _scanNextButton = [self createButtonWithTitle:@"Tìm Tiếp" frame:CGRectMake(4 + halfW + 6, y, halfW, 30)];
    [_scanNextButton addTarget:self action:@selector(scanNextTapped) forControlEvents:UIControlEventTouchUpInside];
    [page addSubview:_scanNextButton];
    y += 30 + 8;

    _scanResultLabel = [[UILabel alloc] initWithFrame:CGRectMake(4, y, w - 8, 18)];
    _scanResultLabel.text = @"Số kết quả: 0";
    _scanResultLabel.font = [UIFont systemFontOfSize:11 weight:UIFontWeightMedium];
    _scanResultLabel.textColor = COLOR_CYAN;
    _scanResultLabel.textAlignment = NSTextAlignmentCenter;
    [page addSubview:_scanResultLabel];
    y += 18 + 10;

    _scanNewValueField = [[UITextField alloc] initWithFrame:CGRectMake(4, y, w - 8, 30)];
    _scanNewValueField.placeholder = @"Giá trị mới";
    _scanNewValueField.borderStyle = UITextBorderStyleRoundedRect;
    _scanNewValueField.keyboardType = UIKeyboardTypeNumbersAndPunctuation;
    _scanNewValueField.returnKeyType = UIReturnKeyDone;
    _scanNewValueField.delegate = self;
    [page addSubview:_scanNewValueField];
    y += 30 + 6;

    _scanEditButton = [self createButtonWithTitle:@"Sửa Tất Cả" frame:CGRectMake(4, y, w - 8, 30)];
    [_scanEditButton setTitleColor:COLOR_CYAN forState:UIControlStateNormal];
    [_scanEditButton addTarget:self action:@selector(scanEditAllTapped) forControlEvents:UIControlEventTouchUpInside];
    [page addSubview:_scanEditButton];
    y += 30 + 6;

    _scanClearButton = [self createButtonWithTitle:@"Xóa Kết Quả" frame:CGRectMake(4, y, w - 8, 30)];
    [_scanClearButton setTitleColor:[UIColor colorWithRed:0.95 green:0.35 blue:0.45 alpha:0.85] forState:UIControlStateNormal];
    [_scanClearButton addTarget:self action:@selector(scanClearTapped) forControlEvents:UIControlEventTouchUpInside];
    [page addSubview:_scanClearButton];

    return page;
}

- (NSString *)scanSelectedType {
    NSArray<NSString *> *types = @[@"I32", @"I64", @"F32"];
    NSInteger idx = _scanTypeControl.selectedSegmentIndex;
    if (idx < 0 || idx >= (NSInteger)types.count) idx = 0;
    return types[idx];
}

- (void)updateScanResultLabel:(size_t)count {
    dispatch_async(dispatch_get_main_queue(), ^{
        self.scanResultLabel.text = [NSString stringWithFormat:@"Số kết quả: %zu", count];
    });
}

- (void)scanFindTapped {
    NSString *value = _scanValueField.text;
    NSString *type = [self scanSelectedType];
    if (value.length == 0) { [self showToast:@"Nhập giá trị trước"]; return; }
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        size_t count = searchScanner.searchNumber(std::string([value UTF8String]), std::string([type UTF8String]));
        [self updateScanResultLabel:count];
    });
}

- (void)scanNextTapped {
    NSString *value = _scanValueField.text;
    NSString *type = [self scanSelectedType];
    if (value.length == 0) { [self showToast:@"Nhập giá trị trước"]; return; }
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        size_t count = searchScanner.nextScan(std::string([value UTF8String]), std::string([type UTF8String]));
        [self updateScanResultLabel:count];
    });
}

- (void)scanEditAllTapped {
    NSString *value = _scanNewValueField.text;
    NSString *type = [self scanSelectedType];
    if (value.length == 0) { [self showToast:@"Nhập giá trị mới trước"]; return; }
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        bool ok = searchScanner.editAll(std::string([value UTF8String]), std::string([type UTF8String]));
        dispatch_async(dispatch_get_main_queue(), ^{
            [self showToast:ok ? @"Đã sửa xong" : @"Không có kết quả để sửa"];
        });
    });
}

- (void)scanClearTapped {
    searchScanner.clearResults();
    _scanResultLabel.text = @"Số kết quả: 0";
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

- (UIButton *)createButtonWithTitle:(NSString *)title frame:(CGRect)frame {
    UIButton *button = [UIButton buttonWithType:UIButtonTypeCustom];
    button.frame = frame;
    button.backgroundColor = COLOR_BTN_OFF;
    button.layer.cornerRadius = 6.0f;
    [button setTitle:title forState:UIControlStateNormal];
    [button setTitleColor:COLOR_TEXT_DIM forState:UIControlStateNormal];
    button.titleLabel.font = [UIFont systemFontOfSize:12 weight:UIFontWeightMedium];
    button.titleLabel.adjustsFontSizeToFitWidth = YES;
    return button;
}

- (void)updateMenu {
    _menuView.hidden = !MenDeal;

    get_players();

    if (!MenDeal) return;

    [self updateButton:_enableCheatsButton forState:Vars.Enable];

    NSArray *buttons = @[_boxESPButton, _linesESPButton, _nameButton, _healthButton, _distanceButton, _skeletonButton, _countButton];
    NSArray *states = @[@(Vars.Box), @(Vars.lines), @(Vars.Name), @(Vars.Health), @(Vars.Distance), @(Vars.skeleton), @(Vars.counts)];

    for (int i = 0; i < buttons.count; i++) {
        UIButton *btn = buttons[i];
        BOOL state = [states[i] boolValue];
        btn.alpha = Vars.Enable ? 1.0f : 0.4f;
        btn.userInteractionEnabled = Vars.Enable;
        [self updateButton:btn forState:state];
    }
}

- (void)updateButton:(UIButton *)button forState:(BOOL)state {
    if (state) {
        button.backgroundColor = [COLOR_PURPLE colorWithAlphaComponent:0.22];
        [button setTitleColor:COLOR_CYAN forState:UIControlStateNormal];
    } else {
        button.backgroundColor = COLOR_BTN_OFF;
        [button setTitleColor:COLOR_TEXT_DIM forState:UIControlStateNormal];
    }
}

#pragma mark - ESP Toggle Actions
- (void)toggleEnable { Vars.Enable = !Vars.Enable; }
- (void)toggleBox { if (Vars.Enable) Vars.Box = !Vars.Box; }
- (void)toggleLines { if (Vars.Enable) Vars.lines = !Vars.lines; }
- (void)toggleName { if (Vars.Enable) Vars.Name = !Vars.Name; }
- (void)toggleHealth { if (Vars.Enable) Vars.Health = !Vars.Health; }
- (void)toggleDistance { if (Vars.Enable) Vars.Distance = !Vars.Distance; }
- (void)toggleSkeleton { if (Vars.Enable) Vars.skeleton = !Vars.skeleton; }
- (void)toggleCount { if (Vars.Enable) Vars.counts = !Vars.counts; }

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

// OB53 OFFSETS - UnityFramework
void game_sdk_t::init()
{
    this->Curent_Match = (void *(*)())getRealOffset(0x4E355B0);
    this->GetLocalPlayer = (void *(*)(void *))getRealOffset(0x4C5A64C);
    this->get_position = (Vector3(*)(void *))getRealOffset(0x8552BAC);
    this->Component_GetTransform = (void *(*)(void *))getRealOffset(0x854060C);
    this->get_camera = (void *(*)())getRealOffset(0x84E7148);
    this->WorldToScreenPoint = (Vector3(*)(void *, Vector3))getRealOffset(0x84E6AC8);
    this->GetForward = (Vector3(*)(void *))getRealOffset(0x85534CC);
    this->get_isLocalTeam = (bool (*)(void *))getRealOffset(0x4A38D90);
    this->get_IsDieing = (bool (*)(void *))getRealOffset(0x4A02EA8);
    this->get_MaxHP = (int (*)(void *))getRealOffset(0x4A8489C);
    this->GetHp = (int (*)(void *))getRealOffset(0x4A8478C);
    this->name = (monoString * (*)(void *player))getRealOffset(0x4A16D38);

    this->_GetHeadPositions = (void *(*)(void *))getRealOffset(0x4AA1A28);
    this->_newHipMods = (void *(*)(void *))getRealOffset(0x4AA1BD8);
    this->_GetLeftAnkleTF = (void *(*)(void *))getRealOffset(0x4AA2028);
    this->_GetRightAnkleTF = (void *(*)(void *))getRealOffset(0x4AA2134);
    this->_GetLeftToeTF = (void *(*)(void *))getRealOffset(0x4AA2240);
    this->_GetRightToeTF = (void *(*)(void *))getRealOffset(0x4AA234C);
    this->_getLeftHandTF = (void *(*)(void *))getRealOffset(0x4A1B9B4);
    this->_getRightHandTF = (void *(*)(void *))getRealOffset(0x4A1BAB8);
    this->_getLeftForeArmTF = (void *(*)(void *))getRealOffset(0x4A1BBBC);
    this->_getRightForeArmTF = (void *(*)(void *))getRealOffset(0x4A1BCC0);
}
