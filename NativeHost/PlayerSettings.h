#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN
// Frame target and render scale come from PlayerProfiles.plist. Screen mode and vibration
// are saved per game. Only picture settings override the verified manifest environment.
NSDictionary<NSString *, NSString *> *SatchelPlayerSettings(NSString *titleID);
/// Saves screen mode and game vibration (DM_HAPTICS); other keys are ignored.
BOOL SatchelSavePlayerSettings(NSString *titleID, NSDictionary<NSString *, NSString *> *values);
void SatchelApplyPlayerSettings(NSString *titleID);
void SatchelClearPlayerSession(void);
int SatchelScreenMode(void);
BOOL SatchelChangeScreenMode(NSString *mode);
FOUNDATION_EXPORT NSNotificationName const SatchelScreenModeDidChange;
NSDictionary *SatchelCurrentPlayerSettings(void);
NSDictionary *SatchelNextPlayerSettings(void);
BOOL SatchelQueuePlayerSetting(NSString *key, NSString *value);
NS_ASSUME_NONNULL_END

BOOL SatchelPlayerHaptics(void);
BOOL SatchelChangePlayerHaptics(BOOL enabled);
