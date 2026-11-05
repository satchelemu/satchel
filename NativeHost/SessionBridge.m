#import "graphics/PipelineSettings.h"
#import "SessionBridge.h"
#import "PlayerSettings.h"
#import "InGameMenu.h"
#import <Metal/Metal.h>
#include <dlfcn.h>
#include <mach-o/loader.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "loader_image.h"
#include "front.h"
#ifdef SATCHEL_MANAGED_SESSION
#include "session/SessionBoundary.h"
#endif
#include <errno.h>
#include <mach/mach.h>
#import <AVFAudio/AVFAudio.h>

static NSString *g_package;
static void *g_image_handle;
static BOOL g_prepared, g_started, g_menuPaused, g_stopping, g_ending;
#include "session/SessionSaveAccess.inc"
NSNotificationName const SatchelNativeSessionDidEnd = @"SatchelNativeSessionDidEnd";
NSNotificationName const SatchelNativeSessionDidFail = @"SatchelNativeSessionDidFail";
static dispatch_queue_t session_queue(void) {
    static dispatch_queue_t queue; static dispatch_once_t once;
    dispatch_once(&once, ^{ queue = dispatch_queue_create("satchel.session", DISPATCH_QUEUE_SERIAL); });
    return queue;
}
static char *g_argv[11];
static int g_argc;
static NSDictionary<NSString *, NSString *> *g_environment;
static NSString *g_previousDirectory;
static int g_originalStderr = -1;
extern char **environ;
static NSDictionary *session_environment(void) {
    NSMutableDictionary *values = [NSMutableDictionary dictionary];
    for (char **entry = environ; entry && *entry; entry++) {
        const char *equal = strchr(*entry, '='); if (!equal) continue;
        NSString *key = [[NSString alloc] initWithBytes:*entry length:(size_t)(equal - *entry) encoding:NSUTF8StringEncoding];
        if ([key hasPrefix:@"DM_"] || [key isEqualToString:@"TZ"]) {
            NSString *value = @(equal + 1); if (key && value) values[key] = value;
        }
    }
    return values;
}
static void restore_session_environment(void) {
    if (g_environment) {
        for (NSString *key in session_environment()) unsetenv(key.UTF8String);
        for (NSString *key in g_environment) setenv(key.UTF8String, g_environment[key].UTF8String, 1);
        g_environment = nil; tzset();
    }
    if (g_previousDirectory) { chdir(g_previousDirectory.fileSystemRepresentation); g_previousDirectory = nil; }
    if (g_originalStderr >= 0) { fflush(stderr); dup2(g_originalStderr, STDERR_FILENO); close(g_originalStderr); g_originalStderr = -1; }
}
int dm_main(int argc, char **argv);
UIViewController *front_ios_root(void);
void front_ios_focus_event(int event);
void front_ios_audio_session(void);
void front_ios_boot(void);
void dm_cloud_saves_env(void);

static BOOL fail(NSError **error, NSString *message) {
    if (error) *error = [NSError errorWithDomain:@"SatchelNativeHost" code:1 userInfo:@{NSLocalizedDescriptionKey:message}];
    return NO;
}

