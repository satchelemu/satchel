#include "HorizonIPC.h"
#include "libnx_reference.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
static void check(int okay,const char *reason) {++checks;if(!okay){fprintf(stderr,"FAIL: %s\n",reason);exit(1);}}
static void put32(uint8_t *p,uint32_t v) {for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(i*8));}
static void put64(uint8_t *p,uint64_t v) {put32(p,(uint32_t)v);put32(p+4,(uint32_t)(v>>32));}
static int zero(const void *p,size_t n) {const uint8_t *b=p;for(size_t i=0;i<n;i++)if(b[i])return 0;return 1;}
static SatchelHIPCStatus parse(const uint8_t *bytes,size_t n,SatchelHIPCSessionMode mode,SatchelHIPCRequest *r) {
    memset(r,0xa5,sizeof(*r));
    SatchelHIPCStatus status=satchel_hipc_parse_request(bytes,n,UINT64_C(1)<<39,mode,r);
    if(status!=SATCHEL_HIPC_OK)check(zero(r,sizeof(*r)),"failure clears every output byte");
    return status;
}
static void rejected(const uint8_t *bytes,size_t n,SatchelHIPCSessionMode mode,SatchelHIPCStatus expected) {
    SatchelHIPCRequest r;check(parse(bytes,n,mode,&r)==expected,"expected bounded parse rejection");
}
static void reference_request(uint8_t message[256],int domain,int context) {
    memset(message,0,256);
    CmifRequest r=cmifMakeRequest(message,(CmifRequestFormat){.object_id=domain?7:0,
        .request_id=123,.context=context?0x1234:0,.data_size=12,.num_in_buffers=1,
        .num_out_buffers=1,.num_inout_buffers=1,.num_in_pointers=1,
        .num_out_fixed_pointers=1,.num_objects=domain?2:0,.num_handles=2,.send_pid=1});
    for(unsigned i=0;i<12;i++)((uint8_t*)r.data)[i]=(uint8_t)(i*7+3);
    put64(message+12,UINT64_C(0x123456789abcdef0));
    r.hipc.copy_handles[0]=0x100;r.hipc.copy_handles[1]=0x101;
    r.hipc.send_statics[0]=hipcMakeSendStatic((void*)(uintptr_t)UINT64_C(0x7123456780),0x1234,47);
    r.hipc.send_buffers[0]=hipcMakeBuffer((void*)(uintptr_t)UINT64_C(0x1234567890),UINT64_C(0x100000055),HipcBufferMode_NonSecure);
    r.hipc.recv_buffers[0]=hipcMakeBuffer((void*)(uintptr_t)UINT64_C(0x2234567890),0x77,HipcBufferMode_NonDevice);
    r.hipc.exch_buffers[0]=hipcMakeBuffer((void*)(uintptr_t)UINT64_C(0x3234567890),0x88,HipcBufferMode_Normal);
    r.hipc.recv_list[0]=hipcMakeRecvStatic((void*)(uintptr_t)UINT64_C(0x4234567890),0x99);
    if(domain){r.objects[0]=3;r.objects[1]=4;}
}
static void tipc_checks(void) {
    SatchelHIPCRequest r;
    Handle handles[15];for(unsigned i=0;i<15;i++)handles[i]=0x100+i;
    SfBuffer buffers[8]={{(void*)(uintptr_t)UINT64_C(0x7123456000),0x321},
        {(void*)(uintptr_t)0x200000,0x47},{(void*)(uintptr_t)0x300000,0x89}};
    const SfBufferAttrs attrs={.attr0=SfBufferAttr_In|SfBufferAttr_HipcMapAlias,
        .attr1=SfBufferAttr_Out|SfBufferAttr_HipcMapAlias|SfBufferAttr_HipcMapTransferAllowsNonSecure,
        .attr2=SfBufferAttr_In|SfBufferAttr_Out|SfBufferAttr_HipcMapAlias|SfBufferAttr_HipcMapTransferAllowsNonDevice};
    for(unsigned pid=0;pid<2;pid++)for(unsigned count=0;count<=15;count++)for(unsigned bytes=0;bytes<=9;bytes++) {
        memset(reference_tls,0,256);
        void *data=tipcMakeRequest(17,bytes,pid,attrs,buffers,count,handles);
        if(bytes)memset(data,0x5a,bytes);
        if(pid)put64(reference_tls+12,UINT64_C(0x123456789abcdef0));
        check(parse(reference_tls,256,SATCHEL_HIPC_SESSION,&r)==SATCHEL_HIPC_OK,"public TIPC encoder parses");
        check(r.kind==SATCHEL_HIPC_TIPC_REQUEST && r.type==33 && r.command_id==17 &&
              !r.cmif_offset && !r.cmif_version && !r.token && !r.payload_size_exact,"TIPC is explicit, never CMIF");
        check((!bytes || r.payload_offset==(size_t)((uint8_t*)data-reference_tls)) &&
              r.payload_offset==8+((pid||count)?4:0)+pid*8+count*4+36 && r.payload_capacity==(bytes+3)/4*4,
              "TIPC raw payload has no CMIF alignment or padding");
        check(r.send_pid==pid && r.copy_handle_count==count && r.buffer_count==3 &&
              r.buffers[0].address==UINT64_C(0x7123456000) && r.buffers[1].mode==1 && r.buffers[2].mode==3,
              "TIPC handles/PID and bounded map-alias descriptors");
        uint8_t payload[12]={0};
        check(satchel_hipc_copy_payload(&r,0,payload,bytes) && (!bytes || !memcmp(payload,data,bytes)),"TIPC payload copied with bounds");
        for(size_t n=0;n<r.consumed_size;n++) {
            SatchelHIPCRequest cut;
            check(parse(reference_tls,n,SATCHEL_HIPC_SESSION,&cut)!=SATCHEL_HIPC_OK,"every TIPC truncated prefix rejected");
        }
    }
    memset(reference_tls,0,256);
    tipcMakeRequest(0,0,true,(SfBufferAttrs){0},buffers,0,NULL);
    check(parse(reference_tls,20,SATCHEL_HIPC_SESSION,&r)==SATCHEL_HIPC_OK && r.command_id==0 && r.send_pid &&
          r.payload_capacity==0 && r.payload_offset==20,"SM TIPC Initialize PID and no argument");
    rejected(reference_tls,256,SATCHEL_HIPC_DOMAIN,SATCHEL_HIPC_UNSUPPORTED);
    memset(reference_tls,0,256);tipcMakeRequest(1,8,false,(SfBufferAttrs){0},buffers,0,NULL);
    check(parse(reference_tls,16,SATCHEL_HIPC_SESSION,&r)==SATCHEL_HIPC_OK && r.command_id==1 && r.payload_capacity==8,
          "SM TIPC GetService eight-byte schema capacity");
    reference_tls[0]=0xff;reference_tls[1]=0xff;
    check(parse(reference_tls,16,SATCHEL_HIPC_SESSION,&r)==SATCHEL_HIPC_OK && r.command_id==65519,"TIPC full header command range");
    memset(reference_tls,0,256);hipcMakeRequest(reference_tls,(HipcMetadata){.type=15});
    check(parse(reference_tls,8,SATCHEL_HIPC_SESSION,&r)==SATCHEL_HIPC_OK && r.kind==SATCHEL_HIPC_TIPC_CLOSE,"TIPC close distinct from CMIF close");
    reference_tls[4]=1;rejected(reference_tls,256,SATCHEL_HIPC_SESSION,SATCHEL_HIPC_INVALID);
    memset(reference_tls,0,256);hipcMakeRequest(reference_tls,(HipcMetadata){.type=16,.num_send_statics=1});
    rejected(reference_tls,256,SATCHEL_HIPC_SESSION,SATCHEL_HIPC_UNSUPPORTED);
    memset(reference_tls,0,256);hipcMakeRequest(reference_tls,(HipcMetadata){.type=16,.num_recv_statics=1});
    rejected(reference_tls,256,SATCHEL_HIPC_SESSION,SATCHEL_HIPC_UNSUPPORTED);

    uint8_t data[256];for(unsigned i=0;i<256;i++)data[i]=(uint8_t)(i*7+3);
    for(unsigned count=0;count<=15;count++)for(unsigned bytes=0;bytes<=256;bytes++) {
        SatchelHIPCReply reply,old;memset(&reply,0xa5,sizeof(reply));old=reply;
        SatchelTIPCReplyFormat f={.data=data,.data_size=bytes,.move_handles=handles,.move_handle_count=count};
        const size_t raw=8+(count?4+count*4:0),end=raw+4+(bytes+3)/4*4;
        SatchelHIPCStatus status=satchel_tipc_encode_reply(&f,&reply);
        if(end>256){check(status==SATCHEL_HIPC_LIMIT && !memcmp(&reply,&old,sizeof(reply)),"oversize TIPC reply atomic rejection");continue;}
        check(status==SATCHEL_HIPC_OK && reply.message_size==end,"TIPC reply exact bounded length");
        check(zero(reply.snapshot+end,256-end) && zero(reply.snapshot+raw+4+bytes,end-raw-4-bytes),"reply tail and word padding zeroed");
        memcpy(reference_tls,reply.snapshot,256);
        HipcResponse wire=hipcParseResponse(reference_tls);
        check(wire.num_move_handles==count && wire.num_copy_handles==0 && wire.num_data_words==1+(bytes+3)/4 &&
              *wire.data_words==0,"public HIPC decoder validates reply envelope");
        TipcService objects[15]={0};Handle out_handles[8]={0};void *out=NULL;
        check(tipcParseResponse(bytes,&out,count,objects,(SfOutHandleAttrs){0},out_handles)==0 &&
              (!bytes || !memcmp(out,data,bytes)),"public TIPC response decoder recovers payload");
        for(unsigned i=0;i<count;i++)check(objects[i].session==handles[i],"public TIPC decoder recovers moved session handles");
    }
    SatchelHIPCReply reply,old;SatchelTIPCReplyFormat f={.result=0x415};
    check(satchel_tipc_encode_reply(&f,&reply)==SATCHEL_HIPC_OK && reply.message_size==12,"TIPC error is result-only twelve bytes");
    memcpy(reference_tls,reply.snapshot,256);TipcService object={0x777};void *out=data;Handle out_handles[8]={0};
    check(tipcParseResponse(4,&out,1,&object,(SfOutHandleAttrs){0},out_handles)==0x415 &&
          out==data && object.session==0x777,"public TIPC error decoder leaves outputs untouched");
    for(unsigned scenario=0;scenario<8;scenario++) {
        f=(SatchelTIPCReplyFormat){0};
        switch(scenario) {
            case 0:f.data_size=1;break;
            case 1:f.move_handle_count=1;break;
            case 2:f.data_size=SIZE_MAX;break;
            case 3:f.move_handle_count=UINT32_MAX;break;
            case 4:f.result=1;f.data=data;f.data_size=1;break;
            case 5:f.result=1;f.move_handles=handles;f.move_handle_count=1;break;
            case 6:handles[0]=0;f.move_handles=handles;f.move_handle_count=1;break;
            case 7:f.data=data;f.data_size=245;break;
        }
        memset(&reply,0xa5,sizeof(reply));old=reply;
        check(satchel_tipc_encode_reply(&f,&reply)!=SATCHEL_HIPC_OK && !memcmp(&old,&reply,sizeof(reply)),"invalid reply preserves entire output");
    }
    old=reply;check(satchel_tipc_encode_reply(NULL,&reply)==SATCHEL_HIPC_INVALID && !memcmp(&old,&reply,sizeof(reply)),"null format atomic rejection");
    f=(SatchelTIPCReplyFormat){0};check(satchel_tipc_encode_reply(&f,NULL)==SATCHEL_HIPC_INVALID,"null reply rejected");
    memset(&reply,0xa5,sizeof(reply));f=(SatchelTIPCReplyFormat){.data=reply.snapshot,.data_size=17};
    check(satchel_tipc_encode_reply(&f,&reply)==SATCHEL_HIPC_OK,"reply may alias source payload");
    for(unsigned i=12;i<29;i++)check(reply.snapshot[i]==0xa5,"aliased payload copied before output commit");
}
static void cmif_reply_checks(void) {
    uint8_t data[256];for(unsigned i=0;i<256;i++)data[i]=(uint8_t)(i*13+1);
    uint32_t handles[15],objects[8];
    for(unsigned i=0;i<15;i++)handles[i]=0x101+i;
    for(unsigned i=0;i<8;i++)objects[i]=0x201+i;
    for(unsigned domain=0;domain<2;domain++)for(unsigned count=0;count<=15;count++)
    for(unsigned object_count=0;object_count<=(domain?8U:0U);object_count++)for(unsigned bytes=0;bytes<=256;bytes++) {
        const size_t raw=8+(count?4+count*4:0),aligned=(raw+15)&~(size_t)15;
        const size_t cmif=aligned+domain*16,payload=cmif+16,end=(payload+bytes+object_count*4+3)&~(size_t)3;
        SatchelCMIFReplyFormat f={.mode=(SatchelHIPCSessionMode)domain,.cmif_version=bytes&1,.token=0x12345678,
            .data=data,.data_size=bytes,.move_handles=handles,.move_handle_count=count,.objects=objects,.object_count=object_count};
        SatchelHIPCReply reply,old;memset(&reply,0xa5,sizeof(reply));old=reply;
        SatchelHIPCStatus status=satchel_cmif_encode_reply(&f,&reply);
        if(end>256){check(status==SATCHEL_HIPC_LIMIT && !memcmp(&reply,&old,sizeof(reply)),"oversize CMIF reply atomic rejection");continue;}
        check(status==SATCHEL_HIPC_OK && reply.message_size==end,"CMIF reply exact bounded length");
        check(zero(reply.snapshot+raw,aligned-raw) && zero(reply.snapshot+end,256-end) &&
              zero(reply.snapshot+payload+bytes+object_count*4,end-payload-bytes-object_count*4),"CMIF padding/tail initialized");
        memcpy(reference_tls,reply.snapshot,256);
        HipcResponse wire=hipcParseResponse(reference_tls);
        check(wire.num_data_words==(end-raw)/4 && wire.num_move_handles==count && !wire.num_copy_handles,
              "public HIPC decoder validates CMIF envelope");
        CmifOutHeader *header=(CmifOutHeader*)(reference_tls+cmif);
        check(header->magic==CMIF_OUT_HEADER_MAGIC && header->version==(bytes&1) &&
              header->token==0x12345678 && header->result==0,"CMIF explicit version/token/result preserved");
        if(domain) {
            CmifDomainOutHeader *dh=(CmifDomainOutHeader*)(reference_tls+aligned);
            check(dh->num_out_objects==object_count && zero(reference_tls+aligned+4,12),"domain object count/padding");
        }
        CmifResponse decoded={0};
        check(cmifParseResponse(&decoded,reference_tls,domain,bytes)==0 &&
              !memcmp(decoded.data,data,bytes),"public CMIF decoder recovers service payload");
        for(unsigned i=0;i<count;i++)check(cmifResponseGetMoveHandle(&decoded)==handles[i],"public CMIF decoder recovers move handles");
        for(unsigned i=0;i<object_count;i++) {
            if(!(bytes&3))check(cmifResponseGetObject(&decoded)==objects[i],"public CMIF decoder recovers domain objects");
            else {
                // Public helper dereferences u32; the wire permits byte payloads.
                // Use bytewise comparison when objects follow an odd payload.
                uint8_t expected[4];put32(expected,objects[i]);
                check(!memcmp(reference_tls+payload+bytes+i*4,expected,4),"unaligned domain object encoded bytewise");
            }
        }
    }
    SatchelHIPCReply reply,old;SatchelCMIFReplyFormat f={0};
    check(satchel_cmif_encode_reply(&f,&reply)==SATCHEL_HIPC_OK && reply.message_size==32 && reply.snapshot[4]==6,
          "canonical session result-only CMIF matches existing SM shape");
    // Conversion-to-domain control response uses a normal session header and
    // one schema-defined object ID in data, not a domain output object table.
    uint8_t object_data[4];put32(object_data,0x123);
    f=(SatchelCMIFReplyFormat){.mode=SATCHEL_HIPC_SESSION,.data=object_data,.data_size=4};
    check(satchel_cmif_encode_reply(&f,&reply)==SATCHEL_HIPC_OK,"domain conversion reply encodes in session format");
    memcpy(reference_tls,reply.snapshot,256);CmifResponse decoded={0};
    check(cmifParseResponse(&decoded,reference_tls,false,4)==0 && !memcmp(decoded.data,object_data,4),
          "public conversion response decoder sees schema object ID");
    for(unsigned domain=0;domain<2;domain++)for(unsigned version=0;version<2;version++) {
        f=(SatchelCMIFReplyFormat){.mode=(SatchelHIPCSessionMode)domain,.cmif_version=version,.result=0x415,.token=0xfedcba98};
        check(satchel_cmif_encode_reply(&f,&reply)==SATCHEL_HIPC_OK && reply.message_size==(domain?48:32),"CMIF error-only envelope");
        memcpy(reference_tls,reply.snapshot,256);memset(&decoded,0xa5,sizeof(decoded));CmifResponse saved=decoded;
        check(cmifParseResponse(&decoded,reference_tls,domain,0)==0x415 && !memcmp(&decoded,&saved,sizeof(decoded)),
              "public CMIF decoder returns explicit error without publishing data/handles");
    }
    for(unsigned scenario=0;scenario<13;scenario++) {
        f=(SatchelCMIFReplyFormat){0};
        switch(scenario) {
            case 0:f.mode=(SatchelHIPCSessionMode)2;break;
            case 1:f.cmif_version=2;break;
            case 2:f.data_size=1;break;
            case 3:f.move_handle_count=1;break;
            case 4:f.mode=SATCHEL_HIPC_DOMAIN;f.object_count=1;break;
            case 5:f.objects=objects;f.object_count=1;break;
            case 6:f.mode=SATCHEL_HIPC_DOMAIN;f.object_count=9;break;
            case 7:f.move_handle_count=UINT32_MAX;break;
            case 8:f.data_size=SIZE_MAX;break;
            case 9:f.result=1;f.data=data;f.data_size=1;break;
            case 10:f.result=1;f.move_handles=handles;f.move_handle_count=1;break;
            case 11:f.mode=SATCHEL_HIPC_DOMAIN;f.result=1;f.objects=objects;f.object_count=1;break;
            case 12:objects[0]=0;f.mode=SATCHEL_HIPC_DOMAIN;f.objects=objects;f.object_count=1;break;
        }
        memset(&reply,0xa5,sizeof(reply));old=reply;
        check(satchel_cmif_encode_reply(&f,&reply)!=SATCHEL_HIPC_OK && !memcmp(&old,&reply,sizeof(reply)),"invalid CMIF encoding atomic rejection");
    }
    old=reply;check(satchel_cmif_encode_reply(NULL,&reply)==SATCHEL_HIPC_INVALID && !memcmp(&old,&reply,sizeof(reply)),"CMIF null format preserves output");
    f=(SatchelCMIFReplyFormat){0};check(satchel_cmif_encode_reply(&f,NULL)==SATCHEL_HIPC_INVALID,"CMIF null output rejected");
    handles[0]=0;f=(SatchelCMIFReplyFormat){.move_handles=handles,.move_handle_count=1};
    check(satchel_cmif_encode_reply(&f,&reply)==SATCHEL_HIPC_INVALID && !memcmp(&old,&reply,sizeof(reply)),"zero CMIF move handle rejected atomically");
    memset(&reply,0xa5,sizeof(reply));f=(SatchelCMIFReplyFormat){.data=reply.snapshot,.data_size=17};
    check(satchel_cmif_encode_reply(&f,&reply)==SATCHEL_HIPC_OK,"CMIF reply may alias source data");
    for(unsigned i=32;i<49;i++)check(reply.snapshot[i]==0xa5,"aliased CMIF source copied before commit");
}
static void cmif_copy_reply_checks(void) {
    uint8_t data[256];for(unsigned i=0;i<256;i++)data[i]=(uint8_t)(i*7+3);
    uint32_t copies[15],moves[15],objects[8];
    for(unsigned i=0;i<15;i++){copies[i]=0x301+i;moves[i]=0x401+i;}
    for(unsigned i=0;i<8;i++)objects[i]=0x501+i;
    for(unsigned domain=0;domain<2;domain++)for(unsigned nc=1;nc<=15;nc++)
    for(unsigned nm=0;nm<=15;nm++)for(unsigned object_case=0;object_case<=(domain?1u:0u);object_case++)
    for(unsigned bytes=0;bytes<=256;bytes++) {
        unsigned no=object_case?8:0;
        const size_t raw=12+(nc+nm)*4,aligned=(raw+15)&~(size_t)15;
        const size_t cmif=aligned+domain*16,payload=cmif+16,end=(payload+bytes+no*4+3)&~(size_t)3;
        SatchelCMIFReplyFormat f={.mode=(SatchelHIPCSessionMode)domain,.cmif_version=bytes&1,.token=0x91827364,
            .data=data,.data_size=bytes,.move_handles=moves,.move_handle_count=nm,.objects=objects,.object_count=no,
            .copy_handles=copies,.copy_handle_count=nc};
        SatchelHIPCReply reply,old;memset(&reply,0xa5,sizeof(reply));old=reply;
        SatchelHIPCStatus status=satchel_cmif_encode_reply(&f,&reply);
        if(end>256){check(status==SATCHEL_HIPC_LIMIT && !memcmp(&reply,&old,sizeof(reply)),"copy/move reply capacity fails atomically");continue;}
        check(status==SATCHEL_HIPC_OK && reply.message_size==end,"mixed handle reply bounded exact size");
        check(zero(reply.snapshot+raw,aligned-raw) && zero(reply.snapshot+end,256-end) &&
              zero(reply.snapshot+payload+bytes+no*4,end-payload-bytes-no*4),"mixed handle reply padding initialized");
        memcpy(reference_tls,reply.snapshot,256);HipcResponse wire=hipcParseResponse(reference_tls);
        check(wire.num_copy_handles==nc && wire.num_move_handles==nm && wire.num_data_words==(end-raw)/4,
              "public HIPC decoder distinguishes copied and moved handles");
        CmifOutHeader*header=(CmifOutHeader*)(reference_tls+cmif);
        check(header->magic==CMIF_OUT_HEADER_MAGIC && header->version==(bytes&1) &&
              header->token==0x91827364 && !header->result,"mixed handles retain exact SFCO header");
        CmifResponse decoded={0};
        check(cmifParseResponse(&decoded,reference_tls,domain,bytes)==0 && !memcmp(decoded.data,data,bytes),
              "public CMIF decoder recovers mixed-handle payload");
        for(unsigned i=0;i<nc;i++)check(cmifResponseGetCopyHandle(&decoded)==copies[i],"public decoder recovers copy handles in input order");
        for(unsigned i=0;i<nm;i++)check(cmifResponseGetMoveHandle(&decoded)==moves[i],"public decoder recovers move handles after copy category");
        for(unsigned i=0;i<no;i++) {
            if(!(bytes&3))check(cmifResponseGetObject(&decoded)==objects[i],"copy handles coexist with domain object outputs");
            else {uint8_t expected[4];put32(expected,objects[i]);check(!memcmp(reference_tls+payload+bytes+i*4,expected,4),"unaligned domain IDs remain bytewise encoded");}
        }
    }
    SatchelHIPCReply reply,old;SatchelCMIFReplyFormat f={.copy_handles=copies,.copy_handle_count=1};
    check(satchel_cmif_encode_reply(&f,&reply)==SATCHEL_HIPC_OK && reply.message_size==32,"readable-event reply one copied handle");
    uint8_t expected[256]={0};put32(expected+4,0x80000004);put32(expected+8,2);put32(expected+12,copies[0]);put32(expected+16,0x4f434653);
    check(!memcmp(reply.snapshot,expected,256),"original canonical GetEventHandle wire fixture");
    for(unsigned scenario=0;scenario<6;scenario++) {
        f=(SatchelCMIFReplyFormat){.copy_handles=copies,.copy_handle_count=1};
        uint32_t invalid=0;
        switch(scenario) {
            case 0:f.copy_handles=NULL;break;
            case 1:f.copy_handle_count=16;break;
            case 2:f.copy_handle_count=UINT32_MAX;break;
            case 3:f.result=0x415;break;
            case 4:f.copy_handles=&invalid;break;
            case 5:f.copy_handle_count=15;f.move_handles=moves;f.move_handle_count=15;f.data=data;f.data_size=256;break;
        }
        memset(&reply,0xa5,sizeof(reply));old=reply;
        check(satchel_cmif_encode_reply(&f,&reply)!=SATCHEL_HIPC_OK && !memcmp(&reply,&old,sizeof(reply)),"invalid copied-handle reply preserves full output");
    }
    // The output overlaps actual aligned u32 input handles and data; encoder
    // must finish all reads before copying its independent owned wire result.
    _Alignas(16) uint32_t alias[(sizeof(SatchelHIPCReply)+3)/4];
    for(size_t i=0;i<sizeof(alias)/sizeof(alias[0]);i++)alias[i]=0x600+(uint32_t)i;
    uint32_t original[8];memcpy(original,alias,sizeof(original));
    f=(SatchelCMIFReplyFormat){.copy_handles=alias,.copy_handle_count=3,.move_handles=alias+3,.move_handle_count=2,
                         .data=alias+5,.data_size=12};
    check(satchel_cmif_encode_reply(&f,(SatchelHIPCReply*)(void*)alias)==SATCHEL_HIPC_OK,"copied/moved handles may alias reply destination");
    memcpy(&reply,alias,sizeof(reply));memcpy(reference_tls,reply.snapshot,256);CmifResponse decoded={0};
    check(cmifParseResponse(&decoded,reference_tls,false,12)==0 && !memcmp(decoded.data,original+5,12),"aliased payload preserved until commit");
    for(unsigned i=0;i<3;i++)check(cmifResponseGetCopyHandle(&decoded)==original[i],"aliased copy handles snapshotted before commit");
    for(unsigned i=0;i<2;i++)check(cmifResponseGetMoveHandle(&decoded)==original[3+i],"aliased move handles snapshotted before commit");
}

