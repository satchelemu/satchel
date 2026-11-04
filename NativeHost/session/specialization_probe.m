#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include <pthread.h>
static uint64_t satchel_shader_now(void) { return 0; }
static void satchel_shader_event(const char *label, uint64_t start) { (void)label; (void)start; }
#define SATCHEL_SHADER_TIME(label, expression) (expression)
#include "ShaderBuildQueue.inc"
int main(int argc, char **argv) {
    unsigned cycles = argc > 1 ? (unsigned)atoi(argv[1]) : 1;
    __weak id weakBase, weakCompiler, weakLibrary;
    __block __weak id weakA, weakB;
    for (unsigned cycle = 0; cycle < cycles; cycle++) { @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        NSError *error = nil;
        NSString *source = @"#include <metal_stdlib>\nusing namespace metal;\nvertex float4 vert(uint id [[vertex_id]]) { return float4(float(id),0,0,1); }\nfragment float4 frag() { return float4(1,0,0,1); }\n";
        id<MTLLibrary> library = [device newLibraryWithSource:source options:nil error:&error];
        assert(library); weakLibrary = library;
        id<MTL4Compiler> compiler = [device newCompilerWithDescriptor:[MTL4CompilerDescriptor new] error:&error];
        assert(compiler); weakCompiler = compiler;
        MTL4LibraryFunctionDescriptor *v = [MTL4LibraryFunctionDescriptor new]; v.library = library; v.name = @"vert";
        MTL4LibraryFunctionDescriptor *f = [MTL4LibraryFunctionDescriptor new]; f.library = library; f.name = @"frag";
        MTL4RenderPipelineDescriptor *d = [MTL4RenderPipelineDescriptor new];
        d.vertexFunctionDescriptor = v; d.fragmentFunctionDescriptor = f;
        for (unsigned i = 0; i < 8; i++) {
            MTL4RenderPipelineColorAttachmentDescriptor *a = d.colorAttachments[i];
            a.pixelFormat = MTLPixelFormatUnspecialized; a.writeMask = MTLColorWriteMaskUnspecialized; a.blendingState = MTL4BlendStateUnspecialized;
            a.sourceRGBBlendFactor = a.destinationRGBBlendFactor = a.sourceAlphaBlendFactor = a.destinationAlphaBlendFactor = MTLBlendFactorUnspecialized;
            a.rgbBlendOperation = a.alphaBlendOperation = MTLBlendOperationUnspecialized;
        }
        id<MTLRenderPipelineState> base = [compiler newRenderPipelineStateWithDescriptor:d compilerTaskOptions:nil error:&error];
        assert(base); weakBase = base;
        MTL4RenderPipelineDescriptor *s = (MTL4RenderPipelineDescriptor *)[base newRenderPipelineDescriptorForSpecialization];
        for (unsigned i = 0; i < 8; i++) {
            MTL4RenderPipelineColorAttachmentDescriptor *a = s.colorAttachments[i];
            a.pixelFormat = i ? MTLPixelFormatInvalid : MTLPixelFormatBGRA8Unorm;
            a.writeMask = MTLColorWriteMaskAll; a.blendingState = MTL4BlendStateDisabled;
            a.sourceRGBBlendFactor = a.sourceAlphaBlendFactor = MTLBlendFactorOne;
            a.destinationRGBBlendFactor = a.destinationAlphaBlendFactor = MTLBlendFactorZero;
            a.rgbBlendOperation = a.alphaBlendOperation = MTLBlendOperationAdd;
        }
        dispatch_apply(2, dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^(size_t i) { @autoreleasepool {
        MTL4RenderPipelineDescriptor *copy = [s copy];
        NSError *taskError = nil;
        id<MTLRenderPipelineState> specialized = satchel_render_specialize(compiler, copy, base, &taskError);
        assert(specialized); if (i) weakB = specialized; else weakA = specialized;
        } });
    } }
    printf("synthetic cycles=%u base_alive=%d specialized_alive=%d compiler_alive=%d library_alive=%d\n", cycles, weakBase != nil, (weakA != nil || weakB != nil), weakCompiler != nil, weakLibrary != nil);
    fflush(stdout);
    sleep(40);
    return 0;
}
