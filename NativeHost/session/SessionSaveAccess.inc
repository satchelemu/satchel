#import <Foundation/Foundation.h>
#include <errno.h>

NSString *SatchelNativeSaveTitle(void);
static NSString *g_saveTitle;
static NSRecursiveLock *save_lock(void) {
    static NSRecursiveLock *lock; static dispatch_once_t once;
    dispatch_once(&once, ^{ lock = [NSRecursiveLock new]; });
    return lock;
}
BOOL SatchelBeginSaveAccess(NSString *title, BOOL allowRunning) {
    [save_lock() lock];
    NSString *active = SatchelNativeSaveTitle();
    if (!allowRunning && active && [active caseInsensitiveCompare:title] == NSOrderedSame) {
        [save_lock() unlock]; return NO;
    }
    return YES;
}
void SatchelEndSaveAccess(void) { [save_lock() unlock]; }
NSString *SatchelNativeSaveTitle(void) {
    @synchronized(save_lock()) { return g_saveTitle; }
}
BOOL SatchelCloneCommittedSave(NSString *source, NSString *destination, NSError **error) {
    // Reuse the pinned engine's cloud snapshot primitive. It holds the same path
    // and mount locks as Commit, and clonefile preserves guest-name attributes.
    int dm_fs_clone_committed(const char *, const char *);
    if (!dm_fs_clone_committed(source.fileSystemRepresentation, destination.fileSystemRepresentation)) return YES;
    if (error) *error = [NSError errorWithDomain:NSPOSIXErrorDomain code:errno userInfo:nil];
    return NO;
}

static void save_set_title(NSString *title) {
    @synchronized(save_lock()) { g_saveTitle = [title copy]; }
}
