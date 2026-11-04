// Synthetic UIKit integration fixture. No guest code, game files or renderer is loaded.
#import "DisplayControls.h"
#import "InGameMenu.h"
#import "SessionBridge.h"
#include "dm.h"
#include "front.h"
#include "haptics_apple.h"
#include <assert.h>

static BOOL paused;
static int fixtureThermal;
int nvn_thermal_limited(void) { return fixtureThermal; }
static unsigned suspendReasons;
static uint64_t inputBits;
const struct dm_profile *dm_profile(void) { static struct dm_profile profile = {.pad_layout = 1}; return &profile; }
void dm_input_set(uint64_t b, int32_t x, int32_t y, int32_t rx, int32_t ry) { inputBits = b; }
void dm_pad_set(uint64_t b, int32_t x, int32_t y, int32_t rx, int32_t ry) {}
void dm_pad_set_player(int p, uint64_t b, int32_t x, int32_t y, int32_t rx, int32_t ry) {}
void dm_pad_players(uint32_t mask) {}
uint32_t dm_player_npad(int p) { return p; }
void dm_motion_set(int p, const float a[3], const float r[3]) {}
void dm_motion_set_switch(int p, const float a[3], const float r[3]) {}
void dm_motion_clear(int p) {}
void dm_touch_set(const dm_touch *t, int n) {}
void dm_haptics_apple_controller(int p, GCController *controller) {}
void dm_haptics_suspend(unsigned reason, int on) { if (on) suspendReasons |= reason; else suspendReasons &= ~reason; }
int dm_opmode(void) { return 0; }
void SatchelNativeSetMenuPaused(BOOL value) { paused = value; fprintf(stderr, "FIXTURE pause=%d\n", value); }
void mtl_grab_next_present(void (^callback)(CGImageRef)) {
    UIGraphicsImageRenderer *renderer = [[UIGraphicsImageRenderer alloc] initWithSize:CGSizeMake(640, 360)];
    UIImage *frame = [renderer imageWithActions:^(UIGraphicsImageRendererContext *context) {
        [[UIColor colorWithRed:0.08 green:0.18 blue:0.26 alpha:1] setFill]; UIRectFill(CGRectMake(0, 0, 640, 360));
        [@"SYNTHETIC GAME SURFACE" drawAtPoint:CGPointMake(130, 150) withAttributes:@{NSForegroundColorAttributeName:UIColor.whiteColor, NSFontAttributeName:[UIFont boldSystemFontOfSize:24]}];
    }];
    callback(frame.CGImage);
}
UIView *dm_gamepad_view(CGRect frame);
@interface FixtureController : UIViewController
@property UIView *game;
@property UIView *pad;
@end
@implementation FixtureController
- (void)viewDidLoad {
    [super viewDidLoad]; self.view.backgroundColor = UIColor.blackColor;
    self.game = [UIView new]; [self.view addSubview:self.game];
    self.pad = dm_gamepad_view(self.view.bounds); self.pad.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    [self.view addSubview:self.pad];
    UILabel *fps = [UILabel new]; fps.text = @"60 fps (fixture)"; fps.textColor = UIColor.whiteColor;
    fps.frame = CGRectMake(150, 20, 140, 24); [self.view addSubview:fps];
    SatchelInstallDisplayControls(self.view, self.game, self.pad, fps);
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC), dispatch_get_main_queue(), ^{ [self testMenu]; });
}
- (void)viewDidLayoutSubviews { [super viewDidLayoutSubviews]; self.pad.frame = self.view.bounds; SatchelDisplayDidLayout(); SatchelLayoutGameView(self.game, self.view.bounds); }
- (UIInterfaceOrientationMask)supportedInterfaceOrientations { return UIInterfaceOrientationMaskLandscape; }
- (void)testMenu {
    assert([SatchelCurrentPlayerSettings()[@"DM_FPS_TARGET"] isEqual:@"30"]);
    assert([SatchelCurrentPlayerSettings()[@"DM_RENDER_SCALE"] isEqual:@"1.0"]);
    assert(SatchelMenuPerform(@"open", nil));
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC), dispatch_get_main_queue(), ^{
        assert(paused && SatchelMenuBlocksInput()); assert([SatchelMenuState()[@"state"] isEqual:@"menu"]);
        // Frame rate and render scale follow the game: the menu has no mode or frame-cap actions.
        assert(!SatchelMenuPerform(@"mode", @"graphics"));
        assert(!SatchelMenuPerform(@"frame-cap", @"60"));
        assert(!SatchelMenuPerform(@"render-scale", @"0.8"));
        assert(!SatchelMenuPerform(@"advanced", @"on"));
        assert(!SatchelMenuState()[@"mode"] && !SatchelMenuState()[@"presets"]);
        assert([SatchelNextPlayerSettings()[@"DM_FPS_TARGET"] isEqual:@"30"]);
        assert(SatchelMenuPerform(@"volume", @"0.5") && SatchelMenuVolume() == 0.5f);
        assert(!SatchelMenuPerform(@"volume", @"NaN") && !SatchelMenuPerform(@"size", @"2"));
        assert(SatchelMenuPerform(@"haptics", @"off") && (suspendReasons & 4));
        assert(SatchelMenuPerform(@"opacity", @"0.7") && SatchelMenuPerform(@"size", @"1.25"));
        assert(SatchelMenuPerform(@"fps", @"off"));
        assert(![SatchelMenuState()[@"fps"] boolValue]);
        assert(SatchelMenuPerform(@"fps", @"on"));
        assert(!SatchelMenuPerform(@"screen", @"zoom"));
        assert(SatchelMenuPerform(@"screen", @"stretch"));
        assert(![SatchelMenuState()[@"thermalLimited"] boolValue]);
        fixtureThermal = 1;
        assert(SatchelMenuPerform(@"library", nil) && paused);
        assert(!SatchelMenuPerform(@"resume", nil));
        assert(!SatchelMenuPerform(@"open", nil));
        assert(SatchelMenuPerform(@"return-to-game", nil));
        SatchelMenuSurfaceAttached(YES);
        assert([SatchelMenuState()[@"state"] isEqual:@"menu"]);
        assert(SatchelMenuPerform(@"quit", nil)); assert([SatchelMenuState()[@"state"] isEqual:@"confirmQuit"]);
        assert(SatchelMenuPerform(@"cancel", nil));
        assert(SatchelMenuPerform(@"resume", nil) && !paused && !SatchelMenuBlocksInput());
        assert(!SatchelMenuPerform(@"confirm-quit", nil));
        satchel_display_command("posture half");
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC), dispatch_get_main_queue(), ^{
            assert([SatchelMenuState()[@"thermalLimited"] boolValue]);
            NSUInteger indicators = 0;
            for (UIView *view in self.view.subviews) if ([view.accessibilityIdentifier isEqual:@"thermal.limit"]) {
                assert(!view.hidden && view.frame.size.width > 0); indicators++;
            }
            assert(indicators == 1);
            assert([SatchelMenuState()[@"thermalMessage"] containsString:@"limited to 30 FPS"]);
            SatchelMenuPerform(@"open", nil);
            // Verify both gesture registrations. Physical recognition still needs hands.
            NSUInteger two = 0, three = 0;
            for (UIGestureRecognizer *gesture in self.view.gestureRecognizers) if ([gesture isKindOfClass:UITapGestureRecognizer.class]) {
                UITapGestureRecognizer *tap = (UITapGestureRecognizer *)gesture;
                assert(!tap.cancelsTouchesInView && !tap.delaysTouchesBegan && !tap.delaysTouchesEnded);
                if (tap.numberOfTouchesRequired == 2 && tap.numberOfTapsRequired == 2) two++;
                if (tap.numberOfTouchesRequired == 3 && tap.numberOfTapsRequired == 2) three++;
            }
            assert(two == 1 && three == 1);
            SatchelMenuPerform(@"resume", nil);
            CGRect b = self.view.bounds; assert(SatchelGameBounds(b).size.height == floor(b.size.height / 2));
            assert(SatchelControlBounds(b).origin.y == floor(b.size.height / 2));
            // Simulator provides a virtual Gamepad. Exercise the existing disconnect UI
            // notification so the composed fixture also shows the real touch-control paths.
            for (GCController *c in GCController.controllers)
                [NSNotificationCenter.defaultCenter postNotificationName:GCControllerDidDisconnectNotification object:c];
            satchel_display_command("screen zoom");
            satchel_display_command("screen fit");
            satchel_display_command("screen next");
            satchel_display_command("screen next");
            satchel_display_command("screen-shot half-fixture.png");
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 300 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
            assert(SatchelScreenMode() == 0);
            satchel_display_command("posture auto");
            SatchelMenuPerform(@"open", nil);
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC), dispatch_get_main_queue(), ^{
                satchel_display_command("screen-shot menu-fixture.png");
                satchel_display_command("menu state");
                assert([SatchelMenuState()[@"thermalLimited"] boolValue]);
                fprintf(stderr, "PASS: presets, custom values, zoom rejection, gesture registration and thermal UI (synthetic latch)\n");
                fprintf(stderr, "PASS: UIKit menu actions, pause, library, quit cancellation, live preferences and half-fold geometry (synthetic engine)\n");
                UIWindowSceneGeometryPreferencesIOS *geometry = [[UIWindowSceneGeometryPreferencesIOS alloc] initWithInterfaceOrientations:UIInterfaceOrientationMaskLandscapeLeft];
                [self.view.window.windowScene requestGeometryUpdateWithPreferences:geometry errorHandler:^(NSError *error) {
                    fprintf(stderr, "FAIL: fixture rotation %s\n", error.localizedDescription.UTF8String); assert(NO);
                }];
                dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC), dispatch_get_main_queue(), ^{
                    assert(self.view.window.windowScene.effectiveGeometry.interfaceOrientation == UIInterfaceOrientationLandscapeLeft);
                    CGRect available = SatchelInteractiveBounds(self.view, self.view.bounds);
                    fprintf(stderr, "FIXTURE rotated bounds=%s available=%s orientation=%ld\n", NSStringFromCGRect(self.view.bounds).UTF8String,
                        NSStringFromCGRect(available).UTF8String, (long)self.view.window.windowScene.effectiveGeometry.interfaceOrientation);
                    for (UIViewReservedRegion *r in [self.view reservedRegionsOfKind:UIViewReservedRegionKind.occlusionRegionKind])
                        fprintf(stderr, "FIXTURE rotated region=%s active=%d\n", NSStringFromCGRect(r.frame).UTF8String, r.active);
                    assert(available.origin.x == 0 && CGRectGetMaxX(available) < self.view.bounds.size.width);
                    satchel_display_command("screen-shot menu-right-fixture.png");
                    fprintf(stderr, "PASS: UIKit rotated camera clearance from left to right on the simulator's current screen\n");
                    fixtureThermal = 0;
                    [@"PASS" writeToFile:@"fixture-result.txt" atomically:YES encoding:NSUTF8StringEncoding error:nil];
                    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 300 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
                        assert(![SatchelMenuState()[@"thermalLimited"] boolValue]);
                        assert([SatchelMenuState()[@"thermalMessage"] isEqual:@""]);
                        assert(SatchelMenuPerform(@"quit", nil));
                        SatchelMenuPerform(@"confirm-quit", nil);
                        assert(NO); // Confirm must end this fixture process.
                    });
                });
            });
            });
        });
    });
}
@end
@interface FixtureScene : UIResponder <UIWindowSceneDelegate>
@property (nonatomic, strong) UIWindow *window;
@end
@implementation FixtureScene
- (void)scene:(UIScene *)scene willConnectToSession:(UISceneSession *)session options:(UISceneConnectionOptions *)options {
    self.window = [[UIWindow alloc] initWithWindowScene:(UIWindowScene *)scene];
    self.window.rootViewController = [FixtureController new]; [self.window makeKeyAndVisible];
}
@end
@interface FixtureApp : UIResponder <UIApplicationDelegate>
@end
@implementation FixtureApp
- (BOOL)application:(UIApplication *)app didFinishLaunchingWithOptions:(NSDictionary *)options {
    NSString *documents = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
    chdir(documents.fileSystemRepresentation);
    [@"RUNNING" writeToFile:@"fixture-result.txt" atomically:YES encoding:NSUTF8StringEncoding error:nil];
    freopen("fixture.log", "w", stderr); setvbuf(stderr, NULL, _IOLBF, 0);
    [NSUserDefaults.standardUserDefaults removePersistentDomainForName:NSBundle.mainBundle.bundleIdentifier];
    setenv("DM_MOTION", "0", 1); SatchelApplyPlayerSettings(@"0100F2C0115B6000");
    return YES;
}
- (UISceneConfiguration *)application:(UIApplication *)application configurationForConnectingSceneSession:(UISceneSession *)session options:(UISceneConnectionOptions *)options {
    UISceneConfiguration *config = [[UISceneConfiguration alloc] initWithName:@"Fixture" sessionRole:session.role];
    config.delegateClass = FixtureScene.class; return config;
}
@end
int main(int argc, char **argv) { @autoreleasepool { return UIApplicationMain(argc, argv, nil, NSStringFromClass(FixtureApp.class)); } }
