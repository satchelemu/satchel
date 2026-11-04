// Synthetic submit-thread fixture, using the production first-use adapter.
#import <Foundation/Foundation.h>
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
@interface NvShader : NSObject
@end
@implementation NvShader
@end
typedef struct { const void *control; uint64_t data; int fixtureStage; } NVNshaderData;
typedef struct { uint64_t serial; unsigned nstage; NVNshaderData sd[2]; } prog_t;
static NSMutableDictionary *g_shmap, *g_shcache;
static uint64_t satchel_phone_epoch;
static unsigned calls, invalidations;
static int prepareStatus = 1;
static uint32_t lastWanted;
static int satchel_phone_enabled(void) { return 1; }
static void shmap_load(void) { if (!g_shmap) g_shmap = [NSMutableDictionary dictionary]; }
static void serial_del(uint64_t serial) { assert(serial); invalidations++; }
static int satchel_capture_stage(const NVNshaderData *s) { return s->fixtureStage; }
static int prog_key(const prog_t *p, unsigned i, uint8_t *key) { memset(key, p->sd[i].fixtureStage + 1, 8); return 1; }
static NvShader *builtin_get(const char *key) { return nil; }
static NSString *satchel_capture_key(const prog_t *p, int stage, unsigned *index) { return @"0202020202020202"; }
static uint32_t inputs(const char *key) { return 1u << 3; }
static uint32_t (*satchel_phone_shader_inputs)(const char *) = inputs;
static int satchel_phone_shader_prepare(const char *key, const void *ctl, size_t nctl, const void *code, size_t ncode, uint32_t wanted) {
    assert(nctl == 0x880 && ncode == 32 && ctl && code);
    calls++; lastWanted = wanted; return prepareStatus;
}
#define MLOG(...) ((void)0)
#define MDIE(...) abort()
#include "PhoneShaderRuntime.inc"
// The pinned engine's actual string contract, not the translator's stage spelling.
static int stage_of(NSString *stage) {
    if ([stage isEqualToString:@"vertex"]) return 0; if ([stage isEqualToString:@"pixel"]) return 1; if ([stage isEqualToString:@"compute"]) return 5;
    if ([stage isEqualToString:@"hull"]) return 3; if ([stage isEqualToString:@"domain"]) return 4; if ([stage isEqualToString:@"geometry"]) return 2; return -1;
}
int main(void) { @autoreleasepool {
    uint8_t control[0x880] = {0}, code[32] = {0}; uint32_t size = sizeof code;
    memcpy(control + 0x704, &size, 4);
    g_shcache = [NSMutableDictionary dictionary]; NvShader *old = [NvShader new]; g_shcache[@"0101010101010101_b"] = old;
    prog_t p = {.serial = 1, .nstage = 2, .sd = {{control, (uintptr_t)code, 0}, {control, (uintptr_t)code, 1}}};
    satchel_phone_prepare_program(&p, 0);
    assert(calls == 2 && lastWanted == (1u << 3));
    assert(stage_of(g_shmap[@"0101010101010101"]) == 0 && stage_of(g_shmap[@"0202020202020202"]) == 1);
    assert(!g_shcache.count && [satchel_phone_retired containsObject:old]);
    assert(satchel_phone_epoch == 2 && invalidations == 1);
    satchel_phone_prepare_program(&p, 0); assert(calls == 2);
    satchel_phone_prepare_program(&p, 1u << 5); assert(calls == 4 && lastWanted == ((1u << 3) | (1u << 5)));
    prog_t delayed = {.serial = 2, .nstage = 1, .sd = {{control, (uintptr_t)code, -1}}};
    satchel_phone_prepare_program(&delayed, 0); assert(calls == 4);
    delayed.sd[0].fixtureStage = 5; satchel_phone_prepare_program(&delayed, 0);
    assert(calls == 5 && stage_of(g_shmap[@"0606060606060606"]) == 5);
    prog_t geometry = {.serial = 3, .nstage = 1, .sd = {{control, (uintptr_t)code, 2}}};
    prepareStatus = -4; satchel_phone_prepare_program(&geometry, 0);
    assert(calls == 6 && !g_shmap[@"0303030303030303"]);
    prepareStatus = 1; satchel_phone_prepare_program(&geometry, 0);
    assert(calls == 7 && stage_of(g_shmap[@"0303030303030303"]) == 2);
    satchel_phone_prepare_program(&geometry, 0); assert(calls == 7);
    puts("PASS: first-use stage mapping, observed padding, invalidation, retained revisions delayed mapping, geometry and failed preparation retry");
} }
