#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

NS_ASSUME_NONNULL_BEGIN
// A separate compiler context. Never initializes the engine or changes its environment.
@interface SatchelWarmupPipelines : NSObject
- (nullable instancetype)initWithDirectory:(NSURL *)directory reserveBytes:(uint64_t)reserve error:(NSError **)error;
- (NSInteger)warmVertex:(id<MTLLibrary>)vertex fragment:(id<MTLLibrary>)fragment
            vertexInfo:(NSDictionary *)vertexInfo fragmentInfo:(NSDictionary *)fragmentInfo
                  zero:(BOOL)zero coverage:(BOOL)coverage error:(NSError **)error;
- (BOOL)flushWithError:(NSError **)error;
@end
FOUNDATION_EXPORT NSString *SatchelPipelineArchiveDirectory(NSString *runtime);
NS_ASSUME_NONNULL_END
