#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <assert.h>
static id<MTLDevice> g_dev;
static int t_lib_layer;
#define MLOG(...) ((void)0)
static id<MTLLibrary> load(NSString *path, NSError **err) {
#include "MacSourceShaders.inc"
    return nil;
}
int main(int argc, const char **argv) {
    @autoreleasepool {
        assert(argc == 2);
        g_dev = MTLCreateSystemDefaultDevice(); assert(g_dev);
        NSString *root = [@(argv[1]) stringByAppendingPathComponent:NSUUID.UUID.UUIDString];
        [NSFileManager.defaultManager createDirectoryAtPath:root withIntermediateDirectories:YES attributes:nil error:nil];
        NSString *base = [root stringByAppendingPathComponent:@"synthetic"];
        NSString *source = [base stringByAppendingPathExtension:@"metal"];
        NSString *library = [base stringByAppendingPathExtension:@"metallib"];
        NSError *error = nil;
        assert(!load(library, &error));
        [@"#include <metal_stdlib>\nusing namespace metal; kernel void testShader(uint i [[thread_position_in_grid]]) {}" writeToFile:source atomically:YES encoding:NSUTF8StringEncoding error:nil];
        id<MTLLibrary> result = load(library, &error);
        assert(result && [result newFunctionWithName:@"testShader"] && !t_lib_layer);
        [@"this is invalid synthetic Metal source" writeToFile:source atomically:YES encoding:NSUTF8StringEncoding error:nil];
        error = nil; assert(!load(library, &error));
        assert([error.domain isEqual:@"SatchelMacSourceShader"] && !error.userInfo.count);
        [NSFileManager.defaultManager removeItemAtPath:root error:nil];
        puts("PASS: absent source, synthetic Metal compile and payload-free compiler error");
    }
}
