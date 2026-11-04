"""Own graphics producers without editing the display or pipeline archive patch.

Applied to the already overlaid Metal source. The companion included header is
copied from the same pinned engine. Exact anchors reject an incompatible change.
"""
from pathlib import Path
import re
import runpy
import shutil

replace = runpy.run_path(str(Path(__file__).with_name('owners_overlay.py')))['replace']


def wrap_blocks(text, selector, wrapper):
    # Match literal block arguments and balance braces while skipping C comments
    # and strings. Adding a typed wrapper preserves the original callback body.
    matches = list(re.finditer(selector + r'\s*(\^\([^\n]*?\)\s*\{)', text))
    assert matches, selector
    for match in reversed(matches):
        start = match.start(1)
        brace = match.end(1) - 1
        depth, end = 0, None
        tokens = re.finditer(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]', text[brace:])
        for token in tokens:
            if token[0] == '{': depth += 1
            elif token[0] == '}':
                depth -= 1
                if not depth:
                    end = brace + token.end(); break
        assert end and text[end:].lstrip().startswith(']'), selector
        text = text[:start] + wrapper + '(' + text[start:end] + ')' + text[end:]
    return text


def metal(text):
    text = replace(text, '#include <objc/runtime.h>',
                   '#include <objc/runtime.h>\n#include "SessionGraphicsAsync.h"\n#include "SessionState.h"\n#include "SessionGraphicsLocal.h"')
    for selector, wrapper in [('addCompletedHandler:', 'satchel_mtl_completed'),
                              ('addFeedbackHandler:', 'satchel_mtl_feedback'),
                              ('block:', 'satchel_mtl_event')]:
        text = wrap_blocks(text, selector, wrapper)
    text = replace(text, '    (void)link;\n    double cb = CACurrentMediaTime();',
                   '    (void)link;\n    if (atomic_load(&satchel_present_stop)) return;\n    double cb = CACurrentMediaTime();')
    text = replace(text, 'static CAMetalDisplayLink *g_link; static CFRunLoopRef g_link_rl; static int g_link_started;',
                   'static CAMetalDisplayLink *g_link; static CFRunLoopRef g_link_rl; static _Atomic int g_link_started;')
    text = replace(text, '    CFRunLoopRun();', '''    while (!atomic_load(&satchel_present_stop)) {
        SInt32 status = CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.05, false);
        if (status == kCFRunLoopRunStopped || status == kCFRunLoopRunFinished) break;
    }
    [link invalidate]; link.delegate = nil;
    pthread_mutex_lock(&g_pq_mu);
    g_link = nil; g_link_rl = NULL; g_link_owns_layer = 0;
    pthread_cond_broadcast(&g_pq_cv); pthread_mutex_unlock(&g_pq_mu);''')
    text = replace(text, '    static int applied;\n    pthread_mutex_lock(&g_pq_mu);',
                   '    static int applied;\n    if (atomic_load(&satchel_present_stop)) return;\n    pthread_mutex_lock(&g_pq_mu);')
    text = replace(text, 'g_link_started = 1; pthread_t th; if (pthread_create(&th, NULL, link_main, NULL)) MDIE("cannot start the display link thread"); pthread_detach(th);',
                   'g_link_started = 1; int rc = satchel_native_create(&satchel_present_workers, NULL, link_main, NULL); if (rc) { g_link_started = 0; if (rc != ECANCELED) MDIE("cannot start the display link thread"); }')
    text = replace(text, 'while (g_pq_read == g_pq_in || g_link_owns_layer) pthread_cond_wait(&g_pq_cv, &g_pq_mu);',
                   'while (!atomic_load(&satchel_present_stop) && (g_pq_read == g_pq_in || g_link_owns_layer)) pthread_cond_wait(&g_pq_cv, &g_pq_mu);\n        if (atomic_load(&satchel_present_stop)) { pthread_mutex_unlock(&g_pq_mu); break; }')
    text = replace(text, 'static void present_start(void) { pthread_t th; if (pthread_create(&th, NULL, present_main, NULL)) MDIE("cannot start the present thread"); pthread_detach(th); }',
                   'static void present_start(void) { pthread_mutex_lock(&g_pq_mu); if (!satchel_present_started) { int rc = satchel_native_create(&satchel_present_workers, NULL, present_main, NULL); if (!rc) satchel_present_started = 1; else if (rc != ECANCELED) MDIE("cannot start the present thread"); } pthread_mutex_unlock(&g_pq_mu); }')
    text = replace(text, 'static pthread_once_t once = PTHREAD_ONCE_INIT; pthread_once(&once, present_start);', 'present_start();')
    text = replace(text, 'if (g_classic || atomic_load(&g_present_discard)) return 0;',
                   'if (g_classic || atomic_load(&g_present_discard) || atomic_load(&satchel_present_stop)) return 0;')
    text = replace(text, 'while (g_pq_in - g_pq_done >= PQ) pthread_cond_wait(&g_pq_cv, &g_pq_mu);',
                   'while (!atomic_load(&satchel_present_stop) && g_pq_in - g_pq_done >= PQ) pthread_cond_wait(&g_pq_cv, &g_pq_mu);\n    if (atomic_load(&satchel_present_stop)) { pthread_mutex_unlock(&g_pq_mu); return 0; }')
    # The copied post table transfers ownership of nested readback/visibility
    # allocations to the completion record. The reusable scratch table does
    # not own those values after submission.
    text = replace(text, 'if (n) memcpy(ops, g_post, n * sizeof *ops);',
                   'if (n) memcpy(ops, g_post, n * sizeof *ops); g_npost = 0;')
    # The installed macOS Metal descriptor over-releases an assigned custom
    # feedback queue. A standalone five-cycle fixture reproduces it without a
    # game. Use Metal's documented internally owned default queue.
    text = replace(text, 'g_feedback_q = dispatch_queue_create("dm.nvn.mtl4-feedback", feedback_attr);',
                   '(void)feedback_attr; g_feedback_q = nil;')
    text = replace(text, 'static void pso4_init(void) {\n    static dispatch_once_t once;\n    dispatch_once(&once, ^{',
                   'static pthread_mutex_t satchel_pso_mu = PTHREAD_MUTEX_INITIALIZER;\nstatic int satchel_pso_ready;\nstatic void pso4_init(void) {\n    pthread_mutex_lock(&satchel_pso_mu);\n    if (!satchel_pso_ready) { satchel_pso_ready = 1; void (^initialize)(void) = ^{')
    text = replace(text, '    });\n}\nstatic int pso4_compiler(void)',
                   '    }; initialize(); }\n    pthread_mutex_unlock(&satchel_pso_mu);\n}\nstatic int pso4_compiler(void)')
    text = runpy.run_path(str(Path(__file__).with_name('lifecycle_overlay.py')))['graphics'](text)
    text += '\n' + Path(__file__).with_name('SessionGraphicsStop.inc').read_text()
    text += '\n' + Path(__file__).with_name('SessionGraphicsRetire.inc').read_text()
    return text


