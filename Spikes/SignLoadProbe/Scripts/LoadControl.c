#include <dlfcn.h>
#include <stdio.h>
int main(int argc, const char **argv) {
    if (argc != 2) return 2;
    void *handle = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!handle) { fprintf(stderr, "%s\n", dlerror()); return 1; }
    int (*value)(void) = (int (*)(void))dlsym(handle, "probe_value");
    if (!value) { fprintf(stderr, "%s\n", dlerror()); return 1; }
    int result = value(); printf("Generated Mach-O control returned %d\n", result);
    dlclose(handle);
    return result == 42 ? 0 : 1;
}
