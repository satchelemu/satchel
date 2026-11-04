#include "RasterCompiler.h"
#include <string.h>
#include <limits.h>

/* Independent bounded semantics, reviewed against pinned NVIDIA SPHv3 and
 * Ryujinx Maxwell decoders/emitters. Value forwarding is exact bit movement;
 * there is no floating-point arithmetic, resource access or control-flow loop. */
typedef struct { uint32_t kind,value; } Value; /* 0 undefined,1 bits,2 attribute */
typedef struct {
    SatchelRasterReflection reflection;
    Value outputs[4];
} Program;
typedef struct { uint32_t words[SATCHEL_RASTER_SPIRV_WORDS];size_t count;uint32_t next;int failed; } Emit;
static uint32_t u32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint64_t u64(const uint8_t *p){return u32(p)|(uint64_t)u32(p+4)<<32;}
static int overlap(const void *a,size_t an,const void *b,size_t bn){uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;if(x>UINTPTR_MAX-an || y>UINTPTR_MAX-bn)return 1;return x<y+bn && y<x+an;}
static int state_ok(const SatchelRasterState *s,const Program *p){
    if(p->reflection.unused_state_mask!=SATCHEL_RASTER_STATE_Y_NEGATE || (s->known&~SATCHEL_RASTER_STATE_KNOWN) ||
       (s->known&SATCHEL_RASTER_REQUIRED_STATE)!=SATCHEL_RASTER_REQUIRED_STATE ||
       ((s->known&SATCHEL_RASTER_STATE_Y_NEGATE) && s->y_negate>1))return 0;
    return s->single_vertex_program==1 && s->viewport_transform_enabled==1 && s->depth_zero_to_one==1 &&
           s->origin_upper_left==1 && s->alpha_test_always==1 && !s->alpha_to_coverage && !s->transform_feedback;
}
static int meaningful_state_equal(const SatchelRasterState *a,const SatchelRasterState *b){
    /* Normalize only for comparison, never publish fabricated captured state. */
    SatchelRasterState x=*a,y=*b;x.known&=~SATCHEL_RASTER_STATE_Y_NEGATE;y.known&=~SATCHEL_RASTER_STATE_Y_NEGATE;
    x.y_negate=0;y.y_negate=0;return !memcmp(&x,&y,sizeof(x));
}
static void ins(Emit *e,uint32_t opcode,const uint32_t *a,size_t n){if(n+1>SATCHEL_RASTER_SPIRV_WORDS-e->count){e->failed=1;return;}e->words[e->count++]=(uint32_t)((n+1)<<16)|opcode;if(n)memcpy(e->words+e->count,a,n*4);e->count+=n;}
#define I(e,op,...) do{const uint32_t a_[]={__VA_ARGS__};ins((e),(op),a_,sizeof(a_)/4);}while(0)
static void noargs(Emit *e,uint32_t op){ins(e,op,0,0);}