def recording(text):
    text = replace(text, 'while ((h = atomic_load_explicit(&g_ubc_head, memory_order_acquire)) == t)',
                   'while (!atomic_load(&satchel_encode_stop) && (h = atomic_load_explicit(&g_ubc_head, memory_order_acquire)) == t)')
    text = replace(text, '        for (; t < h; t++)',
                   '        if (t == h && atomic_load(&satchel_encode_stop)) break;\n        for (; t < h; t++)')
    text = replace(text, '    static int started;\n    if (!started) { pthread_t th; if (pthread_create(&th, NULL, ubc_thread, NULL)) MDIE("cannot start the uniform copy thread"); pthread_detach(th); started = 1; }',
                   '    if (!satchel_ubc_started) { if (satchel_native_create(&satchel_encode_workers, NULL, ubc_thread, NULL)) MDIE("cannot start the uniform copy thread"); satchel_ubc_started = 1; }')
    text = replace(text, 'while (!g_rb_head) { g_rb_sleeping',
                   'while (!g_rb_head && !atomic_load(&satchel_encode_stop)) { g_rb_sleeping')
    text = replace(text, '        rec_blk *b = g_rb_head; g_rb_head = b->next;',
                   '        if (!g_rb_head && atomic_load(&satchel_encode_stop)) { pthread_mutex_unlock(&g_rb_mu); break; }\n        rec_blk *b = g_rb_head; g_rb_head = b->next;')
    text = replace(text, 'if (!g_rb_started) { pthread_t th; pthread_attr_t at;',
                   'if (!g_rb_started) { pthread_attr_t at;')
    text = replace(text, 'pthread_create(&th, &at, rec_thread, NULL)',
                   'satchel_native_create(&satchel_encode_workers, &at, rec_thread, NULL)')
    return text


def prepare(engine, destination, metal_source):
    destination.mkdir(parents=True, exist_ok=True)
    for neighbour in list(metal_source.parent.glob('*.inc')) + list(metal_source.parent.glob('PhoneShaderRuntime.h')):
        shutil.copyfile(neighbour, destination / neighbour.name)
    text = metal(metal_source.read_text())
    states = runpy.run_path(str(Path(__file__).with_name('state_overlay.py')))
    text = states['arc_caches'](states['scalar_caches'](text, 'SATCHEL_STATE_GRAPHICS'))
    # A later policy overlay may copy nvn_mtl.m into another directory. Preserve
    # this header's identity instead of silently finding the upstream workers.
    text = replace(text, '#include "nvn_rec.h"',
                   '#include "' + str((destination / 'nvn_rec.h').resolve()) + '"')
    if '#include "PhoneShaderRuntime.inc"' in text:
        text = replace(text, 'satchel_mtl_release_strong();', 'satchel_mtl_release_strong(); satchel_phone_retired = nil; satchel_phone_epoch++;')
    for name in ['RuntimeCapture.inc', 'PhoneShaderRuntime.inc', 'PhoneShaderRuntime.h']:
        path = destination / name
        if path.exists():
            contents = path.read_text()
            if name == 'RuntimeCapture.inc':
                contents = replace(contents, 'static const char *dir; static int checked;',
                    'static const char *dir; SATCHEL_STATE(SATCHEL_STATE_GRAPHICS, dir); static int checked;')
            path.write_text(states['arc_caches'](states['scalar_caches'](contents, 'SATCHEL_STATE_GRAPHICS')))
    (destination / 'nvn_mtl.m').write_text(text)
    (destination / 'nvn_rec.h').write_text(states['scalar_caches'](recording((engine / 'dm/gfx/nvn/nvn_rec.h').read_text()), 'SATCHEL_STATE_GRAPHICS'))
    for name in ['nvn_geometry.inc', 'nvn_feedback.inc']:
        contents = (engine / 'dm/gfx/nvn' / name).read_text()
        (destination / name).write_text(states['arc_caches'](states['scalar_caches'](contents, 'SATCHEL_STATE_GRAPHICS')))
    return destination / 'nvn_mtl.m'
