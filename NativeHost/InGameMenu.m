#import "InGameMenu.h"
#import "DisplayControls.h"
#import "PlayerSettings.h"
#import "SessionBridge.h"
#import <GameController/GameController.h>
#include "hle_haptics.h"
#include <stdatomic.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

NSNotificationName const SatchelMenuDidChange = @"SatchelMenuDidChange";
NSNotificationName const SatchelMenuReturnToGame = @"SatchelMenuReturnToGame";
static BOOL surfaceAttached = YES;
static _Atomic int blocked;
void satchel_haptics_suspend(unsigned, int);
int SatchelNativeSessionAcceptsEvents(void);
static _Atomic float volume = 1;
static NSString *state = @"running";
static CGFloat touchSize = 1, touchOpacity = 1;
static BOOL showFPS = YES, thermalLimited;
static UILabel *thermalView, *pipelineView;
extern uint64_t satchel_pipeline_skipped_draws(void) __attribute__((weak_import));
int nvn_thermal_limited(void);
static NSString *thermalMessage(void) { return thermalLimited ? @"Phone is warm: limited to 30 FPS to cool down" : @""; }
static NSUInteger generation;
static BOOL developerQuitPending;
static NSUInteger developerQuitGeneration;
static __weak UIView *rootView, *padView;
static __weak UILabel *fpsView;
static UIView *customView;
static UIScrollView *menuView;
static UIStackView *stack;
static NSMutableArray<UIButton *> *items;
static NSUInteger selected;
static NSMapTable<GCController *, NSMutableDictionary *> *controllerStates;
static NSTimer *controllerTimer;
void dm_gamepad_reset(void);
void dm_gamepad_next_layout(void);