// Inspect before dlopen: a package must not execute initializers as dyld loads it.
static BOOL check_image(NSString *path, NSError **error) {
    NSData *data = [NSData dataWithContentsOfFile:path options:NSDataReadingMappedIfSafe error:error];
    if (!data) return NO;
    if (data.length < sizeof(struct mach_header_64)) return fail(error, @"Game dylib header is truncated.");
    const struct mach_header_64 *mh = data.bytes;
    if (mh->magic != MH_MAGIC_64 || mh->cputype != CPU_TYPE_ARM64 || mh->filetype != MH_DYLIB ||
        mh->ncmds > 4096 || mh->sizeofcmds > data.length - sizeof(*mh)) return fail(error, @"Unsupported game dylib header.");
    const uint8_t *p = (const uint8_t *)(mh + 1), *end = p + mh->sizeofcmds;
    for (uint32_t i = 0; i < mh->ncmds; i++) {
        if (end - p < sizeof(struct load_command)) return fail(error, @"Truncated dylib command.");
        const struct load_command *lc = (const void *)p;
        if (lc->cmdsize < sizeof(*lc) || lc->cmdsize > end - p) return fail(error, @"Invalid dylib command size.");
        if (lc->cmd == LC_ROUTINES || lc->cmd == LC_ROUTINES_64 || lc->cmd == LC_RPATH)
            return fail(error, @"Game dylib contains a constructor or search path.");
        if (lc->cmd == LC_LOAD_DYLIB || lc->cmd == LC_LOAD_WEAK_DYLIB || lc->cmd == LC_REEXPORT_DYLIB || lc->cmd == LC_LOAD_UPWARD_DYLIB) {
            if (lc->cmdsize < sizeof(struct dylib_command)) return fail(error, @"Invalid dylib dependency.");
            const struct dylib_command *dc = (const void *)p;
            if (dc->dylib.name.offset >= lc->cmdsize) return fail(error, @"Invalid dylib dependency name.");
            const char *name = (const char *)p + dc->dylib.name.offset;
            size_t length = lc->cmdsize - dc->dylib.name.offset;
            if (!memchr(name, 0, length) || strcmp(name, "/usr/lib/libSystem.B.dylib")) return fail(error, @"Unexpected game dylib dependency.");
        }
        if (lc->cmd == LC_SEGMENT_64) {
            if (lc->cmdsize < sizeof(struct segment_command_64)) return fail(error, @"Truncated dylib segment.");
            const struct segment_command_64 *sg = (const void *)p;
            if (sg->nsects > (lc->cmdsize - sizeof(*sg)) / sizeof(struct section_64) || sg->fileoff > data.length || sg->filesize > data.length - sg->fileoff)
                return fail(error, @"Invalid dylib segment bounds.");
            if ((sg->initprot & 6) == 6) return fail(error, @"Game dylib has writable executable pages.");
            const struct section_64 *sections = (const void *)(sg + 1);
            for (uint32_t j = 0; j < sg->nsects; j++) {
                uint32_t type = sections[j].flags & SECTION_TYPE;
                if (type == S_MOD_INIT_FUNC_POINTERS || type == S_MOD_TERM_FUNC_POINTERS || type == S_THREAD_LOCAL_INIT_FUNCTION_POINTERS || type == S_INIT_FUNC_OFFSETS)
                    return fail(error, @"Game dylib contains initializers or destructors.");
            }
        }
        p += lc->cmdsize;
    }
    return p == end ? YES : fail(error, @"Game dylib command count mismatch.");
}

