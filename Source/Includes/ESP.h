#pragma once
#import <UIKit/UIKit.h>
#import <Foundation/Foundation.h>
#import <QuartzCore/QuartzCore.h>
#import <stdlib.h>

#import "UnityTypes.h"
#import "Vector3.h"
#import "Vector2.h"
#import "Quaternion.h"
#import "MemoryUtils.h"

// ===== BASIC STRUCTURES =====
struct SimpleVec2 {
    float x, y;
    SimpleVec2() : x(0), y(0) {}
    SimpleVec2(float x, float y) : x(x), y(y) {}
};

// Unified Red Color Constant (iOS 9+ Compatible)
#define kUnifiedRedColor [UIColor redColor].CGColor
// Health bar foreground bands - green/yellow/red by HP fraction (see drawHealthBarAt:),
// instead of a single fixed green, so the bar reads at a glance like the game's own UI.
#define kHealthGreenColor [UIColor colorWithRed:0.15 green:0.9 blue:0.3 alpha:1.0].CGColor
#define kHealthYellowColor [UIColor colorWithRed:0.95 green:0.8 blue:0.15 alpha:1.0].CGColor
#define kHealthRedColor [UIColor colorWithRed:0.95 green:0.2 blue:0.2 alpha:1.0].CGColor
#define kHealthYellowThreshold 0.3f
#define kHealthGreenThreshold 0.6f

// ===== SIMPLIFIED VARIABLES =====
struct Vars_t
{
    bool Enable = false;
    bool lines = false;
    bool Box = false;
    bool Name = false;
    bool Health = false;
    bool Distance = false;
    bool skeleton = false;
    bool counts = false;
    bool NoFog = true;
    bool AimHead = false;
    // A second, independent aim toggle - same targeting (closest-to-crosshair in FOV,
    // same Always/Fire-Scope mode and AimPreferLowHP below) but a higher vertical offset
    // (see kAimNheTamHeightOffset in ESP.h) than Aim Head's. Mutually exclusive with
    // AimHead at the UI level (toggling one off the other) since both drive the same
    // Player.SetAimRotation call - running both at once would just fight each other
    // every frame between the two heights/targets.
    bool AimNheTam = false;
    // 0 = Always (snap every frame a target is in FOV, current behavior), 1 = only while
    // actually firing or scoped (checked via game_sdk->get_IsFiring/get_IsSighting) -
    // mirrors the AimWhen gate from the AimHead.md reference. Lets the user pick between
    // the more "magnetic"/always-on feel and a less conspicuous, input-gated one.
    int AimHeadMode = 0;
    // When true, FindAimHeadTarget ignores "closest to crosshair" and instead picks
    // randomly among enemies below kWoundedHpFraction (yellow/red health bar, i.e.
    // already hurt) that are in FOV/range - falls back to the normal closest-to-
    // crosshair target if nobody in range is currently wounded.
    bool AimPreferLowHP = false;
    // Shared: ESP tab's FOV circle radius IS Aim Head's snap radius, but the two stay
    // separate switches - Show FOV Circle just draws the circle, Aim Head does the actual
    // person-detection + snap. Radius kept modest (well under half a typical screen width)
    // so the drawn circle stays fully on-screen - at the old default of 250 (slider allowed
    // up to 400) it was mostly clipped off both side edges on a ~400pt-wide screen.
    float AimFOV = 120.0f;
    bool ShowFOVCircle = false;
    // Continuously rotates the local player's own character (not the camera/aim) - see
    // ProcessSpinBot below. SpinSpeed is degrees per second, adjustable via the Mod tab
    // slider (how fast/slow the character spins).
    bool SpinBot = false;
    float SpinSpeed = 180.0f;
} Vars;

