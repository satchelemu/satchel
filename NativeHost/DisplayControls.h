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

// A game on screen on a half-folded iPhone Duo plays upright (portrait). The hinge crosses the
// long side of the inner screen, so the two halves sit one above the other only when the
// phone is turned upright: the picture on the top half, the controls on the bottom half.
// Every other state (app screens, the library over a paused game, closed, fully open) stays
// landscape, and the launch-time Info.plist stays landscape only. Main thread.
FOUNDATION_EXPORT NSNotificationName const SatchelGameOrientationDidChange;
UIInterfaceOrientationMask SatchelGameOrientationMask(void);
// Turns the scene to the current mask if it is not there yet. The app calls this when the
// notification fires and when a scene becomes active.
void SatchelFollowGameOrientation(UIWindowScene *scene);
// A fixture that draws an emulated screen inside the real one keeps the real scene as it is.
void SatchelGameOrientationFollowsPosture(BOOL follows);