static BOOL prepare_native_session(NSString *package, NSError **error) {
    if (g_prepared) return fail(error, @"The previous game has not finished stopping.");
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (!device || !device.supportsBCTextureCompression) return fail(error, @"This device does not support the required Metal BC textures.");
    NSString *path = [package stringByAppendingPathComponent:@"game.dylib"];
    if (!check_image(path, error)) return NO;
    // The Swift package verifier has checked every manifest digest before this call.
    NSData *manifestData = [NSData dataWithContentsOfFile:[package stringByAppendingPathComponent:@"manifest.json"] options:0 error:error];
    if (!manifestData) return NO;
    NSDictionary *manifest = [NSJSONSerialization JSONObjectWithData:manifestData options:0 error:error];
    if (![manifest isKindOfClass:NSDictionary.class]) return NO;
    NSURL *registryURL = [NSBundle.mainBundle URLForResource:@"NativeGameProfiles" withExtension:@"json"];
    NSData *registryData = registryURL ? [NSData dataWithContentsOfURL:registryURL options:0 error:error] : nil;
    if (!registryData) return fail(error, @"The app's reviewed profile registry is missing.");
    NSArray *registry = [NSJSONSerialization JSONObjectWithData:registryData options:0 error:error];
    if (![registry isKindOfClass:NSArray.class]) return fail(error, @"The app's reviewed profile registry is invalid.");
    NSDictionary *profile = nil;
    for (NSDictionary *row in registry) {
        if ([row[@"titleID"] isEqual:manifest[@"titleID"]] && [row[@"version"] isEqual:manifest[@"version"]]) { profile = row; break; }
    }
    if ([manifest[@"format"] isEqual:@2]) {
        // Swift regenerates and validates this profile from bounded recipe facts,
        // then checks every package binding before calling the bridge.
        NSDictionary *generated = manifest[@"preparedProfile"];
        if (![generated isKindOfClass:NSDictionary.class] || ![manifest[@"signingStatus"] isEqual:@"signed"] ||
            ![generated[@"titleID"] isEqual:manifest[@"titleID"]] ||
            ![generated[@"enginePin"] isEqual:@"bbc3b73784751d2a706932dc69fd67627e3445d4"] ||
            ![generated[@"shaderInputs"] isEqual:@"runtime-capture-v1"]) return fail(error, @"Invalid native preparation contract.");
        profile = generated;
    }
    if (!profile) return fail(error, @"This package has no valid preparation recipe. Prepare it in Satchel.");
    for (NSString *key in @[@"enginePin", @"shaderChain", @"shaderInputs", @"modules", @"environment"]) {
        if (![manifest[key] isEqual:profile[key]]) return fail(error, @"Prepared package does not match its reviewed profile.");
    }
    NSArray *modules = profile[@"modules"];
    if (modules.count < 1 || modules.count > 8) return fail(error, @"Unsupported module count.");
    void *handle = dlopen(path.fileSystemRepresentation, RTLD_NOW | RTLD_LOCAL);
    if (!handle) return fail(error, [NSString stringWithFormat:@"dlopen failed: %s", dlerror() ?: "unknown error"]);
    dlerror();
    void *marker = dlsym(handle, "satchel_game_container_v1");
    Dl_info info = {0};
    if (!marker || !dladdr(marker, &info) || !info.dli_fbase || !info.dli_fname ||
        ![[@(info.dli_fname) stringByResolvingSymlinksInPath] isEqualToString:[path stringByResolvingSymlinksInPath]]) {
        dlclose(handle);
        return fail(error, @"Game dylib marker does not belong to the expected image.");
    }
    const struct mach_header_64 *mh = info.dli_fbase;
    if (mh->magic != MH_MAGIC_64 || mh->filetype != MH_DYLIB) { dlclose(handle); return fail(error, @"Loaded image is not the game dylib."); }
    NSString *documents = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
    NSString *runtime = [documents stringByAppendingPathComponent:[@"NativeRuntime" stringByAppendingPathComponent:profile[@"titleID"]]];
    for (NSString *name in @[@"save", @"temp", @"aoc"]) {
        if (![NSFileManager.defaultManager createDirectoryAtPath:[runtime stringByAppendingPathComponent:name] withIntermediateDirectories:YES attributes:nil error:error]) { dlclose(handle); return NO; }
    }
    if ([manifest[@"format"] isEqual:@2] && ![NSFileManager.defaultManager createDirectoryAtPath:[runtime stringByAppendingPathComponent:@"shader-capture"] withIntermediateDirectories:YES attributes:nil error:error]) { dlclose(handle); return NO; }
    g_previousDirectory = NSFileManager.defaultManager.currentDirectoryPath;
    g_environment = session_environment();
    g_originalStderr = dup(STDERR_FILENO);
    if (g_originalStderr < 0) { restore_session_environment(); dlclose(handle); return fail(error, @"Cannot preserve the app log destination."); }
    if (chdir(runtime.fileSystemRepresentation)) { restore_session_environment(); dlclose(handle); return fail(error, @"Cannot open the private runtime directory."); }
    // Native diagnostics can include guest addresses and instructions. Keep them private in
    // the app container; they are never collected by this build worker.
    NSString *log = [runtime stringByAppendingPathComponent:@"host.log"];
    if (!freopen(log.fileSystemRepresentation, "w", stderr)) { restore_session_environment(); dlclose(handle); return fail(error, @"Cannot create the private host log."); }
    setvbuf(stderr, NULL, _IOLBF, 0);
    // Do not let inherited per-game switches override the verified profile.
    for (NSString *key in session_environment())
        if ([key hasPrefix:@"DM_"]) unsetenv(key.UTF8String);
    NSDictionary *env = manifest[@"environment"];
    for (NSString *key in env) setenv(key.UTF8String, [env[key] UTF8String], 1);
    NSString *serviceReport = [runtime stringByAppendingPathComponent:@"services.log"];
    if (![NSData.data writeToFile:serviceReport options:NSDataWritingAtomic error:error]) {
        restore_session_environment(); dlclose(handle); return NO;
    }
    setenv("DM_SERVICE_REPORT", serviceReport.fileSystemRepresentation, 1);
    NSString *missingReport = [runtime stringByAppendingPathComponent:@"missing-services.json"];
    if (![[NSData dataWithBytes:"[]\n" length:3] writeToFile:missingReport options:NSDataWritingAtomic error:error]) {
        restore_session_environment(); dlclose(handle); return NO;
    }
    setenv("DM_MISSING_SERVICES", missingReport.fileSystemRepresentation, 1);
    SatchelApplyPlayerSettings(manifest[@"titleID"]);
    if ([manifest[@"format"] isEqual:@2]) {
        NSString *capture = [runtime stringByAppendingPathComponent:@"shader-capture"];
        setenv("DM_NVN_SHDUMP", capture.fileSystemRepresentation, 1);
        setenv("DM_PHONE_SHADERS", "1", 1);
        setenv("DM_PHONE_SHADER_CACHE", [runtime stringByAppendingPathComponent:@"shader-cache"].fileSystemRepresentation, 1);
    }
    NSDictionary *paths = @{
        @"DM_PROFILE": [package stringByAppendingPathComponent:@"profile.tsv"],
        @"DM_ROMFS": [package stringByAppendingPathComponent:@"romfs.img"],
        @"DM_NVN_SHMAP": [package stringByAppendingPathComponent:@"dm/shader_map.tsv"],
        @"DM_MSL_DIR": [package stringByAppendingPathComponent:@"msl"],
        @"DM_FONT_DIR": [NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:@"NativeAssets/font"],
        @"DM_SAVE": [runtime stringByAppendingPathComponent:@"save"],
        @"DM_TEMP": [runtime stringByAppendingPathComponent:@"temp"],
        @"DM_AOC_DIR": [package stringByAppendingPathComponent:@"aoc"]
    };
    for (NSString *key in paths) setenv(key.UTF8String, [paths[key] fileSystemRepresentation], 1);
    SatchelApplyPipelineSettings(runtime);
    dm_cloud_saves_env();
    // Dev only: the host polls this file for key presses and "shot <path>" requests, so the Mac
    // can drive the game and see frames through devicectl (its own screen capture is black on this
    // phone). Each session starts with an empty file; the Mac replaces it with a longer copy.
    NSString *ctl = [runtime stringByAppendingPathComponent:@"ctl.txt"];
    [NSData.data writeToFile:ctl atomically:NO];
    setenv("DM_CTL", ctl.fileSystemRepresentation, 1);
    g_image_handle = handle; // Retired only after every engine borrower has stopped.
    g_package = [package copy];
    dm_set_game_image_header(info.dli_fbase);
    g_argv[0] = strdup("satchel-native");
    g_argv[1] = strdup([package stringByAppendingPathComponent:@"dm"].fileSystemRepresentation);
    g_argc = 2;
    for (NSString *module in modules) g_argv[g_argc++] = strdup(module.UTF8String);
    g_argv[g_argc] = NULL;
#ifdef SATCHEL_MANAGED_SESSION
    int result = satchel_host_session_prepare();
    if (result) {
        for (int i = 0; i < g_argc; i++) { free(g_argv[i]); g_argv[i] = NULL; }
        g_argc = 0; g_package = nil; g_image_handle = NULL; dm_set_game_image_header(NULL); dlclose(handle); restore_session_environment();
        return fail(error, [NSString stringWithFormat:@"Cannot prepare the session (%d).", result]);
    }
    satchel_host_session_load(g_argc, g_argv);
    void satchel_memstat_start(void); satchel_memstat_start();
#endif
    g_prepared = YES;
    save_set_title([profile[@"titleID"] uppercaseString]);
    return YES;
}