// ===== GAME SDK =====
class game_sdk_t
{
public:
    void init();
    void *(*Curent_Match)();
    void *(*GetLocalPlayer)(void *Game);
    Vector3 (*get_position)(void *player);
    void *(*Component_GetTransform)(void *player);
    void *(*get_camera)();
    Vector3 (*WorldToScreenPoint)(void *, Vector3);
    Vector3 (*GetForward)(void *player);
    void (*set_forward)(void *transform, Vector3 direction);
    // Player.SetAimRotation(Quaternion, bool) - sets the LOCAL PLAYER's own aim rotation
    // directly, rather than the camera's transform. Found via a working aimbot reference
    // (AimHead.md) that uses this exact pattern; writing to the camera's transform never
    // had any visible effect, which now makes sense if the game reads aim direction from
    // the player's own aim state (this function) rather than deriving it from the camera.
    void (*set_aim)(void *player, Quaternion rotation, bool sendToServer);
    bool (*get_isLocalTeam)(void *player);
    bool (*get_IsDieing)(void *player);
    // Non-virtual instance methods on Player (dump.cs OB54, no vtable Slot: annotation -
    // safe to call directly like set_aim). Used to gate Aim Head to "only while
    // firing/scoped" mode.
    bool (*get_IsFiring)(void *player);
    bool (*get_IsSighting)(void *player);
    int (*get_MaxHP)(void *player);
    int (*GetHp)(void *player);
    monoString *(*name)(void *player);

    void *(*_GetHeadPositions)(void *);
    void *(*_newHipMods)(void *);
    void *(*_GetLeftAnkleTF)(void *);
    void *(*_GetRightAnkleTF)(void *);
    void *(*_GetLeftToeTF)(void *);
    void *(*_GetRightToeTF)(void *);
    void *(*_getLeftHandTF)(void *);
    void *(*_getRightHandTF)(void *);
    void *(*_getLeftForeArmTF)(void *);
    void *(*_getRightForeArmTF)(void *);
};

extern game_sdk_t *game_sdk;

// ===== WORLD TO SCREEN HELPER =====
namespace Camera$$WorldToScreen
{
    // Parameterized on an explicit camera so callers that already have one (e.g.
    // FindAimHeadTarget below) don't need a redundant get_camera() lookup.
    inline SimpleVec2 FromCamera(void *cam, Vector3 pos)
    {
        if (!cam) return SimpleVec2(0, 0);
        Vector3 worldPoint = game_sdk->WorldToScreenPoint(cam, pos);

        if (worldPoint.z < 0.01f) return SimpleVec2(0, 0);

        CGRect screenBounds = [UIScreen mainScreen].nativeBounds;
        CGFloat screenWidth = screenBounds.size.width / [UIScreen mainScreen].nativeScale;
        CGFloat screenHeight = screenBounds.size.height / [UIScreen mainScreen].nativeScale;

        if (screenWidth < screenHeight) {
            CGFloat temp = screenWidth;
            screenWidth = screenHeight;
            screenHeight = temp;
        }

        float lx = screenWidth * worldPoint.x;
        float ly = screenHeight * (1.0f - worldPoint.y);

        return SimpleVec2(lx, ly);
    }

    inline SimpleVec2 Regular(Vector3 pos)
    {
        return FromCamera(game_sdk->get_camera(), pos);
    }
}

// ===== HELPERS =====
inline Vector3 getPosition(void *transform)
{
    if (!transform) return Vector3();
    return game_sdk->get_position(game_sdk->Component_GetTransform(transform));
}

inline Vector3 GetBonePosition(void *player, void *(*transformGetter)(void *)) {
    if (!player || !transformGetter) return Vector3();
    void *transform = transformGetter(player);
    if (!transform) return Vector3();
    void *tf = game_sdk->Component_GetTransform(transform);
    return tf ? game_sdk->get_position(tf) : Vector3();
}

// Roughly matches Free Fire's own health-bar color bands (green above this, yellow/red
// below it) - used by AimPreferLowHP to decide who counts as "already wounded".
#define kWoundedHpFraction 0.6f
#define kMaxWoundedCandidates 64

// Vertical offset from root used as the aim point, for each of the two independent aim
// toggles (AimHead / AimNheTam - see Vars_t). Both are geometric estimates, not read
// from a bone (the actual head-bone getter crashes on-device, see game_sdk_t::init()).
// 1.6m was Aim Head's original value, found to overshoot above the head, so Aim Head
// was tuned down to 1.3m - Aim Nhe Tam intentionally reuses the old 1.6m as its own
// distinct, higher aim point rather than being a regression of that fix.
#define kAimHeadHeightOffset 1.3f
#define kAimNheTamHeightOffset 1.6f

