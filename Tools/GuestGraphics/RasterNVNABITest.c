#include "GuestRasterPrograms.h"
#include "Fixtures.h"
#include "nvn.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Independent pinned public NVN enum, not a second copy of our own mapping. */
_Static_assert((int)SATCHEL_RASTER_NVN_VERTEX_BIT==(int)NVN_SHADER_STAGE_VERTEX_BIT,"vertex NVN bit mismatch");
_Static_assert((int)SATCHEL_RASTER_NVN_FRAGMENT_BIT==(int)NVN_SHADER_STAGE_FRAGMENT_BIT,"fragment NVN bit mismatch");
_Static_assert((int)SATCHEL_GRP_NVN_FRAGMENT_BIT==(int)NVN_SHADER_STAGE_FRAGMENT_BIT,"owner fragment bit mismatch");
_Static_assert(NVN_SHADER_STAGE_FRAGMENT==1 && SATCHEL_GRP_SPH_FRAGMENT==5,"distinct stage namespaces");
_Static_assert((SATCHEL_GRP_NVN_VERTEX_BIT|SATCHEL_GRP_NVN_FRAGMENT_BIT)==3,"paired NVN mask");
static unsigned checks;
#define CHECK(v) do{checks++;if(!(v)){fprintf(stderr,"NVN ABI assertion line%d\n",__LINE__);exit(1);}}while(0)
int main(void){
    SatchelRasterAdmission results[2];
    for(unsigned i=0;i<2;i++){
        unsigned stage=i?SATCHEL_RASTER_FRAGMENT:SATCHEL_RASTER_VERTEX;RasterFixture f;fixture_init(&f,stage);
        if(!i){fixture_ald(&f,0,0,0,4);fixture_mov(&f,4,0);fixture_ast(&f,0,0,4);}else for(unsigned j=0;j<4;j++)fixture_imm(&f,j,j==3?0x3f800000:0);
        fixture_exit(&f);SatchelRasterStageInput full=fixture_input(&f,stage);
        SatchelRasterCodeInput input={1,sizeof(input),stage,full.sph,full.sph_size,full.code,full.code_size};
        CHECK(satchel_raster_admit(&input,results+i)==SATCHEL_RASTER_OK);
        CHECK(results[i].nvn_stage_bit==(uint32_t)(i?NVN_SHADER_STAGE_FRAGMENT_BIT:NVN_SHADER_STAGE_VERTEX_BIT));
        CHECK(results[i].reflection.stage==stage && !results[i].reflection.spirv_words);
        /* Feeding the NVN ordinal in place of the actual SPH number rejects. */
        SatchelRasterAdmission previous=results[i];input.stage=i?NVN_SHADER_STAGE_FRAGMENT:NVN_SHADER_STAGE_VERTEX;
        CHECK(satchel_raster_admit(&input,results+i)!=SATCHEL_RASTER_OK && !memcmp(results+i,&previous,sizeof(previous)));
    }
    CHECK((results[0].nvn_stage_bit|results[1].nvn_stage_bit)==(NVN_SHADER_STAGE_VERTEX_BIT|NVN_SHADER_STAGE_FRAGMENT_BIT));
    CHECK(results[1].nvn_stage_bit!=(uint32_t)NVN_SHADER_STAGE_TESS_EVALUATION_BIT);
    printf("PASS %u independent pinned NVN stage ABI assertions\n",checks);return 0;
}
