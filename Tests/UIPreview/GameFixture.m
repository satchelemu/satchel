// Simulator-only game screen for the UI preview (build-preview.zsh game). It runs the real
// native menu bridge, display controls, pause button and touch pad over a synthetic picture.
// No guest code, game files or renderer is loaded. Stubs follow NativeHost/display/simulator.m.
#import "DisplayControls.h"
#import "InGameMenu.h"
#import "SessionBridge.h"
#import "PlayerSettings.h"
#include "dm.h"
#include "front.h"
#include "haptics_apple.h"

static BOOL paused, started;
static NSUInteger frameTicks, viewLoads, inputDowns, touchDowns;
static uint64_t inputButtons;
static int touchCount;
static BOOL heldBefore, heldAfter, pictureVisible;
static NSString *pictureVisibilityState;
void SatchelPreviewSetPictureVisible(BOOL visible, NSString *state) {
    pictureVisible = visible; pictureVisibilityState = state;
}
static BOOL heldNow(void) { return inputButtons != 0 || touchCount > 0; }
static NSString *activePackage, *activeTitle;
static NSRecursiveLock *saveLock;
static NSMutableArray<NSString *> *events;
NSNotificationName const SatchelNativeSessionDidFail = @"SatchelNativeSessionDidFail";
static NSTimeInterval fixtureStarted;
NSDictionary *SatchelNativeSessionMetrics(void) {
    return @{@"frames": @(started ? (uint64_t)((NSProcessInfo.processInfo.systemUptime - fixtureStarted) * 60) : 0), @"footprint": @1048576};
}
NSNotificationName const SatchelNativeSessionDidEnd = @"SatchelNativeSessionDidEnd";
static void record(NSString *event) {
    if (!events) events = [NSMutableArray array];
    [events addObject:event];
}
NSString *SatchelPreviewSessionState(void) {
    return [NSString stringWithFormat:@"started=%d paused=%d package=%@ events=%@", started, paused,
            activePackage ?: @"none", [events componentsJoinedByString:@","] ?: @""];
}
NSString *SatchelNativeSaveTitle(void) { return started ? activeTitle : nil; }
BOOL SatchelBeginSaveAccess(NSString *title, BOOL allowRunning) {
    static dispatch_once_t once;
    dispatch_once(&once, ^{ saveLock = [NSRecursiveLock new]; });
    [saveLock lock];
    if (!allowRunning && started && [activeTitle caseInsensitiveCompare:title] == NSOrderedSame) {
        [saveLock unlock]; return NO;
    }
    return YES;
}
void SatchelEndSaveAccess(void) { [saveLock unlock]; }
BOOL SatchelCloneCommittedSave(NSString *source, NSString *destination, NSError **error) {
    // The fixture has no guest writers. Exercise the real archive and restore code
    // over a copied snapshot; engine commit locking remains a hardware boundary.
    return [NSFileManager.defaultManager copyItemAtPath:source toPath:destination error:error];
}
int nvn_thermal_limited(void) { return 0; }
const struct dm_profile *dm_profile(void) { static struct dm_profile profile = {.pad_layout = 1}; return &profile; }
void dm_input_set(uint64_t b, int32_t x, int32_t y, int32_t rx, int32_t ry) { if (b && !inputButtons) inputDowns++; inputButtons = b; }
void dm_pad_set(uint64_t b, int32_t x, int32_t y, int32_t rx, int32_t ry) {}
static uint64_t guestButtons;
uint64_t SatchelPreviewGuestButtons(void) { return guestButtons; }
void SatchelPreviewResetGuestButtons(void) { guestButtons = 0; }
void dm_pad_set_player(int p, uint64_t b, int32_t x, int32_t y, int32_t rx, int32_t ry) { guestButtons |= b; }
void dm_pad_players(uint32_t mask) {}
void satchel_hid_controllers(uint32_t mask) {}
void satchel_hid_suppress_player(int p, uint64_t buttons) {}
void satchel_hid_release(void) {}
uint32_t dm_player_npad(int p) { return p; }
void dm_motion_set(int p, const float a[3], const float r[3]) {}
void dm_motion_set_switch(int p, const float a[3], const float r[3]) {}
void dm_motion_clear(int p) {}
void dm_touch_set(const dm_touch *t, int n) { if (n && !touchCount) touchDowns++; touchCount = n; }
void dm_haptics_apple_controller(int p, GCController *controller) {}
void dm_haptics_suspend(unsigned reason, int on) {}
void satchel_haptics_suspend(unsigned reason, int on) {}
int dm_opmode(void) { return 0; }
void SatchelNativeSetMenuPaused(BOOL value) { paused = value; }
// Only the game screen has a running session; the dev screens start before one.
BOOL SatchelNativeSessionStarted(void) { return started || [[NSUserDefaults.standardUserDefaults stringForKey:@"SatchelPreviewScreen"] isEqualToString:@"game"]; }
BOOL SatchelNativeSessionAcceptsEvents(void) { return SatchelNativeSessionStarted(); }
void SatchelNativeFocusEvent(int event) {}
BOOL SatchelPrepareNativeSession(NSString *package, NSError **error) {
    dispatch_sync(dispatch_get_main_queue(), ^{
        NSCAssert(!started, @"The previous synthetic session must stop before another starts");
        activePackage = package.lastPathComponent; fixtureStarted = NSProcessInfo.processInfo.systemUptime; started = YES; paused = NO;
        NSData *data = [NSData dataWithContentsOfFile:[package stringByAppendingPathComponent:@"manifest.json"]];
        activeTitle = [NSJSONSerialization JSONObjectWithData:data options:0 error:nil][@"titleID"];
        SatchelApplyPlayerSettings(activeTitle);
        record([@"start:" stringByAppendingString:activePackage]);
    });
    return YES;
}
void SatchelStopNativeSession(void (^completion)(NSError *)) {
    NSCAssert(NSThread.isMainThread, @"Stop must begin on the main thread");
    NSString *ending = activePackage ?: @"Preview-Game";
    NSString *endingTitle = activeTitle ?: @"";
    record([@"stop-request:" stringByAppendingString:ending]);
    SatchelMenuStopping(NO);
    // Asynchronous, like the device stop/join boundary. Tests observe ordering.
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 250 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
        started = NO; paused = NO; activePackage = nil;
        SatchelClearPlayerSession();
        record([@"stop-complete:" stringByAppendingString:ending]);
        SatchelMenuEndSession();
        [NSNotificationCenter.defaultCenter postNotificationName:SatchelNativeSessionDidEnd object:nil userInfo:@{@"package": ending, @"titleID": endingTitle}];
        completion(nil);
    });
}

