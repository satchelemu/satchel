// Synthetic GPU regression for full depth clears with live color attachments.
// Uses the MIT NX2Apple engine through the same graphics overlay as the app.
#include "nvn_mtl.m"
void *front_metal_layer(void) { return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
module_t *g_mod; int g_nmod;
void svc_thread_dump(void) {}
uint64_t now_ns(void) { return bp_now(); }
static int checks, failed;
#define CHECK(c, label) do { checks++; if (!(c)) { failed++; fprintf(stderr, "FAIL: %s\n", label); } } while (0)

int main(void) { @autoreleasepool {
    mtl_init();
    NSError *error = nil;
    id<MTLLibrary> library = [g_dev newLibraryWithSource:@"#include <metal_stdlib>\nusing namespace metal;\n"
        "kernel void planes(texture2d<uint,access::read> s [[texture(0)]], depth2d<float,access::read> d [[texture(1)]], device uint2 *o [[buffer(0)]], uint2 p [[thread_position_in_grid]]) { o[p.y*d.get_width()+p.x]=uint2(s.read(p).r, as_type<uint>(d.read(p))); }\n" options:nil error:&error];
    id<MTLComputePipelineState> reader = [g_dev newComputePipelineStateWithFunction:[library newFunctionWithName:@"planes"] error:&error];
    CHECK(reader != nil, "synthetic plane reader compiles");
    if (!reader) return 1;
    id<MTLCommandQueue> queue = [g_dev newCommandQueue];
    struct { int depth, stencil, size; const char *label; } cases[] = {
        {1, 0, 16, "full depth clear preserves stencil and five colors"},
        {0, 255, 16, "full stencil clear preserves depth and five colors"},
        {1, 255, 16, "full combined clear preserves five colors"},
        {1, 255, 32, "larger depth target keeps the original split"},
    };
    for (unsigned i = 0; i < sizeof cases / sizeof *cases; i++) {
        unsigned size = cases[i].size;
        tex_t depth = {.magic=MAGIC('T','E','X',0), .serial=994100+i*8};
        depth.d = (nvn_texdesc){.target=NVN_TEXTURE_TARGET_2D, .format=NVN_FORMAT_DEPTH32F_STENCIL8, .width=size, .height=size, .depth=1, .levels=1, .samples=1};
        MTLTextureDescriptor *descriptor = tex_desc(&depth, 0); descriptor.usage |= MTLTextureUsagePixelFormatView;
        id<MTLTexture> base = [g_dev newTextureWithDescriptor:descriptor];
        tex_t colors[5] = {0}; id<MTLTexture> color[5];
        MTLRenderPassDescriptor *init = [MTLRenderPassDescriptor renderPassDescriptor];
        init.depthAttachment.texture = init.stencilAttachment.texture = base;
        init.depthAttachment.loadAction = init.stencilAttachment.loadAction = MTLLoadActionClear;
        init.depthAttachment.storeAction = init.stencilAttachment.storeAction = MTLStoreActionStore;
        init.depthAttachment.clearDepth = .75; init.stencilAttachment.clearStencil = 0xa5;
        for (int k = 0; k < 5; k++) {
            colors[k].magic = MAGIC('T','E','X',0); colors[k].serial = depth.serial+k+1;
            colors[k].d = (nvn_texdesc){.target=NVN_TEXTURE_TARGET_2D, .format=NVN_FORMAT_RGBA8, .width=16, .height=16, .depth=1, .levels=1, .samples=1};
            color[k] = [g_dev newTextureWithDescriptor:tex_desc(&colors[k], 0)];
            init.colorAttachments[k].texture = color[k];
            init.colorAttachments[k].loadAction = MTLLoadActionClear;
            init.colorAttachments[k].storeAction = MTLStoreActionStore;
            init.colorAttachments[k].clearColor = MTLClearColorMake(1, 0, k & 1, 1);
        }
        // Initialize the entire depth target independently of the smaller colors.
        MTLRenderPassDescriptor *planes = [MTLRenderPassDescriptor renderPassDescriptor];
        planes.depthAttachment = init.depthAttachment; planes.stencilAttachment = init.stencilAttachment;
        id<MTLCommandBuffer> cb = [queue commandBuffer];
        [[cb renderCommandEncoderWithDescriptor:planes] endEncoding];
        [[cb renderCommandEncoderWithDescriptor:init] endEncoding];
        [cb commit]; [cb waitUntilCompleted];
        CHECK(cb.status == MTLCommandBufferStatusCompleted, "literal initial attachments reach the GPU");
        res_add(base); mtl_tex_init(&depth); serial_put(depth.serial, base);
        for (int k = 0; k < 5; k++) { res_add(color[k]); mtl_tex_init(&colors[k]); serial_put(colors[k].serial, color[k]); }
        state_reset(); memset(&R, 0, sizeof R); mtl_sub_begin();
        R.has_rt=1; R.rt.depth=&depth; R.rt.n=5;
        for (int k = 0; k < 5; k++) R.rt.color[k]=&colors[k];
        R.has_sc=1; R.sc=(p_scissor){0,0,(int)size,(int)size};
        uint64_t passes = g_stat_pass;
        uint64_t skipped = g_bar_skip;
        CHECK(rp_begin() != nil, "preexisting pass with five colors opens");
        CHECK(g_bar_skip == skipped, "first encoder of a new command buffer keeps its consumer barrier");
        struct { op_hdr head; p_clear_ds clear; op_hdr end; } ops = {0};
        ops.head=(op_hdr){OP_CLEAR_DS, sizeof(op_hdr)+sizeof(p_clear_ds)};
        ops.clear=(p_clear_ds){.depth=.25, .depth_mask=cases[i].depth, .stencil=0x3c, .stencil_mask=cases[i].stencil};
        ops.end=(op_hdr){OP_END,sizeof(op_hdr)};
        mtl_exec((const uint8_t *)&ops);
        // Reopen the ordinary pass as the next game draw would. A flush alone
        // opens a depth-only clear pass, which does not match the five-color target.
        CHECK(rp_begin() != nil, "ordinary pass resumes after the clear");
        mtl_sub_commit(); mtl_drain();
        CHECK(!mtl_last_commit_error(), "production clear completes without GPU error");
        CHECK(g_stat_pass-passes == 2, "default clear preserves the existing render-pass split");
        CHECK(g_bar_skip == skipped, "default rendering preserves consumer barriers");
        id<MTLBuffer> result = [g_dev newBufferWithLength:size*size*8 options:MTLResourceStorageModeShared];
        id<MTLTexture> stencil = [base newTextureViewWithPixelFormat:MTLPixelFormatX32_Stencil8];
        cb=[queue commandBuffer]; id<MTLComputeCommandEncoder> e=[cb computeCommandEncoder];
        [e setComputePipelineState:reader]; [e setTexture:stencil atIndex:0]; [e setTexture:base atIndex:1]; [e setBuffer:result offset:0 atIndex:0];
        [e dispatchThreads:MTLSizeMake(size,size,1) threadsPerThreadgroup:MTLSizeMake(8,8,1)]; [e endEncoding];
        id<MTLBuffer> rgba = [g_dev newBufferWithLength:5*4096 options:MTLResourceStorageModeShared];
        id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
        for (int k = 0; k < 5; k++) [blit copyFromTexture:color[k] sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0) sourceSize:MTLSizeMake(16,16,1) toBuffer:rgba destinationOffset:k*4096 destinationBytesPerRow:256 destinationBytesPerImage:4096];
        [blit endEncoding]; [cb commit]; [cb waitUntilCompleted];
        CHECK(cb.status == MTLCommandBufferStatusCompleted, "all seven resulting planes read back");
        uint32_t *pixels=result.contents; int valid=1;
        for (unsigned k=0;k<size*size;k++) valid &= pixels[k*2] == (cases[i].stencil ? 0x3c : 0xa5) && pixels[k*2+1] == (cases[i].depth ? 0x3e800000 : 0x3f400000);
        CHECK(valid, cases[i].label);
        const uint8_t *bytes = rgba.contents; valid=1;
        for (int k=0;k<5;k++) for (int y=0;y<16;y++) for (int x=0;x<16;x++) {
            const uint8_t *p=bytes+k*4096+y*256+x*4;
            valid &= p[0]==255 && p[1]==0 && p[2]==(k&1 ? 255 : 0) && p[3]==255;
        }
        CHECK(valid, "every color byte is unchanged");
        printf("case %u: %s\n",i,cases[i].label);
        mtl_tex_release(&depth); for (int k=0;k<5;k++) mtl_tex_release(&colors[k]);
    }
    printf("gpu clear: %d checks, %d failed\n", checks, failed);
    return failed != 0;
} }
