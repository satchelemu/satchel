/* Direct SVC register-boundary tests of the same implementation used by guests. */
#include "../Core/Interpreter.c"
#include <assert.h>

static SatchelVM *sample(const char *path) {
    FILE *f=fopen(path,"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);
    uint8_t*b=malloc((size_t)n);assert(b);assert(fread(b,1,(size_t)n,f)==(size_t)n);fclose(f);
    char error[256];SatchelVM*v=satchel_create_nro(b,(size_t)n,error,sizeof error);free(b);assert(v);return v;
}
static uint64_t invoke(SatchelVM*v,unsigned svc,uint64_t a,uint64_t b,uint64_t c,uint64_t d){
    v->x[0]=a;v->x[1]=b;v->x[2]=c;v->x[3]=d;assert(horizon_kernel(v,svc));assert(!v->fault);return v->x[0];
}
int main(int argc,char**argv){
    assert(argc==2);SatchelVM*v=sample(argv[1]);
    assert(read_memory(v,SATCHEL_NRO_CONFIG+8,8)==0x80); /* a real handle, no mutex flag bits */
    uint32_t sleep_op=0xD4000161;memcpy(v->memory+SATCHEL_NRO_BASE,&sleep_op,4);
    v->x[0]=50000000;assert(satchel_frame(v,0,0,1)==SATCHEL_WAIT);
    uint64_t before=v->instructions;assert(satchel_frame(v,0,0,1)==SATCHEL_WAIT);assert(v->instructions==before);
    v->sleep_until=0;
    assert(invoke(v,0x29,0,12,0xFFFF8001,0)==0 && v->x[1]==SATCHEL_NRO_BASE);
    assert(invoke(v,0x29,0,5,0xFFFF8001,0)==0 && v->x[1]==SATCHEL_NRO_HEAP_SIZE);
    assert(invoke(v,0x29,0,12,0,0)==0xE401);
    assert(invoke(v,0x29,0,19,0,0)==0xF001);
    assert(invoke(v,2,SATCHEL_NRO_HEAP+1,4096,1,0)==0xCC01);
    assert(invoke(v,2,SATCHEL_NRO_HEAP,0,1,0)==0xCA01);
    assert(invoke(v,2,SATCHEL_NRO_HEAP,4096,5,0)==0xD801);
    assert(invoke(v,2,SATCHEL_NRO_CONFIG,4096,3,0)==0xD401);
    assert(invoke(v,2,SATCHEL_NRO_BASE,4096,3,0)==0xD401);
    assert(invoke(v,2,SATCHEL_NRO_HEAP,UINT64_MAX-4095,1,0)==0xD401);
    assert(invoke(v,2,SATCHEL_NRO_HEAP,4096,0,0)==0);
    assert(!v->permissions[SATCHEL_NRO_HEAP/4096]);
    assert(invoke(v,6,SATCHEL_NRO_STACK,0,SATCHEL_NRO_HEAP,0)==0);
    assert(read_memory(v,SATCHEL_NRO_STACK,8)==SATCHEL_NRO_HEAP);
    assert(read_memory(v,SATCHEL_NRO_STACK+8,8)==4096);
    assert(read_memory(v,SATCHEL_NRO_STACK+16,4)!=0); /* no-access is not unmapped */
    assert(invoke(v,2,SATCHEL_NRO_HEAP,4096,3,0)==0); /* restore allowed */
    write_memory(v,SATCHEL_NRO_HEAP,8,0x12345678);assert(!v->fault);
    assert(read_memory(v,SATCHEL_NRO_HEAP,8)==0x12345678);
    assert(invoke(v,3,SATCHEL_NRO_HEAP,4096,8,8)==0);
    assert(invoke(v,6,SATCHEL_NRO_STACK,0,SATCHEL_NRO_HEAP,0)==0);
    assert(read_memory(v,SATCHEL_NRO_STACK+20,4)==8);
    assert(invoke(v,3,SATCHEL_NRO_HEAP,4096,8,0)==0);
    assert(invoke(v,2,SATCHEL_NRO_HEAP,4096,1,0)==0);
    write_memory(v,SATCHEL_NRO_HEAP,4,99);assert(v->fault);satchel_destroy(v);
    puts("PASS: Horizon GetInfo/QueryMemory, permission changes and restore, immutable code/config, alignment/overflow/results, memory attributes, protected writes");
}
