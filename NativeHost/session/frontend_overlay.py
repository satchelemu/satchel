"""Keep UIKit alive while denying old audio callbacks during guest retirement."""
from pathlib import Path
import runpy
replace = runpy.run_path(str(Path(__file__).with_name('owners_overlay.py')))['replace']


def prepare(source, destination):
    text = source.read_text()
    text = 'extern int SatchelNativeSessionAcceptsEvents(void);\n' + text
    first = text.index('// Real audio output is suspended')
    last = text.index('\n}', text.index('void front_ios_focus_event(', first)) + 2
    text = text[:first] + Path(__file__).with_name('SessionFrontendLifecycle.inc').read_text() + text[last:]
    text = replace(text, '    if (self.launched || !front_ios_boot_ok()) return;',
        '    if (self.launched) { if (front_ios_boot_ok()) front_ios_boot(); return; }\n    if (!front_ios_boot_ok()) return;')
    text = replace(text, 'if (now - t0 < 1.5) l.text =',
        'if (SatchelNativeSessionAcceptsEvents() && n >= n0 && now - t0 < 1.5) l.text =')
    text += '''
// Main queue only. The view, Metal layer and system observers are app owners.
// Session AudioUnits have already been disposed before ending activation.
void satchel_front_session_begin(void) {
    satchel_front_epoch++; satchel_audio_retry_pending = 0;
    g_session_off = 0; satchel_audio_hold(0); satchel_haptics_suspend(16, 0);
    satchel_lifecycle_begin((SatchelLifecycleHooks){satchel_front_pause, satchel_front_audio, satchel_front_flush, satchel_front_pressure});
    void satchel_gamepad_refresh(void); satchel_gamepad_refresh();
    front_ios_audio_session();
    if (g_session_off) satchel_lifecycle_route();
}
void satchel_front_session_end(void) {
    satchel_front_epoch++; satchel_audio_retry_pending = 0;
    satchel_lifecycle_end(); satchel_audio_hold(0);
    dm_gamepad_reset();
    [g_prog removeFromSuperview]; g_prog = nil; g_prog_count = nil; g_prog_bar = nil; g_prog_title = nil;
    [AVAudioSession.sharedInstance setActive:NO withOptions:AVAudioSessionSetActiveOptionNotifyOthersOnDeactivation error:nil];
    g_session_off = 0;
}
'''
    destination.write_text(text)
    return destination
