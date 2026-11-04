#ifndef VI_REPORT_FIXTURE_H
#define VI_REPORT_FIXTURE_H
#include "GuestVIDispatch.h"
SatchelCPUProbe *vi_report_fixture_create(SatchelGuestVIEntries *,SatchelGuestVIBorrow *,SatchelCPUProbeResult *);
int vi_report_fixture_unchanged(SatchelCPUProbe *);
#endif