static UIImage *syntheticFrame(void) {
    UIGraphicsImageRenderer *renderer = [[UIGraphicsImageRenderer alloc] initWithSize:CGSizeMake(640, 360)];
    return [renderer imageWithActions:^(UIGraphicsImageRendererContext *context) {
        CGContextRef cg = context.CGContext;
        CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
        CGFloat colors[] = {0.10, 0.28, 0.36, 1, 0.42, 0.24, 0.30, 1};
        CGGradientRef gradient = CGGradientCreateWithColorComponents(space, colors, NULL, 2);
        CGContextDrawLinearGradient(cg, gradient, CGPointZero, CGPointMake(640, 360), 0);
        CGGradientRelease(gradient); CGColorSpaceRelease(space);
        CGContextSetStrokeColorWithColor(cg, UIColor.whiteColor.CGColor);
        CGContextSetLineWidth(cg, 3);
        CGContextStrokeEllipseInRect(cg, CGRectMake(288, 68, 64, 64));
        [@"SYNTHETIC GAME SURFACE" drawAtPoint:CGPointMake(150, 160) withAttributes:@{
            NSForegroundColorAttributeName: [UIColor colorWithWhite:1 alpha:0.8], NSFontAttributeName: [UIFont boldSystemFontOfSize:26]}];
    }];
}
void mtl_grab_next_present(void (^callback)(CGImageRef)) { callback(syntheticFrame().CGImage); }
UIView *dm_gamepad_view(CGRect frame);

