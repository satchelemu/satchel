"""Adapt the existing ORIGINAL diagnostic solely at its host dispatch seam."""
from pathlib import Path
import argparse
parser=argparse.ArgumentParser();parser.add_argument("--output",type=Path,required=True);parser.add_argument("--source-root",type=Path,default=Path(__file__).resolve().parents[2]);args=parser.parse_args()
HERE=Path(__file__).resolve().parent
s=(args.source_root/'Tools/GuestGraphics/GuestNVNRasterDiagnostic.c').read_text()
def replace(a,b):
 global s
 assert s.count(a)==1,a
 s=s.replace(a,b)
s='#include "HostNVN.h"\n'+s
s=s.replace('SatchelGuestDispatch *','SatchelHostNVN *')
replace('SatchelGuestDispatchReport out;uint32_t actual=satchel_guest_dispatch_handle(f->dispatch,f->cpu,&stop,f->cancel,f->context,&out);f->events=out.events;', 'uint32_t actual=satchel_host_nvn_handle(f->dispatch,f->cpu,&stop,f->cancel,f->context);SatchelHostNVNReport out;CHECK(satchel_host_nvn_report(f->dispatch,&out));f->events=out.events;')
replace('f->dispatch=satchel_guest_dispatch_create(IBOOT,ISTUBS,4096,30000);CHECK(f->dispatch);SatchelGuestDeviceBackendV2 backend;CHECK(satchel_vulkan_guest_device_backend_v2(&backend));CHECK(satchel_guest_dispatch_set_device_backend_v2(f->dispatch,&backend));CHECK(satchel_guest_dispatch_enable_vulkan_raster(f->dispatch));uint64_t points[2];CHECK(satchel_guest_dispatch_breakpoints(f->dispatch,points));', 'f->dispatch=satchel_host_nvn_create(IBOOT,ISTUBS,satchel_host_nvn_now_ns()+30000000000ull,30000,4096);CHECK(f->dispatch);SatchelHostNVNReport before;CHECK(satchel_host_nvn_report(f->dispatch,&before)&&!before.created&&!before.reached);uint64_t points[2]={IBOOT,ISTUBS};')
replace('static int collect(void){if(handoff_pending&&!satchel_guest_dispatch_handoff(&handoff_pending))return 0;if(cpu_pending){if(!satchel_cpu_probe_try_destroy(cpu_pending))return 0;cpu_pending=NULL;}if(retirement_pending){SatchelGuestRetirementReport report;if(satchel_guest_dispatch_retirement_poll(NULL,NULL,&report)!=SATCHEL_GUEST_RETIRE_IDLE)return 0;retirement_pending=0;}return 1;}', 'static int collect(void){if(handoff_pending&&!satchel_host_nvn_handoff(handoff_pending))return 0;if(cpu_pending){if(!satchel_cpu_probe_try_destroy(cpu_pending))return 0;cpu_pending=NULL;}if(handoff_pending){SatchelHostNVNReport report;if(!satchel_host_nvn_finish(&handoff_pending,5000,&report))return 0;}if(retirement_pending){SatchelGuestRetirementReport report;if(satchel_guest_dispatch_retirement_poll(NULL,NULL,&report)!=SATCHEL_GUEST_RETIRE_IDLE)return 0;retirement_pending=0;}return 1;}')
replace('if(f.dispatch){if(!satchel_guest_dispatch_handoff(&f.dispatch))handoff_pending=f.dispatch;retirement_pending=1;}', 'if(f.dispatch){satchel_host_nvn_handoff(f.dispatch);handoff_pending=f.dispatch;retirement_pending=1;}')
# This fixture is never linked into host-runtime.
assert args.output.resolve().is_relative_to(Path('/tmp').resolve())
args.output.write_text(s)