BOOL SatchelPrepareNativeSession(NSString *package, NSError **error) {
    [save_lock() lock];
    BOOL result = prepare_native_session(package, error);
    [save_lock() unlock];
    return result;
}

UIViewController *SatchelNativeGameController(void) {
    static UIViewController *controller;
    if (!controller) controller = front_ios_root();
    return controller;
}
BOOL SatchelNativeSessionStarted(void) { return g_started; }
NSDictionary *SatchelNativeSessionMetrics(void) {
    NSCAssert(NSThread.isMainThread, @"Session metrics require the main thread");
    extern uint64_t dm_guest_frame(void);
    task_vm_info_data_t info = {0}; mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    kern_return_t result = task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&info, &count);
    NSMutableDictionary *row = [@{@"frames": @(g_started ? dm_guest_frame() : 0)} mutableCopy];
    if (result == KERN_SUCCESS && count >= TASK_VM_INFO_REV1_COUNT) row[@"footprint"] = @(info.phys_footprint);
    return row;
}
int SatchelNativeSessionAcceptsEvents(void) { return g_started && !g_ending; }
void SatchelNativeFocusEvent(int event) {
    if (g_started && !g_ending) front_ios_focus_event(event);
}
void SatchelNativeSetMenuPaused(BOOL paused) {
    NSCAssert(NSThread.isMainThread, @"Focus changes require the main thread");
    if (g_ending) return;
    if (g_menuPaused == paused) return;
    g_menuPaused = paused;
    if (!g_started) return;
#ifdef SATCHEL_MANAGED_SESSION
    void satchel_front_menu_paused(int); satchel_front_menu_paused(paused);
#else
    if (paused) { front_ios_focus_event(0); front_ios_focus_event(1); }
    else if (UIApplication.sharedApplication.applicationState == UIApplicationStateActive) {
        front_ios_focus_event(2); front_ios_focus_event(3);
    }
#endif
    fprintf(stderr, "Satchel menu: pause=%d\n", paused);
}
int front_ios_boot_ok(void) { return g_prepared; }
const char *front_ios_boot_message(void) { return NULL; }
const char *front_ios_device_message(void) { return NULL; }
int dm_locate_game_data(void) { return g_prepared; }
bool dm_show_launcher(void *controller, void (*done)(void)) { (void)controller; (void)done; return false; }
void dm_cloud_saves_env(void) { setenv("DM_SAVE_SYNC", "0", 1); }