float SatchelMenuVolume(void) { return atomic_load(&volume); }
int SatchelMenuBlocksInput(void) { return atomic_load(&blocked); }
int SatchelMenuHaptics(void) { return SatchelPlayerHaptics(); }
CGFloat SatchelTouchSize(void) { return touchSize; }
CGFloat SatchelTouchOpacity(void) { return touchOpacity; }
NSDictionary *SatchelMenuState(void) {
    NSCAssert(NSThread.isMainThread, @"Menu state requires the main thread");
    return @{@"state": state, @"paused": @(![state isEqualToString:@"running"] && ![state isEqualToString:@"opening"]), @"attached": @(surfaceAttached), @"current": SatchelCurrentPlayerSettings(),
             @"nextLaunch": SatchelNextPlayerSettings(), @"fps": @(showFPS), @"opacity": @(touchOpacity),
             @"size": @(touchSize), @"haptics": @(SatchelMenuHaptics()), @"volume": @(SatchelMenuVolume()),
             @"thermalLimited": @(thermalLimited), @"thermalMessage": thermalMessage(), @"restartRequired": @YES};
}
static void redraw(void);
static void changed(void) {
    satchel_haptics_suspend(4, !SatchelMenuHaptics());
    satchel_haptics_suspend(8, SatchelMenuBlocksInput()); // Separate reason bit from focus and interruptions.
    fpsView.hidden = !showFPS;
    thermalView.hidden = !showFPS || !thermalLimited;
    [padView setNeedsLayout];
    redraw();
    [NSNotificationCenter.defaultCenter postNotificationName:SatchelMenuDidChange object:nil];
}
static BOOL number(NSString *text, double low, double high, double *result) {
    if (!text.length || text.length > 16 || [text rangeOfCharacterFromSet:
        [NSCharacterSet characterSetWithCharactersInString:@"0123456789."].invertedSet].location != NSNotFound) return NO;
    const char *s = text.UTF8String; char *end; double v = strtod(s, &end);
    if (s == end || *end || !isfinite(v) || v < low || v > high) return NO;
    *result = v; return YES;
}
static void persist(void) {
    NSMutableDictionary *saved = [[NSUserDefaults.standardUserDefaults dictionaryForKey:@"SatchelLiveControls"] mutableCopy] ?: [NSMutableDictionary new];
    [saved addEntriesFromDictionary:@{@"fps": @(showFPS), @"opacity": @(touchOpacity),
        @"size": @(touchSize), @"volume": @(SatchelMenuVolume())}];
    // Keep an old global haptics value only as a migration default for unsaved titles.
    [NSUserDefaults.standardUserDefaults setObject:saved forKey:@"SatchelLiveControls"];
}
// Diagnostics for the Mac (DM_CTL "view-shot <name>.png"): draws the real window, including
// SwiftUI overlays that the composed game screenshot leaves out, and logs the menu views.
void SatchelMenuDebugShot(NSString *filename) {
    UIWindow *window = rootView.window;
    fprintf(stderr, "Satchel menu views: state=%s root=%s window=%s custom=%s superview=%d hidden=%d alpha=%.2f plain=%s plainHidden=%d\n",
            state.UTF8String, NSStringFromCGRect(rootView.bounds).UTF8String, NSStringFromCGRect(window.bounds).UTF8String,
            NSStringFromCGRect(customView.frame).UTF8String, customView.superview != nil, customView.hidden, customView.alpha,
            NSStringFromCGRect(menuView.frame).UTF8String, menuView.hidden);
    if (!window) return;
    // A presented menu (Satchel's) is drawn in the same window, above the game.
    NSMutableArray *chain = [NSMutableArray array];
    for (UIViewController *c = window.rootViewController; c; c = c.presentedViewController) [chain addObject:NSStringFromClass(c.class)];
    fprintf(stderr, "Satchel menu presented: %s\n", [chain componentsJoinedByString:@" > "].UTF8String);
    UIGraphicsImageRenderer *renderer = [[UIGraphicsImageRenderer alloc] initWithBounds:window.bounds];
    UIImage *image = [renderer imageWithActions:^(UIGraphicsImageRendererContext *context) {
        [window drawViewHierarchyInRect:window.bounds afterScreenUpdates:YES];
    }];
    NSString *runtime = [NSFileManager.defaultManager currentDirectoryPath];
    BOOL ok = [UIImagePNGRepresentation(image) writeToFile:[runtime stringByAppendingPathComponent:filename] atomically:YES];
    fprintf(stderr, "Satchel view shot: %s %s\n", filename.UTF8String, ok ? "ok" : "failed");
}

static BOOL developerQuitEnabled(void) {
    NSArray<NSString *> *arguments = NSProcessInfo.processInfo.arguments;
    for (NSInteger i = (NSInteger)arguments.count - 1; i >= 0; i--) {
        if ([arguments[i] isEqualToString:@"-SatchelDeveloperControls"])
            return i + 1 < (NSInteger)arguments.count &&
                [@[@"yes", @"true", @"1"] containsObject:arguments[i + 1].lowercaseString];
    }
    return NO;
}

// A queued request belongs to this menu generation. It cannot stop a later session.
static void finishDeveloperQuit(NSUInteger ticket, unsigned attempts) {
    if (!developerQuitPending || ticket != developerQuitGeneration) return;
    if (ticket != generation) { developerQuitPending = NO; return; }
    if (!attempts || !developerQuitEnabled()) {
        developerQuitPending = NO; return;
    }
    if ([state isEqualToString:@"menu"]) {
        developerQuitPending = NO;
        if (SatchelMenuPerform(@"quit", nil)) SatchelMenuPerform(@"confirm-quit", nil);
        return;
    }
    if ([state isEqualToString:@"confirmQuit"]) {
        developerQuitPending = NO; SatchelMenuPerform(@"confirm-quit", nil); return;
    }
    if ([state isEqualToString:@"library"] && surfaceAttached) {
        SatchelMenuPerform(@"open", nil);
    }
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 50 * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
        finishDeveloperQuit(ticket, attempts - 1);
    });
}
BOOL SatchelMenuRequestDeveloperQuit(void) {
    NSCAssert(NSThread.isMainThread, @"Developer Quit requires the main thread");
    if (!developerQuitEnabled() || !rootView ||
        !SatchelNativeSessionStarted() || [@[@"quitting", @"stopFailed", @"ended"] containsObject:state]) return NO;
    if (developerQuitPending && developerQuitGeneration == generation) return YES;
    if ([state isEqualToString:@"library"]) SatchelMenuPerform(@"return-to-game", nil);
    else if (![state isEqualToString:@"confirmQuit"] && !SatchelMenuPerform(@"open", nil)) return NO;
    developerQuitPending = YES; developerQuitGeneration = generation;
    finishDeveloperQuit(generation, 100);
    return YES;
}

