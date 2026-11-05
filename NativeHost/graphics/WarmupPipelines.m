#import "WarmupPipelines.h"

// Descriptor construction follows MIT nxnative nvn_mtl.m pre_desc / pso_desc4.
// Copyright and license: NativeHost/THIRD_PARTY.md, nxnative notice shipped by the app.
@implementation SatchelWarmupPipelines {
    NSURL *_directory;
    uint64_t _reserve;
    id<MTLDevice> _device;
    id<MTL4Compiler> _compiler;
    id<MTL4PipelineDataSetSerializer> _serializer;
    NSMutableArray<id<MTL4Archive>> *_archives;
    NSUInteger _pending;
}
static BOOL failure(NSError **error, NSString *message) {
    if (error) *error = [NSError errorWithDomain:@"WarmupPipelines" code:1 userInfo:@{NSLocalizedDescriptionKey:message}];
    return NO;
}
- (BOOL)epoch:(NSError **)error {
    MTL4PipelineDataSetSerializerDescriptor *descriptor = [MTL4PipelineDataSetSerializerDescriptor new];
    descriptor.configuration = MTL4PipelineDataSetSerializerConfigurationCaptureBinaries;
    _serializer = [_device newPipelineDataSetSerializerWithDescriptor:descriptor];
    MTL4CompilerDescriptor *compiler = [MTL4CompilerDescriptor new]; compiler.pipelineDataSetSerializer = _serializer;
    _compiler = [_device newCompilerWithDescriptor:compiler error:error];
    return _serializer && _compiler;
}
- (instancetype)initWithDirectory:(NSURL *)directory reserveBytes:(uint64_t)reserve error:(NSError **)error {
    if (!(self = [super init])) return nil;
    _directory = directory; _reserve = reserve; _device = MTLCreateSystemDefaultDevice(); _archives = [NSMutableArray array];
    if (![[NSFileManager defaultManager] createDirectoryAtURL:directory withIntermediateDirectories:YES attributes:nil error:error]) return nil;
    for (NSURL *file in [[NSFileManager defaultManager] contentsOfDirectoryAtURL:directory includingPropertiesForKeys:@[NSURLIsSymbolicLinkKey] options:0 error:error]) {
        NSNumber *link; [file getResourceValue:&link forKey:NSURLIsSymbolicLinkKey error:nil];
        if (link.boolValue || ![file.pathExtension isEqual:@"mtl4a"]) continue;
        id<MTL4Archive> archive = [_device newArchiveWithURL:file error:nil]; if (archive) [_archives addObject:archive];
    }
    return [self epoch:error] ? self : nil;
}
static MTL4FunctionDescriptor *function(id<MTLLibrary> library, NSString *name) {
    if (!name) return nil;
    id<MTLFunction> f = [library newFunctionWithName:name]; if (!f) return nil;
    MTL4LibraryFunctionDescriptor *d = [MTL4LibraryFunctionDescriptor new]; d.library = library; d.name = name;
    if (!f.functionConstantsDictionary.count) return d;
    MTL4SpecializedFunctionDescriptor *s = [MTL4SpecializedFunctionDescriptor new];
    s.functionDescriptor = d; s.constantValues = [MTLFunctionConstantValues new]; return s;
}
- (NSInteger)warmVertex:(id<MTLLibrary>)vertex fragment:(id<MTLLibrary>)fragment
            vertexInfo:(NSDictionary *)vj fragmentInfo:(NSDictionary *)fj zero:(BOOL)zero coverage:(BOOL)coverage error:(NSError **)error {
    // The caller supplies the pulling library when reflection declares one.
    if (![vj[@"chain"] isEqual:@"26dc2fa9477c"] || ![fj[@"chain"] isEqual:@"26dc2fa9477c"]) return 0;
    if (![vj[@"pull"] isKindOfClass:NSDictionary.class] && [vj[@"vertex_inputs"] count]) return 0;
    MTL4FunctionDescriptor *vf = function(vertex, zero && vj[@"entry_z"] ? vj[@"entry_z"] : vj[@"entry"]);
    MTL4FunctionDescriptor *ff = function(fragment, fj[@"entry"] ?: @"main0");
    if (!vf || !ff) return 0;
    MTL4RenderPipelineDescriptor *d = [MTL4RenderPipelineDescriptor new];
    d.vertexFunctionDescriptor = vf; d.fragmentFunctionDescriptor = ff;
    d.vertexDescriptor = [MTLVertexDescriptor vertexDescriptor]; d.rasterSampleCount = 1;
    d.alphaToCoverageState = coverage ? MTL4AlphaToCoverageStateEnabled : MTL4AlphaToCoverageStateDisabled;
    for (NSUInteger i = 0; i < 8; i++) {
        MTL4RenderPipelineColorAttachmentDescriptor *a = d.colorAttachments[i];
        a.pixelFormat = MTLPixelFormatUnspecialized; a.writeMask = MTLColorWriteMaskUnspecialized; a.blendingState = MTL4BlendStateUnspecialized;
        a.sourceRGBBlendFactor = a.destinationRGBBlendFactor = a.sourceAlphaBlendFactor = a.destinationAlphaBlendFactor = MTLBlendFactorUnspecialized;
        a.rgbBlendOperation = a.alphaBlendOperation = MTLBlendOperationUnspecialized;
    }
    for (id<MTL4Archive> archive in _archives) if ([archive newRenderPipelineStateWithDescriptor:d error:nil]) return 1;
    if (![_compiler newRenderPipelineStateWithDescriptor:d compilerTaskOptions:nil error:error]) return -1;
    _pending++;
    return 2;
}
- (BOOL)flushWithError:(NSError **)error {
    if (!_pending) return YES;
    NSDictionary *space = [[NSFileManager defaultManager] attributesOfFileSystemForPath:_directory.path error:error];
    if ([space[NSFileSystemFreeSize] unsignedLongLongValue] < _reserve + 256ULL * 1024 * 1024) return failure(error, @"Not enough space to save warmed pipelines.");
    NSString *name = [@"pwarmup-" stringByAppendingString:NSUUID.UUID.UUIDString];
    NSURL *temporary = [_directory URLByAppendingPathComponent:[name stringByAppendingString:@".pending"]];
    NSURL *final = [_directory URLByAppendingPathComponent:[name stringByAppendingString:@".mtl4a"]];
    // No work is in flight during serialization; each batch owns a fresh epoch.
    BOOL ok = [_serializer serializeAsArchiveAndFlushToURL:temporary error:error];
    if (ok) ok = [[NSFileManager defaultManager] moveItemAtURL:temporary toURL:final error:error];
    if (!ok) { [[NSFileManager defaultManager] removeItemAtURL:temporary error:nil]; return NO; }
    id<MTL4Archive> archive = [_device newArchiveWithURL:final error:error]; if (!archive) return NO;
    [_archives addObject:archive]; _pending = 0;
    return [self epoch:error];
}
@end
