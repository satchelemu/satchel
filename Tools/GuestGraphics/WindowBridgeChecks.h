/* Original refusal/reentry assertions shared by synthetic and real Process tests. */
static SatchelGuestDispatch *bridge_d;static SatchelCPUProbeResult bridge_stop;static SatchelGuestWindowBorrow bridge_borrow;static unsigned bridge_nested;
static int bridge_context(void*context,SatchelCPUProbe*cpu,uint64_t process,uint64_t generation){
 SatchelGuestDispatchReport r={99,99,99};bridge_nested++;
 CHECK(satchel_guest_dispatch_handle_windows(bridge_d,cpu,&bridge_stop,&bridge_borrow,&r)==SATCHEL_GUEST_DISPATCH_RESOURCE_BUSY);
 CHECK(r.status==SATCHEL_GUEST_DISPATCH_RESOURCE_BUSY&&!r.operation_id&&!r.events);
 return wi_context(context,cpu,process,generation);
}
static void bridge_checks(Interpreted*f){
 SatchelCPUProbeResult stop=wi_start(f,"nvnWindowGetNativeWindow",WI_OBJECT,0,0,0,0);SatchelGuestWindowBorrow b=wi_borrow;SatchelGuestDispatchReport r={99,99,99};uint64_t value;
 CHECK(satchel_guest_dispatch_handle_windows(f->dispatch,f->cpu,&stop,NULL,&r)==SATCHEL_GUEST_DISPATCH_INVALID);CHECK(r.status==SATCHEL_GUEST_DISPATCH_INVALID&&!r.events&&!r.operation_id);
 b.context_generation++;r=(SatchelGuestDispatchReport){99,99,99};CHECK(satchel_guest_dispatch_handle_windows(f->dispatch,f->cpu,&stop,&b,&r)==SATCHEL_GUEST_DISPATCH_INVALID);CHECK(r.status==SATCHEL_GUEST_DISPATCH_INVALID&&!r.events&&!r.operation_id);
 b=wi_borrow;b.return_pc+=4;CHECK(satchel_guest_dispatch_handle_windows(f->dispatch,f->cpu,&stop,&b,&r)==SATCHEL_GUEST_DISPATCH_INVALID);
 b=wi_borrow;b.return_pc=WI_OUTPUT;CHECK(satchel_cpu_probe_set_x(f->cpu,30,WI_OUTPUT));CHECK(satchel_guest_dispatch_handle_windows(f->dispatch,f->cpu,&stop,&b,&r)==SATCHEL_GUEST_DISPATCH_BAD_RETURN);CHECK(r.status==SATCHEL_GUEST_DISPATCH_BAD_RETURN&&!r.events);CHECK(satchel_cpu_probe_set_x(f->cpu,30,wi_borrow.return_pc));
 _Alignas(8) unsigned char bytes[sizeof(r)+8];memset(bytes,0xa5,sizeof(bytes));CHECK(satchel_guest_dispatch_handle_windows(f->dispatch,f->cpu,&stop,&wi_borrow,(SatchelGuestDispatchReport*)(void*)(bytes+1))==SATCHEL_GUEST_DISPATCH_INVALID);for(unsigned i=0;i<sizeof(bytes);i++)CHECK(bytes[i]==0xa5);
 CHECK(satchel_cpu_probe_get_x(f->cpu,0,&value)&&value==WI_OBJECT);CHECK(satchel_cpu_probe_run(f->cpu,0,NULL,NULL).pc==stop.pc);
 bridge_d=f->dispatch;bridge_stop=stop;bridge_borrow=wi_borrow;bridge_borrow.validate_context=bridge_context;bridge_nested=0;
 CHECK(satchel_guest_dispatch_handle_windows(f->dispatch,f->cpu,&stop,&bridge_borrow,&r)==SATCHEL_GUEST_DISPATCH_RETURNED);CHECK(r.status==SATCHEL_GUEST_DISPATCH_RETURNED&&bridge_nested);CHECK(satchel_cpu_probe_get_x(f->cpu,0,&value)&&value!=WI_OBJECT);SatchelCPUProbeResult after=satchel_cpu_probe_run(f->cpu,8,NULL,NULL);CHECK(after.reason==SATCHEL_CPU_SVC&&after.svc==0x42);
}