// ===== AIM HEAD TARGETING =====
// Shared by the ESP FOV circle (cosmetic) and the Camera.Render hook (the actual aim
// write) so both agree on exactly which enemy counts as "in FOV". Self-contained -
// re-fetches match/local player/players list itself rather than relying on state from
// the separate get_players() display-link loop, since the two run on different call paths.
inline bool FindAimHeadTarget(void *camera, Vector3 &outHeadWorldPos, float heightOffset = kAimHeadHeightOffset)
{
    if (!camera) return false;

    void *current_Match = game_sdk->Curent_Match();
    if (!current_Match) return false;

    void *local_player = game_sdk->GetLocalPlayer(current_Match);
    if (!local_player) return false;

    void *playersListAddr = (void*)getRealOffset(0x563CC18);
    if (!playersListAddr) return false;

    monoList<void **> *players = ((monoList<void **>* (*)(void*))playersListAddr)(current_Match);
    if (!players || !players->getItems()) return false;

    Vector3 localPos = getPosition(local_player);

    CGRect screenBounds = [UIScreen mainScreen].nativeBounds;
    CGFloat sW = screenBounds.size.width / [UIScreen mainScreen].nativeScale;
    CGFloat sH = screenBounds.size.height / [UIScreen mainScreen].nativeScale;
    if (sW < sH) { CGFloat t = sW; sW = sH; sH = t; }

    bool found = false;
    float bestScreenDist = Vars.AimFOV;

    // Collected in the same pass as the closest-to-crosshair search below, so
    // AimPreferLowHP costs nothing extra when it's off (the array just stays empty).
    Vector3 woundedHeads[kMaxWoundedCandidates];
    int woundedCount = 0;

    for (int u = 0; u < players->getSize(); u++) {
        void *enemy = players->getItems()[u];
        if (!enemy || enemy == local_player) continue;
        if (!game_sdk->Component_GetTransform(enemy)) continue;
        if (game_sdk->get_IsDieing(enemy) || game_sdk->get_isLocalTeam(enemy)) continue;

        Vector3 pos = getPosition(enemy);
        if (Vector3::Distance(pos, localPos) > 150.0f) continue;

        // Not calling _GetHeadPositions here at all: the "corrected" Player override offset
        // crashed on-device (reverted in game_sdk_t::init()), and the original base-class
        // offset only ever read waist height. This flat root+offset estimate is less
        // precise but stable - see kAimHeadHeightOffset/kAimNheTamHeightOffset above for
        // which value each caller passes in.
        Vector3 headWorld = pos + Vector3(0, heightOffset, 0);

        SimpleVec2 headScreen = Camera$$WorldToScreen::FromCamera(camera, headWorld);
        if (headScreen.x == 0 && headScreen.y == 0) continue;

        float dx = headScreen.x - sW / 2.0f;
        float dy = headScreen.y - sH / 2.0f;
        float screenDist = sqrtf(dx * dx + dy * dy);
        if (screenDist >= Vars.AimFOV) continue;

        if (screenDist < bestScreenDist) {
            bestScreenDist = screenDist;
            outHeadWorldPos = headWorld;
            found = true;
        }

        if (Vars.AimPreferLowHP && woundedCount < kMaxWoundedCandidates) {
            int maxHp = game_sdk->get_MaxHP(enemy);
            int hp = game_sdk->GetHp(enemy);
            if (maxHp > 0 && (float)hp / (float)maxHp < kWoundedHpFraction) {
                woundedHeads[woundedCount++] = headWorld;
            }
        }
    }

    // Random pick among the wounded takes priority over "closest to crosshair" - that's
    // the whole point of the mode. Falls back to the normal target if nobody's hurt.
    if (Vars.AimPreferLowHP && woundedCount > 0) {
        outHeadWorldPos = woundedHeads[arc4random_uniform((uint32_t)woundedCount)];
        return true;
    }

    return found;
}