static int decode(const SatchelRasterCodeInput *s,Program *p){
    if(s->version!=1 || s->size!=sizeof(*s) || !s->sph || s->sph_size!=80 || !s->code || !s->code_size || s->code_size>SATCHEL_RASTER_CODE_BYTES || s->code_size%8)return SATCHEL_RASTER_INVALID;
    if(s->stage!=SATCHEL_RASTER_VERTEX && s->stage!=SATCHEL_RASTER_FRAGMENT)return SATCHEL_RASTER_UNSUPPORTED;
    uint32_t h[20];for(unsigned i=0;i<20;i++)h[i]=u32(s->sph+i*4);
    const int vertex=s->stage==SATCHEL_RASTER_VERTEX;
    /* SPH version3/SASS3 only, no semantic flags. CommonWord4 store-request
     * allocation hints do not change admitted pure register/attribute semantics.
     * No silent "compute header means vertex" fallback from the old translator. */
    uint32_t expected=(vertex?1u:2u)|(3u<<5)|(s->stage<<10)|(3u<<17);
    if(h[0]!=expected || h[1] || h[2] || h[3] || (h[4]&~UINT32_C(0xff0ff000)))return SATCHEL_RASTER_UNSUPPORTED;
    for(unsigned i=5;i<20;i++){
        if(vertex && (i==6 || i==13))continue;
        if(!vertex && i==18)continue;
        if(h[i])return SATCHEL_RASTER_UNSUPPORTED;
    }
    if(vertex ? h[13]!=UINT32_C(0xf0000000) : h[18]!=15)return SATCHEL_RASTER_UNSUPPORTED;
    memset(p,0,sizeof(*p));p->reflection=(SatchelRasterReflection){.version=1,.size=sizeof(SatchelRasterReflection),.stage=s->stage,.required_capabilities=1,.unused_memory_mask=7};
    Value regs[256]={0};regs[255]=(Value){1,0};int exited=0;
    for(size_t at=0;at<s->code_size;at+=8){
        uint64_t op=u64(s->code+at);if(!(at&31))continue; /* scheduling, no architectural data effects */
        if(exited){if(op && op!=UINT64_C(0x50b0000000070f00))return SATCHEL_RASTER_UNSUPPORTED;continue;}
        if(++p->reflection.semantic_ops>SATCHEL_RASTER_MAX_OPS)return SATCHEL_RASTER_LIMIT;
        if(op==UINT64_C(0xe30000000007000f)){exited=1;continue;}
        uint64_t fields=UINT64_C(255)|(UINT64_C(0xffffffff)<<20);
        if((op&~fields)==UINT64_C(0x010000000007f000)){
            unsigned d=op&255;if(d!=255)regs[d]=(Value){1,(uint32_t)(op>>20)};continue;
        }
        fields=UINT64_C(255)|(UINT64_C(255)<<20);
        if((op&~fields)==UINT64_C(0x5c98078000070000)){
            unsigned d=op&255,a=(op>>20)&255;if(!regs[a].kind)return SATCHEL_RASTER_UNSUPPORTED;if(d!=255)regs[d]=regs[a];continue;
        }
        fields=UINT64_C(255)|(UINT64_C(0x7ff)<<20)|(UINT64_C(3)<<47);
        if(vertex && (op&~fields)==UINT64_C(0xefd87f800007ff00)){
            unsigned d=op&255,off=(op>>20)&0x7ff,n=((op>>47)&3)+1;
            if(off<0x80 || off%4 || off+4*n>0x100 || d+n>255)return SATCHEL_RASTER_UNSUPPORTED;
            for(unsigned j=0;j<n;j++){
                unsigned index=(off-0x80)/4+j;
                if(!(h[6]&(1u<<index)))return SATCHEL_RASTER_UNSUPPORTED;
                p->reflection.input_components[index/4]|=1u<<(index%4);
                regs[d+j]=(Value){2,index};
            }
            continue;
        }
        if(vertex && (op&~fields)==UINT64_C(0xeff07f800007ff00)){
            unsigned src=op&255,off=(op>>20)&0x7ff,n=((op>>47)&3)+1;
            if(off<0x70 || off%4 || off+4*n>0x80 || src+n>256)return SATCHEL_RASTER_UNSUPPORTED;
            for(unsigned j=0;j<n;j++){
                if(!regs[src+j].kind)return SATCHEL_RASTER_UNSUPPORTED;
                unsigned component=(off-0x70)/4+j;p->outputs[component]=regs[src+j];p->reflection.position_components|=1u<<component;
            }
            continue;
        }
        return SATCHEL_RASTER_UNSUPPORTED;
    }
    if(!exited)return SATCHEL_RASTER_UNSUPPORTED;
    if(vertex){if(p->reflection.position_components!=15)return SATCHEL_RASTER_UNSUPPORTED;}
    else {for(unsigned i=0;i<4;i++){if(!regs[i].kind)return SATCHEL_RASTER_UNSUPPORTED;p->outputs[i]=regs[i];}p->reflection.color_components=15;}
    /* Proof is published only after complete SPH/instruction/output validation.
     * The admitted FS cannot read FragCoord/IPA/ALD; VS only reads user float
     * attributes and writes Position. No supported operation observes YNegate.
     * Pinned InstEmitAttribute.cs consumes it only for fragment PositionY. */
    p->reflection.unused_state_mask=SATCHEL_RASTER_STATE_Y_NEGATE;
    return SATCHEL_RASTER_OK;
}

static int emit(const Program *p,SatchelRasterModule *out){
    /* SPIR-V1.3: Shader, Logical/GLSL450; no extension/capability normalization.
     * Float constants encode unchanged MOV32I/RZ bits. Attribute loads preserve
     * float32 bits; no conversion/arithmetic or implicit support-buffer access. */
    enum {VOID=1,FLOAT=2,VEC4=3,FUNCTION=4,INPTR=5,OUTPTR=6,MAIN=7,LABEL=8,OUTPUT=9};
    Emit e={.count=5,.next=10};uint32_t vars[8]={0},loaded[8]={0},constants[4]={0};
    for(unsigned i=0;i<8;i++)if(p->reflection.input_components[i])vars[i]=e.next++;
    for(unsigned i=0;i<4;i++)if(p->outputs[i].kind==1)constants[i]=e.next++;
    I(&e,17,1);I(&e,14,0,1);
    uint32_t entry[13]={p->reflection.stage==SATCHEL_RASTER_VERTEX?0u:4u,MAIN,0x6e69616d,0,OUTPUT};size_t en=5;
    for(unsigned i=0;i<8;i++)if(vars[i])entry[en++]=vars[i];ins(&e,15,entry,en);
    if(p->reflection.stage==SATCHEL_RASTER_FRAGMENT)I(&e,16,MAIN,7); /* OriginUpperLeft */
    if(p->reflection.stage==SATCHEL_RASTER_VERTEX)I(&e,71,OUTPUT,11,0); /* BuiltIn Position */
    else I(&e,71,OUTPUT,30,0); /* Location0 */
    for(unsigned i=0;i<8;i++)if(vars[i])I(&e,71,vars[i],30,i);
    I(&e,19,VOID);I(&e,22,FLOAT,32);I(&e,23,VEC4,FLOAT,4);I(&e,33,FUNCTION,VOID);
    I(&e,32,INPTR,1,VEC4);I(&e,32,OUTPTR,3,VEC4);
    for(unsigned i=0;i<4;i++)if(constants[i])I(&e,43,FLOAT,constants[i],p->outputs[i].value);
    for(unsigned i=0;i<8;i++)if(vars[i])I(&e,59,INPTR,vars[i],1);
    I(&e,59,OUTPTR,OUTPUT,3);I(&e,54,VOID,MAIN,0,FUNCTION);I(&e,248,LABEL);
    for(unsigned i=0;i<8;i++)if(vars[i]){loaded[i]=e.next++;I(&e,61,VEC4,loaded[i],vars[i]);}
    uint32_t values[4];for(unsigned i=0;i<4;i++){
        if(constants[i])values[i]=constants[i];
        else {unsigned a=p->outputs[i].value;values[i]=e.next++;I(&e,81,FLOAT,values[i],loaded[a/4],a%4);}
    }
    uint32_t vec=e.next++;I(&e,80,VEC4,vec,values[0],values[1],values[2],values[3]);I(&e,62,OUTPUT,vec);noargs(&e,253);noargs(&e,56);
    if(e.failed)return SATCHEL_RASTER_LIMIT;
    e.words[0]=0x07230203;e.words[1]=0x00010300;e.words[3]=e.next;
    memset(out,0,sizeof(*out));out->reflection=p->reflection;out->reflection.spirv_words=(uint32_t)e.count;memcpy(out->words,e.words,e.count*4);return SATCHEL_RASTER_OK;
}

