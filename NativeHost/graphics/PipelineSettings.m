#import "PipelineSettings.h"
#import "../PlayerSettings.h"
#import "WarmupPipelines.h"
#import <Metal/Metal.h>
#include <sys/sysctl.h>
#include <math.h>
#include <pthread.h>
#include <time.h>

static NSString *argument(NSArray<NSString *> *args, NSString *name) {
    NSUInteger at = [args indexOfObject:name];
    return at != NSNotFound && at + 1 < args.count ? args[at + 1] : nil;
}

void SatchelApplyPipelineSettings(NSString *runtime) {
    // Package manifests can carry the old synchronous policy. The host owns compilation.
    NSArray<NSString *> *args = NSProcessInfo.processInfo.arguments;
    NSString *mode = SatchelCurrentPlayerSettings()[@"DM_PIPELINE_MODE"] ?: argument(args, @"-SatchelPipelineMode") ?: @"exact";
    BOOL skipping = [mode isEqualToString:@"skip"];
    // Bounded: async selection, plus translation and base libraries started as programs are bound.
    BOOL bounded = [mode isEqualToString:@"bounded"];
    BOOL asynchronous = skipping || bounded || [mode isEqualToString:@"async"];
    BOOL hybrid = asynchronous || [mode isEqualToString:@"hybrid"];
    BOOL modern = ![mode isEqualToString:@"legacy"];
    if (!asynchronous && ![mode isEqualToString:@"hybrid"] && ![mode isEqualToString:@"legacy"] && ![mode isEqualToString:@"exact"])
        fprintf(stderr, "Satchel graphics: unknown pipeline mode, using exact\n");
    setenv("DM_PSO_ASYNC", asynchronous ? "1" : "0", 1);
    setenv("DM_PSO_SKIP", skipping ? "1" : "0", 1);
    setenv("DM_PSO_AHEAD", bounded ? "1" : "0", 1);
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
    // Opt-in launch prewarm (-SatchelPipelinePrewarm YES): the pipelines this device recorded
    // (pso_user.tsv) and the package seed are built before the game's first submission, with
    // phone-translated shaders prepared from their own capture, for at most
    // -SatchelPipelinePrewarmMS (default 3000, 250 to 30000) of loading. Nothing of the
    // pair-list bulk prebuild runs. Bulk warm-up stays in the cancellable app job.
    BOOL prewarm = modern && !prebuild && [argument(args, @"-SatchelPipelinePrewarm") isEqualToString:@"YES"];
    NSString *prewarmBudget = argument(args, @"-SatchelPipelinePrewarmMS") ?: @"3000";
    NSScanner *prewarmScanner = [NSScanner scannerWithString:prewarmBudget]; NSInteger prewarmMS = 3000;
    if (![prewarmScanner scanInteger:&prewarmMS] || !prewarmScanner.isAtEnd || prewarmMS < 250 || prewarmMS > 30000) {
        if (prewarm) fprintf(stderr, "Satchel graphics: invalid prewarm budget, using 3000 ms\n");
        prewarmMS = 3000;
    }
    setenv("DM_PSO_PREPARE", prebuild || prewarm ? "1" : "0", 1);
    if (prebuild || prewarm) unsetenv("DM_PSO_MANIFEST"); else setenv("DM_PSO_MANIFEST", "off", 1);
    setenv("DM_PSO_PREBUILD", prebuild ? "1" : "0", 1);
    setenv("DM_PSO_PREBUILD_A2C", prebuild ? "1" : "0", 1);
    setenv("DM_SATCHEL_PREWARM", prewarm ? "1" : "0", 1);
    if (prewarm) setenv("DM_PSO_PREPARE_MS", [NSString stringWithFormat:@"%ld", (long)prewarmMS].UTF8String, 1);
    else unsetenv("DM_PSO_PREPARE_MS");

    // An explicit benchmark namespace starts cold without deleting the player's cache or saves.
    NSString *trial = argument(args, @"-SatchelPipelineTrial");
    NSCharacterSet *tag = [NSCharacterSet characterSetWithCharactersInString:@"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_"];
    NSString *cacheRoot = runtime;
    if (trial.length && trial.length <= 48 && [trial rangeOfCharacterFromSet:tag.invertedSet].location == NSNotFound) {
        NSString *candidate = [[runtime stringByAppendingPathComponent:@"GraphicsTrials"] stringByAppendingPathComponent:trial];
        NSError *error = nil;
        if ([NSFileManager.defaultManager createDirectoryAtPath:candidate withIntermediateDirectories:YES attributes:nil error:&error]) {
            cacheRoot = candidate;
            NSString *list = [candidate stringByAppendingPathComponent:@"pso_user.tsv"];
            // -SatchelPipelineTrialListFrom <tag>: a new trial starts from another trial's recorded
            // pipelines with its own fresh archive (the case of an OS update or a cache namespace bump).
            NSString *from = argument(args, @"-SatchelPipelineTrialListFrom");
            if (from.length && from.length <= 48 && [from rangeOfCharacterFromSet:tag.invertedSet].location == NSNotFound && ![from isEqualToString:trial]) {
                NSString *source = [[[runtime stringByAppendingPathComponent:@"GraphicsTrials"] stringByAppendingPathComponent:from] stringByAppendingPathComponent:@"pso_user.tsv"];
                if (![NSFileManager.defaultManager fileExistsAtPath:list] && ![NSFileManager.defaultManager copyItemAtPath:source toPath:list error:&error])
                    fprintf(stderr, "Satchel graphics: trial list copy failed: %s\n", error.localizedDescription.UTF8String);
            } else if (from) fprintf(stderr, "Satchel graphics: invalid trial list source ignored\n");
            setenv("DM_PSO_USER", list.fileSystemRepresentation, 1);
        } else fprintf(stderr, "Satchel graphics: trial directory failed: %s\n", error.localizedDescription.UTF8String);
    } else if (trial) fprintf(stderr, "Satchel graphics: invalid trial name ignored\n");
    unsetenv("DM_NVN_GPUPROF");
    unsetenv("DM_SATCHEL_FRAME_TIMING");
    if ([argument(args, @"-SatchelGraphicsProfile") isEqualToString:@"YES"]) {
        setenv("DM_SATCHEL_FRAME_TIMING", "1", 1);
        // The engine rewrites graphics-frames.tsv at each launch; the event writers append. Start
        // both event files fresh too, so a warm repeat under the same trial tag never mixes launches,
        // and mark the launch (stall_attribution.py uses the events after the last mark).
        NSString *events = [cacheRoot stringByAppendingPathComponent:@"shader-events.tsv"];
        NSString *skips = [cacheRoot stringByAppendingPathComponent:@"pipeline-skips.tsv"];
        [NSFileManager.defaultManager removeItemAtPath:skips error:nil];
        struct timespec now; clock_gettime(CLOCK_MONOTONIC, &now); uint64_t thread = 0; pthread_threadid_np(NULL, &thread);
        NSString *mark = [NSString stringWithFormat:@"%llu\t0\t%llu\tlaunch\n", (unsigned long long)now.tv_sec * 1000000000ull + (unsigned long long)now.tv_nsec, (unsigned long long)thread];
        if (![[mark dataUsingEncoding:NSUTF8StringEncoding] writeToFile:events options:NSDataWritingAtomic error:nil])
            fprintf(stderr, "Satchel graphics: shader event file could not be reset\n");
        setenv("DM_SHADER_EVENTS", events.fileSystemRepresentation, 1);
        setenv("DM_PSO_SKIP_STATS", skips.fileSystemRepresentation, 1);
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
    fprintf(stderr, "Satchel graphics: pipelines=%s workers=2 archive=%s\n", skipping ? "skip" : bounded ? "bounded" : asynchronous ? "async" : hybrid ? "hybrid" : modern ? "exact" : "legacy", getenv("DM_PSO_ARCHIVE"));
    if (prewarm) fprintf(stderr, "Satchel graphics: launch prewarm of recorded pipelines, at most %ld ms before the first frame\n", (long)prewarmMS);
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
