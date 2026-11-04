#import "PlayerSettings.h"
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>

NSNotificationName const SatchelScreenModeDidChange = @"SatchelScreenModeDidChange";
static _Atomic int screenMode, playerHaptics = 1;
static NSString *sessionTitle;
static NSDictionary *launchSettings;
static NSMutableDictionary *nextSettings;

static NSString *settingsKey(NSString *titleID) {
    return [@"SatchelPlayerSettings." stringByAppendingString:titleID];
}
static NSArray<NSString *> *keys(void) {
    return @[@"DM_FPS_TARGET", @"DM_RENDER_SCALE", @"DM_SCREEN_MODE"];
}
static NSString *validValue(NSString *key, id value) {
    if (![value isKindOfClass:NSString.class]) return nil;
    // 0 leaves the game's own frame rate; 30 and 60 are only for games with a frame-step patch.
    if ([key isEqualToString:@"DM_FPS_TARGET"])
        return [@[@"0", @"30", @"60"] containsObject:value] ? value : nil;
    if ([key isEqualToString:@"DM_HAPTICS"])
        return [@[@"on", @"off"] containsObject:value] ? value : nil;
    if ([key isEqualToString:@"DM_SCREEN_MODE"])
        return [@[@"fit", @"stretch"] containsObject:value] ? value : nil;
    if ([key isEqualToString:@"DM_RENDER_SCALE"]) {
        // Accept decimal fractions only, with no trailing text, signs, NaN or infinity.
        NSString *text = value;
        if (!text.length || text.length > 16 ||
            [text rangeOfCharacterFromSet:[NSCharacterSet characterSetWithCharactersInString:@"0123456789."].invertedSet].location != NSNotFound) return nil;
        const char *start = text.UTF8String; char *end;
        double scale = strtod(start, &end);
        if (end == start || *end || !isfinite(scale) || scale < 0.5 || scale > 1.0) return nil;
        return text;
    }
    return nil;
}
// Each game runs at the frame rate it expects and at its own full resolution (PlayerProfiles.plist): a
// game without an entry keeps its own frame logic (target 0). Render scale stays 1.0; speed comes from
// optimizing the host, not from rendering below the game's size. The player does not choose these.
static NSDictionary *profile(NSString *titleID) {
    static NSDictionary *profiles;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        profiles = [NSDictionary dictionaryWithContentsOfURL:[NSBundle.mainBundle URLForResource:@"PlayerProfiles" withExtension:@"plist"]];
        NSCAssert(profiles[@"default"], @"PlayerProfiles.plist must be bundled");
    });
    return profiles[@"titles"][titleID.uppercaseString] ?: profiles[@"default"];
}
static NSString *migratedValue(NSString *key, id value) {
    if ([key isEqualToString:@"DM_SCREEN_MODE"] && [value isEqual:@"zoom"]) {
        fprintf(stderr, "Satchel settings: migrated screen zoom to fit\n");
        return @"fit";
    }
    return validValue(key, value);
}
static void rejected(NSString *key, NSString *source) {
    fprintf(stderr, "Satchel settings: rejected %s%s\n", source.UTF8String,
        [key isEqualToString:@"DM_RENDER_SCALE"] ? "; allowed render scale range: 0.5 through 1.0" : "");
}
NSDictionary<NSString *, NSString *> *SatchelPlayerSettings(NSString *titleID) {
    NSDictionary *data = profile(titleID);
    NSMutableDictionary *result = [NSMutableDictionary dictionary];
    for (NSString *key in @[@"DM_FPS_TARGET", @"DM_RENDER_SCALE"]) {
        NSString *value = validValue(key, data[key]);
        NSCAssert(value, @"PlayerProfiles.plist has an invalid %@", key);
        result[key] = value;
    }
    result[@"DM_SCREEN_MODE"] = @"fit";
    // Adopt the previous global haptics choice until a title saves its own value.
    id legacy = [NSUserDefaults.standardUserDefaults dictionaryForKey:@"SatchelLiveControls"][@"haptics"];
    result[@"DM_HAPTICS"] = [legacy isKindOfClass:NSNumber.class] && ![legacy boolValue] ? @"off" : @"on";
    // Frame rates and scales saved by older builds are ignored.
    id saved = [NSUserDefaults.standardUserDefaults objectForKey:settingsKey(titleID)];
    if ([saved isKindOfClass:NSDictionary.class]) {
        NSString *value = migratedValue(@"DM_SCREEN_MODE", saved[@"DM_SCREEN_MODE"]);
        if (value) result[@"DM_SCREEN_MODE"] = value;
        NSString *rumble = validValue(@"DM_HAPTICS", saved[@"DM_HAPTICS"]);
        if (rumble) result[@"DM_HAPTICS"] = rumble;
        if (saved[@"DM_FPS_TARGET"] || saved[@"DM_RENDER_SCALE"] || [saved[@"DM_SCREEN_MODE"] isEqual:@"zoom"])
            [NSUserDefaults.standardUserDefaults setObject:@{@"DM_SCREEN_MODE": result[@"DM_SCREEN_MODE"], @"DM_HAPTICS": result[@"DM_HAPTICS"]} forKey:settingsKey(titleID)];
    }
    // Developer overrides for one launch (devicectl ... -SatchelFPSTarget 60); never saved.
    NSDictionary *arguments = @{@"-SatchelFPSTarget": @"DM_FPS_TARGET", @"-SatchelRenderScale": @"DM_RENDER_SCALE", @"-SatchelScreenMode": @"DM_SCREEN_MODE"};
    NSArray<NSString *> *args = NSProcessInfo.processInfo.arguments;
    for (NSUInteger i = 1; i < args.count; i++) {
        NSString *key = arguments[args[i]];
        if (!key) continue;
        NSString *value = i + 1 < args.count ? migratedValue(key, args[i + 1]) : nil;
        if (value) { result[key] = value; i++; }
        else rejected(key, [@"argument " stringByAppendingString:args[i]]);
    }
    return result;
}
BOOL SatchelSavePlayerSettings(NSString *titleID, NSDictionary<NSString *, NSString *> *values) {
    NSString *mode = validValue(@"DM_SCREEN_MODE", values[@"DM_SCREEN_MODE"]);
    if (!mode) { rejected(@"DM_SCREEN_MODE", @"DM_SCREEN_MODE"); return NO; }
    NSString *rumble = values[@"DM_HAPTICS"] ? validValue(@"DM_HAPTICS", values[@"DM_HAPTICS"]) : SatchelPlayerSettings(titleID)[@"DM_HAPTICS"];
    if (!rumble) return NO;
    [NSUserDefaults.standardUserDefaults setObject:@{@"DM_SCREEN_MODE": mode, @"DM_HAPTICS": rumble} forKey:settingsKey(titleID)];
    return YES;
}
static int modeNumber(NSString *mode) {
    return [mode isEqualToString:@"stretch"] ? 1 : 0;
}
void SatchelApplyPlayerSettings(NSString *titleID) {
    NSDictionary *values = SatchelPlayerSettings(titleID);
    sessionTitle = [titleID copy];
    launchSettings = [values copy]; nextSettings = [values mutableCopy];
    // Called once, after the manifest and before any guest, renderer or UI worker starts.
    for (NSString *key in keys()) setenv(key.UTF8String, [values[key] UTF8String], 1);
    atomic_store(&screenMode, modeNumber(values[@"DM_SCREEN_MODE"]));
    atomic_store(&playerHaptics, [values[@"DM_HAPTICS"] isEqualToString:@"on"]);
    fprintf(stderr, "Satchel settings: title=%s fps=%s scale=%s screen=%s\n", titleID.UTF8String,
            [values[@"DM_FPS_TARGET"] UTF8String], [values[@"DM_RENDER_SCALE"] UTF8String], [values[@"DM_SCREEN_MODE"] UTF8String]);
}
void SatchelClearPlayerSession(void) {
    sessionTitle = nil; launchSettings = nil; nextSettings = nil;
    atomic_store(&screenMode, 0); atomic_store(&playerHaptics, 1);
}
int SatchelScreenMode(void) { return atomic_load(&screenMode); }
BOOL SatchelChangeScreenMode(NSString *mode) {
    if (!sessionTitle || !validValue(@"DM_SCREEN_MODE", mode)) return NO;
    // UIKit and DM_CTL both marshal live changes onto the main queue.
    NSCAssert(NSThread.isMainThread, @"Screen changes require the main thread");
    nextSettings[@"DM_SCREEN_MODE"] = mode;
    SatchelSavePlayerSettings(sessionTitle, nextSettings);
    atomic_store(&screenMode, modeNumber(mode));
    // Keep runtime state atomic instead of changing the process environment while the engine reads it.
    fprintf(stderr, "Satchel screen: mode=%s saved title=%s\n", mode.UTF8String, sessionTitle.UTF8String);
    [NSNotificationCenter.defaultCenter postNotificationName:SatchelScreenModeDidChange object:nil];
    return YES;
}

NSDictionary *SatchelCurrentPlayerSettings(void) { return launchSettings ?: @{}; }
NSDictionary *SatchelNextPlayerSettings(void) { return [nextSettings copy] ?: @{}; }
BOOL SatchelQueuePlayerSetting(NSString *key, NSString *value) {
    NSCAssert(NSThread.isMainThread, @"Settings changes require the main thread");
    // Frame rate and render scale follow the game; this command queues screen changes.
    if (!sessionTitle || ![key isEqualToString:@"DM_SCREEN_MODE"] || !validValue(key, value)) { rejected(key, key); return NO; }
    return SatchelChangeScreenMode(value);
}

BOOL SatchelPlayerHaptics(void) { return atomic_load(&playerHaptics); }
BOOL SatchelChangePlayerHaptics(BOOL enabled) {
    NSCAssert(NSThread.isMainThread, @"Settings changes require the main thread");
    if (!sessionTitle) return NO;
    nextSettings[@"DM_HAPTICS"] = enabled ? @"on" : @"off";
    if (!SatchelSavePlayerSettings(sessionTitle, nextSettings)) return NO;
    atomic_store(&playerHaptics, enabled);
    return YES;
}
