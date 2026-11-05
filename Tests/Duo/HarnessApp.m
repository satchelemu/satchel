// Host app for the Duo layout harness. It links Satchel's production display controls,
// player settings, native menu and the overlaid engine gamepad with inert host stubs.
// No guest code, game files, renderer or hinge sensor is used. Stubs follow
// Tests/UIPreview/GameFixture.m and NativeHost/display/simulator.m.
#import <UIKit/UIKit.h>
#import <GameController/GameController.h>
#import "DisplayControls.h"
#import "InGameMenu.h"
#import "SessionBridge.h"
#import "PlayerSettings.h"
#include "dm.h"
#include "front.h"
#include "haptics_apple.h"

// Synthetic host state the tests read.
uint64_t SatchelDuoInputButtons;
int SatchelDuoTouchCount, SatchelDuoResets;
dm_touch SatchelDuoTouches[16];

NSNotificationName const SatchelNativeSessionDidEnd = @"SatchelNativeSessionDidEnd";
NSNotificationName const SatchelNativeSessionDidFail = @"SatchelNativeSessionDidFail";
int nvn_thermal_limited(void) { return 0; }
const struct dm_profile *dm_profile(void) { static struct dm_profile profile = {.pad_layout = 1}; return &profile; }
void dm_input_set(uint64_t b, int32_t x, int32_t y, int32_t rx, int32_t ry) { SatchelDuoInputButtons = b; }
void dm_pad_set(uint64_t b, int32_t x, int32_t y, int32_t rx, int32_t ry) {}
void dm_pad_set_player(int p, uint64_t b, int32_t x, int32_t y, int32_t rx, int32_t ry) {}
void dm_pad_players(uint32_t mask) {}
void satchel_hid_controllers(uint32_t mask) {}
void satchel_hid_suppress_player(int p, uint64_t buttons) {}
void satchel_hid_release(void) { SatchelDuoResets++; }
uint32_t dm_player_npad(int p) { return p; }
void dm_motion_set(int p, const float a[3], const float r[3]) {}
void dm_motion_set_switch(int p, const float a[3], const float r[3]) {}
void dm_motion_clear(int p) {}
void dm_touch_set(const dm_touch *t, int n) {
    SatchelDuoTouchCount = n;
    for (int i = 0; i < n && i < 16; i++) SatchelDuoTouches[i] = t[i];
}
void dm_haptics_apple_controller(int p, GCController *controller) {}
void dm_haptics_suspend(unsigned reason, int on) {}
void satchel_haptics_suspend(unsigned reason, int on) {}
int dm_opmode(void) { return 0; }
void SatchelNativeSetMenuPaused(BOOL value) {}
BOOL SatchelNativeSessionStarted(void) { return YES; }
int SatchelNativeSessionAcceptsEvents(void) { return 1; }
void SatchelNativeFocusEvent(int event) {}
NSString *SatchelNativeSaveTitle(void) { return nil; }
void SatchelStopNativeSession(void (^completion)(NSError *)) { completion(nil); }
UIViewController *SatchelNativeGameController(void) {
    for (UIScene *scene in UIApplication.sharedApplication.connectedScenes)
        if ([scene isKindOfClass:UIWindowScene.class]) return ((UIWindowScene *)scene).keyWindow.rootViewController;
    return nil;
}
void mtl_grab_next_present(void (^callback)(CGImageRef)) {
    UIGraphicsImageRenderer *renderer = [[UIGraphicsImageRenderer alloc] initWithSize:CGSizeMake(64, 36)];
    callback([renderer imageWithActions:^(UIGraphicsImageRendererContext *c) { [UIColor.darkGrayColor setFill]; UIRectFill(CGRectMake(0, 0, 64, 36)); }].CGImage);
}

@interface DuoHarnessScene : UIResponder <UIWindowSceneDelegate>
@property (nonatomic, strong) UIWindow *window;
@end
@implementation DuoHarnessScene
- (void)scene:(UIScene *)scene willConnectToSession:(UISceneSession *)session options:(UISceneConnectionOptions *)options {
    self.window = [[UIWindow alloc] initWithWindowScene:(UIWindowScene *)scene];
    self.window.rootViewController = [UIViewController new];
    self.window.rootViewController.view.backgroundColor = UIColor.blackColor;
    [self.window makeKeyAndVisible];
    // As App/GameOrientation.swift does: the scene follows the host's orientation rule.
    __weak UIWindowScene *weakScene = (UIWindowScene *)scene;
    [NSNotificationCenter.defaultCenter addObserverForName:SatchelGameOrientationDidChange object:nil queue:NSOperationQueue.mainQueue
                                                usingBlock:^(NSNotification *note) { SatchelFollowGameOrientation(weakScene); }];
}
- (UIInterfaceOrientationMask)supportedInterfaceOrientationsForWindowScene:(UIWindowScene *)windowScene { return SatchelGameOrientationMask(); }
@end
@interface DuoHarnessApp : UIResponder <UIApplicationDelegate>
@end
@implementation DuoHarnessApp
- (BOOL)application:(UIApplication *)app didFinishLaunchingWithOptions:(NSDictionary *)options {
    [NSUserDefaults.standardUserDefaults removePersistentDomainForName:NSBundle.mainBundle.bundleIdentifier];
    setenv("DM_MOTION", "0", 1);
    return YES;
}
- (UIInterfaceOrientationMask)application:(UIApplication *)application supportedInterfaceOrientationsForWindow:(UIWindow *)window { return SatchelGameOrientationMask(); }
- (UISceneConfiguration *)application:(UIApplication *)application configurationForConnectingSceneSession:(UISceneSession *)session options:(UISceneConnectionOptions *)options {
    UISceneConfiguration *config = [[UISceneConfiguration alloc] initWithName:@"Harness" sessionRole:session.role];
    config.delegateClass = DuoHarnessScene.class; return config;
}
@end
int main(int argc, char **argv) { @autoreleasepool { return UIApplicationMain(argc, argv, nil, NSStringFromClass(DuoHarnessApp.class)); } }
