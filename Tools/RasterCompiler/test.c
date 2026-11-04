#include "RasterCompiler.h"
#include "Fixtures.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"CHECK failed line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
/* Independent small SPIR-V evaluator for this resource-free float-bit profile.
 * It follows emitted typed SSA and interface decorations, not Maxwell/IR. Real
 * spirv-val and the separate Vulkan backend supply independent schema/GPU gates. */
static void evaluate(const SatchelRasterModule *m,const uint32_t attrs[8][4],uint32_t output[4]){
    enum {CAP=256};uint32_t values[CAP][4]={{0}},location[CAP],builtin[CAP],storage[CAP]={0};
    for(unsigned i=0;i<CAP;i++)location[i]=builtin[i]=UINT32_MAX;
    const uint32_t *w=m->words;CHECK(w[0]==0x07230203 && w[1]==0x10300 && w[3]<CAP && !w[2] && !w[4]);unsigned stores=0,caps=0;
    for(size_t at=5;at<m->reflection.spirv_words;){unsigned n=w[at]>>16,op=w[at]&65535;CHECK(n && n<=m->reflection.spirv_words-at);const uint32_t *a=w+at+1;
        switch(op){
        case 17:CHECK(n==2 && a[0]==1);caps++;break;
        case 14:CHECK(n==3 && a[0]==0 && a[1]==1);break;
        case 15:CHECK(a[0]==(m->reflection.stage==1?0u:4u));break;
        case 16:CHECK(n==3 && a[1]==7);break;
        case 71:CHECK(n==4 && a[0]<CAP);if(a[1]==30)location[a[0]]=a[2];else{CHECK(a[1]==11 && !a[2]);builtin[a[0]]=a[2];}break;
        case 19:case 22:case 23:case 33:case 32:case 54:case 248:case 253:case 56:break;
        case 43:CHECK(n==4 && a[1]<CAP);values[a[1]][0]=a[2];break;
        case 59:CHECK(n==4 && a[1]<CAP);storage[a[1]]=a[2];break;
        case 61:CHECK(n==4 && a[1]<CAP && a[2]<CAP && storage[a[2]]==1 && location[a[2]]<8);memcpy(values[a[1]],attrs[location[a[2]]],16);break;
        case 81:CHECK(n==5 && a[1]<CAP && a[2]<CAP && a[3]<4);values[a[1]][0]=values[a[2]][a[3]];break;
        case 80:CHECK(n==7 && a[1]<CAP);for(unsigned i=0;i<4;i++){CHECK(a[2+i]<CAP);values[a[1]][i]=values[a[2+i]][0];}break;
        case 62:CHECK(n==3 && a[0]<CAP && a[1]<CAP && storage[a[0]]==3);CHECK(m->reflection.stage==1?builtin[a[0]]==0:location[a[0]]==0);memcpy(output,values[a[1]],16);stores++;break;
        default:CHECK(0);
        }at+=n;
    }CHECK(caps==1 && stores==1);
}
static void save(const char *dir,const char *name,const SatchelRasterModule *m){char p[1024];CHECK(snprintf(p,sizeof(p),"%s/%s.spv",dir,name)>0);FILE *f=fopen(p,"wb");CHECK(f);CHECK(fwrite(m->words,4,m->reflection.spirv_words,f)==m->reflection.spirv_words);CHECK(!fclose(f));}
static void rejected(const SatchelRasterStageInput *in,int expected){SatchelRasterModule m,old;memset(&m,0xa5,sizeof(m));old=m;int s=satchel_raster_compile(in,&m);CHECK(expected<0?s!=0:s==expected);CHECK(!memcmp(&m,&old,sizeof(m)));}
int main(int argc,char **argv){
    CHECK(argc==2);uint32_t attrs[8][4];for(unsigned l=0;l<8;l++)for(unsigned c=0;c<4;c++)attrs[l][c]=0x3f000000u+l*4096+c*256;
    RasterFixture v,f;fixture_init(&v,1);fixture_ald(&v,0,0,0,4);fixture_ast(&v,0,0,4);fixture_exit(&v);
    fixture_init(&f,5);uint32_t color[4]={0x3f800000,0x3f000000,0,0x3f800000};for(unsigned i=0;i<4;i++)fixture_imm(&f,i,color[i]);fixture_exit(&f);
    SatchelRasterStageInput vi=fixture_input(&v,1),fi=fixture_input(&f,5);SatchelRasterModule vm,fm;CHECK(satchel_raster_compile_pair(&vi,&fi,&vm,&fm)==0);
    uint32_t actual[4];evaluate(&vm,attrs,actual);CHECK(!memcmp(actual,attrs[0],16));evaluate(&fm,attrs,actual);CHECK(!memcmp(actual,color,16));save(argv[1],"vertex-pass",&vm);save(argv[1],"fragment-color",&fm);
    CHECK(vm.reflection.input_components[0]==15 && vm.reflection.position_components==15 && !vm.reflection.color_components && !vm.reflection.descriptor_count);
    /* Independent native vector swizzles and exact uint32 bit constants. */
    for(unsigned trial=0;trial<24;trial++){
        fixture_init(&v,1);unsigned loc=trial%8;fixture_ald(&v,4,loc,0,4);for(unsigned c=0;c<4;c++){fixture_mov(&v,20+c,4+(c+trial)%4);fixture_ast(&v,20+c,c,1);}fixture_exit(&v);vi=fixture_input(&v,1);CHECK(satchel_raster_compile(&vi,&vm)==0);evaluate(&vm,attrs,actual);for(unsigned c=0;c<4;c++)CHECK(actual[c]==attrs[loc][(c+trial)%4]);
        fixture_init(&f,5);for(unsigned c=0;c<4;c++){color[c]=(trial*UINT32_C(0x9e3779b9))^(c*UINT32_C(0x7f123456));fixture_imm(&f,20+c,color[c]);fixture_mov(&f,c,20+c);}fixture_exit(&f);fi=fixture_input(&f,5);CHECK(satchel_raster_compile(&fi,&fm)==0);evaluate(&fm,attrs,actual);CHECK(!memcmp(actual,color,16));
        if(trial==7){save(argv[1],"vertex-swizzle",&vm);save(argv[1],"fragment-bits",&fm);}
    }
    /* Width1/2/3/4 loads/stores, last attribute endpoint, RZ and overwritten
     * output state; all values are actual interpreted source definitions. */
    for(unsigned width=1;width<=4;width++){
        fixture_init(&v,1);for(unsigned c=0;c<4;c++)fixture_imm(&v,c,0x3f800000);fixture_ald(&v,0,7,4-width,width);fixture_ast(&v,0,0,4);fixture_exit(&v);vi=fixture_input(&v,1);CHECK(!satchel_raster_compile(&vi,&vm));evaluate(&vm,attrs,actual);for(unsigned c=0;c<4;c++)CHECK(actual[c]==(c<width?attrs[7][4-width+c]:0x3f800000));
    }
    fixture_init(&v,1);fixture_ald(&v,0,0,0,4);fixture_ast(&v,0,0,4);fixture_mov(&v,3,255);fixture_ast(&v,3,3,1);fixture_exit(&v);vi=fixture_input(&v,1);CHECK(!satchel_raster_compile(&vi,&vm));evaluate(&vm,attrs,actual);CHECK(actual[3]==0);save(argv[1],"vertex-overwrite",&vm);
    fixture_init(&f,5);for(unsigned i=0;i<4;i++)fixture_mov(&f,i,255);fixture_exit(&f);fi=fixture_input(&f,5);CHECK(!satchel_raster_compile(&fi,&fm));evaluate(&fm,attrs,actual);for(unsigned i=0;i<4;i++)CHECK(!actual[i]);save(argv[1],"fragment-zero",&fm);
    /* Header and metadata mismatches never partially publish modules. */
    SatchelRasterStageInput bad=fi;bad.state.known=0;rejected(&bad,SATCHEL_RASTER_UNSUPPORTED);bad=fi;bad.code_size=1025;rejected(&bad,SATCHEL_RASTER_INVALID);bad=fi;bad.sph_size=79;rejected(&bad,SATCHEL_RASTER_INVALID);bad=fi;bad.code_size=fi.code_size-8;rejected(&bad,SATCHEL_RASTER_UNSUPPORTED);
    for(unsigned k=0;k<9;k++){bad=fi;if(k==4)bad.state.y_negate=2;else ((uint32_t*)&bad.state)[k]^=1;rejected(&bad,SATCHEL_RASTER_UNSUPPORTED);}
    /* YNegate is no longer a guessed known0. Complete decode proves the current
     * VS/FS profile cannot observe it, and emitted semantics stay identical. */
    SatchelRasterModule ybase;CHECK(!satchel_raster_compile(&fi,&ybase));CHECK(ybase.reflection.unused_state_mask==SATCHEL_RASTER_STATE_Y_NEGATE);
    for(unsigned known=0;known<2;known++)for(unsigned value=0;value<3;value++){
        bad=fi;bad.state.known=known?255:SATCHEL_RASTER_REQUIRED_STATE;bad.state.y_negate=known?value:(value==2?UINT32_MAX:value);
        if(known && value==2)rejected(&bad,SATCHEL_RASTER_UNSUPPORTED);
        else {SatchelRasterModule module;CHECK(!satchel_raster_compile(&bad,&module));CHECK(!memcmp(&module,&ybase,sizeof(module)));}
    }
    for(unsigned bit=0;bit<8;bit++)if(bit!=3){bad=fi;bad.state.known=SATCHEL_RASTER_REQUIRED_STATE&~(1u<<bit);rejected(&bad,SATCHEL_RASTER_UNSUPPORTED);}
    bad=fi;bad.state.known=SATCHEL_RASTER_REQUIRED_STATE|256;rejected(&bad,SATCHEL_RASTER_UNSUPPORTED);
    RasterFixture observable=f;fixture64(observable.code+8,UINT64_C(0xefd87f800007ff00)|(UINT64_C(0x74)<<20));bad=fixture_input(&observable,5);bad.state.known=SATCHEL_RASTER_REQUIRED_STATE;bad.state.y_negate=UINT32_MAX;rejected(&bad,SATCHEL_RASTER_UNSUPPORTED);
    SatchelRasterCodeInput observed={1,sizeof(observed),5,bad.sph,bad.sph_size,bad.code,bad.code_size};SatchelRasterAdmission rejected_proof,proof_marker;memset(&rejected_proof,0xa5,sizeof(rejected_proof));proof_marker=rejected_proof;CHECK(satchel_raster_admit(&observed,&rejected_proof)==SATCHEL_RASTER_UNSUPPORTED && !memcmp(&rejected_proof,&proof_marker,sizeof(proof_marker)));
    fixture64(observable.code+8,UINT64_C(0xe000000000070000));bad=fixture_input(&observable,5);bad.state.known=SATCHEL_RASTER_REQUIRED_STATE;rejected(&bad,SATCHEL_RASTER_UNSUPPORTED); /* unadmitted interpolation family */
    for(unsigned bit=0;bit<640;bit++){RasterFixture mutated=f;mutated.sph[bit/8]^=1u<<(bit%8);bad=fixture_input(&mutated,5);if(bit>=140 && bit<148)continue;if(bit>=152 && bit<160)continue;rejected(&bad,SATCHEL_RASTER_UNSUPPORTED);}
    fixture_init(&f,5);fixture_imm(&f,0,0);fixture_exit(&f);bad=fixture_input(&f,5);rejected(&bad,SATCHEL_RASTER_UNSUPPORTED); /* undefined R1..3 */
    fixture_init(&v,1);fixture_ast(&v,0,0,4);fixture_exit(&v);bad=fixture_input(&v,1);rejected(&bad,SATCHEL_RASTER_UNSUPPORTED);
    fixture_init(&v,1);fixture_ald(&v,0,7,3,2);fixture_ast(&v,0,0,4);fixture_exit(&v);bad=fixture_input(&v,1);rejected(&bad,SATCHEL_RASTER_UNSUPPORTED);
    fixture_init(&v,1);fixture_ald(&v,0,0,0,4);fixture_ast(&v,0,3,2);fixture_exit(&v);bad=fixture_input(&v,1);rejected(&bad,SATCHEL_RASTER_UNSUPPORTED);
    fixture_init(&v,1);fixture_ald(&v,254,0,0,2);fixture_exit(&v);bad=fixture_input(&v,1);rejected(&bad,SATCHEL_RASTER_UNSUPPORTED);
    /* Semantic budget exact boundary; arbitrary unsupported operations reject. */
    for(unsigned n=63;n<=64;n++){fixture_init(&f,5);for(unsigned i=0;i<4;i++)fixture_imm(&f,i,0);for(unsigned i=4;i<n;i++)fixture_mov(&f,0,0);fixture_exit(&f);bad=fixture_input(&f,5);if(n==63)CHECK(!satchel_raster_compile(&bad,&fm));else rejected(&bad,SATCHEL_RASTER_LIMIT);}
    fixture_init(&f,5);for(unsigned i=0;i<4;i++)fixture_imm(&f,i,0);fixture_exit(&f);fi=fixture_input(&f,5);
    /* Exact fixed-bit instruction exclusions, including predicate/modifier
     * bits. Mutation of valid fields is evaluated in positive programs above. */
    uint64_t allowed=UINT64_C(255)|(UINT64_C(0xffffffff)<<20);
    for(unsigned bit=0;bit<64;bit++)if(!(allowed&(UINT64_C(1)<<bit))){RasterFixture m=f;m.code[8+bit/8]^=1u<<(bit%8);bad=fixture_input(&m,5);rejected(&bad,SATCHEL_RASTER_UNSUPPORTED);}
    /* Every fixed predicate/modifier/indirect bit of ALD, AST and MOVR must
     * remain outside admission. Mutating field bits has separate value tests. */
    fixture_init(&v,1);fixture_ald(&v,0,0,0,4);fixture_ast(&v,0,0,4);fixture_exit(&v);vi=fixture_input(&v,1);
    allowed=UINT64_C(255)|(UINT64_C(0x7ff)<<20)|(UINT64_C(3)<<47);
    for(unsigned at=8;at<=16;at+=8)for(unsigned bit=0;bit<64;bit++)if(!(allowed&(UINT64_C(1)<<bit))){RasterFixture m=v;m.code[at+bit/8]^=1u<<(bit%8);bad=fixture_input(&m,1);rejected(&bad,SATCHEL_RASTER_UNSUPPORTED);}
    fixture_init(&f,5);for(unsigned i=0;i<4;i++)fixture_mov(&f,i,255);fixture_exit(&f);fi=fixture_input(&f,5);
    allowed=UINT64_C(255)|(UINT64_C(255)<<20);
    for(unsigned bit=0;bit<64;bit++)if(!(allowed&(UINT64_C(1)<<bit))){RasterFixture m=f;m.code[8+bit/8]^=1u<<(bit%8);bad=fixture_input(&m,5);rejected(&bad,SATCHEL_RASTER_UNSUPPORTED);}
    for(unsigned i=0;i<4;i++){RasterFixture m=v;m.sph[24]&=(uint8_t)~(1u<<i);bad=fixture_input(&m,1);rejected(&bad,SATCHEL_RASTER_UNSUPPORTED);}
    /* Trailing known padding cannot execute; unknown instructions reject even
     * there. Scheduler words are inert data under this scalar stage profile. */
    RasterFixture pad=v;fixture_op(&pad,UINT64_C(0x50b0000000070f00));bad=fixture_input(&pad,1);CHECK(!satchel_raster_compile(&bad,&vm));fixture64(pad.code,UINT64_MAX);CHECK(!satchel_raster_compile(&bad,&vm));fixture_op(&pad,UINT64_MAX);bad=fixture_input(&pad,1);rejected(&bad,SATCHEL_RASTER_UNSUPPORTED);
    for(unsigned short_size=0;short_size<vi.code_size;short_size++){bad=vi;bad.code_size=short_size;rejected(&bad,-1);}
    /* Input/output aliasing is safe because all source decode is staged before
     * publication; both modules still match disjoint compilation exactly. */
    CHECK(!satchel_raster_compile_pair(&vi,&fi,&vm,&fm));SatchelRasterModule alias=vm;memcpy(alias.words,v.sph,80);memcpy(alias.words+20,v.code,v.cursor);bad=vi;bad.sph=(const uint8_t*)alias.words;bad.code=(const uint8_t*)(alias.words+20);CHECK(!satchel_raster_compile(&bad,&alias));CHECK(!memcmp(&alias,&vm,sizeof(vm)));
    SatchelRasterStageInput unknownv=vi,knownf=fi;unknownv.state.known=SATCHEL_RASTER_REQUIRED_STATE;unknownv.state.y_negate=UINT32_MAX;knownf.state.y_negate=1;SatchelRasterModule yv,yf;CHECK(!satchel_raster_compile_pair(&unknownv,&knownf,&yv,&yf));CHECK(!memcmp(&yv,&vm,sizeof(vm))&&!memcmp(&yf,&fm,sizeof(fm)));
    /* A failure in either stage preserves the pair, including first success. */
    SatchelRasterModule goodv=vm,goodf=fm;bad=fi;bad.state.alpha_to_coverage=1;CHECK(satchel_raster_compile_pair(&vi,&bad,&vm,&fm)==SATCHEL_RASTER_UNSUPPORTED);CHECK(!memcmp(&vm,&goodv,sizeof(vm))&&!memcmp(&fm,&goodf,sizeof(fm)));
    RasterFixture invalid=f;invalid.sph[72]=3;bad=fixture_input(&invalid,5);CHECK(satchel_raster_compile_pair(&vi,&bad,&vm,&fm)==SATCHEL_RASTER_UNSUPPORTED);CHECK(!memcmp(&vm,&goodv,sizeof(vm))&&!memcmp(&fm,&goodf,sizeof(fm)));
    SatchelRasterModule beforev,beforef;memset(&beforev,0x5a,sizeof(beforev));memset(&beforef,0x6b,sizeof(beforef));vm=beforev;fm=beforef;bad=fi;bad.stage=1;CHECK(satchel_raster_compile_pair(&vi,&bad,&vm,&fm)!=0);CHECK(!memcmp(&vm,&beforev,sizeof(vm))&&!memcmp(&fm,&beforef,sizeof(fm)));CHECK(satchel_raster_compile_pair(&vi,&fi,&vm,&vm)==SATCHEL_RASTER_INVALID);
    CHECK(satchel_raster_compile(NULL,&vm)==SATCHEL_RASTER_INVALID);CHECK(satchel_raster_compile(&fi,NULL)==SATCHEL_RASTER_INVALID);
    printf("PASS %u raster compiler assertions\n",checks);return 0;
}
