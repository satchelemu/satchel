"""Controller integration layered on the pinned MIT engine and session overlays."""
from pathlib import Path
import runpy

HERE = Path(__file__).resolve().parent
replace = runpy.run_path(str(HERE.parent / 'session/owners_overlay.py'))['replace']


def hid(text):
    text = replace(text, 'static uint32_t connected_id(void) { return dm_opmode() == 0 ? NPAD_HANDHELD : NPAD_NO1; }', '''static _Atomic int satchel_physical = -1;
static _Atomic uint32_t g_styles_supported = 0xffffffffu;
static _Atomic uint32_t g_players = 1, g_ids_supported = 0x3ff;
static uint32_t connected_id(void) {
    int physical = atomic_load(&satchel_physical);
    int handheld = physical < 0 ? dm_opmode() == 0 : !(physical & 1);
    uint32_t ids = atomic_load(&g_ids_supported), styles = atomic_load(&g_styles_supported);
    if (handheld && (ids & (1u << 8)) && (styles & STYLE_HANDHELD)) return NPAD_HANDHELD;
    return NPAD_NO1;
}''')
    text = replace(text, 'static _Atomic uint32_t g_players = 1, g_ids_supported = 0x3ff;\nstatic uint32_t id_bit', 'static uint32_t id_bit')
    text = replace(text, 'static uint32_t g_styles_supported = 0xffffffffu;\n', '')
    text = replace(text, 'if (id == connected_id()) return 0;', 'if (!(atomic_load(&g_ids_supported) & id_bit(id))) return -1;\n    if (id == connected_id()) return 0;')
    text = replace(text, 'static uint32_t style_for_player(uint32_t id, int p) { return p < 0 ? 0 : id == NPAD_HANDHELD ? STYLE_HANDHELD : STYLE_FULLKEY; }', '''static uint32_t style_for_player(uint32_t id, int p) {
    uint32_t style = p < 0 ? 0 : id == NPAD_HANDHELD ? STYLE_HANDHELD : STYLE_FULLKEY;
    return style & atomic_load(&g_styles_supported);
}''')
    text = replace(text, 'g_style_ev[DM_PLAYERS]', 'g_style_ev[DM_PLAYERS + 1]')
    text = replace(text, 'static void style_changed(uint32_t id) { void *ev = id < DM_PLAYERS ? atomic_load(&g_style_ev[id]) : NULL; if (ev) hle_signal_bound_system_event(ev); }', '''static int satchel_style_index(uint32_t id) { return id == NPAD_HANDHELD ? DM_PLAYERS : id < DM_PLAYERS ? (int)id : -1; }
static void style_changed(uint32_t id) {
    int index = satchel_style_index(id);
    void *ev = index >= 0 ? atomic_load(&g_style_ev[index]) : NULL;
    if (ev) hle_signal_bound_system_event(ev);
}
// Ryujinx NpadDevices: signal every npad whose accepted style changes, including Handheld.
// Copyright (c) Ryujinx Team and Contributors. MIT, see services/RYUJINX-LICENSE.txt.
static void satchel_styles_snapshot(uint32_t styles[DM_PLAYERS + 1]) {
    for (int p = 0; p <= DM_PLAYERS; p++) styles[p] = style_of(p == DM_PLAYERS ? NPAD_HANDHELD : (uint32_t)p);
}
static void satchel_styles_notify(const uint32_t before[DM_PLAYERS + 1]) {
    for (int p = 0; p <= DM_PLAYERS; p++) {
        uint32_t id = p == DM_PLAYERS ? NPAD_HANDHELD : (uint32_t)p;
        if (before[p] != style_of(id)) style_changed(id);
    }
}
void satchel_hid_controllers(uint32_t mask) {
#ifdef SATCHEL_INPUT_SCOPE
    SATCHEL_INPUT_SCOPE();
#endif
    uint32_t beforeNo1 = style_of(NPAD_NO1), beforeHandheld = style_of(NPAD_HANDHELD);
    int old = atomic_exchange(&satchel_physical, (int)mask);
    dm_pad_players(mask);
    for (int p = 0; p < DM_PLAYERS; p++) if (!(mask >> p & 1)) {
        atomic_store(&g_pad_btn[p], 0); atomic_store(&g_pad_axes[p], 0); atomic_store(&g_pad_latch[p], 0);
    }
    if (old != (int)mask) {
        dm_haptics_clear(0);
        if (beforeNo1 != style_of(NPAD_NO1)) style_changed(NPAD_NO1);
        if (beforeHandheld != style_of(NPAD_HANDHELD)) style_changed(NPAD_HANDHELD);
    }
}''')
    text = replace(text, 'if (*id < DM_PLAYERS) atomic_store(&g_style_ev[*id], ev);', 'int index = satchel_style_index(*id);\n    if (index >= 0) atomic_store(&g_style_ev[index], ev);')
    text = replace(text, '    uint32_t was = 0; for (int p = 1; p < DM_PLAYERS; p++) was |= (uint32_t)reported(p) << p;', '    uint32_t before[DM_PLAYERS + 1]; satchel_styles_snapshot(before);')
    text = replace(text, '    for (int p = 1; p < DM_PLAYERS; p++) if ((uint32_t)reported(p) != (was >> p & 1)) style_changed((uint32_t)p);', '    satchel_styles_notify(before);')
    text = replace(text, '    if (styles != g_styles_supported) LOG("hid supported controller styles %#x", styles); g_styles_supported = styles; }', '''    uint32_t before[DM_PLAYERS + 1]; satchel_styles_snapshot(before);
    atomic_store(&g_styles_supported, styles);
    satchel_styles_notify(before);
    for (int p = 0; p < DM_PLAYERS; p++) dm_haptics_clear(p);
}''')
    text = replace(text, 'int count = 0; for (int p = 0; p < DM_PLAYERS; p++) count += reported(p);', 'int count = 0; for (int p = 0; p < DM_PLAYERS; p++) count += style_of(dm_player_npad(p)) != 0;')
    if 'g_players = 1; g_ids_supported' in text:
        text = replace(text, '    g_players = 1; g_ids_supported', '    satchel_physical = -1; atomic_store(&g_style_ev[DM_PLAYERS], NULL);\n    g_players = 1; g_ids_supported')
    text += """
// Discard pending shortcut edges too, so an unconsumed first press cannot leak
// through the engine's short-press latch after the chord is complete.
void satchel_hid_suppress_player(int p, uint64_t buttons) {
#ifdef SATCHEL_INPUT_SCOPE
    SATCHEL_INPUT_SCOPE();
#endif
    if (p < 0 || p >= DM_PLAYERS) return;
    atomic_fetch_and(&g_pad_btn[p], ~buttons);
    atomic_fetch_and(&g_pad_latch[p], ~buttons);
}
void satchel_hid_release(void) {
#ifdef SATCHEL_INPUT_SCOPE
    SATCHEL_INPUT_SCOPE();
#endif
    atomic_store(&g_btn, 0); atomic_store(&g_axes, 0); atomic_store(&g_latch, 0);
    for (int p = 0; p < DM_PLAYERS; p++) {
        atomic_store(&g_pad_btn[p], 0); atomic_store(&g_pad_axes[p], 0); atomic_store(&g_pad_latch[p], 0);
    }
}
"""
    return text