// The pinned pad exposes facePos: for drawing and hit testing; observe that same point.
@interface UIView (PreviewPadGeometry)
- (CGPoint)facePos:(int)button;
@end

@interface PreviewGameController : UIViewController
@property UIImageView *game;
@property UIView *pad;
@end
@implementation PreviewGameController
- (void)viewDidLoad {
    [super viewDidLoad]; viewLoads++; self.view.backgroundColor = UIColor.blackColor;
    self.game = [[UIImageView alloc] initWithImage:syntheticFrame()];
    self.game.isAccessibilityElement = YES; self.game.accessibilityIdentifier = @"fixture-picture";
    self.game.accessibilityLabel = @"Synthetic 16 by 9 game frame";
    [self.view addSubview:self.game];
    self.pad = dm_gamepad_view(self.view.bounds); [self.view addSubview:self.pad];
    UILabel *fps = [UILabel new]; fps.text = @"60 fps"; fps.textColor = UIColor.whiteColor;
    fps.font = [UIFont monospacedDigitSystemFontOfSize:12 weight:UIFontWeightSemibold];
    [self.view addSubview:fps];
    SatchelInstallDisplayControls(self.view, self.game, self.pad, fps);
    if ([NSUserDefaults.standardUserDefaults boolForKey:@"SatchelPreviewSessionTests"]) {
        UIButton *remote = [UIButton buttonWithType:UIButtonTypeSystem];
        [remote setTitle:@"Synthetic remote Quit" forState:UIControlStateNormal];
        remote.accessibilityIdentifier = @"fixture.remote-quit";
        remote.frame = CGRectMake(180, 65, 230, 44);
        [remote addAction:[UIAction actionWithHandler:^(UIAction *action) {
            int satchel_display_command(const char *); satchel_display_command("quit");
        }] forControlEvents:UIControlEventTouchUpInside];
        [self.view addSubview:remote];
        // Half fold and open again on the real scene (the host's posture test override), for
        // the journey that follows the game upright and back.
        NSArray *folds = @[@[@"Synthetic half fold", @"fixture.fold-half", @"posture half"], @[@"Synthetic open", @"fixture.fold-open", @"posture open"]];
        for (NSUInteger i = 0; i < folds.count; i++) {
            UIButton *fold = [UIButton buttonWithType:UIButtonTypeSystem];
            [fold setTitle:folds[i][0] forState:UIControlStateNormal];
            fold.accessibilityIdentifier = folds[i][1];
            fold.frame = CGRectMake(20 + 170 * i, 115, 160, 44);
            NSString *command = folds[i][2];
            [fold addAction:[UIAction actionWithHandler:^(UIAction *action) { satchel_display_command(command.UTF8String); }] forControlEvents:UIControlEventTouchUpInside];
            [self.view addSubview:fold];
        }
    }
}
- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];
    self.pad.frame = self.view.bounds;
    SatchelDisplayDidLayout(); SatchelLayoutGameView(self.game, self.view.bounds);
}
- (BOOL)prefersStatusBarHidden { return YES; }
@end


// A stable parent emulates the inner screen without recreating the native surface.
// This exercises production display, pad and menu code; it never emulates a hinge sensor.
@interface PreviewPostureWindow : UIWindow
@end
@implementation PreviewPostureWindow
- (UIView *)hitTest:(CGPoint)point withEvent:(UIEvent *)event {
    UIView *hit = [super hitTest:point withEvent:event];
    return [hit isKindOfClass:UIButton.class] ? hit : nil;
}
@end

