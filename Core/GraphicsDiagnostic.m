#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <SatchelShaderProbe/SatchelShaderProbe.h>
#include "GraphicsDiagnostic.h"
#include "ShaderBridge.h"
#include "Fixture.h"
#include <stdlib.h>
#include <string.h>

static id<MTLComputePipelineState> pipeline(id<MTLDevice> device, const char *source,
        const SatchelShaderInfo *info) {
    NSString *text = [[NSString alloc] initWithBytes:source length:info->source_bytes encoding:NSUTF8StringEncoding];
    if (!text) return nil;
    MTLCompileOptions *options = [MTLCompileOptions new];
    options.languageVersion = MTLLanguageVersion2_3;
    // Compiler errors can include source text. Return a stage code only.
    id<MTLLibrary> library = [device newLibraryWithSource:text options:options error:NULL];
    if (!library) return nil;
    id<MTLFunction> function = [library newFunctionWithName:[NSString stringWithUTF8String:info->entry_point]];
    return function ? [device newComputePipelineStateWithFunction:function error:NULL] : nil;
}
static bool execute(id<MTLCommandQueue> queue, id<MTLComputePipelineState> state,
        const SatchelShaderInfo *info, id<MTLBuffer> output) {
    uint64_t threads = (uint64_t)info->local_size[0] * info->local_size[1] * info->local_size[2];
    if (!threads || threads > state.maxTotalThreadsPerThreadgroup) return false;
    id<MTLCommandBuffer> command = [queue commandBuffer];
    id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
    if (!command || !encoder) return false;
    [encoder setComputePipelineState:state];
    if (output) [encoder setBuffer:output offset:0 atIndex:0];
    [encoder dispatchThreadgroups:MTLSizeMake(1, 1, 1)
           threadsPerThreadgroup:MTLSizeMake(info->local_size[0], info->local_size[1], info->local_size[2])];
    dispatch_semaphore_t completed = dispatch_semaphore_create(0);
    // The completion block owns its semaphore after a timeout. It never keeps a
    // pointer to the caller's stack or report; Metal retains submitted resources.
    [command addCompletedHandler:^(id<MTLCommandBuffer> finished) {
        (void)finished; dispatch_semaphore_signal(completed);
    }];
    [encoder endEncoding]; [command commit];
    if (dispatch_semaphore_wait(completed, dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC))) return false;
    return command.status == MTLCommandBufferStatusCompleted;
}
bool satchel_graphics_diagnostic(SatchelGraphicsDiagnostic *result) { @autoreleasepool {
    if (!result) return false;
    memset(result, 0, sizeof(*result));
    result->status = 1;
    result->dynamic_code_features = (uint32_t)satchel_shader_runtime_features();
    if (result->dynamic_code_features != 0) return false;
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    id<MTLCommandQueue> queue = [device newCommandQueue];
    result->status = 2;
    if (!device || !queue) return false;
    // Original resource-free Maxwell compute fixture. It terminates immediately;
    // it does not draw or stand in for any imported game's graphics.
    uint64_t maxwell[4] = {0, UINT64_C(0xe30000000007000f), 0, 0};
    uint8_t spirv[65536];
    int length = satchel_shader_probe((const uint8_t *)maxwell, sizeof(maxwell), spirv, sizeof(spirv));
    result->status = 3;
    if (length < 20 || length > (int)sizeof(spirv)) return false;
    result->maxwell_spirv_bytes = (uint32_t)length;
    NSMutableData *storage = [NSMutableData dataWithLength:SATCHEL_SHADER_MAX_MSL_BYTES];
    if (!storage) return false;
    char *source = storage.mutableBytes;
    SatchelShaderInfo info = {0};
    result->status = 4;
    if (satchel_shader_spirv_to_msl(spirv, (size_t)length, source, storage.length, &info) != SATCHEL_SHADER_OK ||
        info.stage != SATCHEL_SHADER_COMPUTE || info.binding_count > 1 ||
        info.local_size[0] != 1 || info.local_size[1] != 1 || info.local_size[2] != 1) return false;
    // The translator declares a support buffer even for EXIT. Preserve that
    // declaration in reflection while requiring no live resource access here.
    if (info.binding_count == 1 && (info.bindings[0].kind != SATCHEL_SHADER_BUFFER ||
        info.bindings[0].binding != 0 || info.bindings[0].active != 0)) return false;
    result->maxwell_msl_bytes = (uint32_t)info.source_bytes;
    id<MTLComputePipelineState> state = pipeline(device, source, &info);
    result->status = 5;
    if (!state) return false;
    result->status = 6;
    if (!execute(queue, state, &info, nil)) return false;
    result->maxwell_completed = true;

    // Separate original SPIR-V fixture validates a measurable GPU write. This
    // path starts at SPIR-V; do not describe its store as Maxwell translation.
    result->status = 7;
    if (satchel_shader_spirv_to_msl(satchel_shader_fixture, sizeof(satchel_shader_fixture), source, storage.length, &info) != SATCHEL_SHADER_OK ||
        info.stage != SATCHEL_SHADER_COMPUTE || info.binding_count != 1 ||
        info.bindings[0].kind != SATCHEL_SHADER_BUFFER || info.bindings[0].binding != 0 || !info.bindings[0].active ||
        info.local_size[0] != 1 || info.local_size[1] != 1 || info.local_size[2] != 1) return false;
    result->store_msl_bytes = (uint32_t)info.source_bytes;
    state = pipeline(device, source, &info);
    result->status = 8;
    if (!state) return false;
    id<MTLBuffer> output = [device newBufferWithLength:4 * sizeof(uint32_t) options:MTLResourceStorageModeShared];
    if (!output || !output.contents) return false;
    uint32_t *values = output.contents;
    for (unsigned i = 0; i < 4; i++) values[i] = 0xdeadbeef;
    result->status = 9;
    if (!execute(queue, state, &info, output)) return false;
    result->buffer_value_matched = values[0] == 0x13579bdf;
    result->guard_words_matched = values[1] == 0xdeadbeef && values[2] == 0xdeadbeef && values[3] == 0xdeadbeef;
    result->status = 10;
    if (!result->buffer_value_matched || !result->guard_words_matched) return false;
    result->status = 0; return true;
} }
