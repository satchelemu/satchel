#include "GuestSymbols.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
static void check(int condition) { if (!condition) {fprintf(stderr,"guest symbol check %u failed\n",checks+1);exit(1);}++checks; }
typedef struct { uint8_t header[256],image[8192];uint64_t base;unsigned reads,fail_at,cancels,cancel_at; } Fixture;
static void put32(uint8_t *p,uint32_t x) {for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(x>>(i*8));}
static void put64(uint8_t *p,uint64_t x) {for(unsigned i=0;i<8;i++)p[i]=(uint8_t)(x>>(i*8));}
static int read_guest(void *context,uint64_t address,void *output,size_t size) {
    Fixture *f=context;
    if (++f->reads==f->fail_at || address<f->base || address-f->base>sizeof(f->image) || size>sizeof(f->image)-(size_t)(address-f->base)) return 0;
    memcpy(output,f->image+(size_t)(address-f->base),size);return 1;
}
static int cancelled(void *context) {Fixture *f=context;return ++f->cancels==f->cancel_at;}
static void init(Fixture *f) {
    memset(f,0,sizeof(*f));f->base=0x8000000;
    memcpy(f->header,"NSO0",4);put32(f->header+0x18,4096);put32(f->header+0x24,4096);put32(f->header+0x28,4096);
    const char names[]="\0nvnBootstrapLoader\0OtherPublicFunction\0";
    put32(f->header+0x90,0);put32(f->header+0x94,sizeof(names));put32(f->header+0x98,256);put32(f->header+0x9c,4*24);
    memcpy(f->image+4096,names,sizeof(names));
    uint8_t *sym=f->image+4096+256;
    put32(sym+24,1);sym[24+4]=0x12; /* undefined public NVN import */
    put32(sym+48,1);sym[48+4]=0x12;sym[48+6]=1;put64(sym+48+8,64);put64(sym+48+16,32);
    put32(sym+72,20);sym[72+4]=0x12;sym[72+6]=1;put64(sym+72+8,128);put64(sym+72+16,4);
}
static SatchelGuestSymbolStatus find(Fixture *f,SatchelGuestSymbol *out) {
    SatchelGuestSymbolTable table;
    SatchelGuestSymbolStatus status=satchel_guest_symbols_nso_table(f->header,sizeof(f->header),f->base,&table);
    if (status!=SATCHEL_GUEST_SYMBOL_FOUND) {memset(out,0,sizeof(*out));return status;}
    return satchel_guest_symbols_find_function(&table,read_guest,cancelled,f,"nvnBootstrapLoader",18,out);
}
int main(void) {
    Fixture f;SatchelGuestSymbol out;SatchelGuestSymbolTable table;
    init(&f);check(find(&f,&out)==SATCHEL_GUEST_SYMBOL_FOUND);
    check(out.address==f.base+64 && out.size==32 && out.symbol_index==2 && out.undefined_matches==1 && out.binding==1 && out.visibility==0);
    init(&f);f.base=0x12345000;check(find(&f,&out)==SATCHEL_GUEST_SYMBOL_FOUND && out.address==f.base+64);
    init(&f);put64(f.image+4096+256+48+16,0);check(find(&f,&out)==SATCHEL_GUEST_SYMBOL_FOUND && out.size==0);
    init(&f);f.image[4096+256+48+4]=0x22;f.image[4096+256+48+5]=3;check(find(&f,&out)==SATCHEL_GUEST_SYMBOL_FOUND && out.binding==2 && out.visibility==3);
    init(&f);put32(f.image+4096+256+48,20);check(find(&f,&out)==SATCHEL_GUEST_SYMBOL_UNDEFINED && out.address==0 && out.undefined_matches==1);
    init(&f);put32(f.image+4096+256+24,20);put32(f.image+4096+256+48,20);check(find(&f,&out)==SATCHEL_GUEST_SYMBOL_NOT_FOUND && out.address==0);
    init(&f);memcpy(f.image+4096+256+72,f.image+4096+256+48,24);check(find(&f,&out)==SATCHEL_GUEST_SYMBOL_AMBIGUOUS && out.address==0);
    for(unsigned bind=0;bind<16;bind++) if(bind!=1 && bind!=2) {init(&f);f.image[4096+256+48+4]=(uint8_t)((bind<<4)|2);check(find(&f,&out)==SATCHEL_GUEST_SYMBOL_UNSUPPORTED);}
    for(unsigned type=0;type<16;type++) if(type!=2) {init(&f);f.image[4096+256+48+4]=(uint8_t)(0x10|type);check(find(&f,&out)==SATCHEL_GUEST_SYMBOL_UNSUPPORTED);}
    for(unsigned visibility=1;visibility<=2;visibility++) {init(&f);f.image[4096+256+48+5]=(uint8_t)visibility;check(find(&f,&out)==SATCHEL_GUEST_SYMBOL_UNSUPPORTED);}
    init(&f);f.image[4096+256+48+7]=0xff;check(find(&f,&out)==SATCHEL_GUEST_SYMBOL_UNSUPPORTED);
    for(unsigned value=0;value<4;value++) {init(&f);put64(f.image+4096+256+48+8,4093+value);check(find(&f,&out)==SATCHEL_GUEST_SYMBOL_INVALID && !out.address);}
    init(&f);put64(f.image+4096+256+48+16,UINT64_MAX);check(find(&f,&out)==SATCHEL_GUEST_SYMBOL_INVALID);
    init(&f);put32(f.image+4096+256+48,0xffffffff);check(find(&f,&out)==SATCHEL_GUEST_SYMBOL_INVALID);
    init(&f);f.image[4096]=1;check(find(&f,&out)==SATCHEL_GUEST_SYMBOL_INVALID);
    init(&f);f.image[4096+256]=1;check(find(&f,&out)==SATCHEL_GUEST_SYMBOL_INVALID);
    for(unsigned i=1;i<=8;i++) {init(&f);f.fail_at=i;check(find(&f,&out)==SATCHEL_GUEST_SYMBOL_MEMORY_FAULT && !out.address);}
    for(unsigned i=1;i<=4;i++) {init(&f);f.cancel_at=i;check(find(&f,&out)==SATCHEL_GUEST_SYMBOL_CANCELLED && !out.address);}
    init(&f);check(satchel_guest_symbols_nso_table(f.header,255,f.base,&table)==SATCHEL_GUEST_SYMBOL_INVALID);
    check(satchel_guest_symbols_nso_table(f.header,256,UINT64_MAX-4095,&table)==SATCHEL_GUEST_SYMBOL_INVALID);
    check(satchel_guest_symbols_nso_table(f.header,256,f.base+1,&table)==SATCHEL_GUEST_SYMBOL_INVALID);
    init(&f);put32(f.header+0x98,0);check(find(&f,&out)==SATCHEL_GUEST_SYMBOL_INVALID); /* table overlap */
    init(&f);put32(f.header+0x9c,25);check(find(&f,&out)==SATCHEL_GUEST_SYMBOL_INVALID);
    init(&f);put32(f.header+0x90,4095);check(find(&f,&out)==SATCHEL_GUEST_SYMBOL_INVALID);
    init(&f);put32(f.header+0x94,0);put32(f.header+0x9c,0);check(find(&f,&out)==SATCHEL_GUEST_SYMBOL_NOT_FOUND);
    init(&f);check(satchel_guest_symbols_nso_table(f.header,256,f.base,&table)==SATCHEL_GUEST_SYMBOL_FOUND);
    check(satchel_guest_symbols_find_function(&table,read_guest,0,&f,"nvn\0BootstrapLoader",18,&out)==SATCHEL_GUEST_SYMBOL_INVALID);
    check(satchel_guest_symbols_find_function(&table,read_guest,0,&f,"",0,&out)==SATCHEL_GUEST_SYMBOL_INVALID);
    check(satchel_guest_symbols_find_function(&table,0,0,&f,"a",1,&out)==SATCHEL_GUEST_SYMBOL_INVALID);
    check(satchel_guest_symbols_find_function(&table,read_guest,0,&f,"a",256,&out)==SATCHEL_GUEST_SYMBOL_INVALID);
    // Hostile structural input sweep; callback guards all guest spans.
    uint32_t random=0x8391;
    for(unsigned i=0;i<2000;i++) {
        init(&f);random=random*1664525u+1013904223u;
        const unsigned locations[]={0,4,8,12,0x14,0x18,0x24,0x28,0x90,0x94,0x98,0x9c};
        put32(f.header+locations[i%12],random);
        const SatchelGuestSymbolStatus status=find(&f,&out);
        check(status>=SATCHEL_GUEST_SYMBOL_FOUND && status<=SATCHEL_GUEST_SYMBOL_CANCELLED);
        if(status==SATCHEL_GUEST_SYMBOL_FOUND) check(out.address>=f.base && out.address-f.base<4096);
    }
    printf("Guest symbol resolver: %u ASan/UBSan checks passed (original synthetic metadata only).\n",checks);
}
