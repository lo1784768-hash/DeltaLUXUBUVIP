#pragma once
#import <UIKit/UIKit.h>
#import <Foundation/Foundation.h>
#import <QuartzCore/QuartzCore.h>

#import "UnityTypes.h"
#import "Vector3.h"
#import "Vector2.h"
#import "MemoryUtils.h"

// ===== BASIC STRUCTURES =====
struct SimpleVec2 {
    float x, y;
    SimpleVec2() : x(0), y(0) {}
    SimpleVec2(float x, float y) : x(x), y(y) {}
};

// Unified Red Color Constant (iOS 9+ Compatible)
#define kUnifiedRedColor [UIColor redColor].CGColor

// ===== SIMPLIFIED VARIABLES =====
struct Vars_t
{
    bool Enable = true;
    bool lines = false;
    bool Box = false;
    bool Name = false;
    bool Health = false;
    bool Distance = false;
    bool skeleton = false;
    bool counts = false;
    bool NoFog = true;
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
    bool (*get_isLocalTeam)(void *player);
    bool (*get_IsDieing)(void *player);
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
    inline SimpleVec2 Regular(Vector3 pos)
    {
        auto cam = game_sdk->get_camera();
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
        bgLayer.backgroundColor = [UIColor colorWithWhite:0.2 alpha:0.8].CGColor;
        [_containerView.layer addSublayer:bgLayer];
        [_healthPool addObject:bgLayer];
        
        fgLayer = [CALayer layer];
        fgLayer.backgroundColor = kUnifiedRedColor;
        [_containerView.layer addSublayer:fgLayer];
        [_healthPool addObject:fgLayer];
    }
    
    float height = max.y - min.y;
    bgLayer.frame = CGRectMake(min.x, min.y, 2, height);
    fgLayer.frame = CGRectMake(min.x, max.y - (height * multiplier), 2, height * multiplier);
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

        // No Fog (auto)
        if (Vars.NoFog) {
            void *noFogAddr = (void*)getRealOffset(0x850363C);
            if (noFogAddr) ((void (*)(bool))noFogAddr)(false);
        }

        void *current_Match = game_sdk->Curent_Match();
        if (!current_Match) {
            [[ESPRenderer sharedInstance] clearDrawings];
            return;
        }

        void *local_player = game_sdk->GetLocalPlayer(current_Match);
        if (!local_player) return;

        void *playersListAddr = (void*)getRealOffset(0x4C869DC);
        if (!playersListAddr) return;
        
        monoList<void **> *players = ((monoList<void **>* (*)(void*))playersListAddr)(current_Match);
        if (!players || !players->getItems()) return;

        UIWindow *keyWindow = [UIApplication sharedApplication].keyWindow;
        if (!keyWindow) return;

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

        SimpleVec2 lineStart(sW / 2.0f, sH - 15.0f);

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
    });
}
