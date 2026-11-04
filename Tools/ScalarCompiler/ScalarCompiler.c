#include "ScalarCompiler.h"
#include <string.h>
#include <limits.h>
/* Independent decoding based on pinned public InstDecoders/InstEmit sources.
 * Accepted bits are an exact base plus the listed field masks; every other
 * encoding/modifier is rejected before emission. Scheduling words have no
 * architectural data effect and are ignored at each32-byte boundary. */
typedef SatchelScalarIROp Op;
typedef struct {uint16_t value;uint8_t pointer_role;uint32_t cb_offset;} Reg;
typedef struct {uint32_t words[SATCHEL_SCALAR_SPIRV_WORDS];size_t count;uint32_t next_id;int failed;} Emit;
typedef struct {uint32_t value,id;} Constant;
static uint32_t u32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint64_t u64(const uint8_t *p){return (uint64_t)u32(p)|(uint64_t)u32(p+4)<<32;}
static int power2(uint32_t v){return v && !(v&(v-1));}
static int overlap(const void *a,size_t an,const void *b,size_t bn){uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;if(x>UINTPTR_MAX-an || y>UINTPTR_MAX-bn)return 1;return x<y+bn && y<x+an;}
static void ins(Emit *e,uint32_t opcode,const uint32_t *args,size_t n){if(n+1>SATCHEL_SCALAR_SPIRV_WORDS-e->count){e->failed=1;return;}e->words[e->count++]=((uint32_t)(n+1)<<16)|opcode;if(n)memcpy(e->words+e->count,args,n*4);e->count+=n;}
#define I(e,op,...) do{const uint32_t args_[]={__VA_ARGS__};ins((e),(op),args_,sizeof(args_)/4);}while(0)
static void noargs(Emit *e,uint32_t op){ins(e,op,NULL,0);}
static uint32_t constant(Constant *c,unsigned *n,uint32_t value,uint32_t *next){for(unsigned i=0;i<*n;i++)if(c[i].value==value)return c[i].id;if(*n==128)return 0;c[*n]=(Constant){value,(*next)++};return c[(*n)++].id;}
int satchel_scalar_admit(const SatchelScalarProgramInput *in,SatchelScalarProgram *out){
    if(!in || !out)return SATCHEL_SCALAR_INVALID;const SatchelScalarProgramInput s=*in;
    if(s.version!=1 || s.size!=sizeof(s) || !(s.known&SATCHEL_SCALAR_KNOWN_WORKGROUP) || (s.known&~25u) || !s.code || !s.code_size || s.code_size>SATCHEL_SCALAR_CODE_BYTES || s.code_size%8)return SATCHEL_SCALAR_INVALID;
    if(s.local_x!=1 || s.local_y!=1 || s.local_z!=1)return SATCHEL_SCALAR_UNSUPPORTED;
    SatchelScalarProgram program;memset(&program,0,sizeof(program));program.version=1;program.size=sizeof(program);program.known=s.known;
    program.local_x=s.local_x;program.local_y=s.local_y;program.local_z=s.local_z;program.local_bytes=s.local_bytes;program.shared_bytes=s.shared_bytes;program.code_size=(uint32_t)s.code_size;
    memcpy(program.code,s.code,s.code_size);
    Reg regs[256]={0};Op *ops=program.ops;unsigned count=0,semantic=0;int exited=0,pointer_seen=0,slot_seen=0;
    SatchelScalarRequirements *r=&program.requirements;r->uniform_declared_bytes=65536;r->unused_memory_mask=3;
    /* IR value0 is architectural RZ; other uninitialized GPR reads reject. */
    regs[255].value=1;
    for(size_t at=0;at<s.code_size;at+=8){uint64_t word=u64(program.code+at);if((at&31)==0)continue;
        if(exited){if(word)return SATCHEL_SCALAR_UNSUPPORTED;continue;}
        if(++semantic>SATCHEL_SCALAR_MAX_OPS)return SATCHEL_SCALAR_LIMIT;
        if(word==UINT64_C(0xe30000000007000f)){exited=1;continue;}
        const uint64_t ldc_fields=UINT64_C(0xff)|(UINT64_C(0xffff)<<20)|(UINT64_C(31)<<36)|(UINT64_C(1)<<48);
        if((word&~ldc_fields)==UINT64_C(0xef9400000007ff00)){
            unsigned dest=word&255,slot=(word>>36)&31,offset=(word>>20)&65535,width=(word&(UINT64_C(1)<<48))?8:4;
            if(slot>17 || (slot_seen && slot!=r->cb_slot) || offset>=32768 || offset%4 || dest>=255 || (width==8 && dest>=254))return SATCHEL_SCALAR_UNSUPPORTED;
            slot_seen=1;r->cb_slot=slot;
            for(unsigned j=0;j<width/4;j++){if(count==64)return SATCHEL_SCALAR_LIMIT;ops[count]=(Op){.kind=1,.offset=offset+j*4};regs[dest+j]=(Reg){.value=(uint16_t)(count+2),.pointer_role=width==8?(uint8_t)(j+1):0,.cb_offset=offset};count++;}
            if(offset+width>r->uniform_required_bytes)r->uniform_required_bytes=offset+width;
        }else{
            const uint64_t add_fields=UINT64_C(0xffff)|(UINT64_C(255)<<20);
            if((word&~add_fields)==UINT64_C(0x5c10000000070000)){
                unsigned dest=word&255,a=(word>>8)&255,b=(word>>20)&255;
                if(!regs[a].value || !regs[b].value || count==64)return SATCHEL_SCALAR_UNSUPPORTED;
                ops[count]=(Op){.kind=2,.a=regs[a].value,.b=regs[b].value};if(dest!=255)regs[dest]=(Reg){.value=(uint16_t)(count+2)};count++;
            }else{
                const uint64_t store_fields=UINT64_C(0xffff)|(UINT64_C(0xffffff)<<20);
                if((word&~store_fields)!=UINT64_C(0xeedc200000070000))return SATCHEL_SCALAR_UNSUPPORTED;
                unsigned value=word&255,base=(word>>8)&255,offset=(word>>20)&0xffffff;
                if(base>=254 || regs[base].pointer_role!=1 || regs[base+1].pointer_role!=2 || regs[base].cb_offset!=regs[base+1].cb_offset || !regs[value].value || offset&0x800000 || offset%4 || offset>1048576 || 4>1048576-offset)return SATCHEL_SCALAR_UNSUPPORTED;
                unsigned pair=regs[base].cb_offset;if(pointer_seen && r->pointer_cb_offset!=pair)return SATCHEL_SCALAR_UNSUPPORTED;
                pointer_seen=1;r->pointer_cb_offset=pair;
                if(r->store_count==SATCHEL_SCALAR_MAX_STORES || count==64)return SATCHEL_SCALAR_LIMIT;
                ops[count++]=(Op){.kind=3,.a=regs[value].value,.offset=offset};r->stores[r->store_count++]=(SatchelScalarWrite){offset,4};if(offset+4>r->storage_required_bytes)r->storage_required_bytes=offset+4;
            }
        }
    }
    if(!exited || !r->store_count)return SATCHEL_SCALAR_UNSUPPORTED;program.semantic_ops=semantic;program.op_count=count;
    *out=program;return SATCHEL_SCALAR_OK;
}