static void report_session(NSString *stage, NSString *result) {
    NSDictionary *row = @{@"package": g_package.lastPathComponent ?: @"", @"stage": stage, @"result": result,
                          @"time": @(NSDate.date.timeIntervalSince1970)};
    NSData *data = [NSJSONSerialization dataWithJSONObject:row options:0 error:nil];
    [data writeToFile:@"session.json" atomically:YES];
}

void satchel_session_phase(const char *phase, int result) {
    report_session(@"stop", [NSString stringWithFormat:@"%s %d", phase, result]);
}

static void *guest_thread(void *unused) {
    (void)unused;
    @autoreleasepool {
#ifndef SATCHEL_MANAGED_SESSION
        static char alt[65536]; stack_t ss = {.ss_sp = alt, .ss_size = sizeof alt}; sigaltstack(&ss, NULL);
#endif
        report_session(@"boot", @"running");
#ifdef SATCHEL_MANAGED_SESSION
        satchel_host_session_enter(NULL); int rc = 0;
#else
        int rc = dm_main(g_argc, g_argv);
#endif
        report_session(@"boot", [NSString stringWithFormat:@"returned %d", rc]);
        fprintf(stderr, "Satchel native session returned %d\n", rc);
    }
    return NULL;
}
void front_ios_boot(void) {
    if (!g_prepared || g_started) return;
    // A long package verification can finish after the user backgrounds the app.
    // Do not start guest GPU work until UIKit reports the app active again.
    if (UIApplication.sharedApplication.applicationState != UIApplicationStateActive) {
        static id observer;
        if (!observer) observer = [NSNotificationCenter.defaultCenter addObserverForName:UIApplicationDidBecomeActiveNotification object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification *note) {
            (void)note;
            [NSNotificationCenter.defaultCenter removeObserver:observer]; observer = nil;
            front_ios_boot();
        }];
        return;
    }
    g_started = YES;
