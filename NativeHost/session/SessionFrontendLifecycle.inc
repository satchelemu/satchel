#include "SessionLifecycle.h"
#include "SessionBoundary.h"
#include <malloc/malloc.h>
void mtl_link_pause(int);
void mtl_present_discard(int);
void satchel_audio_hold(int);
int satchel_audio_outputs_ready(void);
int satchel_fs_lifecycle_flush(void);
unsigned satchel_fs_memory_pressure(void);
unsigned satchel_mtl_memory_pressure(void);
static unsigned satchel_front_epoch;
static int satchel_audio_retry_pending;
void satchel_haptics_suspend(unsigned, int);
static void satchel_front_pause(int on) {
    satchel_haptics_suspend(16, on);
    dm_gamepad_reset();
    if (on) {
        satchel_audio_hold(1);
        dm_focus_set(2); dm_focus_set(3);
        guest_clock_freeze_for(1, 1);
        // Release queued presentation waits before draining the graphics gate.
        mtl_present_discard(1); mtl_link_pause(1); dm_gfx_suspend(1);
        satchel_session_pause(1);
    } else {
        satchel_audio_hold(0); guest_clock_freeze_for(1, 0);
        mtl_present_discard(0); mtl_link_pause(0); dm_gfx_suspend(0);
        satchel_session_pause(0); dm_focus_set(2); dm_focus_set(1); dm_gamepad_active();
    }
    UIApplication.sharedApplication.idleTimerDisabled = !on;
    LOG("Satchel lifecycle: guest=%s graphics=%s display=%s", on ? "paused" : "resumed", on ? "drained" : "active", on ? "paused" : "active");
}
static int satchel_front_audio(int off, int reset) {
    if (off) { dm_audio_suspend(1); return 1; }
    // Route changes can invalidate a running unit. Keep one consumer while
    // restoring the preferred 48 kHz / 5 ms configuration and output unit.
    dm_audio_suspend(1); front_ios_audio_session();
    if (!g_session_off && reset) dm_audio_reset();
    if (g_session_off || !satchel_audio_outputs_ready()) {
        if (!satchel_audio_retry_pending) {
            satchel_audio_retry_pending = 1; unsigned epoch = satchel_front_epoch;
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC), dispatch_get_main_queue(), ^{
                if (epoch != satchel_front_epoch) return;
                satchel_audio_retry_pending = 0;
                if (SatchelNativeSessionAcceptsEvents() && UIApplication.sharedApplication.applicationState == UIApplicationStateActive)
                    satchel_lifecycle_retry_audio();
            });
        }
        return 0;
    }
    dm_audio_suspend(0); return 1;
}
static void satchel_front_flush(void) { satchel_fs_lifecycle_flush(); }
static void satchel_front_pressure(void) {
    unsigned directories = satchel_fs_memory_pressure(), pipelines = satchel_mtl_memory_pressure();
    size_t bytes = malloc_zone_pressure_relief(NULL, 0);
    LOG("Satchel lifecycle: memory warning directories=%u speculative-pipelines=%u allocator-bytes=%zu", directories, pipelines, bytes);
}
void satchel_front_menu_paused(int on) { satchel_lifecycle_menu(on); }
void front_ios_focus_event(int event) { satchel_lifecycle_focus(event); }
static void front_ios_audio_watch(void) {
    NSNotificationCenter *nc = NSNotificationCenter.defaultCenter;
    [nc addObserverForName:AVAudioSessionInterruptionNotification object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *note) {
        if (!SatchelNativeSessionAcceptsEvents()) return;
        int began = [note.userInfo[AVAudioSessionInterruptionTypeKey] unsignedIntegerValue] == AVAudioSessionInterruptionTypeBegan;
        int resume = ([note.userInfo[AVAudioSessionInterruptionOptionKey] unsignedIntegerValue] & AVAudioSessionInterruptionOptionShouldResume) != 0;
        satchel_lifecycle_interruption(began, resume);
    }];
    for (NSNotificationName name in @[AVAudioSessionRouteChangeNotification, AVAudioSessionMediaServicesWereResetNotification]) {
        [nc addObserverForName:name object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *note) {
            if ([note.name isEqualToString:AVAudioSessionRouteChangeNotification]) {
                NSUInteger reason = [note.userInfo[AVAudioSessionRouteChangeReasonKey] unsignedIntegerValue];
                if (reason == AVAudioSessionRouteChangeReasonCategoryChange) return;
            }
            if (SatchelNativeSessionAcceptsEvents()) satchel_lifecycle_route();
        }];
    }
    [nc addObserverForName:UIApplicationDidReceiveMemoryWarningNotification object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *note) {
        (void)note; if (SatchelNativeSessionAcceptsEvents()) satchel_lifecycle_pressure();
    }];
    NSArray *events = @[UIApplicationWillResignActiveNotification, UIApplicationDidEnterBackgroundNotification,
                        UIApplicationWillEnterForegroundNotification, UIApplicationDidBecomeActiveNotification];
    [events enumerateObjectsUsingBlock:^(NSNotificationName name, NSUInteger event, BOOL *stop) {
        (void)stop;
        [nc addObserverForName:name object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *note) {
            (void)note; if (SatchelNativeSessionAcceptsEvents()) satchel_lifecycle_focus((int)event);
        }];
    }];
}
