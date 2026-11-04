#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN
// One per-title store shared by the game page, launch and the in-game menu.
// FPS, render scale and pipeline mode take effect on the next game launch.
NSDictionary<NSString *, NSString *> *SatchelPlayerSettings(NSString *titleID);
NSDictionary<NSString *, NSString *> *SatchelSavedPlayerSettings(NSString *titleID);
NSDictionary<NSString *, NSString *> *SatchelPlayerGlobalDefaults(void);
NSDictionary<NSString *, NSString *> *SatchelPlayerDefaults(NSString *titleID);
NSDictionary<NSString *, NSString *> *SatchelPlayerOverrides(NSString *titleID);
/// Atomically merges a validated patch, preserving all other per-title values.
BOOL SatchelSavePlayerSettings(NSString *titleID, NSDictionary<NSString *, NSString *> *values);
BOOL SatchelResetPlayerSetting(NSString *titleID, NSString *key);
FOUNDATION_EXPORT NSNotificationName const SatchelPlayerSettingsDidChange;
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
