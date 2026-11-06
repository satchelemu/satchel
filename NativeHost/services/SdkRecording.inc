// nn::oe game play recording (video capture of the last seconds of play).
// SDK 3 titles hand am a recording buffer at startup: Super Mario Odyssey 1.0.0's
// nninitStartup calls EnableGamePlayRecording, which makes the buffer transfer memory
// (svc 0x15) and passes it to IApplicationFunctions through the applet proxy that the
// host never creates (oe::Initialize is a host no-op), so the SDK read a null proxy.
// Ryujinx 1.3.3 IApplicationFunctions answers InitializeGamePlayRecording (66) and
// SetGamePlayRecordingState (67) with success and records nothing; the host has no
// capture service either, so the buffer stays the game's and nothing is kept, as the
// pinned engine does for InitializeCopyrightFrameBuffer.
// Copyright (c) Ryujinx Team and Contributors. MIT, see RYUJINX-LICENSE.txt.
HLE_FORCE("_ZN2nn2oe23EnableGamePlayRecordingEPvm", "game play recording buffer (through am, IApplicationFunctions 66); the host records no video, the buffer stays the game's", void, h_oe_SatchelRecordingEnable, void *buffer, size_t size) {
    (void)buffer; (void)size;
    static int once; if (!once++) LOG("oe: EnableGamePlayRecording: no capture service, nothing recorded");
}
HLE_FORCE("_ZN2nn2oe22PauseGamePlayRecordingEv", "pause game play recording (IApplicationFunctions 67); nothing is recorded", void, h_oe_SatchelRecordingPause, void) {}
HLE_FORCE("_ZN2nn2oe24RestartGamePlayRecordingEv", "restart game play recording (IApplicationFunctions 67); nothing is recorded", void, h_oe_SatchelRecordingRestart, void) {}