int satchel_scalar_compile(const SatchelScalarInput *in,uint32_t *out,size_t capacity,SatchelScalarReflection *reflection){
    if(!in || !out || !reflection || capacity>SATCHEL_SCALAR_SPIRV_WORDS || overlap(out,capacity*4,reflection,sizeof(*reflection)))return SATCHEL_SCALAR_INVALID;
    const SatchelScalarInput s=*in;
    if(s.version!=1 || s.size!=sizeof(s) || (s.known&7)!=7 || (s.known&~31u) || !s.code || !s.constant_buffer || !s.code_size || s.code_size>512 || s.code_size%8 || s.constant_buffer_size<8 || s.constant_buffer_size>65536 || s.constant_buffer_size%4)return SATCHEL_SCALAR_INVALID;
    if(s.local_x!=1 || s.local_y!=1 || s.local_z!=1 || s.group_x!=1 || s.group_y!=1 || s.group_z!=1)return SATCHEL_SCALAR_UNSUPPORTED;
    if(s.shader_enabled!=1 || s.max_uniform_range<65536 || s.max_storage_range<s.storage_size || s.max_uniform_descriptors<1 || s.max_storage_descriptors<1 || s.max_stage_resources<2)return SATCHEL_SCALAR_UNSUPPORTED;
    if(s.storage_cb_disjoint!=1 || !power2(s.uniform_alignment) || !power2(s.storage_alignment) || s.uniform_alignment>65536 || s.storage_alignment>65536 || s.cb_slot>17 || s.uniform_binding>7 || s.storage_binding>7 || s.uniform_binding==s.storage_binding || !s.storage_address || s.storage_address>=(UINT64_C(1)<<40) || s.storage_address%s.storage_alignment || !s.storage_size || s.storage_size>1048576 || s.storage_size%4 || s.storage_size>(UINT64_C(1)<<40)-s.storage_address)return SATCHEL_SCALAR_INVALID;
    SatchelScalarProgramInput pi={.version=1,.size=sizeof(pi),.known=s.known&25u,.local_x=s.local_x,.local_y=s.local_y,.local_z=s.local_z,.local_bytes=s.local_bytes,.shared_bytes=s.shared_bytes,.code=s.code,.code_size=s.code_size};
    SatchelScalarProgram program;int admission=satchel_scalar_admit(&pi,&program);if(admission)return admission;
    const SatchelScalarRequirements *q=&program.requirements;
    if(q->cb_slot!=s.cb_slot || q->uniform_required_bytes>s.constant_buffer_size || q->storage_required_bytes>s.storage_size || u64(s.constant_buffer+q->pointer_cb_offset)!=s.storage_address)return SATCHEL_SCALAR_UNSUPPORTED;
    const Op *ops=program.ops;const unsigned count=program.op_count;
    SatchelScalarReflection r={.version=1,.size=sizeof(r),.semantic_ops=program.semantic_ops,.cb_slot=q->cb_slot,.uniform_binding=s.uniform_binding,.storage_binding=s.storage_binding,.uniform_declared_bytes=q->uniform_declared_bytes,.uniform_required_bytes=q->uniform_required_bytes,.storage_required_bytes=q->storage_required_bytes,.pointer_cb_offset=q->pointer_cb_offset,.store_count=q->store_count,.expected_storage_address=s.storage_address,.unused_memory_mask=q->unused_memory_mask};
    memcpy(r.stores,q->stores,sizeof(r.stores));
    /* Fixed-capacity SPIR-V1.3, Shader only; every accepted IR op is emitted.
     * Uniform array uint4[4096] preserves std140 layout without float bitcasts. */
    enum {VOID=1,UINT=2,VEC=3,FNTYPE=4,ARRAY=5,UBLOCK=6,UPTR=7,UVALUE=8,UBO=9,RARRAY=10,SBLOCK=11,SPTR=12,SVALUE=13,SSBO=14,MAIN=15,LABEL=16};
    Emit e={.count=5,.next_id=17};Constant constants[128];unsigned cn=0;
    uint32_t zero=constant(constants,&cn,0,&e.next_id),length=constant(constants,&cn,4096,&e.next_id),a[64]={0},b[64]={0};
    for(unsigned i=0;i<count;i++){if(ops[i].kind==1){a[i]=constant(constants,&cn,ops[i].offset/16,&e.next_id);b[i]=constant(constants,&cn,(ops[i].offset%16)/4,&e.next_id);}else if(ops[i].kind==3)a[i]=constant(constants,&cn,ops[i].offset/4,&e.next_id);}
    I(&e,17,1);I(&e,14,0,1);I(&e,15,5,MAIN,0x6e69616d,0);I(&e,16,MAIN,17,1,1,1);
    I(&e,71,ARRAY,6,16);I(&e,71,UBLOCK,2);I(&e,72,UBLOCK,0,35,0);I(&e,71,UBO,34,0);I(&e,71,UBO,33,s.uniform_binding);
    I(&e,71,RARRAY,6,4);I(&e,71,SBLOCK,2);I(&e,72,SBLOCK,0,35,0);I(&e,71,SSBO,34,0);I(&e,71,SSBO,33,s.storage_binding);
    I(&e,19,VOID);I(&e,21,UINT,32,0);I(&e,23,VEC,UINT,4);I(&e,33,FNTYPE,VOID);
    for(unsigned i=0;i<cn;i++)I(&e,43,UINT,constants[i].id,constants[i].value);
    I(&e,28,ARRAY,VEC,length);I(&e,30,UBLOCK,ARRAY);I(&e,32,UPTR,2,UBLOCK);I(&e,32,UVALUE,2,UINT);I(&e,59,UPTR,UBO,2);
    I(&e,29,RARRAY,UINT);I(&e,30,SBLOCK,RARRAY);I(&e,32,SPTR,12,SBLOCK);I(&e,32,SVALUE,12,UINT);I(&e,59,SPTR,SSBO,12);
    I(&e,54,VOID,MAIN,0,FNTYPE);I(&e,248,LABEL);uint32_t values[66]={0};values[1]=zero;
    for(unsigned i=0;i<count;i++){const Op *op=ops+i;uint32_t id=e.next_id++;
        if(op->kind==1){I(&e,65,UVALUE,id,UBO,zero,a[i],b[i]);values[i+2]=e.next_id++;I(&e,61,UINT,values[i+2],id);}
        else if(op->kind==2){values[i+2]=id;I(&e,128,UINT,id,values[op->a],values[op->b]);}
        else{I(&e,65,SVALUE,id,SSBO,zero,a[i]);I(&e,62,id,values[op->a]);}
    }
    noargs(&e,253);noargs(&e,56);if(e.failed || e.count>capacity)return SATCHEL_SCALAR_LIMIT;
    e.words[0]=0x07230203;e.words[1]=0x00010300;e.words[2]=0;e.words[3]=e.next_id;e.words[4]=0;r.spirv_words=(uint32_t)e.count;
    memcpy(out,e.words,e.count*4);*reflection=r;return SATCHEL_SCALAR_OK;
}

