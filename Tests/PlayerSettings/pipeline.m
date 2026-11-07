#import "PlayerSettings.h"
#import "graphics/PipelineSettings.h"
#include <assert.h>
#include <string.h>

static NSString *read_text(NSString *path) { return [NSString stringWithContentsOfFile:path encoding:NSUTF8StringEncoding error:nil]; }

// run.py passes launch arguments and a private runtime folder: prewarm policy, trial list copy, fresh event files.
static int launch_checks(NSArray<NSString *> *args) {
    NSString *runtime = NSProcessInfo.processInfo.environment[@"PIPELINE_RUNTIME"];
    NSString *expect = NSProcessInfo.processInfo.environment[@"EXPECT_PREWARM_MS"];
    NSFileManager *fm = NSFileManager.defaultManager;
    NSString *trial = [runtime stringByAppendingPathComponent:@"GraphicsTrials/check-b"];
    NSString *source = [runtime stringByAppendingPathComponent:@"GraphicsTrials/check-a"];
    assert([fm createDirectoryAtPath:trial withIntermediateDirectories:YES attributes:nil error:nil]);
    assert([fm createDirectoryAtPath:source withIntermediateDirectories:YES attributes:nil error:nil]);
    assert([@"S\tsynthetic\t0\n" writeToFile:[source stringByAppendingPathComponent:@"pso_user.tsv"] atomically:YES encoding:NSUTF8StringEncoding error:nil]);
    // A previous launch under the same tag left appended event rows behind.
    assert([@"1\t1\t1\tframe\n2\t1\t1\tframe\n" writeToFile:[trial stringByAppendingPathComponent:@"shader-events.tsv"] atomically:YES encoding:NSUTF8StringEncoding error:nil]);
    assert([@"0\t0\n1\t0\n" writeToFile:[trial stringByAppendingPathComponent:@"pipeline-skips.tsv"] atomically:YES encoding:NSUTF8StringEncoding error:nil]);
    SatchelApplyPipelineSettings(runtime);
    BOOL prewarm = expect.length > 0;
    assert(!strcmp(getenv("DM_SATCHEL_PREWARM"), prewarm ? "1" : "0"));
    assert(!strcmp(getenv("DM_PSO_PREPARE"), prewarm ? "1" : "0"));
    assert(!strcmp(getenv("DM_PSO_PREBUILD"), "0"));
    if (prewarm) { assert(!getenv("DM_PSO_MANIFEST")); assert(!strcmp(getenv("DM_PSO_PREPARE_MS"), expect.UTF8String)); }
    else { assert(!strcmp(getenv("DM_PSO_MANIFEST"), "off")); assert(!getenv("DM_PSO_PREPARE_MS")); }
    assert(!strcmp(getenv("DM_PSO_USER"), [trial stringByAppendingPathComponent:@"pso_user.tsv"].fileSystemRepresentation));
    if ([args containsObject:@"-SatchelPipelineTrialListFrom"])
        assert([read_text(getenv("DM_PSO_USER") ? @(getenv("DM_PSO_USER")) : @"") isEqualToString:@"S\tsynthetic\t0\n"]);
    NSString *events = read_text([trial stringByAppendingPathComponent:@"shader-events.tsv"]);
    NSArray<NSString *> *rows = [events componentsSeparatedByString:@"\n"];
    assert(rows.count == 2 && [rows[0] hasSuffix:@"\tlaunch"] && [rows[0] componentsSeparatedByString:@"\t"].count == 4 && !rows[1].length);
    assert(![fm fileExistsAtPath:[trial stringByAppendingPathComponent:@"pipeline-skips.tsv"]]);
    assert(!strcmp(getenv("DM_SHADER_EVENTS"), [trial stringByAppendingPathComponent:@"shader-events.tsv"].fileSystemRepresentation));
    NSString *lanes = NSProcessInfo.processInfo.environment[@"EXPECT_LANES"], *ahead = NSProcessInfo.processInfo.environment[@"EXPECT_AHEAD"];
    if (lanes.length) assert(getenv("DM_PSO_LANES") && !strcmp(getenv("DM_PSO_LANES"), lanes.UTF8String)); else assert(!getenv("DM_PSO_LANES"));
    if (ahead.length) assert(getenv("DM_PSO_SPECULATIVE") && !strcmp(getenv("DM_PSO_SPECULATIVE"), ahead.UTF8String)); else assert(!getenv("DM_PSO_SPECULATIVE"));
    puts(prewarm ? "PASS: launch prewarm policy, trial list copy and fresh event files" : "PASS: no launch prewarm by default; trial event files start fresh");
    return 0;
}

int main(void) {
    @autoreleasepool {
        NSArray<NSString *> *args = NSProcessInfo.processInfo.arguments;
        if ([args containsObject:@"-SatchelPipelineTrial"]) return launch_checks(args);
        NSString *title = @"PIPELINE-FIXTURE";
        NSString *runtime = @"/synthetic-runtime";
        for (NSString *mode in @[@"exact", @"async", @"bounded", @"skip", @"hybrid", @"legacy"]) {
            assert(SatchelSavePlayerSettings(title, @{@"DM_PIPELINE_MODE": mode}));
            SatchelApplyPlayerSettings(title);
            SatchelApplyPipelineSettings(runtime);
            assert(!strcmp(getenv("DM_PSO_MTL4"), [mode isEqual:@"legacy"] ? "0" : "1"));
            BOOL asynchronous = [mode isEqual:@"async"] || [mode isEqual:@"bounded"] || [mode isEqual:@"skip"];
            assert(!strcmp(getenv("DM_PSO_HYBRID"), asynchronous || [mode isEqual:@"hybrid"] ? "1" : "0"));
            assert(!strcmp(getenv("DM_PSO_ASYNC"), asynchronous ? "1" : "0"));
            assert(!strcmp(getenv("DM_PSO_SKIP"), [mode isEqual:@"skip"] ? "1" : "0"));
            assert(!strcmp(getenv("DM_PSO_AHEAD"), [mode isEqual:@"bounded"] ? "1" : "0"));
            assert([SatchelSavedPlayerSettings(@"PIPELINE-OTHER")[ @"DM_PIPELINE_MODE"] isEqual:@"exact"]);
            assert(SatchelChangeScreenMode(@"stretch"));
            assert(SatchelChangePlayerHaptics(NO));
            assert([SatchelSavedPlayerSettings(title)[@"DM_PIPELINE_MODE"] isEqual:mode]);
            assert(!strcmp(getenv("DM_PSO_PREBUILD"), "0"));
            assert(!getenv("DM_PSO_LANES") && !getenv("DM_PSO_SPECULATIVE"));
            assert(!strcmp(getenv("DM_PSO_PREPARE_THREADS"), "2"));
            assert(SatchelQueuePlayerSetting(@"DM_PIPELINE_MODE", @"exact"));
            assert([SatchelCurrentPlayerSettings()[@"DM_PIPELINE_MODE"] isEqual:mode]);
            assert([SatchelNextPlayerSettings()[@"DM_PIPELINE_MODE"] isEqual:@"exact"]);
        }
        assert(SatchelResetPlayerSetting(title, @"DM_PIPELINE_MODE"));
        SatchelClearPlayerSession();
        puts("PASS: exact, async, bounded, skip, hybrid and legacy reach the pipeline launch policy; queued changes wait for launch.");
    }
}
