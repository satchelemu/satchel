/* Direct wire/protocol tests of the same IPC implementation included in the VM.
 * Build separately; do not also link Core/Interpreter.c. */
#include "../Core/Interpreter.c"
#include <assert.h>

static void request(SatchelVM *vm,uint32_t handle,uint32_t command,const void *data,size_t size){
    uint8_t *p=vm->memory+SATCHEL_NRO_TLS;memset(p,0,256);
    h_w32(p,4);h_w32(p+4,(uint32_t)((size+16+16+3)/4));h_w32(p+16,0x49434653);h_w32(p+24,command);
    if(size)memcpy(p+32,data,size);vm->x[0]=handle;assert(horizon_ipc(vm,0x21));
}
int main(int argc,char **argv){
    assert(argc==2);FILE *file=fopen(argv[1],"rb");assert(file);assert(fseek(file,0,SEEK_END)==0);long len=ftell(file);assert(len>0);rewind(file);
    uint8_t *bytes=malloc((size_t)len);assert(bytes);assert(fread(bytes,1,(size_t)len,file)==(size_t)len);fclose(file);
    char error[256];SatchelVM *vm=satchel_create_nro(bytes,(size_t)len,error,sizeof(error));assert(vm);free(bytes);
    assert(!horizon_ipc(vm,0xFFFF));
    memcpy(vm->memory+SATCHEL_NRO_HEAP,"sm:",4);vm->x[1]=SATCHEL_NRO_HEAP;assert(horizon_ipc(vm,0x1F));assert(vm->x[0]==0);uint32_t sm=(uint32_t)vm->x[1];
    uint8_t name[8]={0};request(vm,sm,1,name,8);assert(h_u32(vm->memory+SATCHEL_NRO_TLS+24)==0x415);
    request(vm,sm,0,NULL,0);assert(!vm->fault&&vm->horizon.sm_initialized);
    memcpy(name,"hid",4);request(vm,sm,1,name,8);uint32_t hid=h_u32(vm->memory+SATCHEL_NRO_TLS+12);assert(h_object(vm,hid)->kind==H_HID);
    vm->x[0]=0xDEAD;horizon_ipc(vm,0x21);assert(vm->x[0]==0xE401);

    uint32_t shared=h_handle(vm,H_HID_MEMORY,0);vm->x[0]=shared;vm->x[1]=0x1000000;vm->x[2]=0x40000;vm->x[3]=1;horizon_ipc(vm,0x13);
    assert(vm->x[0]==0&&!vm->fault&&vm->permissions[0x1000000/4096]==1);
    vm->horizon.pendingbuttons=1;vm->horizon.inputbuttons=0;
    uint32_t event=h_handle(vm,H_APPLET_EVENT,0);h_w32(vm->memory+SATCHEL_NRO_HEAP,event);
    vm->x[1]=SATCHEL_NRO_HEAP;vm->x[2]=1;vm->x[3]=0;horizon_ipc(vm,0x18);assert(vm->x[0]==0xEA01);
    uint32_t input=0x1000000+0x9A00+8*0x5000+0x28+6*0x350+0x30;
    assert(h_u64(vm->memory+input)==1&&vm->horizon.pendingbuttons==0);
    vm->x[1]=SATCHEL_NRO_HEAP;vm->x[2]=1;horizon_ipc(vm,0x18);assert(h_u64(vm->memory+input)==0);
    assert(!access_ok(vm,0x1000000,4,true)&&vm->fault);vm->fault=false;

    vm->x[1]=SATCHEL_NRO_HEAP+0x1000;vm->x[2]=4096;vm->x[3]=0;horizon_ipc(vm,0x15);assert(vm->x[0]==0);uint32_t transfer=(uint32_t)vm->x[1];
    assert(vm->permissions[(SATCHEL_NRO_HEAP+0x1000)/4096]==0&&vm->memory_type[(SATCHEL_NRO_HEAP+0x1000)/4096]!=0);
    vm->x[0]=transfer;horizon_ipc(vm,0x16);assert(vm->x[0]==0&&vm->permissions[(SATCHEL_NRO_HEAP+0x1000)/4096]==3);

    HRequest r={0};r.raw_size=8;r.num_send=r.num_recv=1;r.send[0]=r.recv[0]=(HBuffer){SATCHEL_NRO_HEAP,40};
    h_w32(r.data,1);h_w32(r.data+4,0xC0080101);h_w32(vm->memory+SATCHEL_NRO_HEAP,4096);vm->horizon.devices[1].kind=H_NV_MAP;
    h_nv_ioctl(vm,&r);assert(!vm->fault);uint32_t map=h_u32(vm->memory+SATCHEL_NRO_HEAP+4);assert(map==1);
    uint8_t *allocation=vm->memory+SATCHEL_NRO_HEAP;memset(allocation,0,32);h_w32(allocation,map);h_w32(allocation+8,1);h_w32(allocation+12,4096);h_w64(allocation+24,SATCHEL_NRO_HEAP+0x3000);h_w32(r.data+4,0xC0200104);
    h_nv_ioctl(vm,&r);assert(!vm->fault&&vm->horizon.maps[map].allocated);
    vm->horizon.devices[2].kind=H_NV_AS_GPU;vm->horizon.devices[2].initialized=true;
    h_w32(r.data,2);h_w32(r.data+4,0xC0284106);memset(allocation,0,40);
    h_w32(allocation,5);h_w32(allocation+8,map);h_w32(allocation+12,65536);
    h_w64(allocation+24,65536);h_w64(allocation+32,0x100000000);
    h_nv_ioctl(vm,&r);assert(!vm->fault&&h_gpu_mapping(vm,0x100000000,65536));
    assert(vm->horizon.maps[map].size==4096); /* VA padding is not CPU backing */
    h_w64(allocation+24,131072);h_nv_ioctl(vm,&r);assert(vm->fault);vm->fault=false;
    h_w32(r.data,1);
    /* Independently encode the documented legacy tiled address formula. */
    for(unsigned y=0;y<8;y++)for(unsigned x=0;x<16;x++){
        unsigned sy=7-y;
        unsigned pos=((sy&127)/16+(x/16)*8+(sy/128)*(16/16*8))*1024;
        pos+=((sy%16)/8)*512+((x%16)/8)*256+((sy%8)/2)*64+((x%8)/4)*32+(sy%2)*16+(x%4)*4;
        uint8_t pixel[4]={(uint8_t)x,(uint8_t)y,123,17};memcpy(vm->memory+SATCHEL_NRO_HEAP+0x3000+pos,pixel,4);
    }
    SatchelHBuffer b={.configured=true,.width=16,.height=8,.stride=16,.format=1,.map=map,.size=4096};assert(h_present(vm,&b,2));
    assert(vm->horizon.frame_ready&&vm->horizon.frame_count==1);
    for(unsigned y=0;y<8;y++)for(unsigned x=0;x<16;x++){uint8_t *pixel=vm->horizon.pixels+(y*16+x)*4;assert(pixel[0]==x&&pixel[1]==y&&pixel[2]==123&&pixel[3]==255);}
    b.offset=4096;assert(!h_present(vm,&b,2)&&vm->fault);vm->fault=false;
    h_w32(r.data+4,0xC00801FF);h_nv_ioctl(vm,&r);assert(vm->fault&&strstr(satchel_error(vm),"NV ioctl not implemented"));vm->fault=false;
    uint8_t *tls=vm->memory+SATCHEL_NRO_TLS;memset(tls,0,256);h_w32(tls,4|(15u<<20));h_w32(tls+4,0x3FF);HRequest bad;assert(!h_parse(vm,&bad)&&vm->fault);
    satchel_destroy(vm);puts("PASS: Horizon IPC handles, shared memory, transfer permissions, latched input, NV maps, tiled CPU display, bounds and unsupported commands");return 0;
}
