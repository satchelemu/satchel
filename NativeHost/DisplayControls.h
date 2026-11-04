#import <UIKit/UIKit.h>
#import "PlayerSettings.h"
#import "DisplayGeometry.h"

void SatchelLayoutGameView(UIView *game, CGRect bounds);
void SatchelInstallDisplayControls(UIView *root, UIView *game, UIView *pad, UILabel *fps);
// Returns 1 for a display command, including invalid display arguments.
int satchel_display_command(const char *line);

CGRect SatchelGameBounds(CGRect bounds);
CGRect SatchelControlBounds(CGRect bounds);
void SatchelDisplayDidLayout(void);
CGRect SatchelInteractiveBounds(UIView *view, CGRect region);