int main(void) {
    _Alignas(16) uint8_t message[256],mutated[256];
    SatchelHIPCRequest r;
    for(int domain=0;domain<2;domain++)for(int context=0;context<2;context++) {
        reference_request(message,domain,context);
        check(parse(message,sizeof(message),(SatchelHIPCSessionMode)domain,&r)==SATCHEL_HIPC_OK,"pinned libnx CMIF encoder comparison");
        check(r.kind==(uint32_t)(domain?SATCHEL_HIPC_DOMAIN_REQUEST:SATCHEL_HIPC_REQUEST) && r.type==(uint32_t)(context?6:4),"request/domain/context types");
        check(r.command_id==123 && r.cmif_version==(uint32_t)context && r.token==(uint32_t)(!domain&&context?0x1234:0),"CMIF command/version/token");
        check(r.send_pid && r.claimed_pid==UINT64_C(0x123456789abcdef0),"PID wire value retained as untrusted metadata");
        check(r.copy_handle_count==2 && r.move_handle_count==0 && r.copy_handles[0]==0x100 && r.copy_handles[1]==0x101,"handle categories preserved");
        check(r.buffer_count==5 && r.buffers[0].kind==SATCHEL_HIPC_SEND_STATIC && r.buffers[0].index==47 &&
              r.buffers[0].address==UINT64_C(0x7123456780) && r.buffers[0].size==0x1234,"static high-address/index decoding");
        check(r.buffers[1].address==UINT64_C(0x1234567890) && r.buffers[1].size==UINT64_C(0x100000055) && r.buffers[1].mode==1,"buffer high-address/high-size decoding");
        check(r.buffers[2].kind==SATCHEL_HIPC_RECEIVE_BUFFER && r.buffers[2].mode==3 && r.buffers[3].kind==SATCHEL_HIPC_EXCHANGE_BUFFER,"receive/exchange modes");
        check(r.buffers[4].kind==SATCHEL_HIPC_RECEIVE_STATIC && r.buffers[4].address==UINT64_C(0x4234567890) && r.buffers[4].size==0x99,"receive-list address/size");
        check(r.payload_capacity==12 && r.payload_size_exact==(uint8_t)domain,"exact domain payload versus session capacity");
        if(domain)check(r.object_id==7 && r.input_object_count==2 && r.input_objects[0]==3 && r.input_objects[1]==4,"domain object identifiers");
        uint8_t data[12];check(satchel_hipc_copy_payload(&r,0,data,sizeof(data)),"bounded payload copy");
        for(unsigned i=0;i<12;i++)check(data[i]==i*7+3,"original fixture payload preserved");
        const size_t used=r.consumed_size;
        for(size_t n=0;n<used;n++) {
            SatchelHIPCRequest short_request;check(parse(message,n,(SatchelHIPCSessionMode)domain,&short_request)!=SATCHEL_HIPC_OK,"every truncated prefix rejects");
        }
        check(!satchel_hipc_copy_payload(&r,12,data,1) && !satchel_hipc_copy_payload(&r,SIZE_MAX,data,1) &&
              !satchel_hipc_copy_payload(&r,0,NULL,1),"payload extent/null/overflow guarded");
        message[r.payload_offset]^=0xff;check(satchel_hipc_copy_payload(&r,0,data,1) && data[0]==3,"result owns immutable snapshot");
        SatchelHIPCRequest aliased=r;
        check(satchel_hipc_copy_payload(&aliased,0,aliased.snapshot+aliased.payload_offset+1,11) &&
              aliased.snapshot[aliased.payload_offset+1]==3 && aliased.snapshot[aliased.payload_offset+11]==73,
              "valid overlapping host output uses defined move semantics");
        check(zero(r.snapshot+r.consumed_size,256-r.consumed_size),"unconsumed TLS tail is not retained");
    }
    reference_request(message,0,0);check(parse(message,256,SATCHEL_HIPC_SESSION,&r)==SATCHEL_HIPC_OK,"baseline request");
    const size_t cmif=r.cmif_offset;
    for(unsigned copies=0;copies<=15;copies++) {
        memset(mutated,0,256);
        HipcRequest layout=hipcMakeRequest(mutated,(HipcMetadata){.type=4,.num_data_words=8,
            .send_pid=1,.num_copy_handles=copies,.num_move_handles=15});
        uint8_t *header=cmifGetAlignedDataStart(layout.data_words,mutated);
        put32(header,CMIF_IN_HEADER_MAGIC);put32(header+8,42);
        for(unsigned i=0;i<copies;i++)layout.copy_handles[i]=0x100+i;
        for(unsigned i=0;i<15;i++)layout.move_handles[i]=0x200+i;
        check(parse(mutated,256,SATCHEL_HIPC_SESSION,&r)==SATCHEL_HIPC_OK,"public HIPC handle/alignment matrix");
        check(r.copy_handle_count==copies && r.move_handle_count==15 && r.payload_capacity==0 &&
              r.command_id==42 && r.cmif_offset==(size_t)(header-mutated),"fixed CMIF pad independent of raw start alignment");
        for(unsigned i=0;i<copies;i++)check(r.copy_handles[i]==0x100+i,"copy handle order");
        for(unsigned i=0;i<15;i++)check(r.move_handles[i]==0x200+i,"move handle order");
    }
    memset(mutated,0,256);
    CmifRequest pointer_request=cmifMakeRequest(mutated,(CmifRequestFormat){.request_id=77,.data_size=3,.num_out_pointers=1});
    memset(pointer_request.data,0x5a,3);pointer_request.out_pointer_sizes[0]=0x1234;
    pointer_request.hipc.recv_list[0]=hipcMakeRecvStatic((void*)(uintptr_t)0x700000,0x1234);
    check(parse(mutated,256,SATCHEL_HIPC_SESSION,&r)==SATCHEL_HIPC_OK && !r.payload_size_exact && r.payload_capacity==8,
          "session capacity explicitly includes pointer-size table and word padding");
    memset(mutated,0,256);
    CmifRequest odd_domain=cmifMakeRequest(mutated,(CmifRequestFormat){.object_id=7,.request_id=78,.data_size=3,.num_objects=1});
    memset(odd_domain.data,0x5a,3);put32((uint8_t*)odd_domain.objects,9);
    check(parse(mutated,256,SATCHEL_HIPC_DOMAIN,&r)==SATCHEL_HIPC_OK && r.payload_size_exact && r.payload_capacity==3 &&
          r.input_objects[0]==9,"unaligned domain object ID decoded without native dereference");
    for(uint32_t type=0;type<15;type++)if(type!=2 && type!=4 && type!=5 && type!=6 && type!=7) {
        memcpy(mutated,message,256);mutated[0]=(uint8_t)type;mutated[1]=0;rejected(mutated,256,SATCHEL_HIPC_SESSION,SATCHEL_HIPC_UNSUPPORTED);
    }
    memcpy(mutated,message,256);mutated[5]|=0x40;rejected(mutated,256,SATCHEL_HIPC_SESSION,SATCHEL_HIPC_INVALID);
    memcpy(mutated,message,256);mutated[6]|=0x10;rejected(mutated,256,SATCHEL_HIPC_SESSION,SATCHEL_HIPC_UNSUPPORTED);
    for(unsigned mode=1;mode<=2;mode++) {
        memcpy(mutated,message,256);mutated[5]=(uint8_t)((mutated[5]&~0x3c)|(mode<<2));
        rejected(mutated,256,SATCHEL_HIPC_SESSION,SATCHEL_HIPC_UNSUPPORTED);
    }
    memcpy(mutated,message,256);mutated[9]|=2;rejected(mutated,256,SATCHEL_HIPC_SESSION,SATCHEL_HIPC_INVALID);
    memcpy(mutated,message,256);mutated[cmif]^=1;rejected(mutated,256,SATCHEL_HIPC_SESSION,SATCHEL_HIPC_INVALID);
    memcpy(mutated,message,256);put32(mutated+cmif+4,2);rejected(mutated,256,SATCHEL_HIPC_SESSION,SATCHEL_HIPC_UNSUPPORTED);
    // A/B/W descriptors start at36 in this public-encoder layout.
    memcpy(mutated,message,256);mutated[44]=(uint8_t)((mutated[44]&~3)|2);rejected(mutated,256,SATCHEL_HIPC_SESSION,SATCHEL_HIPC_INVALID);
    memcpy(mutated,message,256);mutated[46]|=0x80;rejected(mutated,256,SATCHEL_HIPC_SESSION,SATCHEL_HIPC_INVALID);
    memset(mutated,0,256);hipcMakeRequest(mutated,(HipcMetadata){.type=2});
    check(parse(mutated,8,SATCHEL_HIPC_SESSION,&r)==SATCHEL_HIPC_OK && r.kind==SATCHEL_HIPC_CLOSE,"session close has no CMIF");
    mutated[4]=1;rejected(mutated,256,SATCHEL_HIPC_SESSION,SATCHEL_HIPC_INVALID);
    memset(mutated,0,256);cmifMakeCloseRequest(mutated,7);
    check(parse(mutated,256,SATCHEL_HIPC_DOMAIN,&r)==SATCHEL_HIPC_OK && r.kind==SATCHEL_HIPC_DOMAIN_CLOSE && r.object_id==7,"domain close parsed without fabricated response");
    memset(mutated,0,256);cmifMakeControlRequest(mutated,0,0);
    check(parse(mutated,256,SATCHEL_HIPC_DOMAIN,&r)==SATCHEL_HIPC_OK && r.kind==SATCHEL_HIPC_CONTROL,"domain handle control retains non-domain CMIF layout");
    reference_request(message,1,1);check(parse(message,256,SATCHEL_HIPC_DOMAIN,&r)==SATCHEL_HIPC_OK,"domain mutation baseline");
    const size_t domain=r.cmif_offset-16;
    memcpy(mutated,message,256);mutated[domain]=3;rejected(mutated,256,SATCHEL_HIPC_DOMAIN,SATCHEL_HIPC_UNSUPPORTED);
    memcpy(mutated,message,256);mutated[domain+1]=9;rejected(mutated,256,SATCHEL_HIPC_DOMAIN,SATCHEL_HIPC_LIMIT);
    memcpy(mutated,message,256);mutated[domain+2]=255;mutated[domain+3]=255;rejected(mutated,256,SATCHEL_HIPC_DOMAIN,SATCHEL_HIPC_TRUNCATED);
    memcpy(mutated,message,256);put32(mutated+domain+4,0);rejected(mutated,256,SATCHEL_HIPC_DOMAIN,SATCHEL_HIPC_INVALID);
    memcpy(mutated,message,256);put32(mutated+domain+8,1);rejected(mutated,256,SATCHEL_HIPC_DOMAIN,SATCHEL_HIPC_INVALID);
    rejected(NULL,256,SATCHEL_HIPC_SESSION,SATCHEL_HIPC_INVALID);rejected(message,257,SATCHEL_HIPC_SESSION,SATCHEL_HIPC_LIMIT);
    check(satchel_hipc_parse_request(message,256,UINT64_C(1)<<39,SATCHEL_HIPC_SESSION,NULL)==SATCHEL_HIPC_INVALID,"null output rejected");
    check(satchel_hipc_parse_request(message,256,3,SATCHEL_HIPC_SESSION,&r)==SATCHEL_HIPC_INVALID && zero(&r,sizeof(r)),"invalid address-space bound rejected");
    // Deterministic malformed requests remain bounded under ASan/UBSan.
    uint32_t random=0x81573a21;
    for(unsigned i=0;i<10000;i++) {
        reference_request(mutated,i&1,(i>>1)&1);
        for(unsigned n=0;n<1+i%7;n++) {
            random^=random<<13;random^=random>>17;random^=random<<5;
            mutated[(random>>8)&255]^=(uint8_t)random;
        }
        SatchelHIPCStatus status=parse(mutated,(i%5)?256:(random&255),(SatchelHIPCSessionMode)(i&1),&r);
        check(status<=SATCHEL_HIPC_LIMIT,"malformed input returns a declared status");
        if(status==SATCHEL_HIPC_OK) {
            check(r.consumed_size<=r.message_size && r.buffer_count<=SATCHEL_HIPC_BUFFER_LIMIT,"successful output remains bounded");
            for(unsigned j=0;j<r.buffer_count;j++)check(r.buffers[j].address<=(UINT64_C(1)<<39) &&
                r.buffers[j].size<=(UINT64_C(1)<<39)-r.buffers[j].address,"successful descriptor remains in guest address space");
        }
    }
    tipc_checks();
    cmif_reply_checks();
    cmif_copy_reply_checks();
    printf("Horizon IPC: %u original/public-codec checks passed; no service execution or game payloads.\n",checks);
    return 0;
}
