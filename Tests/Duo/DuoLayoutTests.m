// iPhone Duo orientation and hinge coverage for Satchel's production display layout
// (NativeHost/DisplayGeometry.h, NativeHost/DisplayControls.m and the overlaid engine gamepad).
// Adapted from nxnative's dm/ios/tests/duo harness (MIT, upstream e2fadf22 and 600c0b37):
// its layout cases are rewritten for Satchel's posture model (UIHingeInteraction status, with
// the production "posture" test override) and its fold regions. Synthetic: no hinge sensor,
// game, renderer or real touches.
#import <XCTest/XCTest.h>
#import <UIKit/UIKit.h>
#import "DisplayControls.h"
#import "PlayerSettings.h"

UIView *dm_gamepad_view(CGRect frame);
extern int SatchelDuoResets;

static const CGSize kInner = {1004, 700};   // landscape inner screen, about 1.43:1
static const CGSize kCover = {874, 402};    // landscape cover screen, about 2.17:1
static const CGFloat kTolerance = 1.0;

@interface DuoLayoutTests : XCTestCase
@end

@implementation DuoLayoutTests {
    UIView *_root, *_game, *_pad;
}

static void spin(NSTimeInterval seconds) {
    [NSRunLoop.mainRunLoop runUntilDate:[NSDate dateWithTimeIntervalSinceNow:seconds]];
}
static void posture(const char *name) {
    char line[32]; snprintf(line, sizeof line, "posture %s", name);
    XCTAssertEqual(satchel_display_command(line), 1);
    spin(0.05);   // the command applies on the main queue, as from the developer control file
}
static UIWindow *keyWindow(void) {
    for (UIScene *scene in UIApplication.sharedApplication.connectedScenes)
        if ([scene isKindOfClass:UIWindowScene.class])
            for (UIWindow *window in ((UIWindowScene *)scene).windows) if (window.isKeyWindow) return window;
    return nil;
}

- (void)setUp {
    UIWindow *window = keyWindow();
    XCTAssertNotNil(window, @"the harness app shows one window");
    SatchelApplyPlayerSettings(@"DUO-FIXTURE");
    XCTAssertTrue(SatchelChangeScreenMode(@"fit"));
    // One gamepad and one set of display controls for the process, as the app's single
    // native controller has; each test resizes and relays it out.
    static UIView *root, *game, *pad;
    if (!root) {
        root = [[UIView alloc] initWithFrame:(CGRect){CGPointZero, kInner}];
        game = [UIView new]; [root addSubview:game];
        pad = dm_gamepad_view(root.bounds); [root addSubview:pad];
        SatchelInstallDisplayControls(root, game, pad, nil);
    }
    _root = root; _game = game; _pad = pad;
    [window.rootViewController.view addSubview:_root];
    posture("open");
}
- (void)tearDown {
    posture("auto");
    XCTAssertTrue(SatchelChangeScreenMode(@"fit"));
    [_root removeFromSuperview];
}

// front_ios.m's viewDidLayoutSubviews with screen-modes.patch: pad full screen, picture laid out.
- (void)layout:(CGSize)size {
    _root.frame = (CGRect){CGPointZero, size};
    _pad.frame = _root.bounds;
    SatchelDisplayDidLayout();
    SatchelLayoutGameView(_game, _root.bounds);
    [_pad setNeedsLayout]; [_pad layoutIfNeeded];
}
- (CGRect)topHalf { CGRect b = _root.bounds; return CGRectMake(0, 0, b.size.width, floor(b.size.height / 2)); }
- (CGRect)bottomHalf { CGRect b = _root.bounds, t = self.topHalf; return CGRectMake(0, t.size.height, b.size.width, b.size.height - t.size.height); }
- (CGPoint)pad:(NSString *)key { return [[_pad valueForKey:key] CGPointValue]; }
static void assertRectNear(CGRect a, CGRect b, NSString *what) {
    XCTAssertEqualWithAccuracy(a.origin.x, b.origin.x, kTolerance, @"%@ x", what);
    XCTAssertEqualWithAccuracy(a.origin.y, b.origin.y, kTolerance, @"%@ y", what);
    XCTAssertEqualWithAccuracy(a.size.width, b.size.width, kTolerance, @"%@ width", what);
    XCTAssertEqualWithAccuracy(a.size.height, b.size.height, kTolerance, @"%@ height", what);
}
static BOOL inside(CGRect outer, CGRect inner) { return CGRectContainsRect(CGRectInset(outer, -kTolerance, -kTolerance), inner); }