#ifdef SATCHEL_MANAGED_SESSION
    void satchel_front_session_begin(void); satchel_front_session_begin();
#else
    front_ios_audio_session();
#endif
    pthread_attr_t attr; pthread_attr_init(&attr);
    int rc = pthread_attr_setstacksize(&attr, 64u << 20);
    pthread_t thread;
#ifdef SATCHEL_MANAGED_SESSION
    if (!rc) rc = satchel_session_pthread_create(&thread, &attr, guest_thread, NULL);
#else
    if (!rc) rc = pthread_create(&thread, &attr, guest_thread, NULL);
#endif
    pthread_attr_destroy(&attr);
#ifndef SATCHEL_MANAGED_SESSION
    if (!rc) pthread_detach(thread);
#endif
    if (rc) {
        [NSNotificationCenter.defaultCenter postNotificationName:SatchelNativeSessionDidFail object:nil
            userInfo:@{@"package": g_package.lastPathComponent ?: @"", @"code": @(rc)}];
        UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"Cannot start game" message:[NSString stringWithFormat:@"The guest thread could not start (%d). Return to the library and try again.", rc] preferredStyle:UIAlertControllerStyleAlert];
        [alert addAction:[UIAlertAction actionWithTitle:@"Library" style:UIAlertActionStyleDefault handler:^(UIAlertAction *action) {
            SatchelStopNativeSession(^(NSError *error) { if (error) fprintf(stderr, "Satchel boot cleanup failed: %ld\n", (long)error.code); });
        }]];
        UIViewController *vc = nil;
        for (UIScene *scene in UIApplication.sharedApplication.connectedScenes)
            if ([scene isKindOfClass:UIWindowScene.class])
                for (UIWindow *window in ((UIWindowScene *)scene).windows)
                    if (window.isKeyWindow) vc = window.rootViewController;
        while (vc.presentedViewController) vc = vc.presentedViewController;
        [vc presentViewController:alert animated:YES completion:nil];
    }
}

void SatchelStopNativeSession(void (^completion)(NSError *)) {
    NSCAssert(NSThread.isMainThread, @"Session transitions require the main thread");
    if (!g_prepared) { completion(nil); return; }
    if (g_stopping) { completion([NSError errorWithDomain:@"SatchelSession" code:EBUSY userInfo:@{NSLocalizedDescriptionKey:@"The game is still stopping."}]); return; }
    g_stopping = g_ending = YES; SatchelMenuStopping(NO);
    dispatch_async(session_queue(), ^{
#ifdef SATCHEL_MANAGED_SESSION
        int result = satchel_host_session_stop();
        if (!result && g_image_handle && dlclose(g_image_handle)) result = EIO;
#else
        int result = ENOTSUP;
#endif
        [save_lock() lock];
        NSString *saveTitle = SatchelNativeSaveTitle();
        if (!result) save_set_title(nil);
        [save_lock() unlock];
        dispatch_async(dispatch_get_main_queue(), ^{
            g_stopping = NO;
            if (result) {
                SatchelMenuStopping(YES);
                completion([NSError errorWithDomain:@"SatchelSession" code:result userInfo:@{NSLocalizedDescriptionKey:
                    [NSString stringWithFormat:@"The game could not finish stopping (%d). Its remaining resources are retained. Try Quit again.", result]}]);
                return;
            }
#ifdef SATCHEL_MANAGED_SESSION
            void satchel_front_session_end(void); satchel_front_session_end();
#endif
            SatchelClearPlayerSession();
            restore_session_environment();
            NSString *package = g_package.lastPathComponent ?: @"";
            g_image_handle = NULL; g_package = nil;
            for (int i = 0; i < g_argc; i++) { free(g_argv[i]); g_argv[i] = NULL; }
            g_argc = 0; g_prepared = g_started = g_menuPaused = g_ending = NO;
            SatchelMenuEndSession();
            [NSNotificationCenter.defaultCenter postNotificationName:SatchelNativeSessionDidEnd object:nil userInfo:@{@"package": package, @"titleID": saveTitle ?: @""}];
            completion(nil);
        });
    });
}
