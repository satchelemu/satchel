// Round 11 SDK takeovers for families whose Initialize is a host no-op in the pinned
// engine (oe, pctl and aoc in hle_sdkinit.c, bcat and album in hle_offline.c). The
// SDK's own client of such a family keeps the service object that Initialize would
// have opened; with the no-op it stays null, so any function of the family that the
// engine does not take over reads a null proxy, as nn::oe::EnableGamePlayRecording did
// in Super Mario Odyssey 1.0.0 (SdkRecording.inc). These are the application-facing
// functions of those families, in the SDKs of the tested games, that had no takeover.
// Each answers what Ryujinx 1.3.3 answers for the command the function sends, or, where
// the engine already decided a policy for the family (no album, no VR display, no
// delivery data, no network), that policy.
// Copyright (c) Ryujinx Team and Contributors. MIT, see RYUJINX-LICENSE.txt.
// Sources: Am/AppletAE/AllSystemAppletProxiesService/SystemAppletProxy/ISelfController.cs
// (65 ReportUserIsActive, 80 SetWirelessPriorityMode), ICommonStateGetter.cs (62),
// Am/AppletOE/ApplicationProxyService/ApplicationProxy/IApplicationFunctions.cs (30, 31,
// 60, 65, 90), Pctl/ParentalControlServiceFactory/IParentalControlService.cs (1013, 1031,
// 1064, 1065) and Pctl/ResultCode.cs, Ns/Aoc/IAddOnContentManager.cs (5, 7, 50), all under
// src/Ryujinx.HLE/HOS/Services, and src/Ryujinx.Horizon/Bcat/BcatResult.cs.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"   // the takeovers keep the SDK signatures
#define SATCHEL_PCTL_STEREO_DENIED ((104u << 9) | 142u)
#define SATCHEL_BCAT_INTERNET_DENIED ((8u << 9) | 122u)
#define SATCHEL_BCAT_NOT_MOUNTED ((5u << 9) | 122u)
static void satchel_sdk_once(const char *what) {
    // One line per function, no arguments: the same once-only style as the engine.
    static const char *seen[32]; static unsigned count; static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&mu);
    for (unsigned i = 0; i < count; i++) if (seen[i] == what) { pthread_mutex_unlock(&mu); return; }
    if (count < 32) seen[count++] = what;
    pthread_mutex_unlock(&mu);
    LOG("sdk: %s", what);
}

// ---------- oe (am through the application proxy) ----------
HLE_FORCE("_ZN2nn2oe18ReportUserIsActiveEv", "report user activity (ISelfController 65, a reference stub); the host has no idle dimming", void, h_oe_SatchelUserActive, void) {}
HLE_FORCE("_ZN2nn2oe23SetWirelessPriorityModeENS0_20WirelessPriorityModeE", "wireless priority (ISelfController 80, a reference stub); the host has no Joy-Con radio", void, h_oe_SatchelWireless, int mode) {
    (void)mode; satchel_sdk_once("oe::SetWirelessPriorityMode (no radio, ignored)");
}
HLE_FORCE("_ZN2nn2oe25BeginMediaPlaybackSectionEv", "media playback state on (IApplicationFunctions 60, a reference stub)", void, h_oe_SatchelMediaBegin, void) {}
HLE_FORCE("_ZN2nn2oe23EndMediaPlaybackSectionEv", "media playback state off (IApplicationFunctions 60, a reference stub)", void, h_oe_SatchelMediaEnd, void) {}
HLE_FORCE("_ZN2nn2oe28IsGamePlayRecordingSupportedEv", "game play recording support (IApplicationFunctions 65); the host records no video, as EnableGamePlayRecording", int, h_oe_SatchelRecordingSupported, void) { return 0; }
HLE_FORCE("_ZN2nn2oe17EnableCrashReportEv", "crash report switch (IApplicationFunctions 90, a reference stub)", void, h_oe_SatchelCrashOn, void) {}
HLE_FORCE("_ZN2nn2oe18DisableCrashReportEv", "crash report switch (IApplicationFunctions 90, a reference stub)", void, h_oe_SatchelCrashOff, void) {}
HLE_FORCE("_ZN2nn2oe26GetHdcpAuthenticationStateEv", "HDCP state (ICommonStateGetter 62): the reference answers 0", int, h_oe_SatchelHdcp, void) { return 0; }
// Ryujinx's home-button blocking commands (IApplicationFunctions 30 to 33) are success stubs,
// and the engine's BeginBlockingHomeButton hook is a no-op: the host has no HOME button.
HLE_FORCE("_ZN2nn2oe35BeginBlockingHomeButtonShortPressedENS_8TimeSpanE", "block the HOME button short press; the host has no HOME button", void, h_oe_SatchelBlockShortSpan, int64_t span) { (void)span; }
HLE_FORCE("_ZN2nn2oe35BeginBlockingHomeButtonShortPressedEv", "block the HOME button short press; the host has no HOME button", void, h_oe_SatchelBlockShort, void) {}
HLE_FORCE("_ZN2nn2oe33EndBlockingHomeButtonShortPressedEv", "restore the HOME button short press", void, h_oe_SatchelUnblockShort, void) {}