def gamepad(text):
    text = '#include "InGameMenu.h"\nvoid satchel_hid_controllers(uint32_t);\n' + text
    # A connection routes haptics even when the menu currently blocks input.
    text = replace(text, '    if (SatchelMenuBlocksInput()) { dm_pad_set_player(p, 0, 0, 0, 0, 0); return; }\n    dm_haptics_apple_controller(p, g.controller);', '    dm_haptics_apple_controller(p, g.controller);\n    if (SatchelMenuBlocksInput()) { dm_pad_set_player(p, 0, 0, 0, 0, 0); return; }')
    text = replace(text, '    dm_pad_players(pad_slot_mask(&g_slots));', '    satchel_hid_controllers(pad_slot_mask(&g_slots));')
    text = replace(text, '    return pad_slot_mask(&g_slots) != 0;', '    return (pad_slot_mask(&g_slots) & 1) != 0;')
    first = text.index('- (void)padsChanged:(NSNotification *)note {')
    last = text.index('\n}\n@end', first) + 2
    text = text[:first] + '''- (void)padsChanged:(NSNotification *)note {
    if (!NSThread.isMainThread) {
        dispatch_async(dispatch_get_main_queue(), ^{ [self padsChanged:note]; }); return;
    }
    GCController *c = note.object;
    GCController *gone = [note.name isEqual:GCControllerDidDisconnectNotification] ? c : nil;
    BOOL lost = gone && (g_merge ? gone.extendedGamepad != nil : pad_slot_of(&g_slots, (__bridge const void *)gone) >= 0);
    if (lost) SatchelMenuControllerDisconnected();
    BOOL playerOne = g_merge ? pads_merged(gone) : pads_slotted(gone);
    if (g_merge) satchel_hid_controllers(playerOne ? 1 : 0);
    self->_padOff = playerOne; self->_controls.hidden = playerOne; [self reset];
}
''' + text[last:]
    text += '''
// Refresh the app-owned slot table after a guest session resets its HID state.
void satchel_gamepad_refresh(void) { [g_view padsChanged:nil]; }
'''
    text = replace(text, 'void dm_gamepad_reset(void) {', 'void dm_gamepad_reset(void) {\n    void satchel_hid_release(void); satchel_hid_release();')
    # Serialize GameController input callbacks with slot mutations and UIKit.
    text = replace(text, 'static void read_pad(GCExtendedGamepad *g) {', '''static void read_pad(GCExtendedGamepad *g) {
    if (!NSThread.isMainThread) { dispatch_async(dispatch_get_main_queue(), ^{ read_pad(g); }); return; }''')
    text = text.replace('@[GCInputButtonHome, GCInputButtonShare]', '@[GCInputButtonHome, GCInputButtonShare, GCInputButtonMenu, GCInputButtonOptions]')
    text = '#import "' + str(HERE.parents[1] / 'App/ControllerProfiles.h') + '"\n' + text
    first = text.index('    uint64_t m = dm_pad_face(g);')
    last = text.index('\n}', first)
    text = text[:first] + '''    NSDictionary *input = SatchelReadController(g);
    void satchel_hid_suppress_player(int, uint64_t);
    satchel_hid_suppress_player(p, [input[@"suppressedButtons"] unsignedLongLongValue]);
    dm_pad_set_player(p, [input[@"buttons"] unsignedLongLongValue], axis([input[@"lx"] floatValue]),
        axis([input[@"ly"] floatValue]), axis([input[@"rx"] floatValue]), axis([input[@"ry"] floatValue]));''' + text[last:]
    return text