int satchel_raster_compile(const SatchelRasterStageInput *in,SatchelRasterModule *out){
    if(!in || !out)return SATCHEL_RASTER_INVALID;
    SatchelRasterStageInput s=*in;if(s.version!=1 || s.size!=sizeof(s))return SATCHEL_RASTER_INVALID;
    SatchelRasterCodeInput code={.version=1,.size=sizeof(code),.stage=s.stage,.sph=s.sph,.sph_size=s.sph_size,.code=s.code,.code_size=s.code_size};
    Program p;int status=decode(&code,&p);if(status)return status;if(!state_ok(&s.state,&p))return SATCHEL_RASTER_UNSUPPORTED;
    /* Complete decoding precedes writes, so overlapping input is safe. */
    SatchelRasterModule candidate;status=emit(&p,&candidate);if(!status)*out=candidate;return status;
}
int satchel_raster_compile_pair(const SatchelRasterStageInput *v,const SatchelRasterStageInput *f,SatchelRasterModule *vo,SatchelRasterModule *fo){
    if(!v || !f || !vo || !fo || overlap(vo,sizeof(*vo),fo,sizeof(*fo)))return SATCHEL_RASTER_INVALID;
    SatchelRasterStageInput vs=*v,fs=*f;if(vs.version!=1 || fs.version!=1 || vs.size!=sizeof(vs) || fs.size!=sizeof(fs))return SATCHEL_RASTER_INVALID;
    if(vs.stage!=SATCHEL_RASTER_VERTEX || fs.stage!=SATCHEL_RASTER_FRAGMENT)return SATCHEL_RASTER_UNSUPPORTED;
    SatchelRasterCodeInput vc={.version=1,.size=sizeof(vc),.stage=vs.stage,.sph=vs.sph,.sph_size=vs.sph_size,.code=vs.code,.code_size=vs.code_size};
    SatchelRasterCodeInput fc={.version=1,.size=sizeof(fc),.stage=fs.stage,.sph=fs.sph,.sph_size=fs.sph_size,.code=fs.code,.code_size=fs.code_size};
    Program vp,fp;int status=decode(&vc,&vp);if(status)return status;status=decode(&fc,&fp);if(status)return status;
    if(!state_ok(&vs.state,&vp) || !state_ok(&fs.state,&fp) || !meaningful_state_equal(&vs.state,&fs.state))return SATCHEL_RASTER_UNSUPPORTED;
    SatchelRasterModule vm,fm;status=emit(&vp,&vm);if(status)return status;status=emit(&fp,&fm);if(status)return status;
    *vo=vm;*fo=fm;return SATCHEL_RASTER_OK;
}
int satchel_raster_admit(const SatchelRasterCodeInput *in,SatchelRasterAdmission *out){
    if(!in || !out)return SATCHEL_RASTER_INVALID;SatchelRasterCodeInput s=*in;Program p;int status=decode(&s,&p);if(status)return status;
    SatchelRasterAdmission candidate={.reflection=p.reflection,.state_requirements={.known=SATCHEL_RASTER_REQUIRED_STATE,.single_vertex_program=1,.viewport_transform_enabled=1,.depth_zero_to_one=1,.origin_upper_left=1,.alpha_test_always=1},.nvn_stage_bit=s.stage==SATCHEL_RASTER_VERTEX?SATCHEL_RASTER_NVN_VERTEX_BIT:SATCHEL_RASTER_NVN_FRAGMENT_BIT};
    *out=candidate;return SATCHEL_RASTER_OK;
}
