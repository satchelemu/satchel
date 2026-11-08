"""Mac-only census of the renderer's file-scope caches, appended by build_mac.py.

Every file-scope nvn_hmap and Foundation collection in the composed nvn_mtl.m gets
a column, so a long run shows which host table grows. The census is generated from
the composed source rather than a hand list, so a new cache cannot be missed. It is
never part of the app: the phone build does not run this file.
Values are entry counts and table capacities only, never keys or guest data.
"""
import re

TYPES = ('NSMutableArray', 'NSMutableDictionary', 'NSMutableSet', 'NSHashTable', 'NSMapTable', 'NSMutableOrderedSet')


def names(text):
    hmaps, collections = [], []
    for line in text.splitlines():
        if not line.startswith('static '):
            continue
        # One line can hold several declarations: "static A *a; static B *b;".
        for part in line.split('//')[0].split(';'):
            part = part.strip()
            if part.startswith('static nvn_hmap '):
                # "a = {.klen = 8}, b" declares two tables; braces never nest here.
                body = re.sub(r'\{[^}]*\}', '', part[len('static nvn_hmap '):])
                hmaps += [item.split('=')[0].strip() for item in body.split(',') if item.strip()]
            elif part.startswith(tuple('static ' + kind for kind in TYPES)) and '(' not in part:
                collections += re.findall(r'\*\s*(g_[A-Za-z0-9_]+)', part)
    hmaps = [n for n in dict.fromkeys(hmaps) if re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]*', n)]
    return hmaps, list(dict.fromkeys(collections))


def source(text, every=600):
    hmaps, collections = names(text)
    if len(hmaps) < 20 or len(collections) < 10:
        raise RuntimeError('Cache census found too few tables: %d hmaps, %d collections' % (len(hmaps), len(collections)))
    header = ['frame'] + [n + '.n' for n in hmaps] + [n + '.cap' for n in hmaps] + [n + '.count' for n in collections]
    lines = ['// Mac-only cache census (NativeHost/memory/census.py). Counts and capacities only.',
             'static void satchel_cache_census(void) { @autoreleasepool {',
             '    const char *path = getenv("SATCHEL_CACHE_CENSUS");',
             '    if (!path || !*path) return;',
             '    static uint64_t previous = UINT64_MAX;',
             '    uint64_t frame = atomic_load(&g_cur_frame);',
             '    if (previous != UINT64_MAX && frame >= previous && frame - previous < %d) return;' % every,
             '    previous = frame;',
             '    FILE *file = fopen(path, "a"); if (!file) return;',
             '    if (ftell(file) == 0) fputs("%s\\n", file);' % '\\t'.join(header),
             '    fprintf(file, "%llu", (unsigned long long)frame);']
    # Reads are unlocked: a census sample can be one update stale, never a write.
    for name in hmaps:
        lines.append('    fprintf(file, "\\t%%u", %s.n);' % name)
    for name in hmaps:
        lines.append('    fprintf(file, "\\t%%u", %s.cap);' % name)
    for name in collections:
        lines.append('    fprintf(file, "\\t%%lu", (unsigned long)[%s count]);' % name)
    lines += ['    fputc(\'\\n\', file);', '    fclose(file);', '} }']
    return '\n'.join(lines) + '\n', header
