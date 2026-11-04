#!/usr/bin/env python3
"""Overwrite a caller's control bytes before the real deferred shader-key hash."""
from pathlib import Path
import argparse
import os
import runpy
import shutil
import subprocess

HERE = Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--baseline-ref', help='Also reproduce invalid keys from this git revision')
args = parser.parse_args()
ROOT = HERE.parents[1]
ENGINE = Path.home() / 'SatchelDeps/NX2Apple/engine'
OUT = Path(os.environ.get('SESSION_KEY_OUT', Path.home() / 'SatchelBuild/session/round2-shader-key'))
OUT.mkdir(parents=True, exist_ok=True)
shutil.copyfile(ENGINE / 'LICENSE', OUT / 'NXNative-LICENSE.txt')
runpy.run_path(str(ROOT / 'NativeApp/build_support.py'))['wait']()
prefix = r'''
#include <CoreFoundation/CoreFoundation.h>
#include <CommonCrypto/CommonDigest.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
typedef struct { uint64_t data; const void *control; } NVNshaderData;
typedef struct { uint64_t serial; unsigned nstage; NVNshaderData sd[6]; } prog_t;
typedef prog_t NVNprogram;
typedef int NVNboolean;
struct dm_profile { uint32_t shader_skip_len, shader_skip_off; };
static const struct dm_profile *dm_prof(void) { static struct dm_profile p; return &p; }
static int mtl_addr(uint64_t a, uint64_t n, void **b, uint64_t *o) {
    (void)a; (void)n; (void)b; (void)o; return 0;
}
static void mtl_prog_release(prog_t *p) { (void)p; }
static uint64_t nvn_new_serial(void) { static uint64_t serial; return ++serial; }
static void dm_backtrace(uint64_t p) { (void)p; }
#define LOG(...) ((void)0)
#define OLOG(...) ((void)0)
#define NVN(name, result, ...) result name(__VA_ARGS__)
'''
suffix = r'''
int main(void) {
    unsetenv("DM_NVN_SHDUMP"); unsetenv("DM_PHONE_SHADERS"); unsetenv("DM_NVN_PROGTRACE");
    prog_t program = {0};
    int failures = 0;
    for (int cycle = 0; cycle < 5; cycle++) {
        uint8_t control[0x880], code[64], expected[8], actual[8];
        memset(control, cycle + 1, sizeof control); memset(code, cycle + 3, sizeof code);
        uint32_t own = sizeof control, length = sizeof code;
        memcpy(control + 0x20, &own, 4); memcpy(control + 0x704, &length, 4);
        NVNshaderData data = {(uintptr_t)code, control};
        if (!keys_hash(&data, 0, expected) || !nvnProgramSetShaders(&program, 1, &data)) return 2;
        // The registration call has returned. The caller may reuse this memory.
        memset(control, 0x55, sizeof control);
        if (!prog_key(&program, 0, actual)) return 3;
        int matched = !memcmp(actual, expected, sizeof actual);
        failures += !matched;
        printf("deferred-key cycle=%d matched=%d\n", cycle, matched);
    }
    pk_drop(&program); CFRelease(g_pk);
    return failures ? 1 : 0;
}
'''
for name in (['before'] if args.baseline_ref else []) + ['after']:
    folder = OUT / name
    folder.mkdir(exist_ok=True)
    for file in ['hle_nvn.c', 'nvn_mtl.m']:
        shutil.copyfile(ENGINE / 'dm/gfx/nvn' / file, folder / file)
    patch = folder / 'lifetime.patch'
    patch.write_text(subprocess.check_output(['git', 'show', args.baseline_ref + ':NativeHost/phone-capture-lifetime.patch'], text=True)
                     if name == 'before' else (ROOT / 'NativeHost/phone-capture-lifetime.patch').read_text())
    for source in [ROOT / 'NativeGames/runtime-capture.patch', patch]:
        subprocess.run(['patch', '-p4', '--fuzz=0', '-i', source], cwd=folder, check=True)
    source = (folder / 'hle_nvn.c').read_text()
    start = source.index('typedef struct { uint8_t key[6][8];')
    end = source.index('// ---------- Sync / events ----------', start)
    fixture = folder / 'fixture.c'
    fixture.write_text(prefix + source[start:end] + suffix)
    binary = folder / 'fixture'
    subprocess.run(['nice', '-n', '10', 'xcrun', 'clang', '-O1', str(fixture),
                    '-framework', 'CoreFoundation', '-o', str(binary)], check=True)
    result = subprocess.run([str(binary)], timeout=5)
    assert result.returncode == (1 if name == 'before' else 0), (name, result.returncode)
    print('PASS: ' + name + ' deferred-key control lifetime', flush=True)
