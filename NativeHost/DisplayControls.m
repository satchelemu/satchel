#import "DisplayControls.h"
#import "InGameMenu.h"
#import <GameController/GameController.h>
#import <objc/runtime.h>
#include <stdio.h>
#include <string.h>

void mtl_grab_next_present(void (^callback)(CGImageRef image));
void dm_gamepad_reproject(void);
void dm_gamepad_reset(void);
static NSInteger posture;
static NSInteger overridePosture = -1;
static CGFloat hingeAngle;
static NSInteger effectivePosture(void) { return overridePosture >= 0 ? overridePosture : posture; }
static CGFloat pictureAspect = 16.0 / 9.0;   // main thread
CGFloat SatchelPictureAspect(void) { return pictureAspect; }
CGRect SatchelCurrentPictureRect(CGRect region) { return SatchelPictureRectAspect(region, SatchelScreenMode(), pictureAspect); }
CGRect SatchelGameBounds(CGRect bounds) { return SatchelFoldRegion(bounds, effectivePosture() == 2, NO); }
CGRect SatchelControlBounds(CGRect bounds) { return SatchelFoldRegion(bounds, effectivePosture() == 2, YES); }

CGRect SatchelInteractiveBounds(UIView *view, CGRect region) {
    UIEdgeInsets safe = view.safeAreaInsets;
    CGRect result = region;
    // Vertical system areas are independent of the symmetric horizontal camera insets.
    CGFloat top = CGRectGetMinY(region) == CGRectGetMinY(view.bounds) ? safe.top : 0;
    CGFloat bottom = CGRectGetMaxY(region) == CGRectGetMaxY(view.bounds) ? safe.bottom : 0;
    result = UIEdgeInsetsInsetRect(result, UIEdgeInsetsMake(top, 0, bottom, 0));
    if (@available(iOS 27.1, *)) {
        NSArray<UIViewReservedRegion *> *regions = [view reservedRegionsOfKind:UIViewReservedRegionKind.occlusionRegionKind];
        for (UIViewReservedRegion *r in regions) {
            if (!r.active) continue;
            // UIKit can report the cutout and a containing interactive exclusion.
            // Reserve the containing frame once, so a corner camera cannot add two gutters.
            BOOL contained = NO;
            for (UIViewReservedRegion *outer in regions)
                if (outer != r && outer.active && !CGRectEqualToRect(outer.frame, r.frame) && CGRectContainsRect(outer.frame, r.frame)) contained = YES;
            if (!contained) result = SatchelOcclusionSafeRect(result, view.bounds, r.frame);
        }
    } else {
        // Earlier systems have no per-side occlusion API. Assume a portrait-top camera
        // on this window's current screen, then rotate that edge with the interface.
        UIInterfaceOrientation orientation = view.window.windowScene.effectiveGeometry.interfaceOrientation;
        CGFloat camera = MAX(safe.left, safe.right);
        result = UIEdgeInsetsInsetRect(result, UIEdgeInsetsMake(0,
            orientation == UIInterfaceOrientationLandscapeRight ? camera : 0, 0,
            orientation == UIInterfaceOrientationLandscapeLeft ? camera : 0));
    }
    return result;
}

// Sees every touch on the game screen without taking it, so the pause button can brighten.
@interface SatchelTouchWatcher : UIGestureRecognizer
@property (nonatomic, copy) void (^touched)(void);
@end
@implementation SatchelTouchWatcher
- (void)touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    if (self.touched) self.touched();
    self.state = UIGestureRecognizerStateFailed;
}
@end