BOOL SatchelMenuPerform(NSString *action, NSString *value) {
    NSCAssert(NSThread.isMainThread, @"Menu actions require the main thread");
    if (!rootView) return NO;
    if ([action isEqualToString:@"retry-stop"] && [state isEqualToString:@"stopFailed"]) {
        state = @"confirmQuit"; return SatchelMenuPerform(@"confirm-quit", nil);
    }
    if ([@[@"quitting", @"stopFailed", @"ended"] containsObject:state]) return NO;
    if ([action isEqualToString:@"resume"] || [action isEqualToString:@"close"]) {
        // Records who resumed, to find resumes nobody asked for.
        NSArray *stack = NSThread.callStackSymbols;
        for (NSUInteger i = 1; i < MIN(stack.count, (NSUInteger)9); i++) fprintf(stderr, "Satchel menu resume caller %lu: %s\n", (unsigned long)i, [stack[i] UTF8String]);
    }
    if ([action isEqualToString:@"return-to-game"]) {
        if (![state isEqualToString:@"library"]) return NO;
        [NSNotificationCenter.defaultCenter postNotificationName:SatchelMenuReturnToGame object:nil];
    } else if ([action isEqualToString:@"open"]) {
        if (!surfaceAttached) return NO;
        if ([state isEqualToString:@"menu"] || [state isEqualToString:@"opening"]) return YES;
        if ([state isEqualToString:@"library"]) {
            state = @"menu"; changed(); return YES;
        }
        atomic_store(&blocked, 1); dm_gamepad_reset();
        state = @"opening"; NSUInteger ticket = ++generation;
        changed();
        void (^finish)(void) = ^{
            if (generation != ticket || ![state isEqualToString:@"opening"]) return;
            SatchelNativeSetMenuPaused(YES); state = @"menu"; changed();
        };
        SatchelDisplayCaptureForPause(finish);
        // Even a guest that never presents must be pausable. No screenshot is fabricated.
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 500 * NSEC_PER_MSEC), dispatch_get_main_queue(), finish);
    } else if ([action isEqualToString:@"resume"] || [action isEqualToString:@"close"]) {
        if (!surfaceAttached || [state isEqualToString:@"library"]) return NO;
        generation++; state = @"running"; dm_gamepad_reset();
        atomic_store(&blocked, 0); SatchelNativeSetMenuPaused(NO); changed();
    } else if ([action isEqualToString:@"library"]) {
        if (![state isEqualToString:@"menu"]) return NO;
        state = @"library"; surfaceAttached = NO; changed();
    } else if ([action isEqualToString:@"quit"]) {
        if (![state isEqualToString:@"menu"]) return NO;
        state = @"confirmQuit"; changed();
    } else if ([action isEqualToString:@"cancel"]) {
        if (![state isEqualToString:@"confirmQuit"]) return NO;
        state = @"menu"; changed();
    } else if ([action isEqualToString:@"confirm-quit"]) {
        if (![state isEqualToString:@"confirmQuit"]) return NO;
        state = @"quitting"; changed();
        SatchelStopNativeSession(^(NSError *error) {
            if (error) {
                state = @"stopFailed"; changed();
                UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"Could not stop game" message:error.localizedDescription preferredStyle:UIAlertControllerStyleAlert];
                [alert addAction:[UIAlertAction actionWithTitle:@"Try again" style:UIAlertActionStyleDefault handler:^(UIAlertAction *unused) {
                    state = @"confirmQuit"; SatchelMenuPerform(@"confirm-quit", nil);
                }]];
                UIViewController *controller = SatchelNativeGameController();
                while (controller.presentedViewController) controller = controller.presentedViewController;
                [controller presentViewController:alert animated:YES completion:nil];
            }
        });
    } else {
        if (![state isEqualToString:@"menu"]) return NO;
        double n;
        if ([action isEqualToString:@"screen"]) { if (!SatchelQueuePlayerSetting(@"DM_SCREEN_MODE", value)) return NO; }
        else if ([action isEqualToString:@"fps"] || [action isEqualToString:@"haptics"]) {
            if (![@[@"on", @"off"] containsObject:value]) return NO;
            BOOL on = [value isEqualToString:@"on"];
            if ([action isEqualToString:@"fps"]) showFPS = on; else if (!SatchelChangePlayerHaptics(on)) return NO;
        } else if ([action isEqualToString:@"opacity"]) {
            if (!number(value, 0.2, 1, &n)) return NO; touchOpacity = n;
        } else if ([action isEqualToString:@"size"]) {
            if (!number(value, 0.75, 1.25, &n)) return NO; touchSize = n;
        } else if ([action isEqualToString:@"volume"]) {
            if (!number(value, 0, 1, &n)) return NO; atomic_store(&volume, n);
        } else if ([action isEqualToString:@"layout-next"]) dm_gamepad_next_layout();
        else return NO;
        persist(); changed();
    }
    fprintf(stderr, "Satchel menu: action=%s value=%s state=%s\n", action.UTF8String, value.UTF8String ?: "", state.UTF8String);
    return YES;
}
void SatchelMenuSurfaceAttached(BOOL attached) {
    NSCAssert(NSThread.isMainThread, @"Surface changes require the main thread");
    surfaceAttached = attached;
    if (attached && [state isEqualToString:@"library"]) SatchelMenuPerform(@"open", nil);
}
static void addText(NSString *text, BOOL title) {
    UILabel *label = [UILabel new]; label.text = text; label.numberOfLines = 0;
    label.textColor = UIColor.whiteColor;
    label.font = [UIFont systemFontOfSize:title ? 22 : 14 weight:title ? UIFontWeightBold : UIFontWeightRegular];
    [stack addArrangedSubview:label];
}
static void addButton(NSString *title, NSString *action, NSString *value) {
    UIButton *button = [UIButton buttonWithType:UIButtonTypeSystem];
    UIButtonConfiguration *config = UIButtonConfiguration.filledButtonConfiguration;
    config.title = title; config.baseBackgroundColor = [UIColor colorWithWhite:0.22 alpha:1];
    config.baseForegroundColor = UIColor.whiteColor; button.configuration = config;
    button.accessibilityIdentifier = [@"menu." stringByAppendingString:action];
    [button addAction:[UIAction actionWithHandler:^(UIAction *unused) { SatchelMenuPerform(action, value); }] forControlEvents:UIControlEventTouchUpInside];
    [button.heightAnchor constraintGreaterThanOrEqualToConstant:44].active = YES;
    [stack addArrangedSubview:button]; [items addObject:button];
}
static void highlight(void) {
    if (!items.count) return;
    selected = MIN(selected, items.count - 1);
    for (NSUInteger i = 0; i < items.count; i++) {
        items[i].layer.borderWidth = i == selected ? 2 : 0;
        items[i].layer.borderColor = UIColor.systemTealColor.CGColor;
    }
    [menuView layoutIfNeeded];
    [menuView scrollRectToVisible:[items[selected] convertRect:items[selected].bounds toView:menuView] animated:NO];
}
static void redraw(void) {
    BOOL visible = ![@[@"running", @"library", @"ended"] containsObject:state];
    // Use the custom view only when it is really on screen; otherwise the plain menu shows, so a
    // paused game is never left behind an invisible menu.
    BOOL custom = customView.superview != nil;
    menuView.hidden = !visible || custom; customView.hidden = !visible;
    // The custom view sits above the game picture, pad and FPS label while it is shown.
    if (visible && custom) [customView.superview bringSubviewToFront:customView];
    if (custom || !visible) return;
    for (UIView *v in stack.arrangedSubviews) { [stack removeArrangedSubview:v]; [v removeFromSuperview]; }
    items = [NSMutableArray array];
    addText([state isEqualToString:@"opening"] ? @"Pausing game…" : @"Game paused", YES);
    if ([state isEqualToString:@"opening"]) return;
    if ([state isEqualToString:@"quitting"] || [state isEqualToString:@"stopFailed"]) {
        addText([state isEqualToString:@"stopFailed"] ? @"The game could not finish stopping." : @"Stopping game…", YES);
        if ([state isEqualToString:@"stopFailed"]) addButton(@"Try Quit again", @"retry-stop", nil);
    } else if ([state isEqualToString:@"confirmQuit"]) {
        addText(@"Quit returns to your library. Save in the game before quitting; progress since your last save is lost.", NO);
        addButton(@"Cancel", @"cancel", nil); addButton(@"Quit game", @"confirm-quit", nil);
    } else {
        NSDictionary *next = SatchelNextPlayerSettings();
        if (thermalLimited) addText(thermalMessage(), NO);
        addButton(@"Resume", @"resume", nil);
        addButton(@"Return to library (game stays paused)", @"library", nil);
        addText(@"The game runs at its own frame rate.", NO);
        for (NSString *mode in @[@"fit", @"stretch"])
            addButton([NSString stringWithFormat:@"Screen: %@%@", mode, [next[@"DM_SCREEN_MODE"] isEqual:mode] ? @" ✓" : @""], @"screen", mode);
        addButton(showFPS ? @"FPS counter: on" : @"FPS counter: off", @"fps", showFPS ? @"off" : @"on");
        addButton([NSString stringWithFormat:@"Touch opacity: %.0f%% (tap to cycle)", touchOpacity * 100], @"opacity", touchOpacity > 0.9 ? @"0.4" : touchOpacity < 0.5 ? @"0.7" : @"1");
        addButton([NSString stringWithFormat:@"Touch size: %.0f%% (tap to cycle)", touchSize * 100], @"size", touchSize > 1.1 ? @"0.75" : touchSize < 0.9 ? @"1" : @"1.25");
        addButton(SatchelMenuHaptics() ? @"Haptics: on" : @"Haptics: off", @"haptics", SatchelMenuHaptics() ? @"off" : @"on");
        addButton([NSString stringWithFormat:@"Game volume: %.0f%% (tap to cycle)", SatchelMenuVolume() * 100], @"volume", SatchelMenuVolume() > 0.9 ? @"0" : SatchelMenuVolume() < 0.1 ? @"0.5" : @"1");
        addButton(@"Cycle touch controller layout", @"layout-next", nil);
        addButton(@"Quit game…", @"quit", nil);
    }
    SatchelMenuLayout(); highlight();
}
static void pollControllers(void) {
    pipelineView.hidden = !showFPS || !surfaceAttached || ![NSUserDefaults.standardUserDefaults boolForKey:@"SatchelDeveloperControls"];
    uint64_t skipped = satchel_pipeline_skipped_draws ? satchel_pipeline_skipped_draws() : 0;
    pipelineView.text = [NSString stringWithFormat:@"Skipped: %llu / frame", (unsigned long long)skipped];
    pipelineView.frame = CGRectMake(fpsView.frame.origin.x, CGRectGetMaxY(fpsView.frame) + 3, 175, 22);
    BOOL limited = nvn_thermal_limited() != 0;
    if (limited != thermalLimited) {
        thermalLimited = limited;
        fprintf(stderr, "Satchel thermal: limited=%d cap=%s source=engine\n", limited, limited ? "30" : "requested");
        changed();
    }
    // The front end can move the FPS label after its root layout callback.
    thermalView.frame = CGRectMake(CGRectGetMaxX(fpsView.frame) + 3, fpsView.frame.origin.y, 46, fpsView.frame.size.height);

    for (GCController *controller in GCController.controllers) {
        GCExtendedGamepad *g = controller.extendedGamepad; if (!g) continue;
        NSMutableDictionary *s = [controllerStates objectForKey:controller];
        if (!s) { s = [NSMutableDictionary dictionary]; [controllerStates setObject:s forKey:controller]; }
        BOOL combo = g.buttonOptions.isPressed && g.buttonMenu.isPressed;
        double now = CACurrentMediaTime();
        if (combo && ![s[@"combo"] boolValue]) s[@"since"] = @(now);
        if (combo && ![s[@"fired"] boolValue] && now - [s[@"since"] doubleValue] >= 1) {
            s[@"fired"] = @YES;
            if ([state isEqualToString:@"running"]) SatchelMenuPerform(@"open", nil);
        }
        if (!combo) s[@"fired"] = @NO;
        s[@"combo"] = @(combo);
        unsigned keys = (g.dpad.up.isPressed ? 1 : 0) | (g.dpad.down.isPressed ? 2 : 0) |
                        (g.buttonA.isPressed ? 4 : 0) | (g.buttonB.isPressed ? 8 : 0);
        unsigned pressed = keys & ~[s[@"keys"] unsignedIntValue]; s[@"keys"] = @(keys);
        // A custom menu handles touch; a controller's B button still resumes from it.
        if (customView.superview != nil && [state isEqualToString:@"menu"] && (pressed & 8)) SatchelMenuPerform(@"resume", nil);
        if (customView.superview == nil && ([state isEqualToString:@"menu"] || [state isEqualToString:@"confirmQuit"])) {
            if ((pressed & 1) && selected) selected--;
            if ((pressed & 2) && selected + 1 < items.count) selected++;
            if (pressed & 3) highlight();
            if ((pressed & 4) && selected < items.count) [items[selected] sendActionsForControlEvents:UIControlEventTouchUpInside];
            if (pressed & 8) SatchelMenuPerform([state isEqualToString:@"confirmQuit"] ? @"cancel" : @"resume", nil);
        }
    }
}
void SatchelMenuSetCustomView(UIView *view) {
    [customView removeFromSuperview]; customView = view;
    if (view) [rootView addSubview:view]; SatchelMenuLayout(); redraw();
}
void SatchelMenuLayout(void) {
    // The SwiftUI menu keeps its own content inside the safe area, so it covers the whole screen;
    // the interactive bounds left a black band over the home indicator.
    menuView.frame = SatchelInteractiveBounds(rootView, rootView.bounds); customView.frame = rootView.bounds;
    menuView.contentInset = UIEdgeInsetsMake(12, 0, 12, 0);
}
void SatchelMenuInstall(UIView *root, UIView *pad, UILabel *fps) {
    rootView = root; padView = pad; fpsView = fps;
    [pipelineView removeFromSuperview];
    pipelineView = [UILabel new];
    pipelineView.textColor = UIColor.whiteColor;
    pipelineView.backgroundColor = [UIColor colorWithWhite:0 alpha:0.45];
    pipelineView.font = [UIFont monospacedDigitSystemFontOfSize:12 weight:UIFontWeightSemibold];
    pipelineView.accessibilityIdentifier = @"pipeline-skipped-draws";
    pipelineView.hidden = YES;
    if (fps) [root addSubview:pipelineView];
    thermalView = [UILabel new]; thermalView.text = @"🌡30";
    thermalView.textColor = UIColor.systemOrangeColor;
    thermalView.font = [UIFont systemFontOfSize:12 weight:UIFontWeightSemibold];
    thermalView.accessibilityIdentifier = @"thermal.limit";
    thermalView.accessibilityLabel = @"Phone is warm: limited to 30 FPS to cool down";
    if (fps) [root addSubview:thermalView];
    NSDictionary *saved = [NSUserDefaults.standardUserDefaults dictionaryForKey:@"SatchelLiveControls"];
    if (saved) {
        if ([saved[@"fps"] isKindOfClass:NSNumber.class]) showFPS = [saved[@"fps"] boolValue];
        double n;
        if (number([saved[@"opacity"] description], 0.2, 1, &n)) touchOpacity = n;
        if (number([saved[@"size"] description], 0.75, 1.25, &n)) touchSize = n;
        if (number([saved[@"volume"] description], 0, 1, &n)) atomic_store(&volume, n);
    }
    menuView = [UIScrollView new]; menuView.backgroundColor = [UIColor colorWithWhite:0.05 alpha:0.97];
    stack = [UIStackView new]; stack.axis = UILayoutConstraintAxisVertical; stack.spacing = 10;
    stack.translatesAutoresizingMaskIntoConstraints = NO; [menuView addSubview:stack]; [root addSubview:menuView];
    [NSLayoutConstraint activateConstraints:@[
        [stack.topAnchor constraintEqualToAnchor:menuView.contentLayoutGuide.topAnchor],
        [stack.bottomAnchor constraintEqualToAnchor:menuView.contentLayoutGuide.bottomAnchor],
        [stack.centerXAnchor constraintEqualToAnchor:menuView.frameLayoutGuide.centerXAnchor],
        [stack.widthAnchor constraintEqualToAnchor:menuView.frameLayoutGuide.widthAnchor multiplier:0.78]]];
    controllerStates = [NSMapTable weakToStrongObjectsMapTable];
    controllerTimer = [NSTimer timerWithTimeInterval:0.05 repeats:YES block:^(NSTimer *timer) { pollControllers(); }];
    [NSRunLoop.mainRunLoop addTimer:controllerTimer forMode:NSRunLoopCommonModes];
    // Satchel installs its SwiftUI menu when the game surface is created, which can be before
    // this root exists; attach it now so the menu is never registered but invisible.
    if (customView && customView.superview != root) [root addSubview:customView];
    SatchelMenuLayout(); changed();
}

