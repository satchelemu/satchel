#import <UIKit/UIKit.h>
#import "MenuRuntime.h"
NS_ASSUME_NONNULL_BEGIN
// Main thread only. State is a snapshot, observe SatchelMenuDidChange for updates.
NSDictionary<NSString *, id> *SatchelMenuState(void);
BOOL SatchelMenuPerform(NSString *action, NSString * _Nullable value);
FOUNDATION_EXPORT NSNotificationName const SatchelMenuDidChange;
FOUNDATION_EXPORT NSNotificationName const SatchelMenuReturnToGame;
void SatchelMenuSurfaceAttached(BOOL attached);
// Front-end hooks. Redesign can replace the plain view using SatchelMenuSetCustomView.
void SatchelMenuInstall(UIView *root, UIView *pad, UILabel * _Nullable fps);
void SatchelMenuLayout(void);
void SatchelMenuSetCustomView(UIView * _Nullable view);
CGFloat SatchelTouchSize(void);
CGFloat SatchelTouchOpacity(void);
// Capture the last game picture before suspending presents. Completion must run on main.
void SatchelDisplayCaptureForPause(void (^completion)(void));
NS_ASSUME_NONNULL_END

void SatchelMenuBeginSession(void);
void SatchelMenuEndSession(void);
void SatchelMenuStopping(BOOL failed);

void SatchelMenuControllerDisconnected(void);

// Developer requests reuse open, quit and confirm-quit, including asynchronous pause.
BOOL SatchelMenuRequestDeveloperQuit(void);
