#import <Foundation/Foundation.h>
#import <GameController/GameController.h>

NS_ASSUME_NONNULL_BEGIN
// Main-thread state, shared by the UI and the native input publisher.
NSArray<NSString *> *SatchelControllerCategories(void);
NSArray<NSString *> *SatchelControllerInputs(void);
NSArray<NSString *> *SatchelControllerOutputs(void);
NSString *SatchelControllerCategory(GCController *controller);
NSDictionary *SatchelControllerProfile(NSString *title, NSString *category);
BOOL SatchelSaveControllerProfile(NSString *title, NSString *category, NSDictionary *profile);
void SatchelResetControllerProfile(NSString *title, NSString *category);
void SatchelControllerSetTitle(NSString *title);
NSDictionary<NSString *, NSNumber *> *SatchelControllerValues(GCExtendedGamepad *pad);
// Keep one state dictionary per controller through input callbacks and menu polls.
NSDictionary *SatchelMapController(NSDictionary<NSString *, NSNumber *> *values, NSDictionary *profile, BOOL hasHome, NSMutableDictionary *state);
NSDictionary *SatchelReadController(GCExtendedGamepad *pad);
BOOL SatchelControllerMenuHeld(GCExtendedGamepad *pad, NSTimeInterval now);
// Pure edge/hold state also used by synthetic input checks.
BOOL SatchelControllerHold(NSMutableDictionary *state, BOOL held, NSTimeInterval now);
NSString *SatchelControllerEdge(NSMutableDictionary *state, float x, float y, BOOL accept, BOOL back);
NS_ASSUME_NONNULL_END