// ===== SPIN BOT =====
// v2 tried writing Player.m_CurrentRotateDirection (dump.cs OB54 instance field,
// offset 0x1EAC) directly instead of the Transform, hoping the movement system would
// carry it into rotation itself - on-device it had zero effect at all. The field sits
// right next to m_LastGrabRotateDirLeft and near the SnowSlideGrabRotate/
// UpdateSnowSlideTurnEffect methods in dump.cs, which in hindsight means it's almost
// certainly specific to a "snow slide grab" mechanic, not general character facing -
// wrong guess, reverted.
// Back to v1: set_forward on the player's own Transform (Component_GetTransform(local_player),
// NOT the camera - unrelated to why set_forward never worked for aiming). Confirmed
// on-device this only visibly spins while standing completely still; while moving,
// firing, or jumping, the movement system's own per-frame rotation update overwrites
// it every frame, so it has no visible effect then. Finding the actual authoritative
// field/method for that would need more dump.cs digging and another on-device gamble -
// left as-is for now since the last two guesses were wrong (one crashed the game).
inline void ProcessSpinBot(void *local_player)
{
    if (!Vars.SpinBot || !local_player) return;

    void *transform = game_sdk->Component_GetTransform(local_player);
    if (!transform) return;

    // Frame-rate independent: accumulate by real elapsed time rather than a fixed
    // per-frame step, so SpinSpeed (deg/sec) means the same thing regardless of fps.
    static CFTimeInterval lastTime = 0;
    CFTimeInterval now = CACurrentMediaTime();
    CFTimeInterval dt = (lastTime > 0) ? (now - lastTime) : 0.0;
    lastTime = now;
    if (dt > 0.25) dt = 0.25; // clamp huge gaps (e.g. app was backgrounded)

    static float angleDeg = 0.0f;
    angleDeg += Vars.SpinSpeed * (float)dt;
    if (angleDeg >= 360.0f) angleDeg -= 360.0f;

    float rad = angleDeg * (float)M_PI / 180.0f;
    game_sdk->set_forward(transform, Vector3(sinf(rad), 0, cosf(rad)));
}

// ===== OPTIMIZED ESP RENDERER =====
@interface ESPRenderer : NSObject
@property (nonatomic, strong) CAShapeLayer *espLayer;
@property (nonatomic, strong) UIView *containerView;
@property (nonatomic, strong) NSMutableArray<CATextLayer *> *textPool;
@property (nonatomic, strong) NSMutableArray<CALayer *> *healthPool;
@property (nonatomic, assign) int textUsedCount;
@property (nonatomic, assign) int healthUsedCount;

+ (instancetype)sharedInstance;
- (void)renderOnView:(UIView *)view;
- (void)clearDrawings;
- (void)drawBoxFrom:(SimpleVec2)min to:(SimpleVec2)max path:(UIBezierPath *)path;
- (void)drawLineFrom:(SimpleVec2)from to:(SimpleVec2)to path:(UIBezierPath *)path;
- (void)drawFOVCircleAt:(SimpleVec2)center radius:(float)radius path:(UIBezierPath *)path;
- (void)drawTextAt:(SimpleVec2)position text:(NSString*)text;
- (void)drawHealthBarAt:(SimpleVec2)min to:(SimpleVec2)max multiplier:(float)multiplier;
@end

@implementation ESPRenderer

+ (instancetype)sharedInstance {
    static ESPRenderer *instance = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        instance = [[self alloc] init];
    });
    return instance;
}

- (instancetype)init {
    self = [super init];
    if (self) {
        _espLayer = [CAShapeLayer layer];
        _espLayer.name = @"ESP_Layer_Optimized";
        _espLayer.fillColor = [UIColor clearColor].CGColor;
        _espLayer.strokeColor = kUnifiedRedColor;
        _espLayer.lineWidth = 1.2f;
        _textPool = [NSMutableArray new];
        _healthPool = [NSMutableArray new];
    }
    return self;
}

- (void)renderOnView:(UIView *)view {
    if (!view) return;
    if (!_containerView || _containerView != view) {
        _containerView = view;
        [_containerView.layer addSublayer:_espLayer];
    }
    _espLayer.frame = view.bounds;
    _textUsedCount = 0;
    _healthUsedCount = 0;
}

- (void)clearDrawings {
    _espLayer.path = nil;
    for (CATextLayer *layer in _textPool) layer.hidden = YES;
    for (CALayer *layer in _healthPool) layer.hidden = YES;
}

- (void)drawBoxFrom:(SimpleVec2)min to:(SimpleVec2)max path:(UIBezierPath *)path {
    if (!path) return;
    [path moveToPoint:CGPointMake(min.x, min.y)];
    [path addLineToPoint:CGPointMake(max.x, min.y)];
    [path addLineToPoint:CGPointMake(max.x, max.y)];
    [path addLineToPoint:CGPointMake(min.x, max.y)];
    [path closePath];
}

