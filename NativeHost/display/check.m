#import "PlayerSettings.h"
#import "DisplayGeometry.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static NSString *const title = @"display-fixture-A";
static NSString *const otherTitle = @"display-fixture-B";
static void near(double a, double b) { assert(fabs(a - b) < 0.00001); }
static void geometry(void) {
    CGRect cover = CGRectMake(0, 0, 874, 402);
    CGRect left = SatchelOcclusionSafeRect(cover, cover, CGRectMake(0, 160, 62, 82));
    CGRect right = SatchelOcclusionSafeRect(cover, cover, CGRectMake(812, 160, 62, 82));
    near(CGRectGetMinX(left), 62); near(CGRectGetMaxX(left), 874);
    near(CGRectGetMinX(right), 0); near(CGRectGetMaxX(right), 812);
    CGRect inner = CGRectMake(0, 0, 1000, 740);
    CGRect innerSafe = SatchelOcclusionSafeRect(inner, inner, CGRectMake(940, 300, 60, 80));
    near(CGRectGetMinX(innerSafe), 0); near(CGRectGetMaxX(innerSafe), 940);
    CGRect half = SatchelFoldRegion(inner, 1, 1);
    near(half.origin.y, 370); near(half.size.height, 370);
    assert(CGRectEqualToRect(SatchelOcclusionSafeRect(half, inner, CGRectMake(0, 100, 60, 80)), half));
    assert(CGRectEqualToRect(SatchelOcclusionSafeRect(inner, inner, CGRectMake(1200, 0, 50, 50)), inner));
    assert(CGRectEqualToRect(SatchelFoldRegion(inner, 0, 0), inner));
    CGRect topCamera = SatchelOcclusionSafeRect(inner, inner, CGRectMake(480, 0, 40, 30));
    near(topCamera.origin.y, 30); near(topCamera.origin.x, 0); near(topCamera.size.width, 1000);
    CGRect odd = SatchelFoldRegion(CGRectMake(4, 9, 800, 501), 1, 1);
    near(odd.origin.y, 259); near(odd.size.height, 251);
    CGRect wide = CGRectMake(0, 0, 844, 390);
    CGRect fit = SatchelPictureRect(wide, 0), stretch = SatchelPictureRect(wide, 1);
    near(fit.origin.x, 75); near(fit.origin.y, 0); near(fit.size.width, 390 * 16.0 / 9.0); near(fit.size.height, 390);
    assert(CGRectEqualToRect(stretch, wide));
    assert(CGRectEqualToRect(SatchelPictureRect(wide, 2), fit)); // Retired modes cannot crop.
    for (int mode = 0; mode < 2; mode++) {
        CGRect r = SatchelPictureRect(wide, mode);
        for (int x = 1; x < 10; x++) for (int y = 1; y < 10; y++) {
            CGPoint point = CGPointMake(r.origin.x + r.size.width * x / 10, r.origin.y + r.size.height * y / 10);
            CGPoint t = SatchelPictureTouch(point, r); near(t.x, x / 10.0); near(t.y, y / 10.0);
        }
        CGPoint a = SatchelPictureTouch(CGPointMake(-1000, -1000), r), b = SatchelPictureTouch(CGPointMake(2000, 2000), r);
        near(a.x, 0); near(a.y, 0); near(b.x, 0.999); near(b.y, 0.999);
    }
    CGRect offset = SatchelPictureRect(CGRectMake(10, 20, 844, 390), 0);
    near(offset.origin.x, 85); near(offset.origin.y, 20);
    CGRect narrow = SatchelPictureRect(CGRectMake(0, 0, 390, 844), 2);
    assert(narrow.origin.x == 0 && narrow.origin.y > 0 && narrow.size.width == 390);
    assert(CGRectEqualToRect(SatchelPictureRect(CGRectZero, 2), CGRectZero));
    assert(CGPointEqualToPoint(SatchelPictureTouch(CGPointMake(1, 1), CGRectZero), CGPointZero));
}
int main(int argc, const char **argv) {
    @autoreleasepool {
        geometry();
        NSString *phase = @(argv[1]);
        NSUserDefaults *defaults = NSUserDefaults.standardUserDefaults;
        if ([phase isEqualToString:@"setup"]) {
            [defaults removeObjectForKey:@"SatchelPlayerSettings.display-fixture-A"];
            [defaults removeObjectForKey:@"SatchelPlayerSettings.display-fixture-B"];
            // A game without a profile entry keeps its own frame logic at full scale.
            assert(([SatchelPlayerSettings(title) isEqual:@{@"DM_FPS_TARGET": @"0", @"DM_RENDER_SCALE": @"1.0", @"DM_SCREEN_MODE": @"fit", @"DM_HAPTICS": @"on"}]));
            // Tears of the Kingdom expects 30 (its frame-step patch keeps game time real), at full scale.
            [defaults removeObjectForKey:@"SatchelPlayerSettings.0100F2C0115B6000"];
            assert([SatchelPlayerSettings(@"0100F2C0115B6000")[@"DM_FPS_TARGET"] isEqual:@"30"]);
            assert([SatchelPlayerSettings(@"0100F2C0115B6000")[@"DM_RENDER_SCALE"] isEqual:@"1.0"]);
            // Frame rates and scales saved by older builds are ignored and dropped; the screen mode stays.
            [defaults setObject:@{@"DM_FPS_TARGET": @"30", @"DM_RENDER_SCALE": @"0.65", @"DM_SCREEN_MODE": @"stretch", @"DM_HAPTICS": @"on"} forKey:@"SatchelPlayerSettings.display-fixture-B"];
            assert(([SatchelPlayerSettings(otherTitle) isEqual:@{@"DM_FPS_TARGET": @"0", @"DM_RENDER_SCALE": @"1.0", @"DM_SCREEN_MODE": @"stretch", @"DM_HAPTICS": @"on"}]));
            assert(([[defaults dictionaryForKey:@"SatchelPlayerSettings.display-fixture-B"] isEqual:@{@"DM_SCREEN_MODE": @"stretch", @"DM_HAPTICS": @"on"}]));
            [defaults setObject:@{@"DM_SCREEN_MODE": @"zoom"} forKey:@"SatchelPlayerSettings.display-fixture-B"];
            assert([SatchelPlayerSettings(otherTitle)[@"DM_SCREEN_MODE"] isEqual:@"fit"]);
            assert([[defaults dictionaryForKey:@"SatchelPlayerSettings.display-fixture-B"][@"DM_SCREEN_MODE"] isEqual:@"fit"]);
            [defaults setObject:@{@"DM_FPS_TARGET": @120, @"DM_RENDER_SCALE": @"nan", @"DM_SCREEN_MODE": @"bad", @"DM_SAVE": @"bad"} forKey:@"SatchelPlayerSettings.display-fixture-B"];
            assert(([SatchelPlayerSettings(otherTitle) isEqual:@{@"DM_FPS_TARGET": @"0", @"DM_RENDER_SCALE": @"1.0", @"DM_SCREEN_MODE": @"fit", @"DM_HAPTICS": @"on"}]));
            // Frame rate and scale stay in the profile. Screen and vibration are saved.
            SatchelApplyPlayerSettings(otherTitle);
            assert(!strcmp(getenv("DM_FPS_TARGET"), "0") && !strcmp(getenv("DM_RENDER_SCALE"), "1.0"));
            assert(!SatchelQueuePlayerSetting(@"DM_FPS_TARGET", @"60"));
            assert(!SatchelQueuePlayerSetting(@"DM_RENDER_SCALE", @"0.5"));
            assert(!SatchelQueuePlayerSetting(@"DM_SAVE", @"bad"));
            assert([SatchelNextPlayerSettings()[@"DM_FPS_TARGET"] isEqual:@"0"]);
            assert(SatchelSavePlayerSettings(title, @{@"DM_FPS_TARGET": @"60", @"DM_RENDER_SCALE": @"0.75", @"DM_SCREEN_MODE": @"stretch", @"DM_HAPTICS": @"on"}));
            assert(([[defaults dictionaryForKey:@"SatchelPlayerSettings.display-fixture-A"] isEqual:@{@"DM_SCREEN_MODE": @"stretch", @"DM_HAPTICS": @"on"}]));
            assert(!SatchelSavePlayerSettings(title, @{@"DM_SCREEN_MODE": @"wide"}));
            assert(!SatchelSavePlayerSettings(title, @{}));
            setenv("DM_SAVE", "untouched", 1); setenv("DM_SHOW_FPS", "1", 1);
            SatchelApplyPlayerSettings(title);
            assert(!strcmp(getenv("DM_FPS_TARGET"), "0") && SatchelScreenMode() == 1);
            assert(!strcmp(getenv("DM_SAVE"), "untouched") && !strcmp(getenv("DM_SHOW_FPS"), "1"));
            assert(!SatchelChangeScreenMode(@"zoom")); assert(!SatchelChangeScreenMode(@"bad")); assert(SatchelScreenMode() == 1);
            assert(SatchelChangeScreenMode(@"fit")); assert(SatchelScreenMode() == 0);
            assert(SatchelQueuePlayerSetting(@"DM_SCREEN_MODE", @"fit"));
            assert([SatchelPlayerSettings(title)[@"DM_RENDER_SCALE"] isEqual:@"1.0"]);
            assert(SatchelPlayerHaptics());
            assert(SatchelChangePlayerHaptics(NO));
            assert(!SatchelPlayerHaptics());
            assert(SatchelChangeScreenMode(@"stretch"));
            assert([SatchelPlayerSettings(title)[@"DM_HAPTICS"] isEqual:@"off"]);
            assert(SatchelChangeScreenMode(@"fit"));
            assert(!SatchelSavePlayerSettings(title, @{@"DM_SCREEN_MODE": @"fit", @"DM_HAPTICS": @"invalid"}));
            assert([SatchelPlayerSettings(title)[@"DM_HAPTICS"] isEqual:@"off"]);
            SatchelClearPlayerSession();
            assert(!SatchelChangePlayerHaptics(YES));
            SatchelApplyPlayerSettings(otherTitle); assert(SatchelPlayerHaptics());
            SatchelApplyPlayerSettings(title); assert(!SatchelPlayerHaptics());
        } else {
            NSDictionary *values = SatchelPlayerSettings(title);
            assert([values[@"DM_HAPTICS"] isEqual:@"off"]);
            assert([values[@"DM_FPS_TARGET"] isEqual:@(getenv("EXPECT_FPS"))]);
            assert([values[@"DM_RENDER_SCALE"] isEqual:@(getenv("EXPECT_SCALE"))]);
            assert([values[@"DM_SCREEN_MODE"] isEqual:@(getenv("EXPECT_MODE"))]);
            SatchelApplyPlayerSettings(title);
            assert(!strcmp(getenv("DM_FPS_TARGET"), getenv("EXPECT_FPS")));
            assert(!strcmp(getenv("DM_RENDER_SCALE"), getenv("EXPECT_SCALE")));
        }
        [defaults synchronize];
        printf("PASS: settings %s and picture/touch geometry\n", argv[1]);
    }
}
