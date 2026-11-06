#pragma once
// Swift returns retained Objective-C objects. The caller transfers ownership to ARC.
extern int32_t satchel_phone_shader_prepare(const char *, const void *, size_t, const void *, size_t, uint32_t) __attribute__((weak_import));
extern uint32_t satchel_phone_shader_inputs(const char *) __attribute__((weak_import));
extern void *satchel_phone_shader_reflection(const char *) __attribute__((weak_import));
extern void *satchel_phone_shader_library(const char *, int32_t *) __attribute__((weak_import));
extern void satchel_phone_shader_ahead(const char *, const void *, size_t, const void *, size_t,
                                       const char *, const void *, size_t, const void *, size_t) __attribute__((weak_import));
extern int32_t satchel_phone_shader_ahead_ready(const char *) __attribute__((weak_import));
static uint64_t satchel_phone_epoch;
static int satchel_phone_enabled(void) {
    static int enabled = -1;
    if (enabled < 0) { const char *v = getenv("DM_PHONE_SHADERS"); enabled = satchel_phone_shader_prepare && v && atoi(v); }
    return enabled;
}
// Bounded first use: translation and base libraries start when the game binds a program.
static int satchel_phone_ahead_on(void) {
    const char *v = getenv("DM_PSO_AHEAD");   // set by the launch policy before a session starts
    return v && !strcmp(v, "1") && satchel_phone_enabled() && satchel_phone_shader_ahead && satchel_phone_shader_ahead_ready;
}
static NSDictionary *satchel_phone_reflection(NSString *name) {
    if (!satchel_phone_enabled() || !satchel_phone_shader_reflection || name.length < 16) return nil;
    return (__bridge_transfer NSDictionary *)satchel_phone_shader_reflection(name.UTF8String);
}
static id<MTLLibrary> satchel_phone_library(NSString *path, int *layer) {
    if (!satchel_phone_enabled() || !satchel_phone_shader_library) return nil;
    NSString *name = path.lastPathComponent.stringByDeletingPathExtension;
    int32_t value = 0;
    id<MTLLibrary> lib = (__bridge_transfer id<MTLLibrary>)satchel_phone_shader_library(name.UTF8String, &value);
    if (lib && layer) *layer = value;
    return lib;
}
