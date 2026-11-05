#import "PlayerSettings.h"
#import "DisplayGeometry.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static NSString *const title = @"DISPLAY-FIXTURE-A";
static NSString *const otherTitle = @"DISPLAY-FIXTURE-B";
static void near(double a, double b) { assert(fabs(a - b) < 0.00001); }
// Replaces PlayerSettings.m's weak default, as the controller overlay does in the app.
static float tunedGain = -1, tunedCutoff = -1; static int tuneCalls;
void satchel_haptics_tune(float gain, float cutoff) { tunedGain = gain; tunedCutoff = cutoff; tuneCalls++; }
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
            [defaults removeObjectForKey:@"SatchelPlayerSettings.DISPLAY-FIXTURE-A"];
            [defaults removeObjectForKey:@"SatchelPlayerSettings.DISPLAY-FIXTURE-B"];
            // A game without a profile entry keeps its own frame logic at full scale.
            assert(([SatchelPlayerSettings(title) isEqual:@{@"DM_FPS_TARGET": @"0", @"DM_RENDER_SCALE": @"1.0", @"DM_SCREEN_MODE": @"fit", @"DM_HAPTICS": @"on", @"DM_PIPELINE_MODE": @"exact", @"DM_PRESENT_UPSCALE": @"off", @"DM_RUMBLE_GAIN": @"1.0", @"DM_RUMBLE_MIN": @"0", @"DM_SCREEN_ASPECT": @"0"}]));
            // Tears of the Kingdom expects 30 (its frame-step patch keeps game time real), at full scale.
            [defaults removeObjectForKey:@"SatchelPlayerSettings.0100F2C0115B6000"];
            assert([SatchelPlayerSettings(@"0100F2C0115B6000")[@"DM_FPS_TARGET"] isEqual:@"30"]);
            assert([SatchelPlayerSettings(@"0100F2C0115B6000")[@"DM_RENDER_SCALE"] isEqual:@"1.0"]);
            // All validated per-title choices are read by both the page and launch.
            [defaults setObject:@{@"DM_FPS_TARGET": @"30", @"DM_RENDER_SCALE": @"0.65", @"DM_SCREEN_MODE": @"stretch", @"DM_HAPTICS": @"on"} forKey:@"SatchelPlayerSettings.DISPLAY-FIXTURE-B"];
            assert(([SatchelPlayerSettings(otherTitle) isEqual:@{@"DM_FPS_TARGET": @"30", @"DM_RENDER_SCALE": @"0.65", @"DM_SCREEN_MODE": @"stretch", @"DM_HAPTICS": @"on", @"DM_PIPELINE_MODE": @"exact", @"DM_PRESENT_UPSCALE": @"off", @"DM_RUMBLE_GAIN": @"1.0", @"DM_RUMBLE_MIN": @"0", @"DM_SCREEN_ASPECT": @"0"}]));
            assert(([SatchelPlayerOverrides(otherTitle) isEqual:@{@"DM_FPS_TARGET": @"30", @"DM_RENDER_SCALE": @"0.65", @"DM_SCREEN_MODE": @"stretch", @"DM_HAPTICS": @"on"}]));
            [defaults setObject:@{@"DM_SCREEN_MODE": @"zoom"} forKey:@"SatchelPlayerSettings.DISPLAY-FIXTURE-B"];
            assert([SatchelPlayerSettings(otherTitle)[@"DM_SCREEN_MODE"] isEqual:@"fit"]);
            assert([[defaults dictionaryForKey:@"SatchelPlayerSettings.DISPLAY-FIXTURE-B"][@"DM_SCREEN_MODE"] isEqual:@"fit"]);
            [defaults setObject:@{@"DM_FPS_TARGET": @120, @"DM_RENDER_SCALE": @"nan", @"DM_SCREEN_MODE": @"bad", @"DM_SAVE": @"bad"} forKey:@"SatchelPlayerSettings.DISPLAY-FIXTURE-B"];
            assert(([SatchelPlayerSettings(otherTitle) isEqual:@{@"DM_FPS_TARGET": @"0", @"DM_RENDER_SCALE": @"1.0", @"DM_SCREEN_MODE": @"fit", @"DM_HAPTICS": @"on", @"DM_PIPELINE_MODE": @"exact", @"DM_PRESENT_UPSCALE": @"off", @"DM_RUMBLE_GAIN": @"1.0", @"DM_RUMBLE_MIN": @"0", @"DM_SCREEN_ASPECT": @"0"}]));
            // Queue frame rate and scale for the next launch without changing the running environment.
            SatchelApplyPlayerSettings(otherTitle);
            assert(!strcmp(getenv("DM_FPS_TARGET"), "0") && !strcmp(getenv("DM_RENDER_SCALE"), "1.0"));
            assert(SatchelQueuePlayerSetting(@"DM_FPS_TARGET", @"60"));
            assert(SatchelQueuePlayerSetting(@"DM_RENDER_SCALE", @"0.5"));
            assert(!SatchelQueuePlayerSetting(@"DM_SAVE", @"bad"));
            assert([SatchelNextPlayerSettings()[@"DM_FPS_TARGET"] isEqual:@"60"]);
            assert([SatchelCurrentPlayerSettings()[@"DM_FPS_TARGET"] isEqual:@"0"]);
            assert(!strcmp(getenv("DM_FPS_TARGET"), "0") && !strcmp(getenv("DM_RENDER_SCALE"), "1.0"));
            assert(SatchelSavePlayerSettings(title, @{@"DM_FPS_TARGET": @"60", @"DM_RENDER_SCALE": @"0.75", @"DM_SCREEN_MODE": @"stretch", @"DM_HAPTICS": @"on"}));
            assert(([SatchelPlayerOverrides(title) isEqual:@{@"DM_FPS_TARGET": @"60", @"DM_RENDER_SCALE": @"0.75", @"DM_SCREEN_MODE": @"stretch", @"DM_HAPTICS": @"on"}]));
            assert(!SatchelSavePlayerSettings(title, @{@"DM_SCREEN_MODE": @"wide"}));
            assert(!SatchelSavePlayerSettings(title, @{}));
            setenv("DM_SAVE", "untouched", 1); setenv("DM_SHOW_FPS", "1", 1);
            SatchelApplyPlayerSettings(title);
            assert(!strcmp(getenv("DM_FPS_TARGET"), "60") && SatchelScreenMode() == 1);
            assert(!strcmp(getenv("DM_SAVE"), "untouched") && !strcmp(getenv("DM_SHOW_FPS"), "1"));
            assert(!SatchelChangeScreenMode(@"zoom")); assert(!SatchelChangeScreenMode(@"bad")); assert(SatchelScreenMode() == 1);
            assert(SatchelChangeScreenMode(@"fit")); assert(SatchelScreenMode() == 0);
            assert(SatchelQueuePlayerSetting(@"DM_SCREEN_MODE", @"fit"));
            assert([SatchelPlayerSettings(title)[@"DM_RENDER_SCALE"] isEqual:@"0.75"]);
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
            assert(SatchelSavePlayerSettings(title.lowercaseString, @{@"DM_PIPELINE_MODE": @"hybrid"}));
            assert([SatchelPlayerOverrides(title)[@"DM_PIPELINE_MODE"] isEqual:@"hybrid"]);
            assert([SatchelNextPlayerSettings()[@"DM_PIPELINE_MODE"] isEqual:@"hybrid"]);
            assert([SatchelCurrentPlayerSettings()[@"DM_PIPELINE_MODE"] isEqual:@"exact"]);
            NSDictionary *before = SatchelPlayerOverrides(title);
            assert(!SatchelSavePlayerSettings(title, @{@"DM_PIPELINE_MODE": @"bad", @"DM_SCREEN_MODE": @"stretch"}));
            assert([SatchelPlayerOverrides(title) isEqual:before]);
            assert(!SatchelResetPlayerSetting(title, @"DM_SAVE"));
            assert([SatchelPlayerOverrides(otherTitle)[@"DM_FPS_TARGET"] isEqual:@"60"]);
            assert([SatchelPlayerOverrides(otherTitle)[@"DM_RENDER_SCALE"] isEqual:@"0.5"]);
            assert([SatchelSavedPlayerSettings(otherTitle)[@"DM_PIPELINE_MODE"] isEqual:@"exact"]);
            for (NSString *key in @[@"DM_FPS_TARGET", @"DM_RENDER_SCALE", @"DM_PIPELINE_MODE"]) {
                assert(SatchelResetPlayerSetting(title.lowercaseString, key));
                assert(!SatchelPlayerOverrides(title)[key]);
                assert([SatchelSavedPlayerSettings(title)[key] isEqual:SatchelPlayerDefaults(title)[key]]);
            }
            assert([SatchelCurrentPlayerSettings()[@"DM_FPS_TARGET"] isEqual:@"60"]);
            assert(!strcmp(getenv("DM_FPS_TARGET"), "60"));
            assert([SatchelNextPlayerSettings()[@"DM_FPS_TARGET"] isEqual:@"0"]);
            assert([SatchelPlayerOverrides(title)[@"DM_HAPTICS"] isEqual:@"off"]);
            assert(SatchelSavePlayerSettings(@"0100F2C0115B6000", @{@"DM_FPS_TARGET": @"60"}));
            assert(SatchelResetPlayerSetting(@"0100F2C0115B6000", @"DM_FPS_TARGET"));
            assert([SatchelSavedPlayerSettings(@"0100F2C0115B6000")[@"DM_FPS_TARGET"] isEqual:@"30"]);
            assert([SatchelPlayerGlobalDefaults()[@"DM_FPS_TARGET"] isEqual:@"0"]);
            // The picture shape is gated per title (PlayerProfiles.plist screenAspectTitles, empty).
            assert(!SatchelPlayerSupportsScreenAspect(title) && !SatchelPlayerSupportsScreenAspect(@"0100F2C0115B6000"));
            assert(!SatchelSavePlayerSettings(title, @{@"DM_SCREEN_ASPECT": @"1.43"}));
            assert(!SatchelSavePlayerSettings(title, @{@"DM_SCREEN_ASPECT": @"1.5"}));
            assert(SatchelSavePlayerSettings(title, @{@"DM_SCREEN_ASPECT": @"0"}) && SatchelResetPlayerSetting(title, @"DM_SCREEN_ASPECT"));
            [defaults setObject:@{@"DM_SCREEN_ASPECT": @"1.43"} forKey:@"SatchelPlayerSettings.DISPLAY-FIXTURE-C"];
            SatchelApplyPlayerSettings(@"DISPLAY-FIXTURE-C");
            assert(!strcmp(getenv("DM_SCREEN_ASPECT"), "0")); // A stored shape never reaches a title without screen-shape patches.
            [defaults removeObjectForKey:@"SatchelPlayerSettings.DISPLAY-FIXTURE-C"];
            SatchelApplyPlayerSettings(title);
            // A taller presented picture keeps its own shape; invalid shapes fall back to 16:9.
            CGRect duo = CGRectMake(0, 0, 1004, 700);
            CGRect tall = SatchelPictureRectAspect(duo, 0, 1920.0 / 1344.0), sixteen = SatchelPictureRectAspect(duo, 0, NAN);
            near(tall.size.width, 1000); near(tall.size.height, 700);
            assert(CGRectEqualToRect(sixteen, SatchelPictureRect(duo, 0)));
            assert(CGRectEqualToRect(SatchelPictureRectAspect(duo, 1, 1.43), duo));
            // Rumble strength and controller cutoff: validated ranges, launch environment and live retune.
            assert([SatchelPlayerGlobalDefaults()[@"DM_RUMBLE_GAIN"] isEqual:@"1.0"]);
            assert([SatchelPlayerGlobalDefaults()[@"DM_RUMBLE_MIN"] isEqual:@"0"]);
            for (NSString *bad in @[@"3.01", @"-1", @"nan", @"inf", @"1x", @"", @"1.0.0"])
                assert(!SatchelSavePlayerSettings(title, @{@"DM_RUMBLE_GAIN": bad}));
            for (NSString *bad in @[@"0.91", @"1", @"-0.1", @"nan"])
                assert(!SatchelSavePlayerSettings(title, @{@"DM_RUMBLE_MIN": bad}));
            assert(!SatchelPlayerOverrides(title)[@"DM_RUMBLE_GAIN"] && !SatchelPlayerOverrides(title)[@"DM_RUMBLE_MIN"]);
            SatchelApplyPlayerSettings(title);
            assert(!strcmp(getenv("DM_RUMBLE_GAIN"), "1.0") && !strcmp(getenv("DM_RUMBLE_MIN"), "0"));
            assert(SatchelQueuePlayerSetting(@"DM_RUMBLE_GAIN", @"2.5"));
            assert(SatchelQueuePlayerSetting(@"DM_RUMBLE_MIN", @"0.45"));
            assert(tunedGain == 2.5f && tunedCutoff == 0.45f && tuneCalls >= 3);
            assert([SatchelNextPlayerSettings()[@"DM_RUMBLE_GAIN"] isEqual:@"2.5"]);
            assert(!strcmp(getenv("DM_RUMBLE_GAIN"), "1.0")); // A live change never rewrites the process environment.
            assert(SatchelPlayerOverrides(otherTitle)[@"DM_RUMBLE_GAIN"] == nil);
            assert(SatchelSavePlayerSettings(title, @{@"DM_RUMBLE_GAIN": @"0", @"DM_RUMBLE_MIN": @"0.9"}));
            assert(SatchelSavePlayerSettings(title, @{@"DM_RUMBLE_GAIN": @"3"}));
            assert(SatchelResetPlayerSetting(title, @"DM_RUMBLE_GAIN") && SatchelResetPlayerSetting(title, @"DM_RUMBLE_MIN"));
            assert(tunedGain == 1.0f && tunedCutoff == 0.0f);
            assert(!SatchelPlayerOverrides(title)[@"DM_RUMBLE_GAIN"] && [SatchelSavedPlayerSettings(title)[@"DM_RUMBLE_MIN"] isEqual:@"0"]);

        } else {
            NSDictionary *values = SatchelPlayerSettings(title);
            assert([values[@"DM_HAPTICS"] isEqual:@"off"]);
            assert([values[@"DM_FPS_TARGET"] isEqual:@(getenv("EXPECT_FPS"))]);
            assert([values[@"DM_RENDER_SCALE"] isEqual:@(getenv("EXPECT_SCALE"))]);
            assert([values[@"DM_SCREEN_MODE"] isEqual:@(getenv("EXPECT_MODE"))]);
            const char *pipeline = getenv("EXPECT_PIPELINE");
            assert([values[@"DM_PIPELINE_MODE"] isEqual:@(pipeline ?: "exact")]);
            SatchelApplyPlayerSettings(title);
            assert(!strcmp(getenv("DM_FPS_TARGET"), getenv("EXPECT_FPS")));
            assert(!strcmp(getenv("DM_RENDER_SCALE"), getenv("EXPECT_SCALE")));
            // Editing another field must not reset a one-launch picture override.
            assert(SatchelQueuePlayerSetting(@"DM_PIPELINE_MODE", @"hybrid"));
            for (NSString *key in @[@"DM_FPS_TARGET", @"DM_RENDER_SCALE", @"DM_SCREEN_MODE"])
                assert([SatchelNextPlayerSettings()[key] isEqual:values[key]]);
            assert(SatchelScreenMode() == ([values[@"DM_SCREEN_MODE"] isEqual:@"stretch"] ? 1 : 0));
            assert(!strcmp(getenv("DM_FPS_TARGET"), getenv("EXPECT_FPS")));
            assert(!strcmp(getenv("DM_RENDER_SCALE"), getenv("EXPECT_SCALE")));
            assert(SatchelResetPlayerSetting(title, @"DM_PIPELINE_MODE"));
        }
        [defaults synchronize];
        printf("PASS: settings %s and picture/touch geometry\n", argv[1]);
    }
}