// ---------- pctl: the same answers as the engine's CheckStereoVisionPermission (no VR display) ----------
HLE_FORCE("_ZN2nn4pctl20IsRestrictionEnabledEv", "parental controls restriction (IParentalControlService 1031): off, as the reference default", int, h_pctl_SatchelRestricted, void) { return 0; }
HLE_FORCE("_ZN2nn4pctl23IsStereoVisionPermittedEv", "stereoscopic 3D permission (1065); the host has no VR display, as CheckStereoVisionPermission", int, h_pctl_SatchelStereoPermitted, void) { return 0; }
HLE_FORCE("_ZN2nn4pctl29ConfirmStereoVisionPermissionEv", "stereoscopic 3D permission (1013): the reference's StereoVisionDenied, as CheckStereoVisionPermission", Result, h_pctl_SatchelStereoConfirm, void) {
    satchel_sdk_once("pctl::ConfirmStereoVisionPermission (no VR display, denied)"); return SATCHEL_PCTL_STEREO_DENIED;
}
HLE_FORCE("_ZN2nn4pctl36ResetConfirmedStereoVisionPermissionEv", "reset the 3D permission (1064, success)", void, h_pctl_SatchelStereoReset, void) {}

// ---------- aoc: the same answers as the aoc:u IPC service (ServiceIPC.inc) ----------
HLE_FORCE("_ZN2nn3aoc21GetAddOnContentBaseIdEv", "DLC base ID (IAddOnContentManager 5): the program ID plus 0x1000, the package convention", uint64_t, h_aoc_SatchelBaseId, void) { return satchel_host_program_id() + 0x1000; }
HLE_FORCE("_ZN2nn3aoc19PrepareAddOnContentEi", "prepare a DLC (IAddOnContentManager 7): the reference's no-op success; the DLC is mounted as usual", Result, h_aoc_SatchelPrepare, int index) {
    (void)index; satchel_sdk_once("aoc::PrepareAddOnContent (reference no-op)"); return RES_OK;
}
HLE_FORCE("_ZN2nn3aoc28CheckAddOnContentMountStatusEv", "DLC mount status (IAddOnContentManager 50): the reference's success", Result, h_aoc_SatchelMountStatus, void) { return RES_OK; }

// ---------- bcat: the engine fails MountDeliveryCacheStorage (no delivery data) and nifm is offline ----------
// Ryujinx's RequestSyncDeliveryCache hands back a progress object that reports Done; the host cannot
// fill the SDK's DeliveryCacheProgress (its layout is the SDK's own), so it answers the offline result
// instead, which the game handles as it handles any failed sync.
#define SATCHEL_BCAT_SYNC(mangled, cname, ...) \
    HLE_FORCE(mangled, "sync the delivery cache: offline, BCAT InternetRequestDenied", Result, cname, __VA_ARGS__) { \
        satchel_sdk_once("bcat::RequestSyncDeliveryCache (offline)"); return SATCHEL_BCAT_INTERNET_DENIED; }
SATCHEL_BCAT_SYNC("_ZN2nn4bcat24RequestSyncDeliveryCacheEPNS0_21DeliveryCacheProgressE", h_bcat_SatchelSync, void *progress)
SATCHEL_BCAT_SYNC("_ZN2nn4bcat24RequestSyncDeliveryCacheEPNS0_21DeliveryCacheProgressENS_13ApplicationIdEj", h_bcat_SatchelSyncApp, void *progress, uint64_t app, unsigned version)
SATCHEL_BCAT_SYNC("_ZN2nn4bcat24RequestSyncDeliveryCacheEPNS0_21DeliveryCacheProgressENS_13ApplicationIdEjPKc", h_bcat_SatchelSyncAppDir, void *progress, uint64_t app, unsigned version, const char *dir)
SATCHEL_BCAT_SYNC("_ZN2nn4bcat24RequestSyncDeliveryCacheEPNS0_21DeliveryCacheProgressEPKc", h_bcat_SatchelSyncDir, void *progress, const char *dir)
HLE_FORCE("_ZN2nn4bcat30CancelSyncDeliveryCacheRequestEv", "cancel a delivery cache sync: none is running", void, h_bcat_SatchelCancel, void) {}
HLE_FORCE("_ZN2nn4bcat31EnumerateDeliveryCacheDirectoryEPiPNS0_13DirectoryNameEi", "list delivery cache directories: no storage is mounted, BCAT TargetNotMounted", Result, h_bcat_SatchelEnumerate, int *count, void *names, int max) {
    (void)names; (void)max; if (count) *count = 0; return SATCHEL_BCAT_NOT_MOUNTED;
}

