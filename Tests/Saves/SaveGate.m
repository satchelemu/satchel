#import "SaveGate.h"
#include "../../NativeHost/session/SessionSaveAccess.inc"
#include <assert.h>

// The engine's clone is checked by its transaction suite, not called by this gate test.
int dm_fs_clone_committed(const char *source, const char *destination) {
    (void)source; (void)destination; errno = ENOTSUP; return -1;
}
void SaveTestSetTitle(NSString *title) {
    [save_lock() lock]; save_set_title(title); [save_lock() unlock];
}
void SaveTestGate(void) {
    NSString *title = @"010000000000000A";
    SaveTestSetTitle(title);
    assert(!SatchelBeginSaveAccess(title.lowercaseString, NO));
    assert(SatchelBeginSaveAccess(title, YES)); SatchelEndSaveAccess();
    assert(SatchelBeginSaveAccess(@"010000000000000B", NO)); SatchelEndSaveAccess();
    // Pause and failed stop retain ownership. Only successful stop clears it.
    assert(!SatchelBeginSaveAccess(title, NO)); SaveTestSetTitle(nil);
    assert(SatchelBeginSaveAccess(title, NO));
    dispatch_semaphore_t entered = dispatch_semaphore_create(0);
    dispatch_semaphore_t prepared = dispatch_semaphore_create(0);
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        dispatch_semaphore_signal(entered);
        SaveTestSetTitle(title);
        dispatch_semaphore_signal(prepared);
    });
    assert(!dispatch_semaphore_wait(entered, dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC)));
    assert(dispatch_semaphore_wait(prepared, dispatch_time(DISPATCH_TIME_NOW, 50 * NSEC_PER_MSEC)));
    // Title inspection on the UI thread does not wait on the long save operation.
    assert(SatchelNativeSaveTitle() == nil);
    SatchelEndSaveAccess();
    assert(!dispatch_semaphore_wait(prepared, dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC)));
    assert(!SatchelBeginSaveAccess(title, NO));
    SaveTestSetTitle(nil);
    puts("PASS: native save gate refuses owned titles and serializes preparation against restore");
}
