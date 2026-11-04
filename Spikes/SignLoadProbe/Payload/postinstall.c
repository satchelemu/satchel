// Compiled and transferred only after app installation, never a bundle resource.
__attribute__((visibility("default"), noinline)) int probe_value(void) {
    volatile int seed = 19;
    return seed * 2 + 4;
}
__attribute__((visibility("default"))) const char postinstall_marker[] = "Satchel post-install-only native image 2026-11-01";
