#include "SatchelShaderFixture.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
static void check_at(int value,unsigned line) { checks++; if(!value){fprintf(stderr,"Original shader fixture assertion %u failed at line %u.\n",checks,line);exit(1);} }
#define check(value) check_at((value),__LINE__)
static void unchanged(const void *p,size_t n) {const uint8_t *s=p;for(size_t j=0;j<n;j++)if(s[j]!=0xa5){check(0);return;}check(1);}
/* Independently assemble original instructions from pinned InstDecoders fields.
 * No words from any game or generated game artifact are used. */
static uint64_t ldc(unsigned rd, unsigned bytes, unsigned offset) {
    return UINT64_C(0xef90000000000000) | ((uint64_t)(bytes==8?5:4)<<48) |
        (UINT64_C(3)<<36) | ((uint64_t)offset<<20) | (UINT64_C(7)<<16) |
        (UINT64_C(255)<<8) | rd;
}
static void encode(uint64_t words[8]) {
    words[0]=0; words[1]=ldc(0,8,0); words[2]=ldc(2,4,12); words[3]=ldc(3,4,16);
    words[4]=0;
    words[5]=UINT64_C(0x5c10000000000000)|(UINT64_C(3)<<20)|(UINT64_C(7)<<16)|(UINT64_C(2)<<8)|4;
    words[6]=UINT64_C(0xeed8000000000000)|(UINT64_C(4)<<48)|(UINT64_C(1)<<45)|(UINT64_C(7)<<16)|4;
    words[7]=UINT64_C(0xe300000000000000)|(UINT64_C(7)<<16)|15;
}
static void rejected(SatchelShaderFixtureInput *input) {
    uint8_t out[2048];SatchelShaderFixtureReflection reflection;
    memset(out,0xa5,sizeof(out));memset(&reflection,0xa5,sizeof(reflection));
    check(satchel_shader_fixture_compile(input,out,sizeof(out),&reflection)!=SATCHEL_SHADER_FIXTURE_OK);
    unchanged(out,sizeof(out));unchanged(&reflection,sizeof(reflection));
}
static void rejected_spirv(SatchelShaderFixtureInput *input,const uint32_t *raw,uint32_t n) {
    uint8_t out[2048];SatchelShaderFixtureReflection reflection;
    memset(out,0xa5,sizeof(out));memset(&reflection,0xa5,sizeof(reflection));
    check(satchel_shader_fixture_test_normalize(input,(const uint8_t*)raw,n,out,sizeof(out),&reflection)!=SATCHEL_SHADER_FIXTURE_OK);
    unchanged(out,sizeof(out));unchanged(&reflection,sizeof(reflection));
}
static size_t instruction(const uint32_t *words,size_t n,unsigned opcode,unsigned nth) {
    for(size_t j=5;j<n;){unsigned length=words[j]>>16;check(length && length<=n-j);
        if((words[j]&65535)==opcode && nth--==0)return j;
        j+=length;
    }check(0);return 0;
}
int main(int argc,char **argv) {
    uint64_t words[8]; encode(words);
    uint32_t cb[8]={0x10000,0,16,5,7,0,0,0};
    SatchelShaderFixtureInput input={.version=1,.struct_size=sizeof(input),.code=(const uint8_t *)words,
        .constant_buffer=(const uint8_t *)cb,.code_size=64,.constant_buffer_size=32,
        .local_size_x=1,.local_size_y=1,.local_size_z=1,.constant_buffer_slot=3,
        .uniform_binding=1,.storage_binding=2,.storage_size=16,.storage_offset_alignment=16,.storage_gpu_address=0x10000};
    uint8_t out[65536],baseline[65536];uint32_t raw[4096],mutated[4096];SatchelShaderFixtureReflection reflection;
    check(satchel_shader_fixture_test_log()==0);
    int raw_size=satchel_shader_fixture_test_raw(&input,(uint8_t*)raw,sizeof(raw));
    check(raw_size>0 && raw_size<(int)sizeof(raw));
    check(satchel_shader_fixture_compile(&input,out,sizeof(out),&reflection)==SATCHEL_SHADER_FIXTURE_OK);
    uint32_t n=reflection.spirv_size;check(n>=20 && n<sizeof(out));memcpy(baseline,out,n);
    check(reflection.version==1 && reflection.struct_size==sizeof(reflection));
    check(reflection.buffer_count==3 && reflection.active_buffer_count==2 && reflection.required_capability==1);
    check(reflection.local_size_x==1 && reflection.local_size_y==1 && reflection.local_size_z==1);
    check(reflection.buffers[0].binding==0 && reflection.buffers[0].flags==0 && reflection.buffers[0].required_bytes==0);
    check(reflection.buffers[1].kind==SATCHEL_SHADER_FIXTURE_UNIFORM && reflection.buffers[1].binding==1 &&
        reflection.buffers[1].slot==3 && reflection.buffers[1].flags==SATCHEL_SHADER_FIXTURE_ACTIVE && reflection.buffers[1].required_bytes==32);
    check(reflection.buffers[2].kind==SATCHEL_SHADER_FIXTURE_STORAGE && reflection.buffers[2].binding==2 &&
        reflection.buffers[2].address_cb_slot==3 && reflection.buffers[2].address_cb_byte_offset==0 &&
        reflection.buffers[2].flags==(SATCHEL_SHADER_FIXTURE_ACTIVE|SATCHEL_SHADER_FIXTURE_WRITE) && reflection.buffers[2].required_bytes==4);
    unsigned capabilities=0;for(size_t j=5;j<n/4;){uint32_t first;memcpy(&first,out+j*4,4);unsigned length=first>>16;
        check(length && length<=n/4-j);if((first&65535)==17){uint32_t cap;memcpy(&cap,out+j*4+4,4);check(cap==1);capabilities++;}j+=length;}
    check(capabilities==1 && (uint32_t)raw_size==n+7*8);
    // Caller data must not be embedded/specialized into the program. These bit
    // patterns will be exercised by the independently checked SPIR-V evaluator.
    const uint32_t parameters[][2]={{0,0},{5,7},{UINT32_MAX,1},{0x80000000,0x80000000},{0xdeadbeef,0x11223344}};
    for(size_t j=0;j<sizeof(parameters)/sizeof(parameters[0]);j++){
        cb[3]=parameters[j][0];cb[4]=parameters[j][1];
        check(satchel_shader_fixture_compile(&input,out,sizeof(out),&reflection)==SATCHEL_SHADER_FIXTURE_OK);
        check(reflection.spirv_size==n && memcmp(out,baseline,n)==0);
    }
    cb[3]=5;cb[4]=7;
    input.uniform_binding=5;input.storage_binding=7;
    check(satchel_shader_fixture_compile(&input,out,sizeof(out),&reflection)==SATCHEL_SHADER_FIXTURE_OK);
    check(reflection.buffers[1].binding==5 && reflection.buffers[2].binding==7);
    input.uniform_binding=1;input.storage_binding=2;
    // Every Maxwell bit, including scheduler/padding bits, is admission-checked.
    for(unsigned j=0;j<64*8;j++){words[j/64]^=UINT64_C(1)<<(j%64);rejected(&input);words[j/64]^=UINT64_C(1)<<(j%64);}
    SatchelShaderFixtureInput original=input;
#define BAD(field,value) do{input=original;input.field=(value);rejected(&input);}while(0)
    BAD(version,0);BAD(struct_size,sizeof(input)-1);BAD(code,NULL);BAD(constant_buffer,NULL);
    BAD(code_size,0);BAD(code_size,63);BAD(code_size,65);BAD(constant_buffer_size,31);BAD(constant_buffer_size,33);
    BAD(local_size_x,0);BAD(local_size_x,2);BAD(local_size_y,2);BAD(local_size_z,2);BAD(local_memory_size,4);BAD(shared_memory_size,4);
    BAD(constant_buffer_slot,2);BAD(uniform_binding,0);BAD(uniform_binding,2);BAD(uniform_binding,8);BAD(storage_binding,0);BAD(storage_binding,8);
    BAD(storage_size,0);BAD(storage_size,15);BAD(storage_size,17);BAD(storage_size,4097);
    BAD(storage_offset_alignment,0);BAD(storage_offset_alignment,3);BAD(storage_offset_alignment,131072);
    BAD(storage_gpu_address,0);BAD(storage_gpu_address,0x10001);BAD(storage_gpu_address,UINT64_C(1)<<40);
    BAD(reserved[0],1);BAD(reserved[3],1);input=original;
    cb[0]^=16;rejected(&input);cb[0]^=16;cb[2]=32;rejected(&input);cb[2]=16;cb[7]=1;rejected(&input);cb[7]=0;
    memset(out,0xa5,sizeof(out));memset(&reflection,0xa5,sizeof(reflection));
    check(satchel_shader_fixture_compile(&input,out,n-1,&reflection)==SATCHEL_SHADER_FIXTURE_CAPACITY);
    unchanged(out,sizeof(out));unchanged(&reflection,sizeof(reflection));
    check(satchel_shader_fixture_compile(&input,out,0,&reflection)==SATCHEL_SHADER_FIXTURE_CAPACITY);
    unchanged(out,sizeof(out));unchanged(&reflection,sizeof(reflection));
    check(satchel_shader_fixture_compile(&input,out,2048,(SatchelShaderFixtureReflection*)(out+16))==SATCHEL_SHADER_FIXTURE_INVALID);
    unchanged(out,sizeof(out));
    // Prove input snapshotting: caller output may overwrite original code or CB
    // storage only after validation/translation. Reflection output is separate.
    memcpy(out,words,64);input.code=out;
    check(satchel_shader_fixture_compile(&input,out,sizeof(out),&reflection)==SATCHEL_SHADER_FIXTURE_OK);
    check(memcmp(out,baseline,n)==0);input=original;
    memcpy(out,cb,32);input.constant_buffer=out;
    check(satchel_shader_fixture_compile(&input,out,sizeof(out),&reflection)==SATCHEL_SHADER_FIXTURE_OK);
    check(memcmp(out,baseline,n)==0);input=original;
    size_t rn=(size_t)raw_size/4;
#define MUTATE(index,value) do{memcpy(mutated,raw,(size_t)raw_size);mutated[(index)]=(value);check(mutated[(index)]!=raw[(index)]);rejected_spirv(&input,mutated,(uint32_t)raw_size);}while(0)
    MUTATE(0,0);MUTATE(1,0x10400);MUTATE(3,129);MUTATE(3,1);MUTATE(4,1);
    size_t at=instruction(raw,rn,17,0);MUTATE(at+1,10); // Float64
    at=instruction(raw,rn,17,1);MUTATE(at+1,999); // Unknown capability
    at=instruction(raw,rn,21,0);MUTATE(at+2,64);MUTATE(at+3,2);
    at=instruction(raw,rn,22,0);MUTATE(at+2,64);
    at=instruction(raw,rn,32,0);MUTATE(at+2,4); // Workgroup pointer
    at=instruction(raw,rn,128,0);MUTATE(at,(raw[at]&0xffff0000)|333); // Subgroup op
    MUTATE(at,(raw[at]&0xffff0000)|129); // Floating arithmetic
    MUTATE(at+2,raw[3]);MUTATE(at+3,0); // Out-of-bound result / invalid source ID
    at=instruction(raw,rn,61,0);MUTATE(at,(raw[at]&0xffff0000)|98); // Image read
    at=instruction(raw,rn,23,0);MUTATE(at,(raw[at]&0xffff0000)|25); // Image type
    at=instruction(raw,rn,5,0);MUTATE(at,(raw[at]&0xffff0000)|10); // Extension
    at=instruction(raw,rn,65,0);MUTATE(at+5,raw[instruction(raw,rn,43,0)+2]); // SSBO index8 -> rejected
    at=instruction(raw,rn,65,1);MUTATE(at+6,raw[instruction(raw,rn,43,5)+2]); // CB byte4 instead of byte12
    at=instruction(raw,rn,62,0);MUTATE(at+1,raw[instruction(raw,rn,65,1)+2]); // Attempt UBO write
    at=instruction(raw,rn,16,0);MUTATE(at+3,2); // Workgroup size mutation
    // Truncate at every byte and corrupt each instruction length independently.
    for(uint32_t j=0;j<(uint32_t)raw_size;j++)rejected_spirv(&input,raw,j);
    for(size_t j=5;j<rn;j+=raw[j]>>16){MUTATE(j,raw[j]&65535);MUTATE(j,(65535u<<16)|(raw[j]&65535));}
    // Appended forbidden operations/types/capabilities are not ignored.
    const unsigned forbidden[]={10,25,224,333,17};
    for(size_t j=0;j<sizeof(forbidden)/sizeof(forbidden[0]);j++){
        memcpy(mutated,raw,(size_t)raw_size);mutated[rn]=(2u<<16)|forbidden[j];mutated[rn+1]=1;
        rejected_spirv(&input,mutated,(uint32_t)raw_size+8);
    }
    // A supported-opcode mutation that changes the actual result must also fail.
    at=instruction(raw,rn,128,0);MUTATE(at+4,raw[at+3]); // A+A, not A+B
    check(satchel_shader_fixture_compile(&input,out,sizeof(out),&reflection)==SATCHEL_SHADER_FIXTURE_OK);
    check(memcmp(out,baseline,n)==0);
    if(argc==2){FILE*f=fopen(argv[1],"wb");if(!f)return 2;int bad=fwrite(out,1,n,f)!=n;if(fclose(f)||bad)return 3;}
    printf("Original-only Maxwell compute facade: %u checks passed; %u normalized SPIR-V bytes, two active buffers, no GPU execution claim.\n",checks,n);
    return 0;
}
