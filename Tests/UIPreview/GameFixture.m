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
static NSString *activePackage, *activeTitle;
static NSRecursiveLock *saveLock;
static NSMutableArray<NSString *> *events;
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
void dm_input_set(uint64_t b, int32_t x, int32_t y, int32_t rx, int32_t ry) {}
void dm_pad_set(uint64_t b, int32_t x, int32_t y, int32_t rx, int32_t ry) {}
void dm_pad_set_player(int p, uint64_t b, int32_t x, int32_t y, int32_t rx, int32_t ry) {}
void dm_pad_players(uint32_t mask) {}
uint32_t dm_player_npad(int p) { return p; }
void dm_motion_set(int p, const float a[3], const float r[3]) {}
void dm_motion_set_switch(int p, const float a[3], const float r[3]) {}
void dm_motion_clear(int p) {}
void dm_touch_set(const dm_touch *t, int n) {}
void dm_haptics_apple_controller(int p, GCController *controller) {}
void dm_haptics_suspend(unsigned reason, int on) {}
void satchel_haptics_suspend(unsigned reason, int on) {}
int dm_opmode(void) { return 0; }
void SatchelNativeSetMenuPaused(BOOL value) { paused = value; }
// Only the game screen has a running session; the dev screens start before one.
BOOL SatchelNativeSessionStarted(void) { return started || [[NSUserDefaults.standardUserDefaults stringForKey:@"SatchelPreviewScreen"] isEqualToString:@"game"]; }
void SatchelNativeFocusEvent(int event) {}
BOOL SatchelPrepareNativeSession(NSString *package, NSError **error) {
    dispatch_sync(dispatch_get_main_queue(), ^{
        NSCAssert(!started, @"The previous synthetic session must stop before another starts");
        activePackage = package.lastPathComponent; started = YES; paused = NO;
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
        [@"SYNTHETIC GAME SURFACE" drawAtPoint:CGPointMake(150, 160) withAttributes:@{
            NSForegroundColorAttributeName: [UIColor colorWithWhite:1 alpha:0.8], NSFontAttributeName: [UIFont boldSystemFontOfSize:26]}];
    }];
}
void mtl_grab_next_present(void (^callback)(CGImageRef)) { callback(syntheticFrame().CGImage); }
UIView *dm_gamepad_view(CGRect frame);

@interface PreviewGameController : UIViewController
@property UIImageView *game;
@property UIView *pad;
@end
@implementation PreviewGameController
- (void)viewDidLoad {
    [super viewDidLoad]; self.view.backgroundColor = UIColor.blackColor;
    self.game = [[UIImageView alloc] initWithImage:syntheticFrame()]; [self.view addSubview:self.game];
    self.pad = dm_gamepad_view(self.view.bounds); [self.view addSubview:self.pad];
    UILabel *fps = [UILabel new]; fps.text = @"60 fps"; fps.textColor = UIColor.whiteColor;
    fps.font = [UIFont monospacedDigitSystemFontOfSize:12 weight:UIFontWeightSemibold];
    [self.view addSubview:fps];
    SatchelInstallDisplayControls(self.view, self.game, self.pad, fps);
}
- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];
    self.pad.frame = self.view.bounds;
    SatchelDisplayDidLayout(); SatchelLayoutGameView(self.game, self.view.bounds);
}
- (BOOL)prefersStatusBarHidden { return YES; }
@end

UIViewController *SatchelNativeGameController(void) {
    static UIViewController *controller;
    if (!controller) controller = [PreviewGameController new];
    return controller;
}