def haptics(text):
    first = text.index('static void apply(dm_haptic_state s) {')
    last = text.index('static void add_engine(', first)
    body = text[first:last]
    body = replace(body, 'static void apply(dm_haptic_state s) {\n    dispatch_async(queue, ^{ @autoreleasepool {', 'static void satchel_rumble_apply_one(dm_haptic_state s) {')
    body = replace(body, '    } });\n}', '}')
    text = text[:first] + body + (HERE / 'HapticQueue.inc').read_text() + '\n' + text[last:]
    return text + '\nvoid satchel_haptics_suspend(unsigned reason, int on) { if (queue) suspend(reason, on != 0); }\n'


def slots(text):
    # Keep occupied players stable, but a replacement second pad uses player 2.
    line = '    for (int i = 0; p < 0 && i < DM_PLAYERS; i++) if (!s->owner[i] && !s->last[i][0]) p = i;\n'
    text = replace(text, line, '')
    return text.replace('the lowest slot\n// no controller has held yet (so a slot stays kept for a controller that went away while others are in use), then the lowest free slot.', 'the lowest free slot.')


def apply(root, engine, out, groups):
    destination = out / 'overlays/controllers'; destination.mkdir(parents=True, exist_ok=True)
    (destination / 'pad_slots.h').write_text(slots((engine / 'dm/host/pad_slots.h').read_text()))
    for row in groups['SatchelNativeHost']:
        path = Path(row['path']); text = None
        if path.name == 'hle_hid.c': text = hid(path.read_text())
        elif path.name == 'DmGamepad.m': text = gamepad(path.read_text())
        elif path.name == 'haptics_apple.m':
            text = haptics(path.read_text())
        if text is not None:
            target = destination / path.name; target.write_text(text); row['path'] = str(target)