// Open (and closed): the picture overlays the whole screen, fitted 16:9, controls everywhere.
- (void)testOpenLandscapeFitsThePictureAcrossTheWholeScreen {
    [self layout:kInner];
    CGRect bounds = _root.bounds;
    assertRectNear(_game.frame, SatchelPictureRect(bounds, 0), @"open picture");
    XCTAssertEqualWithAccuracy(_game.frame.size.width / _game.frame.size.height, 16.0 / 9.0, 0.01);
    XCTAssertTrue(CGRectEqualToRect(SatchelGameBounds(bounds), bounds));
    XCTAssertTrue(CGRectEqualToRect(SatchelControlBounds(bounds), bounds));
    posture("closed"); [self layout:kCover];
    assertRectNear(_game.frame, SatchelPictureRect(_root.bounds, 0), @"closed picture");
}

// Half folded: the game on the top half, the controls on the bottom half, like a handheld.
- (void)testHalfFoldedPutsThePictureAboveTheControls {
    posture("half"); [self layout:kInner];
    CGRect bounds = _root.bounds, top = self.topHalf, bottom = self.bottomHalf;
    XCTAssertTrue(CGRectEqualToRect(SatchelGameBounds(bounds), top));
    XCTAssertTrue(CGRectEqualToRect(SatchelControlBounds(bounds), bottom));
    XCTAssertTrue(inside(top, _game.frame), @"picture %@ stays in the top half %@", NSStringFromCGRect(_game.frame), NSStringFromCGRect(top));
    assertRectNear(_game.frame, SatchelPictureRect(top, 0), @"half-folded picture");
    XCTAssertEqualWithAccuracy(CGRectGetMidX(_game.frame), CGRectGetMidX(bounds), kTolerance, @"centred across the fold");
    for (NSString *key in @[@"faceCenter", @"stickHome", @"dpadCenter"]) {
        CGPoint p = [self pad:key];
        XCTAssertTrue(CGRectContainsPoint(bottom, p), @"%@ %@ is below the fold %@", key, NSStringFromCGPoint(p), NSStringFromCGRect(bottom));
    }
    CGRect safe = [[_pad valueForKey:@"safe"] CGRectValue];
    XCTAssertTrue(inside(bottom, safe), @"control area %@ stays below the fold", NSStringFromCGRect(safe));
    XCTAssertNotNil(_game.layer.mask, @"the picture is clipped to its region while the hinge moves");
}

- (void)testHalfFoldedStretchFillsOnlyTheTopHalf {
    posture("half"); XCTAssertTrue(SatchelChangeScreenMode(@"stretch")); spin(0.05);
    [self layout:kInner];
    assertRectNear(_game.frame, self.topHalf, @"stretched half-folded picture");
}

// Each change of posture relays the picture out at once and keeps held input owned
// (DisplayControls.m geometryChanged reprojects through dm_gamepad_reproject; since the
// postures work it no longer calls dm_gamepad_reset, so a held control survives the hinge).
- (void)testPostureChangesRelayoutAtOnceAndKeepHeldInput {
    [self layout:kInner];
    NSArray *steps = @[@"half", @"closed", @"half", @"open", @"half"];
    for (NSString *step in steps) {
        int before = SatchelDuoResets;
        posture(step.UTF8String);
        // No resize: the posture alone must move the picture.
        SatchelLayoutGameView(_game, _root.bounds);
        BOOL half = [step isEqualToString:@"half"];
        assertRectNear(_game.frame, SatchelPictureRect(half ? self.topHalf : _root.bounds, 0), step);
        XCTAssertEqual(SatchelDuoResets, before, @"%@ must not release held input", step);
    }
}

// Rejected values leave the posture as it was; auto returns to UIKit's report.
- (void)testUnknownPostureOverrideIsRejectedAndAutoFollowsUIKit {
    posture("half"); [self layout:kInner];
    CGRect half = _game.frame;
    posture("sideways"); SatchelLayoutGameView(_game, _root.bounds);
    assertRectNear(_game.frame, half, @"rejected override");
    posture("auto"); SatchelLayoutGameView(_game, _root.bounds);
    // The Simulator reports no half-open hinge, so auto lays the picture over the screen.
    assertRectNear(_game.frame, SatchelPictureRect(_root.bounds, 0), @"auto posture");
}

