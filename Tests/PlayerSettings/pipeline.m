#import "PlayerSettings.h"
#import "graphics/PipelineSettings.h"
#include <assert.h>
#include <string.h>

int main(void) {
    @autoreleasepool {
        NSString *title = @"PIPELINE-FIXTURE";
        NSString *runtime = @"/synthetic-runtime";
        for (NSString *mode in @[@"exact", @"async", @"skip", @"hybrid", @"legacy"]) {
            assert(SatchelSavePlayerSettings(title, @{@"DM_PIPELINE_MODE": mode}));
            SatchelApplyPlayerSettings(title);
            SatchelApplyPipelineSettings(runtime);
            assert(!strcmp(getenv("DM_PSO_MTL4"), [mode isEqual:@"legacy"] ? "0" : "1"));
            BOOL asynchronous = [mode isEqual:@"async"] || [mode isEqual:@"skip"];
            assert(!strcmp(getenv("DM_PSO_HYBRID"), asynchronous || [mode isEqual:@"hybrid"] ? "1" : "0"));
            assert(!strcmp(getenv("DM_PSO_ASYNC"), asynchronous ? "1" : "0"));
            assert(!strcmp(getenv("DM_PSO_SKIP"), [mode isEqual:@"skip"] ? "1" : "0"));
            assert([SatchelSavedPlayerSettings(@"PIPELINE-OTHER")[ @"DM_PIPELINE_MODE"] isEqual:@"exact"]);
            assert(SatchelChangeScreenMode(@"stretch"));
            assert(SatchelChangePlayerHaptics(NO));
            assert([SatchelSavedPlayerSettings(title)[@"DM_PIPELINE_MODE"] isEqual:mode]);
            assert(!strcmp(getenv("DM_PSO_PREBUILD"), "0"));
            assert(!strcmp(getenv("DM_PSO_PREPARE_THREADS"), "2"));
            assert(SatchelQueuePlayerSetting(@"DM_PIPELINE_MODE", @"exact"));
            assert([SatchelCurrentPlayerSettings()[@"DM_PIPELINE_MODE"] isEqual:mode]);
            assert([SatchelNextPlayerSettings()[@"DM_PIPELINE_MODE"] isEqual:@"exact"]);
        }
        assert(SatchelResetPlayerSetting(title, @"DM_PIPELINE_MODE"));
        SatchelClearPlayerSession();
        puts("PASS: exact, async, skip, hybrid and legacy reach the pipeline launch policy; queued changes wait for launch.");
    }
}
