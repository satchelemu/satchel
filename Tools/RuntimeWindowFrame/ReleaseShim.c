/* TEST ONLY. No production fault switch. State accessed on the serial worker. */
#include "GuestVulkanWindows.h"
static unsigned mode,calls,injections;
void satchel_window_test_release_mode(unsigned v){mode=v;calls=injections=0;}
unsigned satchel_window_test_release_calls(void){return calls;}
unsigned satchel_window_test_release_injections(void){return injections;}
SatchelGuestWindowStatus satchel_window_test_release(uint64_t window,uint64_t frame){
 calls++;
 if(mode==1){mode=0;injections++;return SATCHEL_GWIN_BUSY;}
 SatchelGuestWindowStatus s=satchel_guest_dispatch_window_release(window,frame);
 if(mode==2&&s==SATCHEL_GWIN_OK){mode=0;injections++;return SATCHEL_GWIN_BUSY;}
 return s;
}