// ---------- album: the engine's policy for SaveScreenshot (no album, fails), for the other overloads ----------
#define SATCHEL_ALBUM_FAIL(mangled, cname, ...) \
    HLE_FORCE(mangled, "save a picture to the album: no album, fails", Result, cname, __VA_ARGS__) { \
        satchel_sdk_once("album: save (no album)"); return RES_FAIL; }
SATCHEL_ALBUM_FAIL("_ZN2nn5album14SaveScreenshotEPKvmNS0_9ImageSizeENS0_16ImageOrientationENS0_17AlbumReportOptionEPKNS_7account3UidEi", h_album_SatchelSaveUsers, const void *p, size_t n, int size, int orient, int report, const void *users, int count)
SATCHEL_ALBUM_FAIL("_ZN2nn5album26SaveScreenshotWithUserDataEPKvmNS0_9ImageSizeENS0_16ImageOrientationENS0_17AlbumReportOptionES2_m", h_album_SatchelSaveData, const void *p, size_t n, int size, int orient, int report, const void *data, size_t data_size)
SATCHEL_ALBUM_FAIL("_ZN2nn5album22SaveAndShareScreenshotEPKvmNS0_9ImageSizeENS0_16ImageOrientationENS0_17AlbumReportOptionERKNS_7account3UidE", h_album_SatchelShareOrient, const void *p, size_t n, int size, int orient, int report, const void *uid)
SATCHEL_ALBUM_FAIL("_ZN2nn5album22SaveAndShareScreenshotEPKvmNS0_9ImageSizeENS0_17AlbumReportOptionERKNS_7account3UidE", h_album_SatchelShare, const void *p, size_t n, int size, int report, const void *uid)
SATCHEL_ALBUM_FAIL("_ZN2nn5album22SaveAndShareScreenshotEPKvmNS0_9ImageSizeENS0_17AlbumReportOptionERKNS_7account3UidEPKc", h_album_SatchelShareText, const void *p, size_t n, int size, int report, const void *uid, const char *text)
SATCHEL_ALBUM_FAIL("_ZN2nn5album22SaveAndShareScreenshotEPNS0_7ShareIdEPKvmNS0_9ImageSizeENS0_16ImageOrientationENS0_17AlbumReportOptionERKNS_7account3UidE", h_album_SatchelShareIdOrient, void *id, const void *p, size_t n, int size, int orient, int report, const void *uid)
SATCHEL_ALBUM_FAIL("_ZN2nn5album22SaveAndShareScreenshotEPNS0_7ShareIdEPKvmNS0_9ImageSizeENS0_16ImageOrientationENS0_17AlbumReportOptionERKNS_7account3UidEPKc", h_album_SatchelShareIdOrientText, void *id, const void *p, size_t n, int size, int orient, int report, const void *uid, const char *text)
SATCHEL_ALBUM_FAIL("_ZN2nn5album22SaveAndShareScreenshotEPNS0_7ShareIdEPKvmNS0_9ImageSizeENS0_17AlbumReportOptionERKNS_7account3UidE", h_album_SatchelShareId, void *id, const void *p, size_t n, int size, int report, const void *uid)
SATCHEL_ALBUM_FAIL("_ZN2nn5album34SaveAndShareScreenshotWithUserDataEPKvmNS0_9ImageSizeENS0_16ImageOrientationENS0_17AlbumReportOptionERKNS_7account3UidEPKcS2_m", h_album_SatchelShareData, const void *p, size_t n, int size, int orient, int report, const void *uid, const char *text, const void *data, size_t data_size)
HLE_FORCE("_ZN2nn5album17ShowAllAlbumFilesEv", "album applet: not shown, as ShowAlbumFiles", Result, h_album_SatchelShowAll, void) { return RES_FAIL; }
#pragma clang diagnostic pop
