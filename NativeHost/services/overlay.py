"""Layer service coverage over the pinned host and optional session changes."""
from pathlib import Path
import re
import shlex
import shutil

HERE = Path(__file__).resolve().parent

def replace(text, old, new):
    if text.count(old) != 1:
        raise RuntimeError('Service overlay anchor mismatch: ' + old[:100])
    return text.replace(old, new)


def prepare(source, destination):
    destination.mkdir(parents=True, exist_ok=True)
    text = source.read_text()
    first = text.index('// Services whose client')
    last = text.index('#include "hle.h"', first)
    text = text[:first] + '// Satchel Horizon service HIPC adapter. See NativeHost/services/README.md.\n' + text[last:]
    text = replace(text, '#include "hle.h"', '#include "hle.h"\n#include "EngineAdapter.inc"')
    start = text.index('#define SF_H0')
    end = text.index('HLE_FORCE(', start)
    text = text[:start] + '#include "SessionIPC.inc"\n\n' + text[end:]
    bodies = {
        'h_sf_Connect': 'return satchel_session_connect(out, name, dm_bigstack_caller(__builtin_return_address(0)));',
        'h_sf_Close': 'satchel_session_close(h);',
        'h_sf_PtrBuf': 'uint32_t value; Result result = satchel_session_control(h, 0, &value); if (!result) *out = (uint16_t)value; return result;',
        'h_sf_ToDomain': 'return satchel_session_control(h, 1, out);',
        'h_sf_Dup': 'return satchel_session_control(h, 2, out);',
        'h_sf_Send': 'return satchel_session_send(h, m, size, dm_bigstack_caller(__builtin_return_address(0)));',
    }
    for name, body in bodies.items():
        start = text.index('{', text.index(', ' + name + ','))
        end = start + 1
        depth = 1
        while depth:
            if text[end] == '{': depth += 1
            if text[end] == '}': depth -= 1
            end += 1
        text = text[:start] + '{ ' + body + ' }' + text[end:]
    # DLC discovery uses libc directory scanning, as does the existing big-stack SDK hook.
    text = replace(text, 'HLE_FORCE("_ZN2nn2sf4hipc15SendSyncRequest',
                   'HLE_FORCE_BIG("_ZN2nn2sf4hipc15SendSyncRequest')
    text = replace(text, 'HLE_FORCE("_ZN2nn2sf4hipc20ConnectToHipcService',
                   'HLE_FORCE_BIG("_ZN2nn2sf4hipc20ConnectToHipcService')
    if 'int satchel_sf_session_reset(void)' in text:
        text = replace(text, 'memset(g_sf, 0, sizeof g_sf);', 'satchel_sessions_reset();')
    target = destination / 'hle_sf.c'; target.write_text(text)
    for name in ['ServiceIPC.inc', 'EngineAdapter.inc', 'SessionIPC.inc', 'MissingServices.inc', 'SystemIPC.inc', 'Round8IPC.inc', 'Round8Trace.inc']:
        shutil.copyfile(HERE / name, destination / name)
    shutil.copyfile(HERE / 'coverage/MissingServiceNames.inc', destination / 'MissingServiceNames.inc')
    return target


def apply(root, engine, out, groups):
    found = set()
    for row in groups['SatchelNativeHost']:
        if Path(row['path']).name == 'hle_sf.c':
            row['path'] = str(prepare(Path(row['path']), out / 'overlays/services'))
            found.add('sf')
        elif Path(row['path']).name in ('hle_thread.c', 'hle_misc.c'):
            row['path'] = str(prepare_support(Path(row['path']), out / 'overlays/services'))
            found.add(Path(row['path']).stem)
        elif Path(row['path']).name == 'hle_fs.c':
            source = Path(row['path'])
            # Preserve quoted headers supplied by the preceding graphics overlay.
            row['compilerFlags'] = (row.get('compilerFlags', '') +
                ' -iquote ' + shlex.quote(str(source.parent))).strip()
            row['path'] = str(prepare_fs(source, out / 'overlays/services'))
            found.add('fs')
        elif Path(row['path']).name == 'hle_time.c':
            row['path'] = str(prepare_time(Path(row['path']), out / 'overlays/services'))
            found.add('time')
    if found != {'sf', 'fs', 'time', 'hle_thread', 'hle_misc'}: raise RuntimeError('Service overlay requires sf, time, thread and misc sources')