- (void)drawLineFrom:(SimpleVec2)from to:(SimpleVec2)to path:(UIBezierPath *)path {
    if (!path) return;
    [path moveToPoint:CGPointMake(from.x, from.y)];
    [path addLineToPoint:CGPointMake(to.x, to.y)];
}

- (void)drawFOVCircleAt:(SimpleVec2)center radius:(float)radius path:(UIBezierPath *)path {
    if (!path) return;
    [path appendPath:[UIBezierPath bezierPathWithArcCenter:CGPointMake(center.x, center.y)
                                                     radius:radius
                                                 startAngle:0
                                                   endAngle:M_PI * 2
                                                  clockwise:YES]];
}

- (void)drawTextAt:(SimpleVec2)position text:(NSString*)text {
    if (!text || !_containerView) return;
    CATextLayer *textLayer;
    if (_textUsedCount < _textPool.count) {
        textLayer = _textPool[_textUsedCount];
    } else {
        textLayer = [CATextLayer layer];
        textLayer.fontSize = 9.0f;
        textLayer.foregroundColor = kUnifiedRedColor;
        textLayer.alignmentMode = kCAAlignmentCenter;
        textLayer.contentsScale = [UIScreen mainScreen].scale;
        [_containerView.layer addSublayer:textLayer];
        [_textPool addObject:textLayer];
    }
    textLayer.string = text;
    textLayer.hidden = NO;
    CGSize textSize = [text sizeWithAttributes:@{NSFontAttributeName: [UIFont systemFontOfSize:9.0f]}];
    textLayer.frame = CGRectMake(position.x - textSize.width/2, position.y, textSize.width + 4, textSize.height + 2);
    _textUsedCount++;
}

- (void)drawHealthBarAt:(SimpleVec2)min to:(SimpleVec2)max multiplier:(float)multiplier {
    if (!_containerView) return;
    CALayer *bgLayer;
    CALayer *fgLayer;
    int index = _healthUsedCount * 2;

    if (index + 1 < _healthPool.count) {
        bgLayer = _healthPool[index];
        fgLayer = _healthPool[index + 1];
    } else {
        bgLayer = [CALayer layer];
        bgLayer.backgroundColor = [UIColor colorWithWhite:0.05 alpha:0.85].CGColor;
        // Thin dark outline so the bar reads clearly over any background (grass, sky,
        // other players) instead of just a soft-edged color sliver.
        bgLayer.borderWidth = 1.0f;
        bgLayer.borderColor = [UIColor colorWithWhite:0.0 alpha:0.9].CGColor;
        [_containerView.layer addSublayer:bgLayer];
        [_healthPool addObject:bgLayer];

        fgLayer = [CALayer layer];
        fgLayer.backgroundColor = kHealthGreenColor;
        [_containerView.layer addSublayer:fgLayer];
        [_healthPool addObject:fgLayer];
    }

    // Green/yellow/red bands instead of a single fixed green - readable at a glance.
    if (multiplier < kHealthYellowThreshold) {
        fgLayer.backgroundColor = kHealthRedColor;
    } else if (multiplier < kHealthGreenThreshold) {
        fgLayer.backgroundColor = kHealthYellowColor;
    } else {
        fgLayer.backgroundColor = kHealthGreenColor;
    }

    // Widened from 2pt to 3pt - noticeably sharper/more visible without crowding the box.
    float barWidth = 3.0f;
    float height = max.y - min.y;
    bgLayer.frame = CGRectMake(min.x, min.y, barWidth, height);
    fgLayer.frame = CGRectMake(min.x, max.y - (height * multiplier), barWidth, height * multiplier);
    bgLayer.hidden = NO;
    fgLayer.hidden = NO;
    _healthUsedCount++;
}

@end

