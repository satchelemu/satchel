#!/usr/bin/env python3
"""Exercise pinned AOC count/list, mount routing and DMROMFS1 reading with synthetic bytes."""
import os
from pathlib import Path
import ast
import runpy
import subprocess
import sys
engine=Path(os.environ.get('SATCHEL_DEPS_ROOT', Path.home() / 'SatchelDeps')) / 'NX2Apple/engine/dm/host'
out=Path(sys.argv[1]); out.mkdir(parents=True,exist_ok=True)
misc=(engine/'hle_misc.c').read_text()
aoc=misc[misc.index('static int g_aoc['):misc.index('// The event object')]
# Compile the exact misc reset transform used by the app's session overlay.
overlay = Path(__file__).resolve().parents[2]/'NativeHost/session/services_overlay.py'
namespace = runpy.run_path(str(overlay))
for node in ast.walk(ast.parse(overlay.read_text())):
    if isinstance(node, ast.Dict):
        for key, value in zip(node.keys, node.values):
            if isinstance(key, ast.Constant) and key.value == 'hle_misc.c':
                transform = eval(compile(ast.Expression(value), str(overlay), 'eval'), namespace)
                # The transform covers the whole file since the engine pin move (purchase and
                # stream play events too), so it runs on the whole file and the reset it appends
                # joins the add-on content slice. The other state it clears is declared below.
                whole = transform(misc)
                aoc += whole[whole.rindex('\nint satchel_misc_session_reset(void) {'):]
fs=(engine/'hle_fs.c').read_text()
lookup=fs[fs.index('static int aoc_dir('):fs.index('HLE_FORCE("_ZN2nn2fs31QueryMountAddOnContentCacheSize')]
mount=fs[fs.index('HLE_FORCE("_ZN2nn2fs17MountAddOnContent'):fs.index('// System data archives')]
source='''#include <assert.h>
#include <stdio.h>
#include <dirent.h>
#include <errno.h>
#include <unistd.h>
static int quiescent = 1;
static int satchel_session_quiescent(void) { return quiescent; }
#include <stdatomic.h>
#include <stdint.h>
// Stand-ins for the non add-on state the same reset clears (hle_misc.c purchase and stream play events).
static uint8_t purchase_event[0x80], g_stpl_ev[0x80]; static int satchel_purchase_ready;
static _Atomic uint32_t g_stpl_styles, g_stpl_hold;
#include "romfs_image.h"
#define LOG(...) do { if (0) printf(__VA_ARGS__); } while (0)
#define HLE_FORCE_BIG(symbol, doc, ret, name, ...) ret name(__VA_ARGS__)
#define HLE_FORCE(symbol, doc, ret, name, ...) ret name(__VA_ARGS__)
typedef unsigned Result;
#define RES_OK 0u
#define RES_TARGET_NOT_FOUND 0x7D402u
static romfs_img *mounted;
static Result mount(const char *name, const char *path, int readonly) {
    (void)name; assert(readonly); mounted = romfs_image_open(path); return mounted ? RES_OK : RES_TARGET_NOT_FOUND;
}
'''+aoc+lookup+mount+'''
int main(int argc, char **argv) {
    assert(argc == 3); setenv("DM_AOC_DIR", argv[1], 1); unsetenv("DM_AOC_OWNED");
    unsetenv("DM_AOC_DIR"); assert(h_aoc_Count() == 0);
    setenv("DM_AOC_DIR", argv[1], 1); assert(h_aoc_Count() == 0);
    quiescent = 0; assert(satchel_misc_session_reset() == EBUSY && h_aoc_Count() == 0);
    quiescent = 1; assert(satchel_misc_session_reset() == 0);
    assert(h_aoc_Count() == 1);
    int indices[3] = {0}; assert(h_aoc_List(indices, 0, 3) == 1 && indices[0] == 1);
    assert(h_aoc_List(indices, 1, 3) == 0);
    assert(h_fs_MountAoc("dlc", 1, NULL, 0) == RES_OK);
    assert(mounted && mounted->n == 2 && mounted->e[1].type == 1 && mounted->e[1].b == 600);
    assert(mounted->e[1].nlen == 5 && !memcmp(mounted->names + mounted->e[1].name, "hello", 5));
    unsigned char data[600]; assert(romfs_image_read(mounted->fd, data, sizeof data, mounted->e[1].a));
    for (int i=0;i<600;i++) assert(data[i] == (unsigned char)i);
    romfs_image_close(mounted); mounted = NULL;
    assert(h_fs_MountAoc("missing", 2, NULL, 0) == RES_TARGET_NOT_FOUND);
    char folder[4096], source[4096], second[4096], ninth[4096];
    snprintf(folder, sizeof folder, "%s/late-XXXXXX", argv[2]); assert(mkdtemp(folder));
    snprintf(source, sizeof source, "%s/1.img", argv[1]);
    snprintf(second, sizeof second, "%s/2.img", folder);
    snprintf(ninth, sizeof ninth, "%s/9.img", folder);
    setenv("DM_AOC_DIR", folder, 1); assert(satchel_misc_session_reset() == 0);
    assert(h_aoc_Count() == 0); assert(!link(source, ninth) && !link(source, second));
    assert(h_aoc_Count() == 0); assert(satchel_misc_session_reset() == 0);
    assert(h_aoc_Count() == 2 && h_aoc_List(indices, 0, 3) == 2);
    assert(indices[0] == 2 && indices[1] == 9);
    assert(h_aoc_List(indices, 1, 1) == 1 && indices[0] == 9);
    assert(h_fs_MountAoc("late", 9, NULL, 0) == RES_OK);
    romfs_image_close(mounted); mounted = NULL;
    assert(!unlink(second) && !unlink(ninth) && !rmdir(folder));
    setenv("DM_AOC_OWNED", "0", 1); assert(satchel_misc_session_reset() == 0); assert(h_aoc_Count() == 0);
    puts("PASS: pinned engine AOC count/list/pagination, mount routing, image contents, missing-index rejection and post-boot session reset");
}
'''
(out/'aoc.c').write_text(source)
subprocess.run(['xcrun','clang','-O1','-Wall','-Wextra','-Werror','-I',str(engine),str(out/'aoc.c'),'-o',str(out/'aoc-test')],check=True)
subprocess.run([str(out/'aoc-test'),Path(sys.argv[2]).read_text() if Path(sys.argv[2]).is_file() else sys.argv[2], str(out)],check=True)
