#include "GuestVIDispatch.h"
#include <string.h>
#include <time.h>
static const char *const names[SATCHEL_GVI_ENTRY_COUNT]={
 "_ZN2nn2vi18OpenDefaultDisplayEPPNS0_7DisplayE",
 "_ZN2nn2vi11CreateLayerEPPNS0_5LayerEPNS0_7DisplayE",
 "_ZN2nn2vi15GetNativeWindowEPPvPNS0_5LayerE",
 "_ZN2nn2vi12DestroyLayerEPNS0_5LayerE",
 "_ZN2nn2vi12CloseDisplayEPNS0_7DisplayE",
 "_ZN2nn2vi11CreateLayerEPPNS0_5LayerEPNS0_7DisplayEii",
 "_ZN2nn2vi11CreateLayerEPPNS0_5LayerEPNS0_7DisplayEPKNS0_21LayerCreationSettingsE"};
static int aligned(const void*p,size_t n){return p&&((uintptr_t)p%n)==0;}
const char *satchel_guest_vi_entry_name(uint32_t op){return op&&op<=SATCHEL_GVI_ENTRY_COUNT?names[op-1]:NULL;}
static uint64_t clock_ns(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t)||t.tv_sec<0||(uint64_t)t.tv_sec>UINT64_MAX/1000000000)return UINT64_MAX;uint64_t n=(uint64_t)t.tv_sec*1000000000;return(uint64_t)t.tv_nsec>UINT64_MAX-n?UINT64_MAX:n+(uint64_t)t.tv_nsec;}
typedef struct {SatchelGuestSymbolRead read;SatchelGuestSymbolCancelled cancel;void*context;uint64_t deadline;int expired;} Resolver;
static int read_guest(void*c,uint64_t a,void*out,size_t n){Resolver*r=c;return r->read(r->context,a,out,n);}
static int stopped(void*c){Resolver*r=c;uint64_t n=clock_ns();if(n==UINT64_MAX||n>=r->deadline){r->expired=1;return 1;}return r->cancel&&r->cancel(r->context);}
static int entries_valid(const SatchelGuestVIEntries*e){
 if(!aligned(e,_Alignof(SatchelGuestVIEntries))||e->version!=1||e->size!=sizeof(*e)||e->reserved||e->present_mask&~127u||(e->present_mask&7u)!=7)return 0;
 for(unsigned i=0;i<SATCHEL_GVI_ENTRY_COUNT;i++){int present=(e->present_mask>>i)&1;if(present!=!!e->address[i]||(present&&(e->address[i]&3)))return 0;for(unsigned j=0;j<i;j++)if(present&&e->address[i]==e->address[j])return 0;}return 1;
}
SatchelGuestVIStatus satchel_guest_vi_resolve_entries(const SatchelGuestSymbolTable*input,size_t count,SatchelGuestSymbolRead read,SatchelGuestSymbolCancelled cancel,void*context,uint32_t timeout,SatchelGuestVIEntries*out){
 if(!aligned(input,_Alignof(SatchelGuestSymbolTable))||!aligned(out,_Alignof(SatchelGuestVIEntries))||!read||!count||count>SATCHEL_GVI_MODULE_LIMIT||!timeout||timeout>1000)return SATCHEL_GVI_INVALID;
 SatchelGuestSymbolTable tables[SATCHEL_GVI_MODULE_LIMIT];memcpy(tables,input,count*sizeof(*input));
 uint64_t n=clock_ns(),span=(uint64_t)timeout*1000000;if(n==UINT64_MAX||n>UINT64_MAX-span)return SATCHEL_GVI_CANCELLED;
 Resolver r={read,cancel,context,n+span,0};SatchelGuestVIEntries found={.version=1,.size=sizeof(found)};
 for(unsigned op=0;op<SATCHEL_GVI_ENTRY_COUNT;op++)for(size_t m=0;m<count;m++){
  if(stopped(&r))return r.expired?SATCHEL_GVI_LIMIT:SATCHEL_GVI_CANCELLED;
  SatchelGuestSymbol symbol={0};SatchelGuestSymbolStatus s=satchel_guest_symbols_find_function(&tables[m],read_guest,stopped,&r,names[op],strlen(names[op]),&symbol);
  if(s==SATCHEL_GUEST_SYMBOL_NOT_FOUND||s==SATCHEL_GUEST_SYMBOL_UNDEFINED)continue;
  if(s==SATCHEL_GUEST_SYMBOL_CANCELLED)return r.expired?SATCHEL_GVI_LIMIT:SATCHEL_GVI_CANCELLED;
  if(s!=SATCHEL_GUEST_SYMBOL_FOUND||found.address[op])return SATCHEL_GVI_SYMBOL;
  found.address[op]=symbol.address;found.present_mask|=1u<<op;
 }
 if(!entries_valid(&found))return SATCHEL_GVI_SYMBOL;if(stopped(&r))return r.expired?SATCHEL_GVI_LIMIT:SATCHEL_GVI_CANCELLED;*out=found;return SATCHEL_GVI_OK;
}
size_t satchel_guest_vi_entry_points(const SatchelGuestVIEntries*e,uint64_t*out,size_t capacity){
 if(!entries_valid(e)||!aligned(out,_Alignof(uint64_t)))return 0;uint64_t points[SATCHEL_GVI_ENTRY_COUNT];size_t count=0;for(unsigned i=0;i<SATCHEL_GVI_ENTRY_COUNT;i++)if(e->address[i])points[count++]=e->address[i];if(capacity<count)return 0;memcpy(out,points,count*sizeof(*out));return count;
}
SatchelGuestVIStatus satchel_guest_vi_dispatch(const SatchelGuestVIEntries*e,SatchelGuestVI*owner,const SatchelGuestVIBorrow*b,const SatchelCPUProbeResult*stop,SatchelGuestVIResult*out){
 if(!entries_valid(e)||!aligned(b,_Alignof(SatchelGuestVIBorrow))||!b->cpu||!aligned(stop,_Alignof(SatchelCPUProbeResult))||!aligned(out,_Alignof(SatchelGuestVIResult))||stop->reason!=SATCHEL_CPU_BREAKPOINT)return SATCHEL_GVI_INVALID;
 unsigned op=0;for(unsigned i=0;i<SATCHEL_GVI_ENTRY_COUNT;i++)if(e->address[i]==stop->pc){op=i+1;break;}if(!op)return SATCHEL_GVI_NOT_OURS;
 SatchelCPUProbeResult current=satchel_cpu_probe_run(b->cpu,0,NULL,NULL);if(current.reason!=SATCHEL_CPU_BUDGET||current.pc!=stop->pc||!satchel_cpu_probe_check_range(b->cpu,stop->pc,4,SATCHEL_CPU_EXECUTE))return SATCHEL_GVI_INVALID;
 uint64_t x0,x1,lr;if(!satchel_cpu_probe_get_x(b->cpu,0,&x0)||!satchel_cpu_probe_get_x(b->cpu,1,&x1)||!satchel_cpu_probe_get_x(b->cpu,30,&lr))return SATCHEL_GVI_INVALID;
 return satchel_guest_vi_execute(owner,b,op,x0,x1,stop->pc,lr,out);
}