// ===== MAIN ESP FUNCTION =====
inline void get_players()
{
    // Safety check for game_sdk
    if (!game_sdk) return;

    // Ensure drawing happens on Main Thread
    dispatch_async(dispatch_get_main_queue(), ^{
        if (!Vars.Enable) {
            [[ESPRenderer sharedInstance] clearDrawings];
            return;
        }

        // No Fog (auto) -> UnityEngine.RenderSettings.set_fog(bool)
        if (Vars.NoFog) {
            void *noFogAddr = (void*)getRealOffset(0x917AFF8);
            if (noFogAddr) ((void (*)(bool))noFogAddr)(false);
        }

        void *current_Match = game_sdk->Curent_Match();
        if (!current_Match) {
            [[ESPRenderer sharedInstance] clearDrawings];
            return;
        }

        void *local_player = game_sdk->GetLocalPlayer(current_Match);
        if (!local_player) return;

        ProcessSpinBot(local_player);

        // EMKJHAJNPDH(match).GEPFGOHGOJI() -> List<Player>. Two equally-plausible
        // List<Player>-returning zero-arg candidates existed in the dump; this is the
        // first one found. If ESP shows nothing/wrong entities, try 0x563CF30 instead.
        void *playersListAddr = (void*)getRealOffset(0x563CC18);
        if (!playersListAddr) return;
        
        monoList<void **> *players = ((monoList<void **>* (*)(void*))playersListAddr)(current_Match);
        if (!players || !players->getItems()) return;

        UIWindow *keyWindow = [UIApplication sharedApplication].keyWindow;
        if (!keyWindow) return;

        // Health bar / box / line layers are bare CALayers (not view-backed), so every
        // .frame/.path assignment below implicitly animates (~0.25s default) unless
        // disabled. At 60fps that queues a new implicit animation on top of the last
        // one every single frame, which is what made the health bar visibly lag/judder
        // behind the target - most noticeable during recoil while firing, since the
        // enemy's screen position is changing fastest right then. Disabling actions for
        // this whole per-frame update makes every layer move snap immediately instead.
        [CATransaction begin];
        [CATransaction setDisableActions:YES];

        ESPRenderer *renderer = [ESPRenderer sharedInstance];
        [renderer renderOnView:keyWindow];
        [renderer clearDrawings];

        UIBezierPath *combinedPath = [UIBezierPath bezierPath];
        Vector3 localPos = getPosition(local_player);
        int drawnCount = 0;
        int totalEnemies = 0;

        CGRect screenBounds = [UIScreen mainScreen].nativeBounds;
        CGFloat sW = screenBounds.size.width / [UIScreen mainScreen].nativeScale;
        CGFloat sH = screenBounds.size.height / [UIScreen mainScreen].nativeScale;
        if (sW < sH) { CGFloat t = sW; sW = sH; sH = t; }

        // Tracer line now starts from the top of the screen instead of the bottom, per
        // user request.
        SimpleVec2 lineStart(sW / 2.0f, 15.0f);

        if (Vars.ShowFOVCircle) {
            [renderer drawFOVCircleAt:SimpleVec2(sW / 2.0f, sH / 2.0f) radius:Vars.AimFOV path:combinedPath];
        }

        // Aim Head write: calls Player.SetAimRotation directly on local_player (see
        // game_sdk_t::init() in Menu.mm) instead of writing the camera's Transform - the
        // game reads aim direction from the player's own aim state, not the camera.
        // Aim Head and Aim Nhe Tam are mutually exclusive at the UI level (toggling one
        // off the other - see toggleAimHead:/toggleAimNheTam: in Menu.mm), so at most one
        // of these is ever true; they only differ in aim-point height.
        // Only runs the (moderately expensive) target search when one of them is on, not
        // just because the FOV circle is showing.
        // Mode 1 (Fire/Scope only) also skips the target search entirely when neither is
        // true - besides matching what the user actually asked for, it's strictly less
        // per-frame work than Mode 0, not more.
        bool aimFeatureOn = Vars.AimHead || Vars.AimNheTam;
        bool aimHeadShouldRun = aimFeatureOn &&
            (Vars.AimHeadMode == 0 || game_sdk->get_IsFiring(local_player) || game_sdk->get_IsSighting(local_player));
        if (aimHeadShouldRun) {
            void *camera = game_sdk->get_camera();
            void *camTransform = camera ? game_sdk->Component_GetTransform(camera) : nullptr;
            Vector3 aimHeadWorld;
            float heightOffset = Vars.AimNheTam ? kAimNheTamHeightOffset : kAimHeadHeightOffset;
            if (camera && camTransform && FindAimHeadTarget(camera, aimHeadWorld, heightOffset)) {
                Vector3 eyePos = game_sdk->get_position(camTransform);
                Vector3 dir = Vector3::Normalized(aimHeadWorld - eyePos);
                Quaternion look = Quaternion::LookRotation(dir, Vector3(0, 1, 0));
                // sendToServer=false: this runs every frame while a target is in range.
                // true flooded a network RPC / tripped server-side anti-cheat rate
                // limiting (crashed the game); false keeps the rotation change local-only.
                game_sdk->set_aim(local_player, look, false);
            }
        }

        for (int u = 0; u < players->getSize(); u++) {
            void *enemy = players->getItems()[u];
            if (!enemy || enemy == local_player) continue;
            
            // Safety checks for enemy components
            if (!game_sdk->Component_GetTransform(enemy)) continue;
            if (game_sdk->get_IsDieing(enemy) || game_sdk->get_isLocalTeam(enemy)) continue;

            totalEnemies++;
            if (drawnCount >= 10) continue;

            Vector3 pos = getPosition(enemy);
            float distance = Vector3::Distance(pos, localPos);
            if (distance > 150.0f) continue;

            SimpleVec2 bot_pos = Camera$$WorldToScreen::Regular(pos);
            if (bot_pos.x == 0 && bot_pos.y == 0) continue;

            SimpleVec2 top_pos = Camera$$WorldToScreen::Regular(pos + Vector3(0, 1.8f, 0));
            float height = fabsf(bot_pos.y - top_pos.y);
            float width = height / 2.0f;

            if (Vars.Box) {
                [renderer drawBoxFrom:SimpleVec2(bot_pos.x - width/2, top_pos.y) to:SimpleVec2(bot_pos.x + width/2, bot_pos.y) path:combinedPath];
            }

            if (Vars.lines) {
                [renderer drawLineFrom:lineStart to:SimpleVec2(bot_pos.x, top_pos.y) path:combinedPath];
            }

            if (Vars.skeleton) {
                Vector3 bones[] = {
                    GetBonePosition(enemy, game_sdk->_GetHeadPositions),
                    GetBonePosition(enemy, game_sdk->_newHipMods),
                    GetBonePosition(enemy, game_sdk->_getLeftHandTF),
                    GetBonePosition(enemy, game_sdk->_getRightHandTF),
                    GetBonePosition(enemy, game_sdk->_GetLeftAnkleTF),
                    GetBonePosition(enemy, game_sdk->_GetRightAnkleTF)
                };
                SimpleVec2 sBones[6];
                for(int i=0; i<6; i++) sBones[i] = Camera$$WorldToScreen::Regular(bones[i]);
                
                if (sBones[0].x != 0) {
                    [renderer drawLineFrom:sBones[0] to:sBones[1] path:combinedPath]; // Head to Hip
                    [renderer drawLineFrom:sBones[1] to:sBones[4] path:combinedPath]; // Hip to LAnkle
                    [renderer drawLineFrom:sBones[1] to:sBones[5] path:combinedPath]; // Hip to RAnkle
                    [renderer drawLineFrom:sBones[0] to:sBones[2] path:combinedPath]; // Head to LHand
                    [renderer drawLineFrom:sBones[0] to:sBones[3] path:combinedPath]; // Head to RHand
                }
            }

            if (Vars.Health) {
                int hpVal = game_sdk->GetHp(enemy);
                int maxHpVal = game_sdk->get_MaxHP(enemy);
                if (maxHpVal > 0) {
                    float hp = (float)hpVal / (float)maxHpVal;
                    if (hp > 1.0f) hp = 1.0f;
                    if (hp < 0.0f) hp = 0.0f;
                    [renderer drawHealthBarAt:SimpleVec2(bot_pos.x - width/2 - 4, top_pos.y) to:SimpleVec2(bot_pos.x - width/2 - 4, bot_pos.y) multiplier:hp];
                }
            }

            if (Vars.Name) {
                monoString *pname = game_sdk->name(enemy);
                if (pname) {
                    NSString *nsName = pname->toNSString();
                    if (nsName) [renderer drawTextAt:SimpleVec2(bot_pos.x, top_pos.y - 12) text:nsName];
                }
            }

            if (Vars.Distance) {
                [renderer drawTextAt:SimpleVec2(bot_pos.x, bot_pos.y + 2) text:[NSString stringWithFormat:@"%.0fm", distance]];
            }

            drawnCount++;
        }

        if (Vars.counts) {
            [renderer drawTextAt:SimpleVec2(sW / 2, 40) text:[NSString stringWithFormat:@"Enemies: %d", totalEnemies]];
        }

        renderer.espLayer.path = combinedPath.CGPath;

        [CATransaction commit];
    });
}
