// Mac regression harness only. Use the existing offline source with its reflection.
// Match NativeShaders/ShaderCompiler.swift options; never log source or compiler text.
#if !TARGET_OS_IPHONE
    if (![NSFileManager.defaultManager fileExistsAtPath:path]) {
        NSString *sourcePath = [[path stringByDeletingPathExtension] stringByAppendingPathExtension:@"metal"];
        NSString *source = [NSString stringWithContentsOfFile:sourcePath encoding:NSUTF8StringEncoding error:nil];
        if (source) {
            MTLCompileOptions *options = [MTLCompileOptions new];
            options.languageVersion = MTLLanguageVersion3_2;
            options.mathMode = MTLMathModeRelaxed;
            options.preserveInvariance = YES;
            options.optimizationLevel = MTLLibraryOptimizationLevelDefault;
            options.libraryType = MTLLibraryTypeExecutable;
            NSError *failure = nil;
            id<MTLLibrary> library = [g_dev newLibraryWithSource:source options:options error:&failure];
            t_lib_layer = [source containsString:@"render_target_array_index"];
            MLOG("MacSourceShader result=%s code=%ld", library ? "compiled" : "failed", (long)failure.code);
            if (!library && err) *err = [NSError errorWithDomain:@"SatchelMacSourceShader" code:failure.code userInfo:nil];
            return library;
        }
    }
#endif
