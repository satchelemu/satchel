/* Test-only Swift symbol forwarding. Production C retains its normal export. */
#define satchel_guest_dispatch_window_release satchel_window_fixture_unused_release_declaration
#include "GuestVulkanWindows.h"
#undef satchel_guest_dispatch_window_release
#include "WindowFrameBridge.h"
SatchelGuestWindowStatus satchel_guest_dispatch_window_release(uint64_t,uint64_t) __asm__("_satchel_window_test_release");
void satchel_window_test_release_mode(unsigned);
unsigned satchel_window_test_release_calls(void);
unsigned satchel_window_test_release_injections(void);
