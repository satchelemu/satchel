#import "SignerBridge.h"
int main(int argc, const char **argv) {
    @autoreleasepool {
        if (argc != 4) { fprintf(stderr, "usage: format-check fixture.p12 unsigned-input output-directory\n"); return 2; }
        NSError *error = nil;
        NSData *p12 = [NSData dataWithContentsOfFile:@(argv[1]) options:0 error:&error];
        // Only the disposable, empty-passphrase format fixture is accepted by this CLI.
        SPSigner *signer = [[SPSigner alloc] initWithPKCS12:p12 passphrase:@"" team:@"PROBETEST1" error:&error];
        if (!signer) { fprintf(stderr, "%s\n", error.description.UTF8String); return 1; }
        NSString *out = @(argv[3]);
        for (NSString *payload in @[@"S1c", @"S1d"]) {
            NSString *input = [out stringByAppendingPathComponent:[payload stringByAppendingString:@"-unsigned.dylib"]];
            BOOL prepared = [payload isEqualToString:@"S1c"]
                ? [[NSData dataWithContentsOfFile:@(argv[2])] writeToFile:input options:NSDataWritingAtomic error:&error]
                : SPGenerateDylib(input, YES, &error);
            if (!prepared) { fprintf(stderr, "%s\n", error.description.UTF8String); return 1; }
            for (NSString *variant in @[@"A", @"B", @"C", @"D"]) {
                NSString *path = [out stringByAppendingPathComponent:[NSString stringWithFormat:@"selfsigned-%@-%@.dylib", payload, variant]];
                if (![[NSData dataWithContentsOfFile:input] writeToFile:path options:NSDataWritingAtomic error:&error] ||
                    ![signer signFile:path identifier:[@"com.dell.satchel.signprobe." stringByAppendingString:payload]
                             variant:variant error:&error]) {
                    fprintf(stderr, "%s\n", error.description.UTF8String); return 1;
                }
            }
        }
        if (!SPGenerateDylib([out stringByAppendingPathComponent:@"generated-macos-unsigned.dylib"], NO, &error)) {
            fprintf(stderr, "%s\n", error.description.UTF8String); return 1;
        }
    }
    return 0;
}
