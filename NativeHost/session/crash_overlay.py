"""Keep the crash report from faulting while it reads the crashed thread's stack."""

# A guest thread that overflows its stack crashes with sp at the edge of an unmapped
# page. The engine's stack scan then loaded words below sp directly, faulted again with
# the signal blocked, and never reached _exit. The crash gate's 15 s alarm then landed
# on the UI thread, which paused forever: the app froze on the previous game's frame.
# Read each word with vm_read_overwrite, which fails instead of faulting, like the
# engine's own backtrace does.
SCAN = '            uint64_t v = ((uint64_t *)sp)[i]; Dl_info di;\n'
SAFE = ('            uint64_t v = 0; vm_size_t got = 0; Dl_info di;\n'
        '            if (vm_read_overwrite(mach_task_self(), (vm_address_t)(sp + (uint64_t)((int64_t)i * 8)), sizeof v, (vm_address_t)&v, &got) != KERN_SUCCESS || got != sizeof v) continue;\n')


def safe_stack_scan(text):
    assert text.count(SCAN) == 1, 'crash.c stack scan changed'
    return text.replace(SCAN, SAFE)
