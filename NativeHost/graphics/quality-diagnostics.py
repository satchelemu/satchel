"""Opt-in private Mac render-scale bursts for picture quality comparisons.

With DM_QUALITY_SCALE_FILE set, every tenth present reads "SCALE DIRECTORY" from that file
(and empties it). The next twelve presents are saved as raw scan-out PNGs, DIRECTORY/f000.png
to f011.png, while the game's render-scale state (Tears of the Kingdom's stub, state +0x10)
is SCALE from the fourth present and back to its previous value from the seventh. Frames a few presents apart
show nearly the same moment at both scales; quality/present-quality.swift --sharpness finds which
frames came out at the reduced size. Built only into the Mac pipeline host with SATCHEL_QUALITY_DIAGNOSTICS=1,
never into the app. Saving each frame slows those presents; the game's own frame step is fixed.
With DM_NVN_SHOT_OUT set the frames are presented pictures (through the title's present filter)
and DM_QUALITY_PAIR=1 also saves each frame's raw texture in storage order (fNNN.raw.png), so
present-quality.swift --check can compare the engine's filter output with its own.
A third word "view" ("SCALE DIRECTORY view") turns the 3D view resample (view_upscale.py) on for the
frames encoded after f005 is saved until f008, so one burst holds the game's own 1.0 frames and the
reduced frames through the resample; the log names how many draws it replaced by each frame.
"""


def apply(text):
    anchor = '    // DM_NVN_SHOT=frame number[,frame number...]:path (%d in the path is replaced with the frame number)'
    if text.count(anchor) != 1:
        raise RuntimeError('Quality diagnostic anchor is not unique')
    hook = r'''    {   // Diagnostic Mac build only (quality-diagnostics.py).
        extern uint8_t *dm_fps_state; const char *qpath = getenv("DM_QUALITY_SCALE_FILE");
        static int qk = -1, qview; static uint64_t q0; static float qs, qold; static char qdir[900];
        if (qpath && dm_fps_state && qk < 0 && g_present_n % 10 == 0) {
            FILE *f = fopen(qpath, "r");
            if (f) { char ql[1000], qm[16] = ""; if (fgets(ql, sizeof ql, f) && sscanf(ql, "%f %899s %15s", &qs, qdir, qm) >= 2 && qs >= 0.5f && qs <= 1.0f) { qk = 0; q0 = g_present_n; qview = !strcmp(qm, "view"); memcpy(&qold, dm_fps_state + 0x10, sizeof qold); } fclose(f);
                if (qk == 0) { f = fopen(qpath, "w"); if (f) fclose(f); if (qview) view_prepare(); MLOG("[quality] burst at scale %.2f from present %llu%s", qs, (unsigned long long)q0, qview ? ", 3D view resample on from present 5 to 8" : ""); } }
        }
        if (qk >= 0) {
            qk = (int)(g_present_n - q0);
            float v = qk == 3 ? qs : qold;
            if (qk == 3 || qk == 6) memcpy(dm_fps_state + 0x10, &v, sizeof v);
            char qshot[1024]; snprintf(qshot, sizeof qshot, "%s/f%03d.png", qdir, qk);
            mtl_finish_gpu(); shot(src, qshot, 1, crop);
            // With DM_NVN_SHOT_OUT the frame above is the presented picture; DM_QUALITY_PAIR=1 adds its raw texture (storage order) for a filter check.
            if (getenv("DM_QUALITY_PAIR")) { snprintf(qshot, sizeof qshot, "%s/f%03d.raw.png", qdir, qk); shot(src, qshot, 0, crop); }
            // "view" as a third word: the 3D view resample (view_upscale.py) runs for frames encoded after f005 was taken, so the 1.0 frames before it stay the game's own.
            if (qview) { satchel_view_diag = qk >= 5 && qk < 9; MLOG("[quality] f%03d: 3D view resamples so far %llu", qk, (unsigned long long)g_view_n); }
            if (qk >= 11) { qk = -1; if (qview) satchel_view_diag = -1; qview = 0; }
        }
    }
''' + anchor
    return text.replace(anchor, hook, 1)