// Touch projection shares the picture rectangle in every posture: corners map to the
// guest screen's corners, touches beyond the picture clamp to its edge.
- (void)testHalfFoldedTouchesMapThroughThePicture {
    posture("half"); [self layout:kInner];
    CGRect picture = SatchelPictureRect(SatchelGameBounds(_root.bounds), SatchelScreenMode());
    assertRectNear(picture, _game.frame, @"touch and presentation rectangles");
    CGPoint origin = SatchelPictureTouch(picture.origin, picture);
    XCTAssertEqualWithAccuracy(origin.x, 0, 0.001); XCTAssertEqualWithAccuracy(origin.y, 0, 0.001);
    CGPoint middle = SatchelPictureTouch(CGPointMake(CGRectGetMidX(picture), CGRectGetMidY(picture)), picture);
    XCTAssertEqualWithAccuracy(middle.x, 0.5, 0.001); XCTAssertEqualWithAccuracy(middle.y, 0.5, 0.001);
    CGPoint below = SatchelPictureTouch(CGPointMake(CGRectGetMidX(picture), CGRectGetMaxY(_root.bounds) - 1), picture);
    XCTAssertEqualWithAccuracy(below.y, 0.999, 0.001, @"a touch on the control half clamps to the picture's bottom edge");
}

// Both screens and an odd height split without overlap or a lost row.
- (void)testFoldRegionsTileBothScreensAndOddHeights {
    posture("half");
    for (NSValue *value in @[[NSValue valueWithCGSize:kInner], [NSValue valueWithCGSize:kCover], [NSValue valueWithCGSize:CGSizeMake(1004, 701)]]) {
        [self layout:value.CGSizeValue];
        CGRect bounds = _root.bounds, game = SatchelGameBounds(bounds), controls = SatchelControlBounds(bounds);
        XCTAssertFalse(CGRectIntersectsRect(game, controls), @"%@", value);
        XCTAssertTrue(CGRectEqualToRect(CGRectUnion(game, controls), bounds), @"%@", value);
        XCTAssertEqual(CGRectGetMaxY(game), CGRectGetMinY(controls));
        XCTAssertTrue(inside(game, _game.frame), @"%@", value);
    }
}

// A camera in either landscape rotation narrows only the half it is in: the picture half
// for a camera beside the top half, the controls half for one beside the bottom half.
- (void)testCameraInEitherRotationOnlyNarrowsItsOwnHalf {
    posture("half"); [self layout:kInner];
    CGRect full = _root.bounds, top = SatchelGameBounds(full), bottom = SatchelControlBounds(full);
    CGRect left = CGRectMake(0, 120, 44, 90), right = CGRectMake(full.size.width - 44, 120, 44, 90);
    for (NSValue *camera in @[[NSValue valueWithCGRect:left], [NSValue valueWithCGRect:right]]) {
        CGRect c = camera.CGRectValue;
        XCTAssertTrue(CGRectEqualToRect(SatchelOcclusionSafeRect(bottom, full, c), bottom), @"controls ignore a camera above the fold");
        CGRect narrowed = SatchelOcclusionSafeRect(top, full, c);
        XCTAssertEqualWithAccuracy(narrowed.size.width, top.size.width - 44, kTolerance);
        XCTAssertEqualWithAccuracy(narrowed.size.height, top.size.height, kTolerance);
        CGRect low = CGRectOffset(c, 0, bottom.origin.y);
        XCTAssertTrue(CGRectEqualToRect(SatchelOcclusionSafeRect(top, full, low), top), @"picture ignores a camera below the fold");
        XCTAssertEqualWithAccuracy(SatchelOcclusionSafeRect(bottom, full, low).size.width, bottom.size.width - 44, kTolerance);
    }
}