@interface SatchelDisplayControls : NSObject <UIGestureRecognizerDelegate>
@property (nonatomic, weak) UIView *root;
@property (nonatomic, weak) UIView *game;
@property (nonatomic, weak) UIView *pad;
@property (nonatomic, strong) UIImage *pausedFrame;
@property (nonatomic, copy) NSString *lastGeometry;
@property (nonatomic, strong) NSTimer *geometryTimer;
@property (nonatomic) BOOL capturePending;
@property (nonatomic) NSUInteger captureGeneration;
@property (nonatomic, strong) NSMutableArray *captureCompletions;
@property (nonatomic, strong) UIButton *pauseButton;
@property (nonatomic, strong) NSTimer *pauseFade;
- (void)changed;
- (void)geometryChanged;
- (void)openMenu;
- (void)toggleScreen;
- (void)screenshot:(NSString *)filename;
- (void)capture:(void (^)(void))completion;
@end
static __weak SatchelDisplayControls *activeControls;
@implementation SatchelDisplayControls
- (void)dealloc { [NSNotificationCenter.defaultCenter removeObserver:self]; [self.geometryTimer invalidate]; [self.pauseFade invalidate]; }
- (BOOL)gestureRecognizer:(UIGestureRecognizer *)gesture shouldRecognizeSimultaneouslyWithGestureRecognizer:(UIGestureRecognizer *)other { return YES; }
- (BOOL)gestureRecognizer:(UIGestureRecognizer *)gesture shouldReceiveTouch:(UITouch *)touch { return !SatchelMenuBlocksInput(); }
- (void)toggleScreen { SatchelChangeScreenMode(SatchelScreenMode() == 0 ? @"stretch" : @"fit"); }
- (void)openMenu { SatchelMenuPerform(@"open", nil); }
// A visible way into the menu: a small pause button in the top row of the touch controls,
// right of the + key, where the pad has no control. It uses the pad's cutout-aware bounds,
// stays while a controller is connected, and dims after a few seconds without touches.
- (void)installPauseButton {
    UIButton *button = [UIButton buttonWithType:UIButtonTypeCustom];
    UIButtonConfiguration *config = UIButtonConfiguration.filledButtonConfiguration;
    config.image = [UIImage systemImageNamed:@"pause.fill" withConfiguration:
        [UIImageSymbolConfiguration configurationWithPointSize:13 weight:UIImageSymbolWeightBlack]];
    config.baseForegroundColor = UIColor.whiteColor;
    // Satchel's shell (#111318) as tinted glass, with a hairline edge.
    config.baseBackgroundColor = [UIColor colorWithRed:0.067 green:0.075 blue:0.094 alpha:0.62];
    config.background.strokeColor = [UIColor colorWithWhite:1 alpha:0.18];
    config.background.strokeWidth = 1;
    config.cornerStyle = UIButtonConfigurationCornerStyleCapsule;
    button.configuration = config;
    button.accessibilityLabel = @"Pause menu";
    button.accessibilityIdentifier = @"game.pause";
    [button addAction:[UIAction actionWithHandler:^(UIAction *unused) {
        fprintf(stderr, "Satchel menu: pause button\n");
        SatchelMenuPerform(@"open", nil);
    }] forControlEvents:UIControlEventTouchUpInside];
    self.pauseButton = button;
    [self.root addSubview:button];
    __weak SatchelDisplayControls *weakSelf = self;
    SatchelTouchWatcher *watcher = [SatchelTouchWatcher new];
    watcher.touched = ^{ [weakSelf brightenPause]; };
    watcher.cancelsTouchesInView = NO; watcher.delaysTouchesBegan = NO; watcher.delaysTouchesEnded = NO;
    watcher.delegate = self; [self.root addGestureRecognizer:watcher];
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(brightenPause) name:GCControllerDidConnectNotification object:nil];
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(menuChanged) name:SatchelMenuDidChange object:nil];
    [self brightenPause];
}
- (void)layoutPause {
    UIView *root = self.root; if (!root || !self.pauseButton) return;
    CGRect b = root.bounds, controls = SatchelControlBounds(b), s = SatchelInteractiveBounds(root, controls);
    // DmGamepad.m's unit (its -u), from the room its controls get (half the screen when half folded).
    CGFloat u = MIN(MIN(s.size.height / 430.0, 1.3) * SatchelTouchSize(), MIN(s.size.height / 330.0, s.size.width / 650.0));
    CGFloat side = MAX(28, round(36 * MIN(u, 1)));
    // DmGamepad.m's top row: − / + are 52 units either side of the middle, 50 wide, 22 down.
    // The button sits 12 points right of the + key, level with it.
    CGPoint c = CGPointMake(CGRectGetMidX(s) + 77 * u + 12 + side / 2, CGRectGetMinY(s) + 22 * u);
    self.pauseButton.frame = CGRectMake(round(c.x - side / 2), round(c.y - side / 2), side, side);
    [root bringSubviewToFront:self.pauseButton]; // hidden while the menu is up
}
- (void)menuChanged {
    NSString *state = SatchelMenuState()[@"state"];
    if ([state isEqualToString:@"ended"]) {
        self.captureGeneration++; self.capturePending = NO; self.pausedFrame = nil;
        [self.captureCompletions removeAllObjects]; [self.pauseFade invalidate]; self.pauseFade = nil;
    }
    BOOL running = [state isEqualToString:@"running"];
    self.pauseButton.hidden = !running;
    if (running) { [self layoutPause]; [self brightenPause]; } // touch size may have changed
}
- (void)brightenPause {
    [self.pauseFade invalidate];
    [UIView animateWithDuration:0.15 animations:^{ self.pauseButton.alpha = 0.9; }];
    __weak SatchelDisplayControls *weakSelf = self;
    self.pauseFade = [NSTimer scheduledTimerWithTimeInterval:4 repeats:NO block:^(NSTimer *timer) {
        [UIView animateWithDuration:0.6 animations:^{ weakSelf.pauseButton.alpha = 0.32; }];
    }];
}
- (void)changed {
    [self.root setNeedsLayout]; [self.root layoutIfNeeded];
    dm_gamepad_reproject();
}
- (void)geometryChanged {
    UIWindowScene *scene = self.root.window.windowScene; UIScreen *screen = scene.screen;
    CGRect b = self.root.bounds; UIEdgeInsets safe = self.root.safeAreaInsets;
    CGRect interactive = SatchelInteractiveBounds(self.root, SatchelControlBounds(b));
    NSString *geometry = [NSString stringWithFormat:@"%p %@ %@ %.2f %ld %@ %ld %ld %@", screen,
        NSStringFromCGRect(b), NSStringFromCGRect(screen.nativeBounds), screen.scale,
        (long)scene.effectiveGeometry.interfaceOrientation, NSStringFromUIEdgeInsets(safe),
        (long)effectivePosture(), (long)overridePosture, NSStringFromCGRect(interactive)];
    if ([geometry isEqualToString:self.lastGeometry]) return;
    self.lastGeometry = geometry;
    [self.root setNeedsLayout]; [self.pad setNeedsLayout];
    // Keep held controls owned until touch-up; reproject game touches into the new picture.
    dm_gamepad_reproject();
    if (@available(iOS 27.1, *)) {
        for (UIViewReservedRegion *r in [self.root reservedRegionsOfKind:UIViewReservedRegionKind.occlusionRegionKind])
            fprintf(stderr, "Satchel cutout: source=UIKit active=%d frame=%s margins=%s\n", r.active,
                NSStringFromCGRect(r.frame).UTF8String, NSStringFromUIEdgeInsets(r.margins).UTF8String);
    }
    CGRect controls = SatchelControlBounds(b);
    fprintf(stderr, "Satchel controls: region=%s inset-left=%.1f inset-right=%.1f thumb-margin=8 source=%s\n",
        NSStringFromCGRect(controls).UTF8String, CGRectGetMinX(interactive) - CGRectGetMinX(controls),
        CGRectGetMaxX(controls) - CGRectGetMaxX(interactive),
        NSProcessInfo.processInfo.operatingSystemVersion.majorVersion >= 27 ? "UIKit-regions" : "orientation-assumption");
    const char *names[] = {"unknown", "closed", "half", "open"};
    NSInteger p = effectivePosture();
    fprintf(stderr, "Satchel posture: source=%s posture=%s angle=%.3f screen=%p points=%.0fx%.0f pixels=%.0fx%.0f scale=%.2f rotation=%ld safe=%.0f,%.0f,%.0f,%.0f\n",
        overridePosture >= 0 ? "test-override" : "UIKit", p >= 0 && p <= 3 ? names[p] : "unknown", hingeAngle,
        screen, b.size.width, b.size.height, screen.nativeBounds.size.width, screen.nativeBounds.size.height,
        screen.scale, (long)scene.effectiveGeometry.interfaceOrientation, safe.top, safe.left, safe.bottom, safe.right);
}
- (void)capture:(void (^)(void))completion {
    if (completion) [self.captureCompletions addObject:[completion copy]];
    if (self.capturePending) return;
    self.capturePending = YES;
    NSUInteger generation = self.captureGeneration;
    __weak SatchelDisplayControls *weakSelf = self;
    mtl_grab_next_present(^(CGImageRef image) {
        UIImage *frame = image ? [UIImage imageWithCGImage:image] : nil;
        dispatch_async(dispatch_get_main_queue(), ^{
            SatchelDisplayControls *self = weakSelf; if (!self || self.captureGeneration != generation) return;
            self.capturePending = NO; self.pausedFrame = frame;
            NSArray *callbacks = [self.captureCompletions copy]; [self.captureCompletions removeAllObjects];
            for (void (^callback)(void) in callbacks) callback();
        });
    });
}
- (void)compose:(NSString *)filename {
    if (![SatchelMenuState()[@"attached"] boolValue]) { fprintf(stderr, "Satchel screen shot: surface detached, use menu return-to-game first\n"); return; }
    [self.root layoutIfNeeded];
    CGRect bounds = self.root.bounds, picture = self.game.frame;
    UIGraphicsImageRendererFormat *format = [UIGraphicsImageRendererFormat defaultFormat];
    format.scale = self.root.traitCollection.displayScale; format.opaque = YES;
    UIGraphicsImageRenderer *renderer = [[UIGraphicsImageRenderer alloc] initWithBounds:bounds format:format];
    UIImage *result = [renderer imageWithActions:^(UIGraphicsImageRendererContext *context) {
        [UIColor.blackColor setFill]; UIRectFill(bounds);
        CGContextSaveGState(context.CGContext);
        CGContextClipToRect(context.CGContext, SatchelGameBounds(bounds));
        [self.pausedFrame drawInRect:picture];
        CGContextRestoreGState(context.CGContext);
        for (UIView *view in self.root.subviews) if (view != self.game && !view.hidden && view.alpha > 0) {
            CGContextSaveGState(context.CGContext);
            CGContextTranslateCTM(context.CGContext, view.frame.origin.x, view.frame.origin.y);
            CGContextSetAlpha(context.CGContext, view.alpha);
            [view.layer renderInContext:context.CGContext];
            CGContextRestoreGState(context.CGContext);
        }
    }];
    BOOL ok = [UIImagePNGRepresentation(result) writeToFile:filename atomically:YES];
    fprintf(stderr, "Satchel screen shot: %s %s game-frame=%s state=%s\n", filename.UTF8String, ok ? "ok" : "failed",
        self.pausedFrame ? "captured" : "unavailable", [SatchelMenuState()[@"state"] UTF8String]);
}
- (void)screenshot:(NSString *)filename {
    if (SatchelMenuBlocksInput()) [self compose:filename];
    else { __weak SatchelDisplayControls *weakSelf = self; [self capture:^{ [weakSelf compose:filename]; }]; }
}
@end
void SatchelDisplayCaptureForPause(void (^completion)(void)) { [activeControls capture:completion]; }
void SatchelDisplayDidLayout(void) { [activeControls geometryChanged]; [activeControls layoutPause]; SatchelMenuLayout(); }
void SatchelLayoutGameView(UIView *game, CGRect bounds) {
    bounds = SatchelGameBounds(bounds);
    int mode = SatchelScreenMode();
    CGRect picture = SatchelPictureRectAspect(bounds, mode, pictureAspect);
    CGRect drawable = SatchelPictureRectAspect(bounds, mode == 1 ? 0 : mode, pictureAspect);
    game.transform = CGAffineTransformIdentity;
    game.bounds = CGRectMake(0, 0, drawable.size.width, drawable.size.height);
    game.center = CGPointMake(CGRectGetMidX(picture), CGRectGetMidY(picture));
    if (mode == 1 && drawable.size.width > 0 && drawable.size.height > 0)
        game.transform = CGAffineTransformMakeScale(picture.size.width / drawable.size.width, picture.size.height / drawable.size.height);
    // Keep the picture inside its region when the hinge changes.
    CAShapeLayer *mask = [CAShapeLayer layer];
    mask.path = [UIBezierPath bezierPathWithRect:[game convertRect:bounds fromView:game.superview]].CGPath;
    game.layer.mask = mask;
}
// Adapted from nxnative front_ios.m front_picture_shape (MIT, upstream 600c0b37): the game's
// view takes the shape of the picture the game presents. A 16:9 game keeps its layout; only a
// taller shape (DM_SCREEN_ASPECT with a title's screen-shape patches) changes it. The present
// thread reports a change of shape, never a size change of the same shape.
void front_picture_shape(int w, int h) {
    if (w <= 0 || h <= 0) return;
    CGFloat aspect = (CGFloat)w / h;
    if (!(aspect >= 1.0 && aspect <= 3.0)) return;
    dispatch_async(dispatch_get_main_queue(), ^{
        if (fabs(aspect - pictureAspect) <= pictureAspect * 0.001) return;
        pictureAspect = aspect;
        fprintf(stderr, "Satchel picture: shape %dx%d (%.3f:1), the game's view follows it\n", w, h, aspect);
        [activeControls changed];
    });
}
void SatchelInstallDisplayControls(UIView *root, UIView *game, UIView *pad, UILabel *fps) {
    SatchelDisplayControls *controls = [SatchelDisplayControls new];
    controls.root = root; controls.game = game; controls.pad = pad;
    controls.captureCompletions = [NSMutableArray array];
    root.clipsToBounds = YES;
    UITapGestureRecognizer *tap = [[UITapGestureRecognizer alloc] initWithTarget:controls action:@selector(openMenu)];
    tap.numberOfTouchesRequired = 3; tap.numberOfTapsRequired = 2;
    tap.cancelsTouchesInView = NO; tap.delaysTouchesBegan = NO; tap.delaysTouchesEnded = NO;
    tap.delegate = controls; [root addGestureRecognizer:tap];
    UITapGestureRecognizer *screenTap = [[UITapGestureRecognizer alloc] initWithTarget:controls action:@selector(toggleScreen)];
    screenTap.numberOfTouchesRequired = 2; screenTap.numberOfTapsRequired = 2;
    screenTap.cancelsTouchesInView = NO; screenTap.delaysTouchesBegan = NO; screenTap.delaysTouchesEnded = NO;
    screenTap.delegate = controls; [root addGestureRecognizer:screenTap];
    objc_setAssociatedObject(root, @selector(openMenu), controls, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    activeControls = controls;
    [NSNotificationCenter.defaultCenter addObserver:controls selector:@selector(changed) name:SatchelScreenModeDidChange object:nil];
    if (@available(iOS 27.1, *)) {
        __weak SatchelDisplayControls *weakControls = controls;
        UIHingeInteraction *hinge = [[UIHingeInteraction alloc] initWithUpdateHandler:^(UIHingeInteraction *interaction, UIHingeInteractionUpdate *update) {
            posture = update.hinge ? update.hinge.status : 0;
            hingeAngle = update.hinge ? update.hinge.angle : 0;
            [weakControls geometryChanged]; [weakControls changed];
        }];
        [root addInteraction:hinge];
    }
    // UIKit layout and hinge callbacks are immediate. This catches same-size screen moves
    // without replacing the SwiftUI scene delegate owned by the app.
    __weak SatchelDisplayControls *weakControls = controls;
    controls.geometryTimer = [NSTimer timerWithTimeInterval:0.25 repeats:YES block:^(NSTimer *timer) { [weakControls geometryChanged]; }];
    [NSRunLoop.mainRunLoop addTimer:controls.geometryTimer forMode:NSRunLoopCommonModes];
    SatchelMenuInstall(root, pad, fps);
    [controls installPauseButton]; [controls layoutPause];
    fprintf(stderr, "Satchel menu: pause button; three-finger double tap; configured controller shortcut hold 0.75s; shortcut inputs are reserved\n");
}
int satchel_display_command(const char *line) {
    if (!strcmp(line, "quit")) {
        dispatch_async(dispatch_get_main_queue(), ^{
            if (!SatchelMenuRequestDeveloperQuit()) fprintf(stderr, "Satchel developer quit: rejected\n");
        }); return 1;
    }
    if (!strncmp(line, "screen ", 7)) {
        NSString *mode = [NSString stringWithUTF8String:line + 7];
        dispatch_async(dispatch_get_main_queue(), ^{
            NSString *value = [mode isEqualToString:@"next"] ? @[@"fit", @"stretch"][(SatchelScreenMode() + 1) % 2] : mode;
            if (!SatchelChangeScreenMode(value)) fprintf(stderr, "Satchel screen: rejected mode (allowed: fit, stretch)\n");
        }); return 1;
    }
    if (!strncmp(line, "menu ", 5)) {
        NSString *command = [NSString stringWithUTF8String:line + 5];
        dispatch_async(dispatch_get_main_queue(), ^{
            NSArray *parts = [command componentsSeparatedByString:@" "];
            if ([command isEqualToString:@"state"]) {
                NSData *json = [NSJSONSerialization dataWithJSONObject:SatchelMenuState() options:NSJSONWritingSortedKeys error:nil];
                fprintf(stderr, "Satchel menu state: %s\n", [[NSString alloc] initWithData:json encoding:NSUTF8StringEncoding].UTF8String);
            } else if (parts.count > 2 || !SatchelMenuPerform(parts[0], parts.count == 2 ? parts[1] : nil)) fprintf(stderr, "Satchel menu: rejected command\n");
        }); return 1;
    }
    if (!strncmp(line, "posture ", 8)) {
        NSString *value = [NSString stringWithUTF8String:line + 8];
        dispatch_async(dispatch_get_main_queue(), ^{
            NSDictionary *values = @{@"auto": @-1, @"closed": @1, @"half": @2, @"open": @3};
            if (!values[value]) { fprintf(stderr, "Satchel posture: rejected test override\n"); return; }
            overridePosture = [values[value] integerValue]; [activeControls geometryChanged]; [activeControls changed];
        }); return 1;
    }
    if (!strncmp(line, "view-shot ", 10)) {
        NSString *filename = [NSString stringWithUTF8String:line + 10];
        if (!filename.length || ![filename.lastPathComponent isEqualToString:filename] || ![filename.pathExtension isEqualToString:@"png"]) {
            fprintf(stderr, "Satchel view shot: expected a PNG basename\n"); return 1;
        }
        void SatchelMenuDebugShot(NSString *filename);
        dispatch_async(dispatch_get_main_queue(), ^{ SatchelMenuDebugShot(filename); }); return 1;
    }
    if (!strncmp(line, "screen-shot ", 12)) {
        NSString *filename = [NSString stringWithUTF8String:line + 12];
        if (!filename.length || ![filename.lastPathComponent isEqualToString:filename] || ![filename.pathExtension isEqualToString:@"png"]) {
            fprintf(stderr, "Satchel screen shot: expected a PNG basename\n"); return 1;
        }
        dispatch_async(dispatch_get_main_queue(), ^{ [activeControls screenshot:filename]; }); return 1;
    }
    return 0;
}
