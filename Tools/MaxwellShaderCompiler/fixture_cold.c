/* Fresh-process production ABI check: no test exports or warm-up calls. */
#include "SatchelShaderFixture.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
static void check(int value) { checks++; if (!value) { fprintf(stderr,"Cold shader assertion %u failed.\n",checks); exit(1); } }
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
int main(int argc,char **argv) {
    uint64_t words[8];encode(words);
    uint32_t cb[8]={0x10000,0,16,5,7,0,0,0};
    SatchelShaderFixtureInput input={.version=1,.struct_size=sizeof(input),.code=(const uint8_t *)words,
        .constant_buffer=(const uint8_t *)cb,.code_size=64,.constant_buffer_size=32,
        .local_size_x=1,.local_size_y=1,.local_size_z=1,.constant_buffer_slot=3,
        .uniform_binding=1,.storage_binding=2,.storage_size=16,.storage_offset_alignment=16,
        .storage_gpu_address=0x10000};
    uint8_t output[16384],second[16384];SatchelShaderFixtureReflection reflection,again;
    /* This must remain the first call into any managed export. */
    check(satchel_shader_fixture_compile(&input,output,sizeof(output),&reflection)==SATCHEL_SHADER_FIXTURE_OK);
    check(reflection.version==1 && reflection.struct_size==sizeof(reflection));
    check(reflection.spirv_size>=20 && reflection.spirv_size<=sizeof(output));
    uint32_t magic;memcpy(&magic,output,4);check(magic==0x07230203);
    check(reflection.buffer_count==3 && reflection.active_buffer_count==2 && reflection.required_capability==1);
    check(reflection.buffers[1].binding==1 && reflection.buffers[1].required_bytes==32);
    check(reflection.buffers[2].binding==2 && reflection.buffers[2].required_bytes==4);
    /* A rejected call releases the gate, preserves outputs, then valid reuse succeeds. */
    memset(second,0xa5,sizeof(second));memset(&again,0xa5,sizeof(again));input.version=0;
    check(satchel_shader_fixture_compile(&input,second,sizeof(second),&again)==SATCHEL_SHADER_FIXTURE_INVALID);
    for(unsigned i=0;i<sizeof(second);i++)check(second[i]==0xa5);
    for(unsigned i=0;i<sizeof(again);i++)check(((const uint8_t *)&again)[i]==0xa5);
    input.version=1;cb[3]=0x7f800001;cb[4]=0x00000001;
    check(satchel_shader_fixture_compile(&input,second,sizeof(second),&again)==SATCHEL_SHADER_FIXTURE_OK);
    check(again.spirv_size==reflection.spirv_size && memcmp(second,output,reflection.spirv_size)==0);
    if(argc==2){FILE *f=fopen(argv[1],"wb");check(f!=NULL);check(fwrite(output,1,reflection.spirv_size,f)==reflection.spirv_size);check(fclose(f)==0);}
    printf("Cold production shader: %u checks passed.\n",checks);return 0;
}
