#include "GuestVI.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
struct SatchelGuestVI {SatchelGuestVIConfig config;uint32_t phase,issued;uint64_t display,layer,native;
 uint64_t display_gen,layer_gen,native_gen,events,refs[SATCHEL_GVI_REFERENCE_LIMIT];};
static atomic_flag gate=ATOMIC_FLAG_INIT;
static SatchelGuestVI *owners[SATCHEL_GVI_OWNER_LIMIT];
static uint64_t next_ref=1;
static int enter(void){return !atomic_flag_test_and_set_explicit(&gate,memory_order_acquire);}
static SatchelGuestVIStatus leave(SatchelGuestVIStatus s){atomic_flag_clear_explicit(&gate,memory_order_release);return s;}
static int aligned(const void*p,size_t n){return p&&((uintptr_t)p%n)==0;}
static int valid(SatchelGuestVI*p){for(unsigned i=0;i<SATCHEL_GVI_OWNER_LIMIT;i++)if(owners[i]&&owners[i]==p)return 1;return 0;}
static uint64_t clock_ns(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t)||t.tv_sec<0||(uint64_t)t.tv_sec>UINT64_MAX/1000000000)return UINT64_MAX;uint64_t n=(uint64_t)t.tv_sec*1000000000;return (uint64_t)t.tv_nsec>UINT64_MAX-n?UINT64_MAX:n+(uint64_t)t.tv_nsec;}
static unsigned references(SatchelGuestVI*p){unsigned n=0;for(unsigned i=0;i<SATCHEL_GVI_REFERENCE_LIMIT;i++)n+=p->refs[i]!=0;return n;}
static int reservation_valid(const SatchelGuestVIReservation*r){return r->version==1&&r->size==sizeof(*r)&&r->process_id&&r->generation&&r->address>=4096&&!(r->address&4095)&&r->length==SATCHEL_GVI_IDENTITY_BYTES&&r->address<=UINT64_MAX-r->length;}
SatchelGuestVIStatus satchel_guest_vi_create(const SatchelGuestVIConfig*input,SatchelCPUProbe*cpu,SatchelGuestVIReservationCheck claim,void*context,SatchelGuestVI**out){
 if(!aligned(out,_Alignof(SatchelGuestVI*)))return SATCHEL_GVI_INVALID;
 SatchelGuestVIConfig c={0};int readable=aligned(input,_Alignof(SatchelGuestVIConfig));if(readable)c=*input;*out=NULL;
 if(!readable||!cpu||!claim||c.version!=1||c.size!=sizeof(c)||!reservation_valid(&c.reservation)||!c.surface_epoch||!c.max_events||c.max_events>SATCHEL_GVI_EVENT_LIMIT||!c.timeout_ms||c.timeout_ms>SATCHEL_GVI_TIMEOUT_LIMIT_MS)return SATCHEL_GVI_INVALID;
 if(!enter())return SATCHEL_GVI_BUSY;unsigned slot=SATCHEL_GVI_OWNER_LIMIT;
 for(unsigned i=0;i<SATCHEL_GVI_OWNER_LIMIT;i++){if(!owners[i]){if(slot==SATCHEL_GVI_OWNER_LIMIT)slot=i;}else if(owners[i]->config.reservation.process_id==c.reservation.process_id&&owners[i]->config.reservation.generation==c.reservation.generation)return leave(SATCHEL_GVI_BUSY);}
 if(slot==SATCHEL_GVI_OWNER_LIMIT)return leave(SATCHEL_GVI_LIMIT);SatchelGuestVI*p=calloc(1,sizeof(*p));if(!p)return leave(SATCHEL_GVI_ALLOCATION);
 p->config=c;p->phase=SATCHEL_GVI_ACTIVE;
 if(!claim(context,cpu,&c.reservation)){free(p);return leave(SATCHEL_GVI_RESERVATION);}
 owners[slot]=p;*out=p;return leave(SATCHEL_GVI_OK);
}
static SatchelGuestVIStatus borrowed(SatchelGuestVI*p,const SatchelGuestVIBorrow*b,int surface){
 if(!aligned(b,_Alignof(SatchelGuestVIBorrow))||!b->cpu||!b->validate||!b->guard)return SATCHEL_GVI_INVALID;
 if(p->phase==SATCHEL_GVI_RETIRING)return SATCHEL_GVI_BUSY;
 if(b->cancelled&&b->cancelled(b->cancel_context))return SATCHEL_GVI_CANCELLED;
 if(!b->validate(b->validation_context,b->cpu,&p->config.reservation))return SATCHEL_GVI_RESERVATION;
 if(surface&&(p->phase!=SATCHEL_GVI_ACTIVE||b->surface_epoch!=p->config.surface_epoch))return SATCHEL_GVI_NO_SURFACE;
 return SATCHEL_GVI_OK;
}
static SatchelGuestVIStatus out_range(SatchelGuestVI*p,const SatchelGuestVIBorrow*b,uint64_t out){
 if(!out||out>UINT64_MAX-8||!satchel_cpu_probe_check_range(b->cpu,out,8,SATCHEL_CPU_WRITE))return SATCHEL_GVI_MEMORY;
 int overlap=1;if(!satchel_cpu_probe_physical_overlap(b->cpu,out,8,p->config.reservation.address,p->config.reservation.length,&overlap)||overlap)return SATCHEL_GVI_MEMORY;
 if(!b->guard(b->guard_context,b->cpu,out,8))return SATCHEL_GVI_MEMORY;return SATCHEL_GVI_OK;
}
static SatchelGuestVIStatus result(SatchelGuestVI*p,SatchelGuestVIResult*out,uint32_t op,SatchelGuestVIStatus s){*out=(SatchelGuestVIResult){s,op,p->events};return leave(s);}
SatchelGuestVIStatus satchel_guest_vi_execute(SatchelGuestVI*p,const SatchelGuestVIBorrow*b,uint32_t op,uint64_t x0,uint64_t x1,uint64_t trap,uint64_t lr,SatchelGuestVIResult*out){
 if(!aligned(out,_Alignof(SatchelGuestVIResult)))return SATCHEL_GVI_INVALID;if(!enter())return SATCHEL_GVI_BUSY;if(!valid(p))return leave(SATCHEL_GVI_INVALID);
 if(!aligned(b,_Alignof(SatchelGuestVIBorrow))||!b->cpu)return result(p,out,op,SATCHEL_GVI_INVALID);
 SatchelCPUProbeResult current=satchel_cpu_probe_run(b->cpu,0,NULL,NULL);if(current.reason!=SATCHEL_CPU_BUDGET||current.pc!=trap)return result(p,out,op,SATCHEL_GVI_INVALID);
 if(p->phase==SATCHEL_GVI_RETIRING)return result(p,out,op,SATCHEL_GVI_BUSY);
 if(p->events>=p->config.max_events)return result(p,out,op,SATCHEL_GVI_LIMIT);p->events++;
 uint64_t n=clock_ns(),span=(uint64_t)p->config.timeout_ms*1000000;if(n==UINT64_MAX||n>UINT64_MAX-span)return result(p,out,op,SATCHEL_GVI_CANCELLED);const uint64_t deadline=n+span;
 int output=op>=SATCHEL_GVI_OPEN_DEFAULT&&op<=SATCHEL_GVI_GET_NATIVE;
 SatchelGuestVIStatus s=borrowed(p,b,output);if(s)return result(p,out,op,s);
 if(op==SATCHEL_GVI_CREATE_SIZED||op==SATCHEL_GVI_CREATE_SETTINGS)return result(p,out,op,SATCHEL_GVI_UNSUPPORTED);
 if(op<SATCHEL_GVI_OPEN_DEFAULT||op>SATCHEL_GVI_CLOSE_DISPLAY)return result(p,out,op,SATCHEL_GVI_INVALID);
 uint64_t value=0;int issue=0;
 switch(op){
 case SATCHEL_GVI_OPEN_DEFAULT:if(p->display)return result(p,out,op,SATCHEL_GVI_BUSY);issue=1;break;
 case SATCHEL_GVI_CREATE_LAYER:if(!p->display||x1!=p->display)return result(p,out,op,SATCHEL_GVI_BAD_OBJECT);if(p->layer)return result(p,out,op,SATCHEL_GVI_BUSY);issue=1;break;
 case SATCHEL_GVI_GET_NATIVE:if(!p->layer||x1!=p->layer)return result(p,out,op,SATCHEL_GVI_BAD_OBJECT);if(p->native)value=p->native;else issue=1;break;
 case SATCHEL_GVI_DESTROY_LAYER:if(!p->layer||x0!=p->layer)return result(p,out,op,SATCHEL_GVI_BAD_OBJECT);if(references(p))return result(p,out,op,SATCHEL_GVI_BUSY);break;
 case SATCHEL_GVI_CLOSE_DISPLAY:if(!p->display||x0!=p->display)return result(p,out,op,SATCHEL_GVI_BAD_OBJECT);if(p->layer)return result(p,out,op,SATCHEL_GVI_BUSY);break;
 default:return result(p,out,op,SATCHEL_GVI_INVALID);
 }
 if(issue){if(p->issued>=SATCHEL_GVI_IDENTITY_BYTES/SATCHEL_GVI_IDENTITY_STRIDE)return result(p,out,op,SATCHEL_GVI_LIMIT);value=p->config.reservation.address+(uint64_t)p->issued*SATCHEL_GVI_IDENTITY_STRIDE;}
 if(output&&(s=out_range(p,b,x0)))return result(p,out,op,s);
 if(b->cancelled&&b->cancelled(b->cancel_context))return result(p,out,op,SATCHEL_GVI_CANCELLED);
 n=clock_ns();if(n==UINT64_MAX||n>=deadline)return result(p,out,op,n==UINT64_MAX?SATCHEL_GVI_CANCELLED:SATCHEL_GVI_LIMIT);
 if(!satchel_cpu_probe_set_pc(b->cpu,lr))return result(p,out,op,SATCHEL_GVI_BAD_RETURN);
 if(output){uint8_t bytes[8];for(unsigned i=0;i<8;i++)bytes[i]=(uint8_t)(value>>(8*i));SatchelCPUProbeWrite w={x0,bytes,8};SatchelCPUWriteResult written=satchel_cpu_probe_writev(b->cpu,&w,1);if(written!=SATCHEL_CPU_WRITE_OK){if(!satchel_cpu_probe_set_pc(b->cpu,trap))return result(p,out,op,SATCHEL_GVI_INTERNAL);return result(p,out,op,written==SATCHEL_CPU_WRITE_ALLOCATION?SATCHEL_GVI_ALLOCATION:SATCHEL_GVI_MEMORY);}}
 // Stopped CPU exclusive, no callbacks/allocations after guest output commit.
 if(issue)p->issued++;
 switch(op){case SATCHEL_GVI_OPEN_DEFAULT:p->display=value;p->display_gen=p->issued;break;
 case SATCHEL_GVI_CREATE_LAYER:p->layer=value;p->layer_gen=p->issued;break;
 case SATCHEL_GVI_GET_NATIVE:if(issue){p->native=value;p->native_gen=p->issued;}break;
 case SATCHEL_GVI_DESTROY_LAYER:p->layer=p->native=p->layer_gen=p->native_gen=0;break;
 case SATCHEL_GVI_CLOSE_DISPLAY:p->display=p->display_gen=0;break;default:break;}
 if(output&&!satchel_cpu_probe_set_x(b->cpu,0,0))return result(p,out,op,SATCHEL_GVI_INTERNAL);
 return result(p,out,op,SATCHEL_GVI_OK);
}
SatchelGuestVIStatus satchel_guest_vi_retain_native(SatchelGuestVI*p,const SatchelGuestVIBorrow*b,uint64_t native,uint64_t*reference,SatchelGuestVIInfo*out){
 if(!aligned(reference,_Alignof(uint64_t))||!aligned(out,_Alignof(SatchelGuestVIInfo)))return SATCHEL_GVI_INVALID;
 uintptr_t a=(uintptr_t)reference,z=(uintptr_t)out;if(a>UINTPTR_MAX-sizeof(*reference)||z>UINTPTR_MAX-sizeof(*out)||(a<z+sizeof(*out)&&z<a+sizeof(*reference)))return SATCHEL_GVI_INVALID;
 if(!enter())return SATCHEL_GVI_BUSY;if(!valid(p))return leave(SATCHEL_GVI_INVALID);SatchelGuestVIStatus s=borrowed(p,b,1);if(s)return leave(s);
 if(!native||native!=p->native)return leave(SATCHEL_GVI_BAD_OBJECT);unsigned slot=SATCHEL_GVI_REFERENCE_LIMIT;for(unsigned i=0;i<SATCHEL_GVI_REFERENCE_LIMIT;i++)if(!p->refs[i]){slot=i;break;}
 if(slot==SATCHEL_GVI_REFERENCE_LIMIT||!next_ref||next_ref==UINT64_MAX)return leave(SATCHEL_GVI_LIMIT);
 if(b->cancelled&&b->cancelled(b->cancel_context))return leave(SATCHEL_GVI_CANCELLED);
 SatchelGuestVIInfo info={1,sizeof(info),p->config.reservation.process_id,p->config.reservation.generation,p->config.surface_epoch,p->display,p->layer,p->native,p->display_gen,p->layer_gen,p->native_gen};
 uint64_t token=next_ref++;p->refs[slot]=token;*reference=token;*out=info;return leave(SATCHEL_GVI_OK);
}
SatchelGuestVIStatus satchel_guest_vi_release_native(SatchelGuestVI*p,uint64_t ref){if(!enter())return SATCHEL_GVI_BUSY;if(!valid(p)||!ref)return leave(SATCHEL_GVI_INVALID);for(unsigned i=0;i<SATCHEL_GVI_REFERENCE_LIMIT;i++)if(p->refs[i]==ref){p->refs[i]=0;return leave(SATCHEL_GVI_OK);}return leave(SATCHEL_GVI_BAD_OBJECT);}
SatchelGuestVIStatus satchel_guest_vi_detach(SatchelGuestVI*p,uint64_t epoch){if(!enter())return SATCHEL_GVI_BUSY;if(!valid(p))return leave(SATCHEL_GVI_INVALID);if(epoch!=p->config.surface_epoch)return leave(SATCHEL_GVI_NO_SURFACE);if(p->phase==SATCHEL_GVI_RETIRING)return leave(SATCHEL_GVI_BUSY);p->phase=SATCHEL_GVI_DETACHED;return leave(SATCHEL_GVI_OK);}
SatchelGuestVIStatus satchel_guest_vi_try_destroy(SatchelGuestVI**out){if(!aligned(out,_Alignof(SatchelGuestVI*)))return SATCHEL_GVI_INVALID;if(!enter())return SATCHEL_GVI_BUSY;SatchelGuestVI*p=*out;if(!p)return leave(SATCHEL_GVI_OK);if(!valid(p))return leave(SATCHEL_GVI_INVALID);p->phase=SATCHEL_GVI_RETIRING;if(references(p))return leave(SATCHEL_GVI_BUSY);for(unsigned i=0;i<SATCHEL_GVI_OWNER_LIMIT;i++)if(owners[i]==p)owners[i]=NULL;free(p);*out=NULL;return leave(SATCHEL_GVI_OK);}
SatchelGuestVIStatus satchel_guest_vi_stats(SatchelGuestVI*p,SatchelGuestVIStats*out){if(!aligned(out,_Alignof(SatchelGuestVIStats)))return SATCHEL_GVI_INVALID;if(!enter())return SATCHEL_GVI_BUSY;if(!valid(p))return leave(SATCHEL_GVI_INVALID);*out=(SatchelGuestVIStats){p->phase,p->display!=0,p->layer!=0,p->native!=0,references(p),p->issued,p->events};return leave(SATCHEL_GVI_OK);}
