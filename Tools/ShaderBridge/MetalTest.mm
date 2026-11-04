#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include "ShaderBridge.h"
#include "Fixture.h"
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>
static bool dispatch(id<MTLDevice> device, const char *source, const SatchelShaderInfo &info, id<MTLBuffer> output) {
    MTLCompileOptions *options=[MTLCompileOptions new];
    options.languageVersion=MTLLanguageVersion2_3;
    NSString *text=[[NSString alloc] initWithBytes:source length:info.source_bytes encoding:NSUTF8StringEncoding];
    id<MTLLibrary> library=[device newLibraryWithSource:text options:options error:NULL];
    if (!library) return false;
    id<MTLFunction> function=[library newFunctionWithName:[NSString stringWithUTF8String:info.entry_point]];
    if (!function) return false;
    id<MTLComputePipelineState> pipeline=[device newComputePipelineStateWithFunction:function error:NULL];
    if (!pipeline || pipeline.maxTotalThreadsPerThreadgroup<1) return false;
    id<MTLCommandQueue> queue=[device newCommandQueue];
    id<MTLCommandBuffer> command=[queue commandBuffer];
    id<MTLComputeCommandEncoder> encoder=[command computeCommandEncoder];
    if (!queue || !command || !encoder) return false;
    [encoder setComputePipelineState:pipeline];
    if (output) [encoder setBuffer:output offset:0 atIndex:0];
    [encoder dispatchThreadgroups:MTLSizeMake(1,1,1) threadsPerThreadgroup:MTLSizeMake(1,1,1)];
    [encoder endEncoding];
    dispatch_semaphore_t completed=dispatch_semaphore_create(0);
    [command addCompletedHandler:^(id<MTLCommandBuffer> unused) { (void)unused; dispatch_semaphore_signal(completed); }];
    [command commit];
    if (dispatch_semaphore_wait(completed,dispatch_time(DISPATCH_TIME_NOW,10*NSEC_PER_SEC))!=0) return false;
    return command.status==MTLCommandBufferStatusCompleted;
}
int main(int argc, char **argv) { @autoreleasepool {
    std::vector<char> source(SATCHEL_SHADER_MAX_MSL_BYTES);
    SatchelShaderInfo info = {};
    if (satchel_shader_spirv_to_msl(satchel_shader_fixture,sizeof(satchel_shader_fixture),source.data(),source.size(),&info)!=SATCHEL_SHADER_OK ||
        info.binding_count!=1 || info.bindings[0].active!=1) return 1;
    id<MTLDevice> device=MTLCreateSystemDefaultDevice();
    if (!device) { std::fputs("Metal device unavailable; execution not validated.\n",stderr); return 2; }
    id<MTLBuffer> output=[device newBufferWithLength:sizeof(uint32_t)*4 options:MTLResourceStorageModeShared];
    if (!output || !output.contents) return 3;
    auto *values=static_cast<uint32_t *>(output.contents);
    for (int i=0;i<4;++i) values[i]=0xdeadbeef;
    if (!dispatch(device,source.data(),info,output) || values[0]!=0x13579bdf ||
        values[1]!=0xdeadbeef || values[2]!=0xdeadbeef || values[3]!=0xdeadbeef) return 4;
    std::puts("Original SPIR-V -> iOS MSL -> native Mac Metal dispatch: expected uint and three untouched guard words verified.");
    if (argc==2) {
        std::ifstream file(argv[1],std::ios::binary);
        std::vector<char> exitSpv((std::istreambuf_iterator<char>(file)),{});
        if (exitSpv.empty() || exitSpv.size()>SATCHEL_SHADER_MAX_SPIRV_BYTES ||
            satchel_shader_spirv_to_msl(exitSpv.data(),exitSpv.size(),source.data(),source.size(),&info)!=SATCHEL_SHADER_OK ||
            info.stage!=SATCHEL_SHADER_COMPUTE || info.binding_count!=1 || info.bindings[0].active!=0 ||
            info.local_size[0]!=1 || info.local_size[1]!=1 || info.local_size[2]!=1) return 5;
        if (!dispatch(device,source.data(),info,nil)) return 6;
        std::puts("Original Maxwell EXIT -> SPIR-V -> iOS MSL -> native Mac Metal dispatch completed with inactive declared buffer and no active bindings (no writes/pixels claimed).");
    }
    return 0;
} }
