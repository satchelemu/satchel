#import <Foundation/Foundation.h>
BOOL SatchelBeginSaveAccess(NSString * _Nonnull title, BOOL allowRunning);
void SatchelEndSaveAccess(void);
void SaveTestSetTitle(NSString * _Nullable title);
void SaveTestGate(void);