def prepare_fs(source, destination):
    destination.mkdir(parents=True, exist_ok=True)
    text = source.read_text()
    # The volume-query SDK header declares mount and mount_t too.
    text = re.sub(r'\bmount_t\b', 'satchel_fs_mount_t', text)
    text = re.sub(r'\bmount(?=\s*\()', 'satchel_fs_mount', text)
    text = (HERE / 'Round8Trace.inc').read_text() + '\n' + text
    text = replace(text, '#include "save_sync.h"', '#include "save_sync.h"\nstatic uint32_t satchel_fs_sdk_free_space(int64_t *, const char *);')
    text = replace(text, '(void)path; if (out) *out = 256 << 20; return RES_OK;',
                   'return satchel_fs_sdk_free_space(out, path);')
    text = replace(text, 'fsp-srv; free space of a mount: 256 MB (the host directory has no size limit of its own)',
                   'fsp-srv; available bytes on a registered mount filesystem')
    text = replace(text, 'if (g_nmnt >= MAX_MOUNT) die("mount table full");',
                   'if (g_nmnt >= MAX_MOUNT) { pthread_mutex_unlock(&g_lock); return ((105u << 9) | 1u); }')
    text += '\n' + (HERE / 'FileQueries.inc').read_text()
    target = destination / 'hle_fs.c'; target.write_text(text)
    return target


def prepare_time(source, destination):
    destination.mkdir(parents=True, exist_ok=True)
    text = source.read_text()
    for name in ['h_time_GetCurrentTime', 'h_time_GetContext', 'h_time_SteadyNow']:
        if ', ' + name + ',' not in text: raise RuntimeError('Clock overlay anchor mismatch: ' + name)
    text += '\n' + (HERE / 'ClockState.inc').read_text()
    declarations = """
uint32_t satchel_host_time_control(unsigned, const uint8_t *, uint8_t *);
int satchel_host_clock_override(uint8_t *);
uint32_t satchel_host_steady_adjust(int64_t *);
"""
    text = replace(text, '#include <time.h>', '#include <time.h>\n' + declarations)
    text = replace(text, 'memcpy(out, &s, 8); memcpy(out + 8, "dmhost-steady-01", 16);',
                   'uint32_t result = satchel_host_steady_adjust(&s); if (result) return result; memcpy(out, &s, 8); memcpy(out + 8, "dmhost-steady-01", 16);')
    text = replace(text, 'int64_t ft = fixed_time(); *out = ft >= 0 ? ft : time(NULL); return RES_OK;', """uint8_t ctx[32], tp[24];
    if (satchel_host_clock_override(ctx)) {
        uint32_t result = h_time_SteadyNow(tp); if (result) return result;
        if (memcmp(ctx + 16, tp + 8, 16)) return ((102u << 9) | 116u);
        int64_t offset, steady; memcpy(&offset, ctx, 8); memcpy(&steady, tp, 8);
        __int128 now = (__int128)offset + steady;
        if (now < INT64_MIN || now > INT64_MAX) return ((201u << 9) | 116u);
        *out = (int64_t)now; return RES_OK;
    }
    int64_t ft = fixed_time(); *out = ft >= 0 ? ft : time(NULL); return satchel_host_steady_adjust(out);""")
    text = replace(text, 'static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER; static uint8_t ctx[0x20];',
                   'if (satchel_host_clock_override(out)) return RES_OK;\n    static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER; static uint8_t ctx[0x20];')
    text = replace(text, 'int64_t ft = fixed_time(); return ft >= 0 ? ft : time(NULL); }',
                   'int64_t value = 0; h_time_GetCurrentTime(&value); return value; }')
    text = replace(text, 'static int h_time_timespec(int64_t *ts) {',
                   'static int h_time_timespec(int64_t *ts) {\n    uint8_t ctx[32]; if (satchel_host_clock_override(ctx)) { ts[1] = 0; return h_time_GetCurrentTime(ts) ? -1 : 0; }')
    text = replace(text, 'if (ft >= 0) { ts[0] = ft; ts[1] = 0; return 0; }',
                   'if (ft >= 0) { ts[0] = ft; ts[1] = 0; return satchel_host_steady_adjust(ts) ? -1 : 0; }')
    for clock in ['CLOCK_REALTIME', 'CLOCK_MONOTONIC']:
        text = replace(text, 'clock_gettime(' + clock + ', &t); ts[0] = t.tv_sec; ts[1] = t.tv_nsec; return 0;',
                       'clock_gettime(' + clock + ', &t); ts[0] = t.tv_sec; ts[1] = t.tv_nsec; return satchel_host_steady_adjust(ts) ? -1 : 0;')
    text = replace(text, 'uint8_t tp[0x18]; int64_t now; h_time_SteadyNow(tp); h_time_GetCurrentTime(&now);',
                   'uint8_t tp[0x18]; int64_t now; uint32_t result = h_time_SteadyNow(tp); if (result) return result; result = h_time_GetCurrentTime(&now); if (result) return result;')
    text = replace(text, 'uint8_t tp[0x18]; h_time_SteadyNow(tp);\n    int64_t now; h_time_GetCurrentTime(&now);\n    h_time_GetContext(s);',
                   'uint8_t tp[0x18]; uint32_t result = h_time_SteadyNow(tp); if (result) return result;\n    int64_t now; result = h_time_GetCurrentTime(&now); if (result) return result;\n    result = h_time_GetContext(s); if (result) return result;')
    text = replace(text, '    h_time_Snapshot(s);\n    s[0xcd] = 2;',
                   '    uint32_t result = h_time_Snapshot(s); if (result) return result;\n    s[0xcd] = 2;')
    text = replace(text, 's[0xcd] = 1;', 'satchel_host_time_control(100, NULL, s + 0xcc); s[0xcd] = 1;')
    text += '''
// Share the existing SDK clock with raw Horizon IPC clients.
uint32_t satchel_host_clock_now(int64_t *out) { return h_time_GetCurrentTime(out); }
uint32_t satchel_host_clock_context(uint8_t *out) { return h_time_GetContext(out); }
uint32_t satchel_host_clock_steady(uint8_t *out) { return h_time_SteadyNow(out); }
'''
    target = destination / 'hle_time.c'; target.write_text(text)
    return target


