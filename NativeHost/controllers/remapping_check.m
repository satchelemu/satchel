#import "ControllerProfiles.h"
#import "PlayerSettings.h"
#include <assert.h>
#include <math.h>

static void near(float a, float b) { assert(fabsf(a - b) < 0.0001); }
int main(int argc, const char **argv) {
    @autoreleasepool {
        // A dedicated preferences domain, never the installed app's settings.
        NSUserDefaults *d = NSUserDefaults.standardUserDefaults;
        if (argc > 1) {
            assert([SatchelControllerProfile(@"fixture-a", @"Xbox")[@"deadZone"] doubleValue] == 0.3);
            assert([SatchelControllerProfile(@"fixture-b", @"Xbox")[@"deadZone"] doubleValue] == 0.2);
            puts("PASS: JSON profiles persist across processes"); return 0;
        }
        for (NSString *title in @[@"default", @"fixture-a", @"fixture-b"]) [d removeObjectForKey:[@"SatchelPlayerSettings." stringByAppendingString:[title isEqual:@"default"] ? title : title.uppercaseString]];
        NSDictionary *base = SatchelControllerProfile(@"", @"Xbox");
        assert([base[@"mapping"][GCInputButtonA] isEqual:@"B"]);
        assert([SatchelControllerProfile(@"", @"Pro Controller")[@"mapping"][GCInputButtonA] isEqual:@"A"]);
        NSMutableDictionary *p = [base mutableCopy]; p[@"deadZone"] = @0.2;
        assert(SatchelSaveControllerProfile(@"", @"Xbox", p));
        p[@"deadZone"] = @0.3;
        assert(SatchelSaveControllerProfile(@"fixture-a", @"Xbox", p));
        assert(SatchelSavePlayerSettings(@"fixture-a", @{@"DM_SCREEN_MODE": @"stretch", @"DM_HAPTICS": @"off"}));
        assert([SatchelControllerProfile(@"fixture-a", @"Xbox")[@"deadZone"] doubleValue] == 0.3);
        assert(SatchelSavePlayerSettings(@"fixture-a", @{@"DM_FPS_TARGET": @"60", @"DM_RENDER_SCALE": @"0.8", @"DM_PIPELINE_MODE": @"hybrid"}));
        assert(SatchelResetPlayerSetting(@"fixture-a", @"DM_RENDER_SCALE"));
        assert([SatchelControllerProfile(@"FIXTURE-A", @"Xbox")[@"deadZone"] doubleValue] == 0.3);
        assert([SatchelPlayerSettings(@"fixture-a")[@"DM_FPS_TARGET"] isEqual:@"60"]);
        p[@"deadZone"] = @0.8; assert(!SatchelSaveControllerProfile(@"", @"Xbox", p));
        p[@"deadZone"] = @0.2;
        NSMutableDictionary *mapping = [p[@"mapping"] mutableCopy]; mapping[GCInputButtonA] = @"ZR"; p[@"mapping"] = mapping;
        NSMutableDictionary *chord = [NSMutableDictionary dictionary];
        NSDictionary *r = SatchelMapController(@{GCInputButtonA: @1, @"lx": @0.1, @"rx": @1}, p, YES, chord);
        assert([r[@"buttons"] unsignedLongLongValue] == (1ull << 9)); near([r[@"lx"] floatValue], 0); near([r[@"rx"] floatValue], 1);
        r = SatchelMapController(@{@"lx": @0.6, @"ly": @0}, p, YES, chord); near([r[@"lx"] floatValue], 0.5);
        r = SatchelMapController(@{@"lx": @(NAN), @"ry": @(INFINITY)}, p, YES, chord); near([r[@"lx"] floatValue], 0); near([r[@"ry"] floatValue], 0);
        mapping[GCInputButtonHome] = @"A";
        r = SatchelMapController(@{GCInputButtonHome: @1}, p, YES, chord);
        assert([r[@"buttons"] unsignedLongLongValue] == 0 && [r[@"menuHeld"] boolValue]);
        // Exercise the no-Home fallback and explicit chord with both press orders.
        for (NSString *shortcut in @[@"home", @"menu-options"]) {
            p[@"shortcut"] = shortcut;
            BOOL hasHome = [shortcut isEqual:@"menu-options"];
            for (NSString *first in @[GCInputButtonMenu, GCInputButtonOptions]) {
                NSString *second = [first isEqual:GCInputButtonMenu] ? GCInputButtonOptions : GCInputButtonMenu;
                uint64_t bit = [first isEqual:GCInputButtonMenu] ? 1ull << 10 : 1ull << 11;
                [chord removeAllObjects];
                // Short Plus and Minus presses reach the guest and release normally.
                r = SatchelMapController(@{first: @1}, p, hasHome, chord);
                assert([r[@"buttons"] unsignedLongLongValue] == bit && ![r[@"menuHeld"] boolValue]);
                r = SatchelMapController(@{}, p, hasHome, chord);
                assert([r[@"buttons"] unsignedLongLongValue] == 0);
                // One sampled frame before completion is delivered, none after completion.
                r = SatchelMapController(@{first: @1}, p, hasHome, chord);
                assert([r[@"buttons"] unsignedLongLongValue] == bit);
                NSMutableDictionary *hold = [NSMutableDictionary dictionary];
                for (NSNumber *time in @[@0, @0.74, @0.75, @1]) {
                    r = SatchelMapController(@{first: @1, second: @1}, p, hasHome, chord);
                    assert([r[@"buttons"] unsignedLongLongValue] == 0 && [r[@"menuHeld"] boolValue]);
                    assert([r[@"suppressedButtons"] unsignedLongLongValue] == ((1ull << 10) | (1ull << 11)));
                    assert(SatchelControllerHold(hold, [r[@"menuHeld"] boolValue], time.doubleValue) == (time.doubleValue == 0.75));
                }
                // Either release order remains suppressed, including re-pressing the chord.
                for (NSString *remaining in @[first, second]) {
                    r = SatchelMapController(@{remaining: @1}, p, hasHome, chord);
                    assert([r[@"buttons"] unsignedLongLongValue] == 0 && ![r[@"menuHeld"] boolValue]);
                    assert(![r[@"shortcutReleased"] boolValue]);
                    r = SatchelMapController(@{first: @1, second: @1}, p, hasHome, chord);
                    assert([r[@"buttons"] unsignedLongLongValue] == 0);
                }
                r = SatchelMapController(@{}, p, hasHome, chord);
                assert([r[@"shortcutReleased"] boolValue]);
                r = SatchelMapController(@{first: @1}, p, hasHome, chord);
                assert([r[@"buttons"] unsignedLongLongValue] == bit);
                // Simultaneous arrival delivers zero chord frames, including a short chord.
                [chord removeAllObjects];
                r = SatchelMapController(@{first: @1, second: @1}, p, hasHome, chord);
                assert([r[@"buttons"] unsignedLongLongValue] == 0);
                r = SatchelMapController(@{second: @1}, p, hasHome, chord);
                assert([r[@"buttons"] unsignedLongLongValue] == 0);
                SatchelMapController(@{}, p, hasHome, chord);
            }
        }
        p[@"shortcut"] = @"off";
        r = SatchelMapController(@{GCInputButtonMenu: @1, GCInputButtonOptions: @1}, p, YES, chord);
        assert([r[@"buttons"] unsignedLongLongValue] == ((1ull << 10) | (1ull << 11)) && ![r[@"menuHeld"] boolValue]);
        // A compact pad with neither Home nor Options (real third-party hardware): holding Menu
        // alone opens the menu after 0.75 seconds, and Menu still reaches the game as Plus.
        p[@"shortcut"] = @"home";
        [chord removeAllObjects];
        r = SatchelMapControllerElements(@{GCInputButtonMenu: @1}, p, NO, NO, chord);
        assert([r[@"buttons"] unsignedLongLongValue] == (1ull << 10) && [r[@"menuHeld"] boolValue]);
        NSMutableDictionary *menuHold = [NSMutableDictionary dictionary];
        assert(!SatchelControllerHold(menuHold, [r[@"menuHeld"] boolValue], 0));
        assert(SatchelControllerHold(menuHold, [r[@"menuHeld"] boolValue], 0.75));
        r = SatchelMapControllerElements(@{}, p, NO, NO, chord);
        assert([r[@"buttons"] unsignedLongLongValue] == 0 && ![r[@"menuHeld"] boolValue] && [r[@"shortcutReleased"] boolValue]);
        // With Options present, Menu alone is an ordinary Plus press and the chord is unchanged.
        r = SatchelMapControllerElements(@{GCInputButtonMenu: @1}, p, NO, YES, chord);
        assert([r[@"buttons"] unsignedLongLongValue] == (1ull << 10) && ![r[@"menuHeld"] boolValue]);
        SatchelMapControllerElements(@{}, p, NO, YES, chord);
        // Missing optional elements read as released, never as a press or a crash.
        r = SatchelMapControllerElements(@{GCInputButtonA: @1, GCInputButtonHome: @0, GCInputButtonOptions: @0,
            GCInputLeftThumbstickButton: @0, GCInputRightThumbstickButton: @0}, p, NO, NO, chord);
        assert([r[@"buttons"] unsignedLongLongValue] == (1ull << 9) && ![r[@"menuHeld"] boolValue]);
        p[@"shortcut"] = @"off";
        assert([SatchelControllerProfile(@"fixture-a", @"DualSense")[@"deadZone"] doubleValue] == 0.15);
        SatchelResetControllerProfile(@"fixture-a", @"Xbox");
        assert([SatchelControllerProfile(@"fixture-a", @"Xbox")[@"deadZone"] doubleValue] == 0.2);
        assert([SatchelPlayerSettings(@"fixture-a")[@"DM_HAPTICS"] isEqual:@"off"]);
        p[@"deadZone"] = @0.3; assert(SatchelSaveControllerProfile(@"fixture-a", @"Xbox", p));
        [d setObject:@{@"ControllerProfiles": @"corrupt"} forKey:@"SatchelPlayerSettings.CORRUPT-FIXTURE"];
        assert([SatchelControllerProfile(@"corrupt-fixture", @"Xbox")[@"deadZone"] doubleValue] == 0.2);
        [d removeObjectForKey:@"SatchelPlayerSettings.CORRUPT-FIXTURE"];
        NSMutableDictionary *state = [NSMutableDictionary dictionary];
        assert(!SatchelControllerHold(state, YES, 0)); assert(!SatchelControllerHold(state, YES, 0.74));
        assert(SatchelControllerHold(state, YES, 0.75)); assert(!SatchelControllerHold(state, YES, 8));
        assert(!SatchelControllerHold(state, NO, 9)); assert(!SatchelControllerHold(state, YES, 10));
        assert(SatchelControllerHold(state, YES, 11));
        [state removeAllObjects];
        assert([SatchelControllerEdge(state, 0.7, 0, NO, NO) isEqual:@"right"]);
        assert([SatchelControllerEdge(state, 0.7, 0, NO, NO) isEqual:@""]);
        assert([SatchelControllerEdge(state, 0.4, 0, NO, NO) isEqual:@""]);
        assert([SatchelControllerEdge(state, 0.1, 0, NO, NO) isEqual:@""]);
        assert([SatchelControllerEdge(state, 0, 1, NO, NO) isEqual:@"up"]);
        assert([SatchelControllerEdge(state, 0, 1, YES, NO) isEqual:@"accept"]);
        assert([SatchelControllerEdge(state, 0, 1, YES, NO) isEqual:@""]);
        // A held direction repeats after the initial delay at a steady rate; a release or a new
        // direction starts over, and the A and B buttons never repeat.
        [state removeAllObjects];
        assert([SatchelControllerEdge(state, 0, -1, NO, NO) isEqual:@"down"]);
        assert([SatchelControllerRepeat(state, 10) isEqual:@""]);
        assert([SatchelControllerEdge(state, 0, -1, NO, NO) isEqual:@""]);
        assert([SatchelControllerRepeat(state, 10.4) isEqual:@""]);
        assert([SatchelControllerRepeat(state, 10.46) isEqual:@"down"]);
        assert([SatchelControllerRepeat(state, 10.5) isEqual:@""]);
        assert([SatchelControllerRepeat(state, 10.58) isEqual:@"down"]);
        assert([SatchelControllerRepeat(state, 12) isEqual:@"down"]);
        assert([SatchelControllerRepeat(state, 12.05) isEqual:@""]);
        assert([SatchelControllerEdge(state, 0.7, 0, NO, NO) isEqual:@"right"]);
        assert([SatchelControllerRepeat(state, 12.2) isEqual:@""]);
        assert([SatchelControllerRepeat(state, 12.66) isEqual:@"right"]);
        assert([SatchelControllerEdge(state, 0, 0, NO, NO) isEqual:@""]);
        assert([SatchelControllerRepeat(state, 13) isEqual:@""]);
        assert([SatchelControllerEdge(state, 0, 0, YES, NO) isEqual:@"accept"]);
        assert([SatchelControllerRepeat(state, 13) isEqual:@""]);
        assert([SatchelControllerRepeat(state, 14) isEqual:@""]);
        assert([SatchelControllerEdge(state, 0, 0.7, NO, NO) isEqual:@"up"]);
        assert([SatchelControllerRepeat(state, 14) isEqual:@""]);
        assert([SatchelControllerRepeat(state, 14.3) isEqual:@""]);
        // GameController's supported synthetic snapshot traverses the same reader as hardware.
        GCController *controller = [GCController controllerWithExtendedGamepad];
        // A pad with no vendor name and no known product category still gets a full
        // position-based mapping (Generic), never a name match requirement.
        NSArray *labelledCategories = @[@"Pro Controller", @"DualSense"];
        assert(![labelledCategories containsObject:SatchelControllerCategory(controller)]);
        assert([SatchelControllerProfile(@"", SatchelControllerCategory(controller))[@"mapping"][GCInputButtonA] isEqual:@"B"]);
        [controller.extendedGamepad.buttonA setValue:1];
        assert([SatchelReadController(controller.extendedGamepad)[@"buttons"] unsignedLongLongValue] == (1ull << 1));
        [controller.extendedGamepad.buttonA setValue:0];
        [controller.extendedGamepad.leftTrigger setValue:0.1];
        assert([SatchelReadController(controller.extendedGamepad)[@"buttons"] unsignedLongLongValue] ==
            (controller.extendedGamepad.leftTrigger.isPressed ? (1ull << 8) : 0));
        [controller.extendedGamepad.leftThumbstick setValueForXAxis:1 yAxis:0];
        near([SatchelReadController(controller.extendedGamepad)[@"lx"] floatValue], 1);
        GCExtendedGamepad *pad = controller.extendedGamepad;
        NSMutableDictionary *hardwareProfile = [SatchelControllerProfile(@"", SatchelControllerCategory(controller)) mutableCopy];
        hardwareProfile[@"shortcut"] = @"menu-options";
        assert(SatchelSaveControllerProfile(@"hardware-fixture", SatchelControllerCategory(controller), hardwareProfile));
        SatchelControllerSetTitle(@"hardware-fixture");
        [pad.leftTrigger setValue:0];
        assert(pad.buttonOptions);
        [pad.buttonMenu setValue:1];
        assert([SatchelReadController(pad)[@"buttons"] unsignedLongLongValue] == (1ull << 10));
        assert(!SatchelControllerMenuHeld(pad, 0));
        [pad.buttonOptions setValue:1];
        assert([SatchelReadController(pad)[@"buttons"] unsignedLongLongValue] == 0);
        assert(!SatchelControllerMenuHeld(pad, 1));
        assert(!SatchelControllerMenuHeld(pad, 1.74));
        assert(SatchelControllerMenuHeld(pad, 1.75));
        [pad.buttonMenu setValue:0];
        assert([SatchelReadController(pad)[@"buttons"] unsignedLongLongValue] == 0);
        assert(!SatchelControllerMenuHeld(pad, 2));
        [pad.buttonMenu setValue:1];
        assert(!SatchelControllerMenuHeld(pad, 4));
        [pad.buttonMenu setValue:0]; [pad.buttonOptions setValue:0];
        assert(!SatchelControllerMenuHeld(pad, 5));
        [pad.buttonOptions setValue:1];
        assert([SatchelReadController(pad)[@"buttons"] unsignedLongLongValue] == (1ull << 11));
        [pad.buttonMenu setValue:1];
        assert(!SatchelControllerMenuHeld(pad, 6));
        assert(SatchelControllerMenuHeld(pad, 6.75));
        // Release and re-press between menu polls must rearm from the input callback.
        [pad.buttonMenu setValue:0]; [pad.buttonOptions setValue:0];
        SatchelReadController(pad);
        [pad.buttonMenu setValue:1]; [pad.buttonOptions setValue:1];
        assert(!SatchelControllerMenuHeld(pad, 7));
        // A brief partial release before firing cancels the continuous hold.
        [pad.buttonMenu setValue:0]; SatchelReadController(pad);
        [pad.buttonMenu setValue:1];
        assert(!SatchelControllerMenuHeld(pad, 7.5));
        assert(!SatchelControllerMenuHeld(pad, 8));
        assert(SatchelControllerMenuHeld(pad, 8.25));
        SatchelControllerSetTitle(@"");
        [d removeObjectForKey:@"SatchelPlayerSettings.HARDWARE-FIXTURE"];
        [d synchronize];
        puts("PASS: remapping, category defaults, title isolation, validation, radial dead zone, shortcut suppression, hold latch, focus edges, synthetic GameController, compact pads without Home or Options");
    }
}
