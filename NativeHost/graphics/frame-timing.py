"""Instrument pinned command-buffer callbacks and the matched CPU frame clock."""
from pathlib import Path
import shutil


def replace(text, anchor, replacement):
    if text.count(anchor) != 1:
        raise RuntimeError('Frame timing anchor is not unique: ' + anchor)
    return text.replace(anchor, replacement, 1)


def metal(path):
    path = Path(path)
    text = path.read_text()
    text = replace(text, 'void mtl_init(void) {\n    if (g_dev) return;',
                   'static void satchel_timing_reset(void);\nvoid mtl_init(void) {\n    if (g_dev) return;\n    satchel_timing_reset();')
    text = replace(text, '#define GRING 1024', '#include "FrameTiming.inc"\n#define GRING 1024')
    text = replace(text, 'static void gpu_tag_begin(uint64_t f) {',
                   'static void gpu_tag_begin(uint64_t f) {\n    satchel_timing_begin(f);')
    for frame, cb, error in [('f', 'c', 'c.status != MTLCommandBufferStatusCompleted'),
                             ('gpu_f', 'cb', 'cb.error != nil'), ('f', 'cb', 'cb.error != nil')]:
        anchor = f'gpu_tag_done({frame}, {cb}.GPUStartTime, {cb}.GPUEndTime);'
        text = replace(text, anchor, anchor + f' if (satchel_timing_on()) satchel_timing_done({frame}, {cb}.GPUStartTime, {cb}.GPUEndTime, {error}, 0);')
    anchor = '    int fx_kind = kind == PRESENT_METALFX;'
    text = replace(text, anchor, anchor + '\n    satchel_timing_begin(frame);')
    anchor = '        if (f.error) MLOG("[RED] display command buffer error: %s", f.error.description.UTF8String);'
    text = replace(text, anchor, '        if (satchel_timing_on()) satchel_timing_done(frame, f.GPUStartTime, f.GPUEndTime, f.error != nil, 1);\n' + anchor)
    anchor = '    uint64_t v = ++g_snap_v;'
    text = replace(text, anchor, anchor + '''
    MTL4CommitOptions *timing_options = nil;
    if (satchel_timing_on()) {
        uint64_t frame = atomic_load(&g_cur_frame); satchel_timing_begin(frame);
        timing_options = [MTL4CommitOptions new];
        MTL4CommitFeedbackHandler timing_feedback = ^(id<MTL4CommitFeedback> feedback) {
            satchel_timing_done(frame, feedback.GPUStartTime, feedback.GPUEndTime, feedback.error != nil, 0);
        };
#if defined(SATCHEL_MANAGED_SESSION)
        timing_feedback = satchel_mtl_feedback(timing_feedback);
#endif
        [timing_options addFeedbackHandler:timing_feedback];
    }''')
    text = replace(text, 'rec_t *r = rec_own_put(b, RO_COMMIT_REAL, 2); r->a[0] = 0; r->a[1] = rec_obj(cb);',
                   'rec_t *r = rec_own_put(b, RO_COMMIT_REAL, 2); r->a[0] = rec_obj(timing_options); r->a[1] = rec_obj(cb);')
    text = replace(text, 'pthread_mutex_lock(&g_res_mu); [g_q4 commit:&cb count:1]; [g_q4 signalEvent:g_snap_ev value:v];',
                   'pthread_mutex_lock(&g_res_mu); [g_q4 commit:&cb count:1 options:timing_options]; [g_q4 signalEvent:g_snap_ev value:v];')
    path.write_text(text)
    for name in ['FrameTiming.h', 'FrameTiming.inc']:
        shutil.copyfile(Path(__file__).with_name(name), path.parent / name)


def host(source, destination):
    text = Path(source).read_text()
    anchor = '    power_line(frame);'
    text = replace(text, anchor, anchor + '\n    void satchel_timing_summary(uint64_t, double); satchel_timing_summary(frame, tgt);')
    anchor = '            trow(f, "@cpu_ms", pcpu && pthread_equal(th, pth) ? (c - pcpu) / 1e6 : -1); pth = th; pcpu = c;'
    text = replace(text, anchor, '''            double frame_cpu = pcpu && pthread_equal(th, pth) && c >= pcpu ? (c - pcpu) / 1e6 : -1;
            void satchel_timing_cpu_frame(uint64_t, double, double); satchel_timing_cpu_frame(f, frame_cpu, fr);
            trow(f, "@cpu_ms", frame_cpu); pth = th; pcpu = c;''')
    Path(destination).write_text(text)
