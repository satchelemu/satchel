#import "PipelineSettings.h"
#import "../PlayerSettings.h"
#import "WarmupPipelines.h"
#import <Metal/Metal.h>
#include <sys/sysctl.h>
#include <math.h>

static NSString *argument(NSArray<NSString *> *args, NSString *name) {
    NSUInteger at = [args indexOfObject:name];
    return at != NSNotFound && at + 1 < args.count ? args[at + 1] : nil;
}

void SatchelApplyPipelineSettings(NSString *runtime) {
    // Package manifests can carry the old synchronous policy. The host owns compilation.
    NSArray<NSString *> *args = NSProcessInfo.processInfo.arguments;
    NSString *mode = SatchelCurrentPlayerSettings()[@"DM_PIPELINE_MODE"] ?: argument(args, @"-SatchelPipelineMode") ?: @"exact";
    BOOL skipping = [mode isEqualToString:@"skip"];
    BOOL asynchronous = skipping || [mode isEqualToString:@"async"];
    BOOL hybrid = asynchronous || [mode isEqualToString:@"hybrid"];
    BOOL modern = ![mode isEqualToString:@"legacy"];
    if (!asynchronous && ![mode isEqualToString:@"hybrid"] && ![mode isEqualToString:@"legacy"] && ![mode isEqualToString:@"exact"])
        fprintf(stderr, "Satchel graphics: unknown pipeline mode, using exact\n");
    setenv("DM_PSO_ASYNC", asynchronous ? "1" : "0", 1);
    setenv("DM_PSO_SKIP", skipping ? "1" : "0", 1);
    NSString *budget = argument(args, @"-SatchelPipelineBudgetMS") ?: @"4";
    NSScanner *scanner = [NSScanner scannerWithString:budget]; double milliseconds = 4;
    if (![scanner scanDouble:&milliseconds] || !scanner.isAtEnd || !isfinite(milliseconds) || milliseconds < 0 || milliseconds > 16) {
        fprintf(stderr, "Satchel graphics: invalid pipeline budget, using 4 ms\n");
        budget = @"4";
    }
    setenv("DM_PSO_BUDGET_MS", budget.UTF8String, 1);
    setenv("DM_PSO_MTL4", modern ? "1" : "0", 1);
    setenv("DM_PSO_HYBRID", hybrid ? "1" : "0", 1);
    setenv("DM_PSO_PREPARE_THREADS", "2", 1);
    setenv("DM_PSO_PREBUILD_THREADS", "2", 1);
    // Building every known pipeline variant before the first frame took about 14 minutes for
    // Odyssey's 20,740 on the iPhone (2026-11-02), so it stays off at launch. That bulk work
    // belongs in Prepare's background warm-up; -SatchelPipelinePrebuild YES turns it on for tests.
    BOOL prebuild = modern && [argument(args, @"-SatchelPipelinePrebuild") isEqualToString:@"YES"];
    // Bulk and recorded warm-up belong to the cancellable app job, never launch.
    setenv("DM_PSO_PREPARE", prebuild ? "1" : "0", 1);
    if (prebuild) unsetenv("DM_PSO_MANIFEST"); else setenv("DM_PSO_MANIFEST", "off", 1);
    setenv("DM_PSO_PREBUILD", prebuild ? "1" : "0", 1);
    setenv("DM_PSO_PREBUILD_A2C", prebuild ? "1" : "0", 1);

    // An explicit benchmark namespace starts cold without deleting the player's cache or saves.
    NSString *trial = argument(args, @"-SatchelPipelineTrial");
    NSCharacterSet *tag = [NSCharacterSet characterSetWithCharactersInString:@"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_"];
    NSString *cacheRoot = runtime;
    if (trial.length && trial.length <= 48 && [trial rangeOfCharacterFromSet:tag.invertedSet].location == NSNotFound) {
        NSString *candidate = [[runtime stringByAppendingPathComponent:@"GraphicsTrials"] stringByAppendingPathComponent:trial];
        NSError *error = nil;
        if ([NSFileManager.defaultManager createDirectoryAtPath:candidate withIntermediateDirectories:YES attributes:nil error:&error]) {
            cacheRoot = candidate;
            setenv("DM_PSO_USER", [candidate stringByAppendingPathComponent:@"pso_user.tsv"].fileSystemRepresentation, 1);
        } else fprintf(stderr, "Satchel graphics: trial directory failed: %s\n", error.localizedDescription.UTF8String);
    } else if (trial) fprintf(stderr, "Satchel graphics: invalid trial name ignored\n");
    unsetenv("DM_NVN_GPUPROF");
    unsetenv("DM_SATCHEL_FRAME_TIMING");
    if ([argument(args, @"-SatchelGraphicsProfile") isEqualToString:@"YES"]) {
        setenv("DM_SATCHEL_FRAME_TIMING", "1", 1);
        setenv("DM_SHADER_EVENTS", [[cacheRoot stringByAppendingPathComponent:@"shader-events.tsv"] fileSystemRepresentation], 1);
        setenv("DM_PSO_SKIP_STATS", [[cacheRoot stringByAppendingPathComponent:@"pipeline-skips.tsv"] fileSystemRepresentation], 1);
        setenv("DM_BOOTPROF", "1", 1);
        setenv("DM_SLOWFRAME", "1", 1);
        setenv("DM_PSO_WAITLOG", "1", 1);
        setenv("DM_NVN_WAITS", "1", 1);
        if ([argument(args, @"-SatchelGraphicsCounters") isEqualToString:@"NO"])
            unsetenv("DM_NVN_GPUPROF");
        else setenv("DM_NVN_GPUPROF", "every:300", 1);
        setenv("DM_NVN_TSV_CALLS", "0", 1);
        setenv("DM_NVN_TSV", [[cacheRoot stringByAppendingPathComponent:@"graphics-frames.tsv"] fileSystemRepresentation], 1);
    }

    if ([argument(args, @"-SatchelPauseProfile") isEqualToString:@"YES"]) {
        setenv("DM_HOST_PAUSE", "1", 1);
        setenv("DM_HITCH_DUMP", "100:40", 1);
        setenv("DM_THRCPU", [[[cacheRoot stringByAppendingPathComponent:@"thread-cpu.tsv"] stringByAppendingString:@",1"] fileSystemRepresentation], 1);
    }

    // Explicit A/B only. Keep pread as the default until device validation.
    BOOL mapped = [argument(args, @"-SatchelRomFSMapped") isEqualToString:@"YES"];
    setenv("DM_ROMFS_MAPPED", mapped ? "1" : "0", 1);
    if ([argument(args, @"-SatchelLoadingTrace") isEqualToString:@"YES"]) {
        setenv("DM_HOST_TRACE", [[cacheRoot stringByAppendingPathComponent:@"loading-trace"] fileSystemRepresentation], 1);
    }
    fprintf(stderr, "Satchel loading: romfs=%s\n", mapped ? "mapped" : "pread");

    setenv("DM_PSO_ARCHIVE", SatchelPipelineArchiveDirectory(cacheRoot).fileSystemRepresentation, 1);
    fprintf(stderr, "Satchel graphics: pipelines=%s workers=2 archive=%s\n", skipping ? "skip" : asynchronous ? "async" : hybrid ? "hybrid" : modern ? "exact" : "legacy", getenv("DM_PSO_ARCHIVE"));
}

NSString *SatchelPipelineArchiveDirectory(NSString *runtime) {
    // Archives are GPU and OS compiler specific. Keep old namespaces intact on upgrades.
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    NSString *gpu = device.architecture.name ?: @"unknown";
    char build[128] = {0}; size_t size = sizeof build;
    if (sysctlbyname("kern.osversion", build, &size, NULL, 0)) snprintf(build, sizeof build, "unknown");
    NSString *identity = [NSString stringWithFormat:@"f8a5618d-26dc2fa9477c-v3-%@-%s", gpu, build];
    NSCharacterSet *safe = [NSCharacterSet characterSetWithCharactersInString:@"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_"];
    identity = [[identity componentsSeparatedByCharactersInSet:safe.invertedSet] componentsJoinedByString:@"_"];
    NSString *archive = [[runtime stringByAppendingPathComponent:@"pipelines"] stringByAppendingPathComponent:identity];
    return archive;
}