def prepare_support(source, destination):
    destination.mkdir(parents=True, exist_ok=True)
    text = source.read_text()
    if source.name == 'hle_misc.c':
        text += """
// IPC and SDK clients share the package's owned DLC index, including reset.
unsigned satchel_host_aoc_list(uint32_t *out, unsigned start, unsigned count) {
    aoc_load();
    if (!out) return (unsigned)g_naoc;
    if (start >= (unsigned)g_naoc) return 0;
    unsigned n = (unsigned)g_naoc - start; if (n > count) n = count;
    for (unsigned i = 0; i < n; i++) out[i] = (uint32_t)g_aoc[start + i];
    return n;
}
uint64_t satchel_host_program_id(void) { return dm_prof()->program_id; }
"""
    elif source.name == 'hle_thread.c':
        text += """
// IPC copy handles are real read ends in the existing kernel table. Each object
// retains one owner handle; clients close their independent copies through SVC.
// Unlike h_new, exhaustion returns an error to IPC instead of aborting the host.
uint32_t satchel_host_event_create(int signaled) {
    pthread_mutex_lock(&G);
    uint32_t handle = 0;
    for (unsigned i = 1; i < MAX_H; i++) if (!g_h[i].type) {
        event_t *e = calloc(1, sizeof *e);
        if (e) {
            e->refs = 1; e->signaled = !!signaled;
            g_h[i].type = O_EVENT_R; g_h[i].obj = e; handle = 0x1000 + i;
        }
        break;
    }
    pthread_mutex_unlock(&G); return handle;
}
uint32_t satchel_host_event_copy(uint32_t owner) {
    pthread_mutex_lock(&G);
    event_t *e = h_peek(owner, O_EVENT_R); uint32_t handle = 0;
    if (e) for (unsigned i = 1; i < MAX_H; i++) if (!g_h[i].type) {
        e->refs++; g_h[i].type = O_EVENT_R; g_h[i].obj = e; handle = 0x1000 + i; break;
    }
    pthread_mutex_unlock(&G); return handle;
}
void satchel_host_event_set(uint32_t handle, int signaled) {
    pthread_mutex_lock(&G);
    event_t *e = h_peek(handle, O_EVENT_R);
    if (e) e->signaled = !!signaled;
    if (e && signaled) wake_sync_waiters(e);
    pthread_mutex_unlock(&G);
}
void satchel_host_event_close(uint32_t handle) {
    pthread_mutex_lock(&G);
    event_t *e = h_peek(handle, O_EVENT_R);
    if (e) {
        unsigned i = handle - 0x1000;
        g_h[i].type = O_FREE; g_h[i].obj = NULL;
        if (!--e->refs) free(e);
    }
    pthread_mutex_unlock(&G);
}
"""
    else:
        raise RuntimeError('Unsupported service support source: ' + source.name)
    target = destination / source.name; target.write_text(text)
    return target
