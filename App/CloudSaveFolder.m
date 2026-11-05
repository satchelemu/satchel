#import "CloudSaveFolder.h"
#include <stdio.h>
#include <stdlib.h>

NSNotificationName const SatchelCloudSaveFolderDidChange = @"SatchelCloudSaveFolderDidChange";
static NSString *const kBookmark = @"SatchelCloudSavesBookmark";
static NSURL *sessionFolder;   // held for one game session, released at its end

// The picker's scope is held while the bookmark is written: one written without it resolves on
// this run and to nothing from the next launch on.
static NSData *bookmarkFor(NSURL *url) {
    BOOL scoped = [url startAccessingSecurityScopedResource];
    NSData *bookmark = [url bookmarkDataWithOptions:0 includingResourceValuesForKeys:nil relativeToURL:nil error:nil];
    if (scoped) [url stopAccessingSecurityScopedResource];
    return bookmark;
}
static NSURL *resolve(BOOL *unusable) {
    if (unusable) *unusable = NO;
    NSData *bookmark = [NSUserDefaults.standardUserDefaults dataForKey:kBookmark];
    if (!bookmark.length) return nil;
    BOOL stale = NO;
    NSURL *url = [NSURL URLByResolvingBookmarkData:bookmark options:0 relativeToURL:nil bookmarkDataIsStale:&stale error:nil];
    if (!url) { if (unusable) *unusable = YES; return nil; }   // kept: the same signature may come back
    if (stale) {
        NSData *again = bookmarkFor(url);
        if (again) [NSUserDefaults.standardUserDefaults setObject:again forKey:kBookmark];
    }
    return url;
}

NSString *SatchelCloudSaveFolderName(void) { return resolve(NULL).lastPathComponent; }
BOOL SatchelCloudSaveFolderNeedsChoosing(void) { BOOL unusable; resolve(&unusable); return unusable; }

BOOL SatchelChooseCloudSaveFolder(NSURL *url) {
    NSData *bookmark = bookmarkFor(url);
    NSURL *back = bookmark ? [NSURL URLByResolvingBookmarkData:bookmark options:0 relativeToURL:nil bookmarkDataIsStale:nil error:nil] : nil;
    if (!back) return NO;   // a bookmark that cannot come back is not saved as if it were good
    [NSUserDefaults.standardUserDefaults setObject:bookmark forKey:kBookmark];
    [NSNotificationCenter.defaultCenter postNotificationName:SatchelCloudSaveFolderDidChange object:nil];
    return YES;
}
void SatchelClearCloudSaveFolder(void) {
    [NSUserDefaults.standardUserDefaults removeObjectForKey:kBookmark];
    [NSNotificationCenter.defaultCenter postNotificationName:SatchelCloudSaveFolderDidChange object:nil];
}

void SatchelCloudSavesSessionBegin(void) {
    BOOL unusable = NO;
    NSURL *url = sessionFolder ?: resolve(&unusable);
    if (url && url != sessionFolder) {
        if (![url startAccessingSecurityScopedResource]) {
            fprintf(stderr, "Satchel cloud saves: the chosen folder is not accessible; sync is off for this game\n");
            url = nil;
        } else sessionFolder = url;
    }
    if (unusable) fprintf(stderr, "Satchel cloud saves: choose the folder again in Settings (the app is signed differently now); sync is off\n");
    setenv("DM_SAVE_SYNC", url ? url.fileSystemRepresentation : "0", 1);
    fprintf(stderr, "Satchel cloud saves: %s\n", url ? "on" : "off");   // the folder path stays out of the log
}
void SatchelCloudSavesSessionEnd(void) {
    [sessionFolder stopAccessingSecurityScopedResource];
    sessionFolder = nil;
}
