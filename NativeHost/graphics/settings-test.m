#import "../PlayerSettings.h"
#import "PipelineSettings.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

int main(int argc, const char **argv) { @autoreleasepool {
    assert(argc >= 3);
    NSString *title = @"pipeline-mode-fixture";
    assert(SatchelSavePlayerSettings(title, @{@"DM_SCREEN_MODE": @"fit", @"DM_PIPELINE_MODE": @(argv[1])}));
    SatchelApplyPlayerSettings(title);
    SatchelApplyPipelineSettings(@(argv[2]));
    assert(!strcmp(getenv("DM_PSO_ASYNC"), getenv("EXPECT_ASYNC")));
    assert(!strcmp(getenv("DM_PSO_SKIP"), getenv("EXPECT_SKIP")));
    assert(!strcmp(getenv("DM_PSO_BUDGET_MS"), getenv("EXPECT_BUDGET")));
    // A launch override must not change the saved per-title choice.
    assert([SatchelSavedPlayerSettings(title)[@"DM_PIPELINE_MODE"] isEqual:@(argv[1])]);
    printf("PASS: pipeline launch settings and override persistence\n");
    return 0;
} }