void SatchelMenuBeginSession(void) {
    developerQuitPending = NO;
    NSCAssert(NSThread.isMainThread, @"Menu state requires the main thread");
    generation++; state = @"running"; surfaceAttached = YES;
    atomic_store(&blocked, 0); [controllerStates removeAllObjects];
    changed();
}

void SatchelMenuEndSession(void) {
    developerQuitPending = NO;
    NSCAssert(NSThread.isMainThread, @"Menu state requires the main thread");
    generation++; surfaceAttached = NO; state = @"ended";
    atomic_store(&blocked, 1); [controllerStates removeAllObjects]; changed();
}

void SatchelMenuStopping(BOOL failed) {
    NSCAssert(NSThread.isMainThread, @"Menu state requires the main thread");
    generation++; state = failed ? @"stopFailed" : @"quitting";
    atomic_store(&blocked, 1); changed();
}

void SatchelMenuControllerDisconnected(void) {
    NSCAssert(NSThread.isMainThread, @"Controller changes require the main thread");
    if (!SatchelNativeSessionAcceptsEvents() || ![state isEqualToString:@"running"]) return;
    // Suspend immediately, including when no frame is available for the menu capture.
    SatchelMenuPerform(@"open", nil);
    SatchelNativeSetMenuPaused(YES);
    fprintf(stderr, "Satchel controller disconnected: game paused, resume explicitly\n");
}