@interface PreviewPostureController : UIViewController
@property PreviewGameController *surface;
@property UIStackView *switches;
@property UILabel *state;
@property NSString *fold;
@property CADisplayLink *clock;
@property PreviewPostureWindow *toolsWindow;
@end
@implementation PreviewPostureController
- (void)viewDidLoad {
    [super viewDidLoad];
    self.view.backgroundColor = UIColor.blackColor;
    self.fold = [NSUserDefaults.standardUserDefaults stringForKey:@"SatchelPreviewFold"] ?: @"front";
    // The inner screen is drawn inside the real window (tall is the upright half-folded game),
    // so the real scene stays landscape. Tests/Duo turns a real scene upright.
    SatchelGameOrientationFollowsPosture(NO);
    self.surface = [PreviewGameController new];
    [self addChildViewController:self.surface]; [self.view addSubview:self.surface.view];
    [self.surface didMoveToParentViewController:self];
    if ([NSUserDefaults.standardUserDefaults boolForKey:@"SatchelPreviewPostureTests"]) {
        self.switches = [UIStackView new]; self.switches.spacing = 4;
        for (NSString *fold in @[@"front", @"open", @"book", @"tall", @"delayed"]) {
            UIButton *button = [UIButton buttonWithType:UIButtonTypeSystem];
            [button setTitle:fold forState:UIControlStateNormal];
            button.accessibilityIdentifier = [@"fixture-fold-" stringByAppendingString:fold];
            [button addAction:[UIAction actionWithHandler:^(UIAction *action) {
                if ([fold isEqual:@"delayed"]) {
                    NSString *next = [NSUserDefaults.standardUserDefaults stringForKey:@"SatchelPreviewNextFold"] ?: @"book";
                    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 2 * NSEC_PER_SEC), dispatch_get_main_queue(), ^{
                        heldBefore = heldNow(); [self changeFold:next];
                        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC), dispatch_get_main_queue(), ^{
                            heldAfter = heldNow();
                        });
                    });
                } else [self changeFold:fold];
            }] forControlEvents:UIControlEventTouchUpInside];
            [self.switches addArrangedSubview:button];
        }
        [self.view addSubview:self.switches];
        self.state = [UILabel new]; self.state.font = [UIFont systemFontOfSize:1];
        self.state.accessibilityIdentifier = @"fixture-posture-state";
        self.state.isAccessibilityElement = YES; self.state.userInteractionEnabled = NO;
        [self.view addSubview:self.state];
    }
    self.clock = [CADisplayLink displayLinkWithTarget:self selector:@selector(tick)];
    [self.clock addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];
    [self changeFold:self.fold];
    // Disconnect only the simulator's virtual pad so the real touch pad is visible.
    // A disconnect during play pauses the game by design; this one is fixture setup,
    // so resume the pause it caused before the journey begins.
    dispatch_async(dispatch_get_main_queue(), ^{
        BOOL disconnected = NO;
        for (GCController *pad in GCController.controllers) {
            [NSNotificationCenter.defaultCenter postNotificationName:GCControllerDidDisconnectNotification object:pad];
            disconnected = YES;
        }
        if (disconnected) dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 500 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
            SatchelMenuPerform(@"resume", nil);
        });
    });
}
- (void)viewDidAppear:(BOOL)animated {
    [super viewDidAppear:animated];
    if (self.switches && !self.toolsWindow) {
        self.toolsWindow = [[PreviewPostureWindow alloc] initWithWindowScene:self.view.window.windowScene];
        self.toolsWindow.windowLevel = UIWindowLevelAlert + 1;
        self.toolsWindow.rootViewController = [UIViewController new];
        self.toolsWindow.rootViewController.view.backgroundColor = UIColor.clearColor;
        [self.toolsWindow.rootViewController.view addSubview:self.switches];
        [self.toolsWindow.rootViewController.view addSubview:self.state];
        self.toolsWindow.hidden = NO;
    }
}
- (void)changeFold:(NSString *)fold {
    self.fold = fold;
    [NSNotificationCenter.defaultCenter postNotificationName:@"SatchelPreviewFoldChanged" object:fold];
    NSString *posture = [@[@"book", @"tall"] containsObject:fold] ? @"half" : [fold isEqual:@"open"] ? @"open" : @"closed";
    satchel_display_command([[@"posture " stringByAppendingString:posture] UTF8String]);
    [self.view setNeedsLayout]; [self.view layoutIfNeeded];
}
- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];
    BOOL inner = ![self.fold isEqual:@"front"];
    CGRect available = self.view.bounds;
    if (inner) available = UIEdgeInsetsInsetRect(available, self.view.safeAreaInsets);
    CGSize size = inner ? ([self.fold isEqual:@"tall"] ? CGSizeMake(669, 951) : CGSizeMake(951, 669)) : available.size;
    CGFloat scale = MIN(available.size.width / size.width, available.size.height / size.height);
    self.surface.view.transform = CGAffineTransformIdentity;
    self.surface.view.bounds = (CGRect){CGPointZero, size};
    self.surface.view.center = CGPointMake(CGRectGetMidX(available), CGRectGetMidY(available));
    self.surface.view.transform = CGAffineTransformMakeScale(scale, scale);
    [self.surface.view setNeedsLayout]; [self.surface.view layoutIfNeeded];
    self.switches.frame = CGRectMake(CGRectGetMidX(self.view.bounds) - 170, CGRectGetHeight(self.view.bounds) - 32, 340, 30);
    self.state.frame = CGRectMake(0, CGRectGetHeight(self.view.bounds) - 2, 2, 2);
}
static NSArray *rectValues(CGRect r) { return @[@(r.origin.x), @(r.origin.y), @(r.size.width), @(r.size.height)]; }
- (void)tick {
    if (started && !paused && self.view.window) frameTicks++;
    if (!self.state) return;
    CGPoint localA = [self.surface.pad facePos:0];
    CGPoint buttonA = [self.surface.pad convertPoint:localA toView:nil];
    NSDictionary *snapshot = @{@"buttonA": @[@(buttonA.x), @(buttonA.y)], @"buttonALocal": @[@(localA.x), @(localA.y)],
        @"touchControlsVisible": @(!self.surface.pad.layer.sublayers.firstObject.hidden), @"fold": self.fold, @"ticks": @(frameTicks), @"viewLoads": @(viewLoads),
        @"session": SatchelPreviewSessionState(), @"picture": rectValues(self.surface.game.frame),
        @"bounds": rectValues(self.surface.view.bounds), @"controls": rectValues(SatchelControlBounds(self.surface.view.bounds)),
        @"inputDowns": @(inputDowns), @"touchDowns": @(touchDowns), @"buttons": @(inputButtons), @"touches": @(touchCount),
        @"heldBefore": @(heldBefore), @"heldAfter": @(heldAfter),
        @"pictureVisible": @(pictureVisible), @"pictureVisibilityState": pictureVisibilityState ?: @""};
    self.state.text = [[NSString alloc] initWithData:[NSJSONSerialization dataWithJSONObject:snapshot options:0 error:nil] encoding:NSUTF8StringEncoding];
}
- (BOOL)prefersStatusBarHidden { return YES; }
@end

UIViewController *SatchelNativeGameController(void) {
    static UIViewController *controller;
    if (!controller) {
        // Preserve the ordinary fixture for the baseline journeys.
        controller = [NSUserDefaults.standardUserDefaults stringForKey:@"SatchelPreviewFold"]
            ? [PreviewPostureController new] : [PreviewGameController new];
    }
    return controller;
}

NSString *SatchelPreviewCurrentFold(void) {
    UIViewController *controller = SatchelNativeGameController();
    if ([controller isKindOfClass:PreviewPostureController.class])
        return ((PreviewPostureController *)controller).fold ?: @"front";
    return [NSUserDefaults.standardUserDefaults stringForKey:@"SatchelPreviewFold"] ?: @"front";
}
