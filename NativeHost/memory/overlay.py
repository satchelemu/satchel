"""Scene memory diagnostics and reclamation, applied after the graphics overlay."""
from pathlib import Path
import shutil


def replace(text, before, after):
    if text.count(before) != 1:
        raise RuntimeError('Memory overlay anchor mismatch: ' + before)
    return text.replace(before, after, 1)


def prepare(source, destination, reclaim=True):
    destination.mkdir(parents=True, exist_ok=True)
    for neighbour in list(source.parent.glob('*.inc')) + list(source.parent.glob('*.h')):
        shutil.copyfile(neighbour, destination / neighbour.name)
    text = source.read_text()
    text = replace(text, 'void mtl_sub_begin(void) {',
                   'static void satchel_scene_memory_sample(void);\nvoid mtl_sub_begin(void) {')
    text = replace(text, '    g_sub_pool = objc_autoreleasePoolPush();',
                   '    g_sub_pool = objc_autoreleasePoolPush();\n    satchel_scene_memory_sample();')
    if reclaim:
        # Finalized descriptors may still be replayed while their pool lives.
        # Remember only those bounded ghost entries, not every historical serial.
        text = replace(text, 'static void cpu_tex_drop_pool(const void *pool);',
                       'static void cpu_tex_drop_pool(const void *pool);\nstatic void satchel_ghost_drop_pool(const void *pool);')
        text = replace(text, '    image_desc_drop_pool(pool);',
                       '    image_desc_drop_pool(pool);\n    satchel_ghost_drop_pool(pool);')
        text = replace(text, 'static nvn_hmap g_ghost = {.klen = 8};',
                       'static nvn_hmap satchel_ghost_pool = {.klen = 8};\nstatic nvn_hmap g_ghost = {.klen = 8};')
        # SATCHEL_POOL_KEEP: a texture registered again by its serial alone keeps the pool recorded for it.
        text = replace(text, 'static void tex_put(uint64_t k, id o) {',
                       '#define SATCHEL_POOL_KEEP ((const void *)-1)\nstatic void tex_put(uint64_t k, id o, const void *pool) {')
        text = replace(text, 'hm_del_id(&g_ghost, slot);',
                       'hm_del_id(&g_ghost, slot); nvn_hm_del(&satchel_ghost_pool, slot);')
        text = replace(text, '    hm_put_id(m, &k, o); ser_note(k);',
                       '    if (m == &g_ghost && pool != SATCHEL_POOL_KEEP) { if (pool) nvn_hm_put(&satchel_ghost_pool, &k, (void *)pool); else nvn_hm_del(&satchel_ghost_pool, &k); }\n'
                       '    hm_put_id(m, &k, o); ser_note(k);')
        for old, new, count in [('tex_put(t->serial, upgraded)', 'tex_put(t->serial, upgraded, t->pool)', 1),
                                ('tex_put(t->serial, nv)', 'tex_put(t->serial, nv, t->pool)', 1),
                                ('tex_put(t->serial, pk)', 'tex_put(t->serial, pk, t->pool)', 2),
                                ('tex_put(t->serial, mt)', 'tex_put(t->serial, mt, t->pool)', 2),
                                ('tex_put(t->serial, shared)', 'tex_put(t->serial, shared, t->pool)', 1),
                                ('tex_put(t->serial, v)', 'tex_put(t->serial, v, t->pool)', 1),
                                ('tex_put(ser[i], mt)', 'tex_put(ser[i], mt, SATCHEL_POOL_KEEP)', 1)]:
            if text.count(old) != count: raise RuntimeError('Texture ownership anchor mismatch: ' + old)
            text = text.replace(old, new)
        # Every registration names its pool: a call site this overlay does not know must not compile silently.
        import re
        if len(re.findall(r'\btex_put\(', text)) != 10: raise RuntimeError('Texture ownership: unexpected tex_put call count')
        anchor = '// Host record of a packaged texture:'
        text = replace(text, anchor, Path(__file__).with_name('GhostPool.inc').read_text() + '\n' + anchor)
        # Session teardown owns the map allocation too, before scalar restoration.
        if 'int satchel_mtl_session_retire(void)' in text:
            text = replace(text, '&g_treg_serial, &g_pso_fail, &g_pbase_pending, &g_cpu_unc};',
                           '&g_treg_serial, &g_pso_fail, &g_pbase_pending, &g_cpu_unc, &satchel_ghost_pool};')
        text = replace(text,
            '    for (NSUInteger i = g_arenas.count; i-- > 0;) { NvArena *a = g_arenas[i];',
            '    size_t idle_bytes = 0;\n'
            '    for (NvArena *a in g_arenas)\n'
            '        if (a->fence && a->fence <= done && g_fence > a->fence + ARENA_IDLE) idle_bytes += a->len;\n'
            '    for (NSUInteger i = g_arenas.count; i-- > 0;) { NvArena *a = g_arenas[i];')
        text = replace(text,
            'a->len > ARENA_KEEP && g_fence > a->fence + ARENA_IDLE) { res_remove(a->buf);',
            '(a->len > ARENA_KEEP || idle_bytes > (64u << 20)) && g_fence > a->fence + ARENA_IDLE) { idle_bytes -= a->len; res_remove(a->buf);')
    text += '\n' + Path(__file__).with_name('SceneMemory.inc').read_text()
    result = destination / 'nvn_mtl.m'
    result.write_text(text)
    return result


def apply(root, engine, out, groups):
    for row in groups['SatchelNativeGraphics']:
        if Path(row['path']).name == 'nvn_mtl.m':
            row['path'] = str(prepare(Path(row['path']), out / 'overlays/memory'))
            return
    raise RuntimeError('Memory overlay requires the NVN Metal source')
