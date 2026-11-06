#import "ControllerProfiles.h"
#include <math.h>

static NSString *activeTitle = @"";
NSArray<NSString *> *SatchelControllerCategories(void) { return @[@"Xbox", @"DualSense", @"Pro Controller", @"Generic"]; }
NSArray<NSString *> *SatchelControllerInputs(void) {
    return @[GCInputButtonA, GCInputButtonB, GCInputButtonX, GCInputButtonY,
             GCInputLeftThumbstickButton, GCInputRightThumbstickButton,
             GCInputLeftShoulder, GCInputRightShoulder, GCInputLeftTrigger, GCInputRightTrigger,
             GCInputButtonMenu, GCInputButtonOptions, @"Direction Pad Left", @"Direction Pad Up",
             @"Direction Pad Right", @"Direction Pad Down", GCInputButtonHome];
}
NSArray<NSString *> *SatchelControllerOutputs(void) {
    // The order is the pinned engine's dm_input.h bit layout.
    return @[@"A", @"B", @"X", @"Y", @"L3", @"R3", @"L", @"R", @"ZL", @"ZR", @"Plus", @"Minus", @"Left", @"Up", @"Right", @"Down", @"Unmapped"];
}
NSString *SatchelControllerCategory(GCController *c) {
    NSString *category = c.productCategory.lowercaseString;
    if ([category containsString:@"xbox"]) return @"Xbox";
    if ([category containsString:@"dualsense"]) return @"DualSense";
    if ([category containsString:@"switch pro"] || [category containsString:@"joy-con"]) return @"Pro Controller";
    return @"Generic";
}
static NSString *key(NSString *title) { return [@"SatchelPlayerSettings." stringByAppendingString:title.length ? title.uppercaseString : @"default"]; }
static NSDictionary *defaults(NSString *category) {
    NSArray *inputs = SatchelControllerInputs(); NSMutableDictionary *mapping = [NSMutableDictionary dictionary];
    NSArray *outputs = SatchelControllerOutputs();
    for (NSUInteger i = 0; i < inputs.count; i++) mapping[inputs[i]] = outputs[i];
    if (![category isEqual:@"Pro Controller"]) {
        mapping[GCInputButtonA] = @"B"; mapping[GCInputButtonB] = @"A";
        mapping[GCInputButtonX] = @"Y"; mapping[GCInputButtonY] = @"X";
    }
    return @{@"version": @1, @"mapping": mapping, @"deadZone": @0.15, @"shortcut": @"home"};
}
static BOOL valid(NSDictionary *p) {
    if (![p isKindOfClass:NSDictionary.class] || ![p[@"version"] isEqual:@1] ||
        ![p[@"deadZone"] isKindOfClass:NSNumber.class] || !isfinite([p[@"deadZone"] doubleValue]) ||
        [p[@"deadZone"] doubleValue] < 0 || [p[@"deadZone"] doubleValue] > 0.5 ||
        ![@[@"home", @"menu-options", @"off"] containsObject:p[@"shortcut"]] ||
        ![p[@"mapping"] isKindOfClass:NSDictionary.class]) return NO;
    NSDictionary *mapping = p[@"mapping"];
    if (mapping.count != SatchelControllerInputs().count) return NO;
    for (NSString *input in SatchelControllerInputs()) if (![SatchelControllerOutputs() containsObject:mapping[input]]) return NO;
    return YES;
}
static NSDictionary *saved(NSString *title, NSString *category) {
    id profiles = [NSUserDefaults.standardUserDefaults dictionaryForKey:key(title)][@"ControllerProfiles"];
    if (![profiles isKindOfClass:NSDictionary.class]) return nil;
    id data = profiles[category];
    if (![data isKindOfClass:NSData.class] || [data length] > 16384) return nil;
    id profile = [NSJSONSerialization JSONObjectWithData:data options:0 error:nil];
    return valid(profile) ? profile : nil;
}
NSDictionary *SatchelControllerProfile(NSString *title, NSString *category) {
    return saved(title, category) ?: saved(@"", category) ?: defaults(category);
}
static void store(NSString *title, NSString *category, NSData *data) {
    NSUserDefaults *d = NSUserDefaults.standardUserDefaults;
    NSMutableDictionary *player = [[d dictionaryForKey:key(title)] mutableCopy] ?: [NSMutableDictionary dictionary];
    NSMutableDictionary *profiles = [player[@"ControllerProfiles"] isKindOfClass:NSDictionary.class] ? [player[@"ControllerProfiles"] mutableCopy] : [NSMutableDictionary dictionary];
    if (data) profiles[category] = data; else [profiles removeObjectForKey:category];
    player[@"ControllerProfiles"] = profiles; [d setObject:player forKey:key(title)];
}
BOOL SatchelSaveControllerProfile(NSString *title, NSString *category, NSDictionary *profile) {
    if (![SatchelControllerCategories() containsObject:category] || !valid(profile)) return NO;
    NSData *data = [NSJSONSerialization dataWithJSONObject:profile options:NSJSONWritingSortedKeys error:nil];
    if (!data) return NO; store(title, category, data); return YES;
}
void SatchelResetControllerProfile(NSString *title, NSString *category) { store(title, category, nil); }
void SatchelControllerSetTitle(NSString *title) { activeTitle = [title copy]; }
NSDictionary<NSString *, NSNumber *> *SatchelControllerValues(GCExtendedGamepad *g) {
    NSMutableDictionary *values = [NSMutableDictionary dictionary];
    NSArray *buttons = @[g.buttonA, g.buttonB, g.buttonX, g.buttonY,
        g.leftThumbstickButton ?: NSNull.null, g.rightThumbstickButton ?: NSNull.null,
        g.leftShoulder, g.rightShoulder, g.leftTrigger, g.rightTrigger,
        g.buttonMenu, g.buttonOptions ?: NSNull.null, g.dpad.left, g.dpad.up, g.dpad.right, g.dpad.down,
        g.buttonHome ?: NSNull.null];
    NSArray *inputs = SatchelControllerInputs();
    for (NSUInteger i = 0; i < inputs.count; i++) {
        GCControllerButtonInput *button = buttons[i] == NSNull.null ? nil : buttons[i];
        values[inputs[i]] = @(button.value);
        values[[@"pressed." stringByAppendingString:inputs[i]]] = @(button.isPressed);
    }
    values[@"lx"] = @(g.leftThumbstick.xAxis.value); values[@"ly"] = @(g.leftThumbstick.yAxis.value);
    values[@"rx"] = @(g.rightThumbstick.xAxis.value); values[@"ry"] = @(g.rightThumbstick.yAxis.value);
    return values;
}
static NSArray *shortcutInputs(NSDictionary *p, BOOL hasHome, BOOL hasOptions) {
    NSString *shortcut = p[@"shortcut"];
    if ([shortcut isEqual:@"off"]) return @[];
    if ([shortcut isEqual:@"home"] && hasHome) return @[GCInputButtonHome];
    if (hasOptions) return @[GCInputButtonMenu, GCInputButtonOptions];
    // A compact pad without Home or Options still opens the menu: hold Menu alone.
    // Menu keeps reaching the game as Plus; only the 0.75 second hold opens the menu.
    return @[GCInputButtonMenu];
}
static float clean(id value) {
    float f = [value respondsToSelector:@selector(floatValue)] ? [value floatValue] : 0;
    return isfinite(f) ? fmaxf(-1, fminf(1, f)) : 0;
}
static BOOL pressed(NSDictionary *values, NSString *input) {
    id flag = values[[@"pressed." stringByAppendingString:input]];
    // Hardware retains GameController's own actuation threshold. Raw synthetic inputs
    // can omit the pressed flag and use the deterministic half-scale threshold.
    return [flag isKindOfClass:NSNumber.class] ? [flag boolValue] : clean(values[input]) >= 0.5;
}
NSDictionary *SatchelMapController(NSDictionary *values, NSDictionary *profile, BOOL hasHome, NSMutableDictionary *state) {
    return SatchelMapControllerElements(values, profile, hasHome, YES, state);
}
NSDictionary *SatchelMapControllerElements(NSDictionary *values, NSDictionary *profile, BOOL hasHome, BOOL hasOptions, NSMutableDictionary *state) {
    if (!valid(profile)) profile = defaults(@"Generic");
    NSArray *inputs = shortcutInputs(profile, hasHome, hasOptions);
    BOOL held = inputs.count > 0;
    for (NSString *input in inputs) held &= pressed(values, input);
    BOOL menu = pressed(values, GCInputButtonMenu), options = pressed(values, GCInputButtonOptions);
    // Take over on the first sample with the complete chord. Keep suppressing even
    // after a partial release, until neither button is down. No input is buffered.
    if (!menu && !options) [state removeObjectForKey:@"chordTaken"];
    if (inputs.count == 2 && held) state[@"chordTaken"] = @YES;
    NSMutableArray *reservedInputs = [NSMutableArray array];
    if (inputs.count == 1 && [inputs[0] isEqual:GCInputButtonHome]) [reservedInputs addObjectsFromArray:inputs];
    if ([state[@"chordTaken"] boolValue])
        [reservedInputs addObjectsFromArray:@[GCInputButtonMenu, GCInputButtonOptions]];

    uint64_t bits = 0; NSDictionary *mapping = profile[@"mapping"];
    for (NSString *input in SatchelControllerInputs()) {
        if ([reservedInputs containsObject:input] || !pressed(values, input)) continue;
        NSUInteger bit = [SatchelControllerOutputs() indexOfObject:mapping[input]];
        if (bit < 16) bits |= 1ull << bit;
    }
    uint64_t suppressed = 0;
    if ([state[@"chordTaken"] boolValue]) {
        for (NSString *input in @[GCInputButtonMenu, GCInputButtonOptions]) {
            NSUInteger bit = [SatchelControllerOutputs() indexOfObject:mapping[input]];
            if (bit < 16) suppressed |= 1ull << bit;
        }
    }
    // Preserve another currently pressed physical input mapped to the same guest bit.
    NSMutableDictionary *result = [@{@"buttons": @(bits), @"suppressedButtons": @(suppressed & ~bits)} mutableCopy];
    for (NSString *stick in @[@"l", @"r"]) {
        NSString *xKey = [stick stringByAppendingString:@"x"], *yKey = [stick stringByAppendingString:@"y"];
        float x = clean(values[xKey]), y = clean(values[yKey]);
        float length = hypotf(x, y), dead = [profile[@"deadZone"] floatValue];
        float scale = length > dead ? (fminf(length, 1) - dead) / ((1 - dead) * length) : 0;
        result[xKey] = @(x * scale); result[yKey] = @(y * scale);
    }
    result[@"menuHeld"] = @(held);
    BOOL released = ![state[@"chordTaken"] boolValue] && !held;
    result[@"shortcutReleased"] = @(released);
    // Input callbacks can observe a release between menu timer ticks.
    NSMutableDictionary *hold = state[@"hold"];
    if (!held && (released || ![hold[@"fired"] boolValue])) [hold removeAllObjects];
    return result;
}
static NSMutableDictionary *controllerState(GCExtendedGamepad *pad) {
    static NSMapTable *states; if (!states) states = [NSMapTable weakToStrongObjectsMapTable];
    NSMutableDictionary *state = [states objectForKey:pad];
    if (!state) { state = [NSMutableDictionary dictionary]; [states setObject:state forKey:pad]; }
    return state;
}
NSDictionary *SatchelReadController(GCExtendedGamepad *pad) {
    return SatchelMapControllerElements(SatchelControllerValues(pad), SatchelControllerProfile(activeTitle, SatchelControllerCategory(pad.controller)),
        pad.buttonHome != nil, pad.buttonOptions != nil, controllerState(pad));
}
BOOL SatchelControllerHold(NSMutableDictionary *s, BOOL held, NSTimeInterval now) {
    if (!held) { [s removeAllObjects]; return NO; }
    if (!s[@"since"]) s[@"since"] = @(now);
    if (![s[@"fired"] boolValue] && now - [s[@"since"] doubleValue] >= 0.75) { s[@"fired"] = @YES; return YES; }
    return NO;
}
BOOL SatchelControllerMenuHeld(GCExtendedGamepad *pad, NSTimeInterval now) {
    NSMutableDictionary *state = controllerState(pad);
    NSMutableDictionary *hold = state[@"hold"];
    if (!hold) { hold = [NSMutableDictionary dictionary]; state[@"hold"] = hold; }
    NSDictionary *input = SatchelReadController(pad);
    // A fired chord rearms only after both buttons have been released.
    if ([hold[@"fired"] boolValue] && ![input[@"shortcutReleased"] boolValue]) return NO;
    return SatchelControllerHold(hold, [input[@"menuHeld"] boolValue], now);
}
NSString *SatchelControllerEdge(NSMutableDictionary *s, float x, float y, BOOL accept, BOOL back) {
    // Hysteresis avoids drift generating repeated moves. Held inputs never repeat.
    int old = [s[@"direction"] intValue], direction = old;
    if (fmaxf(fabsf(x), fabsf(y)) < 0.3) direction = 0;
    else if (fmaxf(fabsf(x), fabsf(y)) >= 0.6) direction = fabsf(x) > fabsf(y) ? (x > 0 ? 1 : 2) : (y > 0 ? 3 : 4);
    NSString *event = @"";
    if (direction && direction != old) event = @[@"", @"right", @"left", @"up", @"down"][direction];
    if (accept && ![s[@"accept"] boolValue]) event = @"accept";
    if (back && ![s[@"back"] boolValue]) event = @"back";
    s[@"direction"] = @(direction); s[@"accept"] = @(accept); s[@"back"] = @(back);
    return event;
}
