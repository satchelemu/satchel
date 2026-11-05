#import <UIKit/UIKit.h>
NS_ASSUME_NONNULL_BEGIN
// Prepare a verified package after the previous session has ended.
BOOL SatchelPrepareNativeSession(NSString *package, NSError **error);
UIViewController *SatchelNativeGameController(void);
void SatchelNativeFocusEvent(int event);
BOOL SatchelNativeSessionStarted(void);
void SatchelStopNativeSession(void (^completion)(NSError * _Nullable error));
FOUNDATION_EXPORT NSNotificationName const SatchelNativeSessionDidEnd;
FOUNDATION_EXPORT NSNotificationName const SatchelNativeSessionDidFail;
NSDictionary *SatchelNativeSessionMetrics(void);
void SatchelNativeSetMenuPaused(BOOL paused);
// Hold across save operations so preparation cannot race a restore. A paused or
// stopping session still owns its save until stop and join have succeeded.
BOOL SatchelBeginSaveAccess(NSString *title, BOOL allowRunning);
void SatchelEndSaveAccess(void);
NSString * _Nullable SatchelNativeSaveTitle(void);
BOOL SatchelCloneCommittedSave(NSString *source, NSString *destination, NSError **error);
NS_ASSUME_NONNULL_END
