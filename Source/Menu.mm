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

#define kWidth  [UIScreen mainScreen].bounds.size.width
#define kHeight [UIScreen mainScreen].bounds.size.height

// Minimalist Colors
#define COLOR_BG [UIColor colorWithWhite:0.05 alpha:0.92]
#define COLOR_ACCENT [UIColor colorWithRed:1.0 green:0.0 blue:0.0 alpha:1.0] // Red Accent
#define COLOR_TEXT [UIColor whiteColor]
#define COLOR_BTN_OFF [UIColor colorWithWhite:0.15 alpha:1.0]

@interface BrazilixMenu : NSObject
@property (nonatomic, strong) UIView *menuView;
@property (nonatomic, strong) UIScrollView *scrollView;
@property (nonatomic, strong) UIButton *enableCheatsButton;
@property (nonatomic, strong) UIButton *boxESPButton;
@property (nonatomic, strong) UIButton *linesESPButton;
@property (nonatomic, strong) UIButton *nameButton;
@property (nonatomic, strong) UIButton *healthButton;
@property (nonatomic, strong) UIButton *distanceButton;
@property (nonatomic, strong) UIButton *skeletonButton;
@property (nonatomic, strong) UIButton *countButton;
@property (nonatomic, strong) UILabel *titleLabel;
@property (nonatomic, strong) CADisplayLink *displayLink;
@property (nonatomic, assign) CGPoint lastPoint;
@end

@implementation BrazilixMenu

static BrazilixMenu *extraInfo;
static BOOL MenDeal;
UIWindow *mainWindow;
game_sdk_t *game_sdk = new game_sdk_t();

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

- (void)setupMenu {
    CGFloat menuWidth = 220;
    CGFloat menuHeight = 320;
    CGFloat x = (kWidth - menuWidth) * 0.5f;
    CGFloat y = (kHeight - menuHeight) * 0.5f;
    
    _menuView = [[UIView alloc] initWithFrame:CGRectMake(x, y, menuWidth, menuHeight)];
    _menuView.backgroundColor = COLOR_BG;
    _menuView.layer.cornerRadius = 12.0f;
    _menuView.layer.borderWidth = 0.5f;
    _menuView.layer.borderColor = [UIColor colorWithWhite:1.0 alpha:0.2].CGColor;
    _menuView.clipsToBounds = YES;
    _menuView.hidden = YES;
    _menuView.userInteractionEnabled = YES;
    
    UIPanGestureRecognizer *panGesture = [[UIPanGestureRecognizer alloc] initWithTarget:self action:@selector(handlePan:)];
    [_menuView addGestureRecognizer:panGesture];
    
    [mainWindow addSubview:_menuView];
    
    _titleLabel = [[UILabel alloc] initWithFrame:CGRectMake(0, 10, menuWidth, 25)];
    _titleLabel.text = @"Delta ESP";
    _titleLabel.textColor = COLOR_TEXT;
    _titleLabel.font = [UIFont systemFontOfSize:14 weight:UIFontWeightBold];
    _titleLabel.textAlignment = NSTextAlignmentCenter;
    [_menuView addSubview:_titleLabel];
    
    _scrollView = [[UIScrollView alloc] initWithFrame:CGRectMake(0, 45, menuWidth, menuHeight - 55)];
    _scrollView.showsVerticalScrollIndicator = NO;
    [_menuView addSubview:_scrollView];
    
    CGFloat btnY = 0;
    CGFloat btnH = 32;
    CGFloat btnGap = 6;
    CGFloat btnX = 12;
    CGFloat btnW = menuWidth - 24;
    
    _enableCheatsButton = [self createButtonWithTitle:@"Master Switch" frame:CGRectMake(btnX, btnY, btnW, btnH)];
    [_enableCheatsButton addTarget:self action:@selector(toggleEnable) forControlEvents:UIControlEventTouchUpInside];
    [_scrollView addSubview:_enableCheatsButton];
    btnY += btnH + btnGap;
    
    _boxESPButton = [self createButtonWithTitle:@"Box" frame:CGRectMake(btnX, btnY, btnW, btnH)];
    [_boxESPButton addTarget:self action:@selector(toggleBox) forControlEvents:UIControlEventTouchUpInside];
    [_scrollView addSubview:_boxESPButton];
    btnY += btnH + btnGap;
    
    _linesESPButton = [self createButtonWithTitle:@"Lines" frame:CGRectMake(btnX, btnY, btnW, btnH)];
    [_linesESPButton addTarget:self action:@selector(toggleLines) forControlEvents:UIControlEventTouchUpInside];
    [_scrollView addSubview:_linesESPButton];
    btnY += btnH + btnGap;
    
    _nameButton = [self createButtonWithTitle:@"Names" frame:CGRectMake(btnX, btnY, btnW, btnH)];
    [_nameButton addTarget:self action:@selector(toggleName) forControlEvents:UIControlEventTouchUpInside];
    [_scrollView addSubview:_nameButton];
    btnY += btnH + btnGap;
    
    _healthButton = [self createButtonWithTitle:@"Health" frame:CGRectMake(btnX, btnY, btnW, btnH)];
    [_healthButton addTarget:self action:@selector(toggleHealth) forControlEvents:UIControlEventTouchUpInside];
    [_scrollView addSubview:_healthButton];
    btnY += btnH + btnGap;
    
    _distanceButton = [self createButtonWithTitle:@"Distance" frame:CGRectMake(btnX, btnY, btnW, btnH)];
    [_distanceButton addTarget:self action:@selector(toggleDistance) forControlEvents:UIControlEventTouchUpInside];
    [_scrollView addSubview:_distanceButton];
    btnY += btnH + btnGap;
    
    _skeletonButton = [self createButtonWithTitle:@"Skeleton" frame:CGRectMake(btnX, btnY, btnW, btnH)];
    [_skeletonButton addTarget:self action:@selector(toggleSkeleton) forControlEvents:UIControlEventTouchUpInside];
    [_scrollView addSubview:_skeletonButton];
    btnY += btnH + btnGap;
    
    _countButton = [self createButtonWithTitle:@"Enemy Count" frame:CGRectMake(btnX, btnY, btnW, btnH)];
    [_countButton addTarget:self action:@selector(toggleCount) forControlEvents:UIControlEventTouchUpInside];
    [_scrollView addSubview:_countButton];
    btnY += btnH + btnGap;
    
    _scrollView.contentSize = CGSizeMake(menuWidth, btnY + 10);
}

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
    [button setTitleColor:[UIColor colorWithWhite:0.8 alpha:1.0] forState:UIControlStateNormal];
    button.titleLabel.font = [UIFont systemFontOfSize:13 weight:UIFontWeightMedium];
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
        button.backgroundColor = [COLOR_ACCENT colorWithAlphaComponent:0.3];
        [button setTitleColor:COLOR_ACCENT forState:UIControlStateNormal];
    } else {
        button.backgroundColor = COLOR_BTN_OFF;
        [button setTitleColor:[UIColor colorWithWhite:0.8 alpha:1.0] forState:UIControlStateNormal];
    }
}

#pragma mark - Toggle Actions
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
