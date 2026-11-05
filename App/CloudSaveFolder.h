// iCloud save sync: the folder the player chose in Settings, kept as a security-scoped
// bookmark. Adapted from nxnative's dm/ios/Sources/CloudSaves.m (MIT, taken at engine
// bbc3b737); see NativeHost/THIRD_PARTY.md.
//
// The engine's save_sync.c packs one file per game, <folder>/<title ID>/save.dmsync, brings
// the cloud's copy in at the game's first save mount and sends the committed save after each
// Commit, on background and when the session stops. Satchel's own save manager (export,
// import, automatic backups in SaveBackups/) works on the same local save folder but never at
// the same time: see App/GameSavePanel.swift.
#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN
/// The chosen folder's display name, nil when sync is off.
NSString *_Nullable SatchelCloudSaveFolderName(void);
/// YES when a folder was chosen but its bookmark no longer resolves (the app was re-signed with
/// another identity). Sync is then off until a folder is chosen again.
BOOL SatchelCloudSaveFolderNeedsChoosing(void);
/// Stores a bookmark for a folder the document picker just granted. NO when the system will not
/// make one that resolves again; the previous choice is kept.
BOOL SatchelChooseCloudSaveFolder(NSURL *url);
/// Turns sync off. Saves already in the folder stay there.
void SatchelClearCloudSaveFolder(void);
FOUNDATION_EXPORT NSNotificationName const SatchelCloudSaveFolderDidChange;

/// Called by the session bridge after the package environment is set and before the guest
/// starts: sets DM_SAVE_SYNC to the folder's path (its security scope held for the session),
/// or to 0 when sync is off. Idempotent within one session.
void SatchelCloudSavesSessionBegin(void);
/// Called after the session has stopped and the sender has been joined: releases the scope.
void SatchelCloudSavesSessionEnd(void);
NS_ASSUME_NONNULL_END
