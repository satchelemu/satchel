#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <assert.h>
#include "PhoneShaderRuntime.h"
static NSString *requested;
int32_t satchel_phone_shader_prepare(const char *k, const void *c, size_t nc, const void *b, size_t nb, uint32_t w) { return 1; }
void *satchel_phone_shader_reflection(const char *name) {
    requested = @(name);
    return (__bridge_retained void *)@{@"name": requested};
}
int main(void) { @autoreleasepool {
    setenv("DM_PHONE_SHADERS", "1", 1);
    for (NSString *suffix in @[@"", @"_b", @"_bg0", @"_bg1", @"_bg2", @"_bg3", @"_bg4", @"_bgv", @"_bp", @"_bf"]) {
        NSString *name = [@"0123456789abcdef" stringByAppendingString:suffix];
        assert([satchel_phone_reflection(name)[@"name"] isEqual:name]);
        assert([requested isEqual:name]);
    }
    puts("PASS: Objective-C reflection forwards canonical and full variant names");
} }
