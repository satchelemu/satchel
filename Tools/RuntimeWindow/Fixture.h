#ifndef SATCHEL_RUNTIME_WINDOW_FIXTURE_H
#define SATCHEL_RUNTIME_WINDOW_FIXTURE_H
#include "SwitchProcess.h"
#include "GuestVulkanWindows.h"
int wf_create(SatchelSwitchProcess **,SatchelGuestDispatch **,SatchelCPUProbe **);
void wf_destroy(SatchelSwitchProcess *,SatchelGuestDispatch *);
int wf_stop(SatchelSwitchProcess *,SatchelGuestDispatch *,SatchelCPUProbe *,int,SatchelCPUProbeResult *);
int wf_unchanged(SatchelCPUProbe *,const SatchelCPUProbeResult *);
void wf_mode(unsigned,unsigned);
unsigned wf_window_calls(void);
unsigned wf_ordinary_calls(void);
int wf_borrow_valid(void);
int wf_policy_seen(void);
#endif
