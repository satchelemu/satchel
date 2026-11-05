#import "PlayerSettings.h"
#import "../App/ControllerProfiles.h"
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>

NSNotificationName const SatchelScreenModeDidChange = @"SatchelScreenModeDidChange";
NSNotificationName const SatchelPlayerSettingsDidChange = @"SatchelPlayerSettingsDidChange";
static _Atomic int screenMode, playerHaptics = 1;
static NSString *sessionTitle;
static NSDictionary *launchSettings;
static NSMutableDictionary *nextSettings;

static NSString *settingsKey(NSString *titleID) {
    return [@"SatchelPlayerSettings." stringByAppendingString:titleID.uppercaseString];
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
    if ([key isEqualToString:@"DM_PIPELINE_MODE"])
        return [@[@"exact", @"hybrid", @"legacy"] containsObject:value] ? value : nil;
    return nil;
}
// Bundled defaults remain the baseline; title entries are recommended defaults.
static NSDictionary *profiles(void) {
    static NSDictionary *data;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        data = [NSDictionary dictionaryWithContentsOfURL:[NSBundle.mainBundle URLForResource:@"PlayerProfiles" withExtension:@"plist"]];
        NSCAssert(data[@"default"], @"PlayerProfiles.plist must be bundled");
    });
    return data;
}
NSDictionary<NSString *, NSString *> *SatchelPlayerGlobalDefaults(void) {
    NSMutableDictionary *result = [profiles()[@"default"] mutableCopy];
    result[@"DM_SCREEN_MODE"] = @"fit";
    result[@"DM_PIPELINE_MODE"] = @"exact";
    id legacy = [NSUserDefaults.standardUserDefaults dictionaryForKey:@"SatchelLiveControls"][@"haptics"];
    result[@"DM_HAPTICS"] = [legacy isKindOfClass:NSNumber.class] && ![legacy boolValue] ? @"off" : @"on";
    return result;
}
NSDictionary<NSString *, NSString *> *SatchelPlayerDefaults(NSString *titleID) {
    NSMutableDictionary *result = [SatchelPlayerGlobalDefaults() mutableCopy];
    [result addEntriesFromDictionary:profiles()[@"titles"][titleID.uppercaseString] ?: @{}];
    return result;
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
NSDictionary<NSString *, NSString *> *SatchelPlayerOverrides(NSString *titleID) {
    NSDictionary *saved = [NSUserDefaults.standardUserDefaults dictionaryForKey:settingsKey(titleID)] ?: @{};
    NSMutableDictionary *result = [NSMutableDictionary dictionary];
    for (NSString *key in saved) {
        NSString *value = migratedValue(key, saved[key]);
        if (value) result[key] = value;
    }
    if ([saved[@"DM_SCREEN_MODE"] isEqual:@"zoom"]) {
        NSMutableDictionary *migrated = [saved mutableCopy];
        migrated[@"DM_SCREEN_MODE"] = @"fit";
        [NSUserDefaults.standardUserDefaults setObject:migrated forKey:settingsKey(titleID)];
    }
    return result;
}
NSDictionary<NSString *, NSString *> *SatchelSavedPlayerSettings(NSString *titleID) {
    NSMutableDictionary *result = [SatchelPlayerDefaults(titleID) mutableCopy];
    [result addEntriesFromDictionary:SatchelPlayerOverrides(titleID)];
    return result;
}
NSDictionary<NSString *, NSString *> *SatchelPlayerSettings(NSString *titleID) {
    NSMutableDictionary *result = [SatchelSavedPlayerSettings(titleID) mutableCopy];
    // Developer overrides for one launch (devicectl ... -SatchelFPSTarget 60); never saved.
    NSDictionary *arguments = @{@"-SatchelFPSTarget": @"DM_FPS_TARGET", @"-SatchelRenderScale": @"DM_RENDER_SCALE", @"-SatchelScreenMode": @"DM_SCREEN_MODE", @"-SatchelPipelineMode": @"DM_PIPELINE_MODE"};
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
static int modeNumber(NSString *mode) {
    return [mode isEqualToString:@"stretch"] ? 1 : 0;
}
static void settingsChanged(NSString *titleID, NSArray<NSString *> *changedKeys) {
    if ([sessionTitle isEqualToString:titleID.uppercaseString]) {
        // Keep unrelated one-launch choices. Only the edited fields change the
        // queued snapshot, and only screen/haptics have live atomic state.
        NSDictionary *saved = SatchelSavedPlayerSettings(titleID);
        for (NSString *key in changedKeys) nextSettings[key] = saved[key];
        if ([changedKeys containsObject:@"DM_SCREEN_MODE"]) {
            atomic_store(&screenMode, modeNumber(nextSettings[@"DM_SCREEN_MODE"]));
            [NSNotificationCenter.defaultCenter postNotificationName:SatchelScreenModeDidChange object:nil];
        }
        if ([changedKeys containsObject:@"DM_HAPTICS"])
            atomic_store(&playerHaptics, [nextSettings[@"DM_HAPTICS"] isEqualToString:@"on"]);
    }
    [NSNotificationCenter.defaultCenter postNotificationName:SatchelPlayerSettingsDidChange object:titleID.uppercaseString];
}
BOOL SatchelSavePlayerSettings(NSString *titleID, NSDictionary<NSString *, NSString *> *values) {
    NSCAssert(NSThread.isMainThread, @"Settings changes require the main thread");
    if (!titleID.length || !values.count) return NO;
    NSMutableDictionary *saved = [[NSUserDefaults.standardUserDefaults dictionaryForKey:settingsKey(titleID)] mutableCopy] ?: [NSMutableDictionary dictionary];
    // Validate the whole patch before writing. Other keys and title overrides stay intact.
    for (NSString *key in values) {
        NSString *value = validValue(key, values[key]);
        if (!value) { rejected(key, key); return NO; }
        saved[key] = value;
    }
    [NSUserDefaults.standardUserDefaults setObject:saved forKey:settingsKey(titleID)];
    settingsChanged(titleID, values.allKeys);
    return YES;
}
BOOL SatchelResetPlayerSetting(NSString *titleID, NSString *key) {
    NSCAssert(NSThread.isMainThread, @"Settings changes require the main thread");
    if (!SatchelPlayerGlobalDefaults()[key]) return NO;
    NSMutableDictionary *saved = [[NSUserDefaults.standardUserDefaults dictionaryForKey:settingsKey(titleID)] mutableCopy] ?: [NSMutableDictionary dictionary];
    [saved removeObjectForKey:key];
    [NSUserDefaults.standardUserDefaults setObject:saved forKey:settingsKey(titleID)];
    settingsChanged(titleID, @[key]);
    return YES;
}
void SatchelApplyPlayerSettings(NSString *titleID) {
    NSDictionary *values = SatchelPlayerSettings(titleID);
    sessionTitle = titleID.uppercaseString;
    SatchelControllerSetTitle(titleID);
    launchSettings = [values copy]; nextSettings = [values mutableCopy];
    // Called once, after the manifest and before any guest, renderer or UI worker starts.
    for (NSString *key in keys()) setenv(key.UTF8String, [values[key] UTF8String], 1);
    atomic_store(&screenMode, modeNumber(values[@"DM_SCREEN_MODE"]));
    atomic_store(&playerHaptics, [values[@"DM_HAPTICS"] isEqualToString:@"on"]);
    fprintf(stderr, "Satchel settings: title=%s fps=%s scale=%s screen=%s\n", titleID.UTF8String,
            [values[@"DM_FPS_TARGET"] UTF8String], [values[@"DM_RENDER_SCALE"] UTF8String], [values[@"DM_SCREEN_MODE"] UTF8String]);
}
void SatchelClearPlayerSession(void) {
    SatchelControllerSetTitle(@"");
    sessionTitle = nil; launchSettings = nil; nextSettings = nil;
    atomic_store(&screenMode, 0); atomic_store(&playerHaptics, 1);
}
int SatchelScreenMode(void) { return atomic_load(&screenMode); }
BOOL SatchelChangeScreenMode(NSString *mode) {
    if (!sessionTitle) return NO;
    return SatchelSavePlayerSettings(sessionTitle, @{@"DM_SCREEN_MODE": mode});
}

NSDictionary *SatchelCurrentPlayerSettings(void) { return launchSettings ?: @{}; }
NSDictionary *SatchelNextPlayerSettings(void) { return [nextSettings copy] ?: @{}; }
BOOL SatchelQueuePlayerSetting(NSString *key, NSString *value) {
    if (!sessionTitle) return NO;
    return SatchelSavePlayerSettings(sessionTitle, @{key: value});
}

BOOL SatchelPlayerHaptics(void) { return atomic_load(&playerHaptics); }
BOOL SatchelChangePlayerHaptics(BOOL enabled) {
    if (!sessionTitle) return NO;
    return SatchelSavePlayerSettings(sessionTitle, @{@"DM_HAPTICS": enabled ? @"on" : @"off"});
}
