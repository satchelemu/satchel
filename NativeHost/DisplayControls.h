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
// The presented picture's width / height (16:9 until the game presents another shape).
CGFloat SatchelPictureAspect(void);
// The picture rectangle for the current screen mode and presented shape, in a game region.
CGRect SatchelCurrentPictureRect(CGRect region);
// front.h's report from the present thread (display/picture-shape.patch); main-thread safe.
void front_picture_shape(int w, int h);
