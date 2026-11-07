#import "PlayerSettings.h"
#include <assert.h>
#include <string.h>

int main(int argc, const char **argv) { @autoreleasepool {
    NSString *title = @"UPSCALE-FIXTURE-A", *other = @"UPSCALE-FIXTURE-B";
    if (argc > 1 && !strcmp(argv[1], "read")) {
        assert([SatchelPlayerSettings(title)[@"DM_PRESENT_UPSCALE"] isEqual:@(getenv("EXPECT_UPSCALE"))]);
        puts("PASS: launch override validation");
        return 0;
    }
    NSUserDefaults *defaults = NSUserDefaults.standardUserDefaults;
    for (NSString *name in @[title, other]) [defaults removeObjectForKey:[@"SatchelPlayerSettings." stringByAppendingString:name]];
    if (argc > 1 && !strcmp(argv[1], "override-save")) {
        SatchelApplyPlayerSettings(title);
        assert([SatchelCurrentPlayerSettings()[@"DM_PRESENT_UPSCALE"] isEqual:@"spatial"]);
        assert([SatchelNextPlayerSettings()[@"DM_PRESENT_UPSCALE"] isEqual:@"spatial"]);
        assert(SatchelChangeScreenMode(@"stretch")); assert(SatchelChangePlayerHaptics(NO));
        assert([SatchelSavedPlayerSettings(title)[@"DM_PRESENT_UPSCALE"] isEqual:@"off"]);
        assert(!SatchelPlayerOverrides(title)[@"DM_PRESENT_UPSCALE"]);
        assert([SatchelNextPlayerSettings()[@"DM_PRESENT_UPSCALE"] isEqual:@"spatial"]);
        assert(SatchelSavePlayerSettings(title, @{@"DM_SCREEN_MODE": @"fit"}));
        assert([SatchelSavedPlayerSettings(title)[@"DM_PRESENT_UPSCALE"] isEqual:@"off"]);
        assert(!SatchelPlayerOverrides(title)[@"DM_PRESENT_UPSCALE"]);
        assert([SatchelNextPlayerSettings()[@"DM_PRESENT_UPSCALE"] isEqual:@"spatial"]);
        [defaults removeObjectForKey:[@"SatchelPlayerSettings." stringByAppendingString:title]];
        puts("PASS: saving other controls never persists the launch override");
        return 0;
    }
    assert([SatchelPlayerSettings(title)[@"DM_PRESENT_UPSCALE"] isEqual:@"off"]);
    assert(SatchelSavePlayerSettings(title, @{@"DM_SCREEN_MODE": @"fit", @"DM_PRESENT_UPSCALE": @"spatial"}));
    assert([SatchelPlayerSettings(title)[@"DM_PRESENT_UPSCALE"] isEqual:@"spatial"]);
    assert([SatchelPlayerSettings(other)[@"DM_PRESENT_UPSCALE"] isEqual:@"off"]);
    assert(SatchelSavePlayerSettings(title, @{@"DM_SCREEN_MODE": @"stretch"}));
    assert([SatchelPlayerSettings(title)[@"DM_PRESENT_UPSCALE"] isEqual:@"spatial"]);
    assert(!SatchelSavePlayerSettings(title, @{@"DM_SCREEN_MODE": @"fit", @"DM_PRESENT_UPSCALE": @"temporal"}));
    // Edge smoothing, alone or before the spatial scaler, is a per-title choice like spatial.
    for (NSString *filter in @[@"edge", @"edge-spatial", @"spatial"]) {
        assert(SatchelSavePlayerSettings(title, @{@"DM_PRESENT_UPSCALE": filter}));
        assert([SatchelPlayerSettings(title)[@"DM_PRESENT_UPSCALE"] isEqual:filter]);
        assert([SatchelPlayerSettings(other)[@"DM_PRESENT_UPSCALE"] isEqual:@"off"]);
    }
    assert(!SatchelSavePlayerSettings(title, @{@"DM_PRESENT_UPSCALE": @"edge-temporal"}));
    assert([SatchelPlayerSettings(title)[@"DM_PRESENT_UPSCALE"] isEqual:@"spatial"]);
    SatchelApplyPlayerSettings(title);
    assert(!strcmp(getenv("DM_PRESENT_UPSCALE"), "spatial"));
    assert(SatchelQueuePlayerSetting(@"DM_PRESENT_UPSCALE", @"off"));
    assert([SatchelCurrentPlayerSettings()[@"DM_PRESENT_UPSCALE"] isEqual:@"spatial"]);
    assert([SatchelNextPlayerSettings()[@"DM_PRESENT_UPSCALE"] isEqual:@"off"]);
    assert(!strcmp(getenv("DM_PRESENT_UPSCALE"), "spatial"));
    assert(SatchelChangePlayerHaptics(NO));
    assert([SatchelPlayerSettings(title)[@"DM_PRESENT_UPSCALE"] isEqual:@"off"]);
    SatchelClearPlayerSession(); SatchelApplyPlayerSettings(title);
    assert(!strcmp(getenv("DM_PRESENT_UPSCALE"), "off"));
    // Screen migration must retain the saved scale and upscaler choices.
    [defaults setObject:@{@"DM_SCREEN_MODE": @"zoom", @"DM_RENDER_SCALE": @"0.65", @"DM_PRESENT_UPSCALE": @"spatial"}
                 forKey:[@"SatchelPlayerSettings." stringByAppendingString:other]];
    assert([SatchelPlayerSettings(other)[@"DM_PRESENT_UPSCALE"] isEqual:@"spatial"]);
    assert([SatchelPlayerSettings(other)[@"DM_SCREEN_MODE"] isEqual:@"fit"]);
    assert([SatchelSavedPlayerSettings(other)[@"DM_RENDER_SCALE"] isEqual:@"0.65"]);
    assert(SatchelResetPlayerSetting(other, @"DM_PRESENT_UPSCALE"));
    assert([SatchelSavedPlayerSettings(other)[@"DM_PRESENT_UPSCALE"] isEqual:@"off"]);
    assert([SatchelSavedPlayerSettings(other)[@"DM_RENDER_SCALE"] isEqual:@"0.65"]);
    for (NSString *name in @[title, other]) [defaults removeObjectForKey:[@"SatchelPlayerSettings." stringByAppendingString:name]];
    SatchelClearPlayerSession();
    puts("PASS: per-title upscale persistence, validation, migration and next-launch isolation");
} }