int satchel_scalar_compile_program(const SatchelScalarProgram *program,const SatchelScalarInput *dispatch,uint32_t *out,size_t capacity,SatchelScalarReflection *reflection){
    if(!program || !dispatch)return SATCHEL_SCALAR_INVALID;
    const SatchelScalarProgram saved=*program;
    const SatchelScalarProgramInput input={.version=saved.version,.size=sizeof(input),.known=saved.known,.local_x=saved.local_x,.local_y=saved.local_y,.local_z=saved.local_z,.local_bytes=saved.local_bytes,.shared_bytes=saved.shared_bytes,.code=saved.code,.code_size=saved.code_size};
    SatchelScalarProgram checked;int status=satchel_scalar_admit(&input,&checked);if(status)return status;
    if(memcmp(&saved,&checked,sizeof(checked)))return SATCHEL_SCALAR_INVALID;
    SatchelScalarInput actual=*dispatch;
    if(actual.known&~31u)return SATCHEL_SCALAR_INVALID;
    actual.known=(actual.known&6u)|checked.known;actual.local_x=checked.local_x;actual.local_y=checked.local_y;actual.local_z=checked.local_z;actual.local_bytes=checked.local_bytes;actual.shared_bytes=checked.shared_bytes;
    actual.code=checked.code;actual.code_size=checked.code_size;
    return satchel_scalar_compile(&actual,out,capacity,reflection);
}