// Half folded, the game plays upright: the scene stays landscape and the host turns the game's
// window a quarter, upright-sized and still covering the whole scene. Open again, the window is
// plain. Checked from both landscape rotations.
- (void)testHalfFoldedGameTurnsUprightAndBackToLandscape {
    UIWindow *window = keyWindow(); UIWindowScene *scene = window.windowScene;
    for (NSNumber *mask in @[@(UIInterfaceOrientationMaskLandscapeRight), @(UIInterfaceOrientationMaskLandscapeLeft)]) {
        UIInterfaceOrientation want = mask.unsignedIntegerValue == UIInterfaceOrientationMaskLandscapeRight ? UIInterfaceOrientationLandscapeRight : UIInterfaceOrientationLandscapeLeft;
        [scene requestGeometryUpdateWithPreferences:[[UIWindowSceneGeometryPreferencesIOS alloc] initWithInterfaceOrientations:mask.unsignedIntegerValue]
                                      errorHandler:^(NSError *error) { XCTFail(@"rotation refused: %@", error); }];
        // The window's bounds follow the reported orientation a moment later (an earlier
        // case may have left the scene turning), so wait for both.
        for (int i = 0; i < 80 && (scene.effectiveGeometry.interfaceOrientation != want || window.bounds.size.width <= window.bounds.size.height); i++) spin(0.05);
        XCTAssertEqual(scene.effectiveGeometry.interfaceOrientation, want);
        CGRect whole = scene.effectiveGeometry.coordinateSpace.bounds;
        [self layout:window.bounds.size];
        XCTAssertGreaterThan(_root.bounds.size.width, _root.bounds.size.height, @"open: landscape");
        XCTAssertFalse(SatchelGameIsUpright());
        XCTAssertTrue(CGAffineTransformIsIdentity(window.transform));

        posture("half");
        XCTAssertTrue(SatchelGameIsUpright(), @"half folded from rotation %ld: the game turns upright", (long)want);
        XCTAssertEqual(scene.effectiveGeometry.interfaceOrientation, want, @"the scene itself stays landscape");
        // The phone's top edge up: a quarter clockwise from landscape left, counterclockwise from landscape right.
        CGFloat turn = atan2(window.transform.b, window.transform.a);
        XCTAssertEqualWithAccuracy(turn, want == UIInterfaceOrientationLandscapeLeft ? M_PI_2 : -M_PI_2, 0.001);
        assertRectNear(window.bounds, CGRectMake(0, 0, whole.size.height, whole.size.width), @"upright window size");
        assertRectNear(window.frame, whole, @"the turned window still covers the whole scene");
        [self layout:window.bounds.size];
        XCTAssertGreaterThan(_root.bounds.size.height, _root.bounds.size.width, @"upright");
        XCTAssertTrue(inside(self.topHalf, _game.frame), @"picture %@ on the top half", NSStringFromCGRect(_game.frame));
        XCTAssertEqualWithAccuracy(_game.frame.size.width, _root.bounds.size.width, kTolerance, @"the picture takes the half's full width");
        for (NSString *key in @[@"faceCenter", @"stickHome", @"dpadCenter"])
            XCTAssertTrue(CGRectContainsPoint(self.bottomHalf, [self pad:key]), @"%@ on the bottom half", key);
        // A point on the upright top half lands on one side of the fold on the landscape screen.
        CGPoint top = [window convertPoint:CGPointMake(CGRectGetMidX(window.bounds), 10) toCoordinateSpace:scene.effectiveGeometry.coordinateSpace];
        CGPoint bottom = [window convertPoint:CGPointMake(CGRectGetMidX(window.bounds), CGRectGetMaxY(window.bounds) - 10) toCoordinateSpace:scene.effectiveGeometry.coordinateSpace];
        XCTAssertEqualWithAccuracy(top.y, CGRectGetMidY(whole), 1); XCTAssertEqualWithAccuracy(bottom.y, CGRectGetMidY(whole), 1);
        XCTAssertGreaterThan((top.x - CGRectGetMidX(whole)) * (CGRectGetMidX(whole) - bottom.x), 0, @"top %@ and bottom %@ on opposite sides of the fold", NSStringFromCGPoint(top), NSStringFromCGPoint(bottom));
        XCTAssertEqual(top.x < CGRectGetMidX(whole), want == UIInterfaceOrientationLandscapeRight, @"which side of the landscape screen is the upright top");
        spin(0.6);   // UIKit and the 250 ms tick leave the turn in place
        XCTAssertEqualWithAccuracy(atan2(window.transform.b, window.transform.a), turn, 0.001);
        assertRectNear(window.frame, whole, @"still covering the scene");

        posture("open");
        XCTAssertFalse(SatchelGameIsUpright(), @"open again: back to landscape");
        XCTAssertTrue(CGAffineTransformIsIdentity(window.transform));
        assertRectNear(window.frame, whole, @"plain window");
        [self layout:window.bounds.size];
        assertRectNear(_game.frame, SatchelPictureRect(_root.bounds, 0), @"open picture after the turn");
    }
}

// Only a game on screen turns: the library over a paused game and an emulated screen stay landscape.
- (void)testOnlyAGameOnScreenTurnsUpright {
    UIWindow *window = keyWindow();
    posture("half");
    XCTAssertTrue(SatchelGameIsUpright());
    XCTAssertFalse(CGAffineTransformIsIdentity(window.transform));
    [_root removeFromSuperview]; SatchelDisplayDidLayout();
    XCTAssertFalse(SatchelGameIsUpright(), @"no game surface in a window");
    XCTAssertTrue(CGAffineTransformIsIdentity(window.transform), @"the window is plain again");
    XCTAssertGreaterThan(window.bounds.size.width, window.bounds.size.height);
    [window.rootViewController.view addSubview:_root]; SatchelDisplayDidLayout();
    XCTAssertTrue(SatchelGameIsUpright());
    SatchelGameOrientationFollowsPosture(NO);
    XCTAssertFalse(SatchelGameIsUpright(), @"a fixture that draws its own screen");
    XCTAssertTrue(CGAffineTransformIsIdentity(window.transform));
    SatchelGameOrientationFollowsPosture(YES);
    posture("closed");
    XCTAssertFalse(SatchelGameIsUpright());
    XCTAssertTrue(CGAffineTransformIsIdentity(window.transform));
}
@end
