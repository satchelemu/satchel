/* Initial legacy-libnx Horizon services. Wire layouts follow switchbrew/libnx
 * 726f8adfe3f3b4970b9d384d85838d644e736676 (2018-05-20). This is a bounded
 * software-display profile, not a generic Horizon/NV implementation. */
enum {
    H_SM = 1, H_APPLET, H_PROXY, H_FUNCTIONS, H_COMMON, H_SELF, H_WINDOW,
    H_LIBRARY, H_AUDIO, H_DISPLAY, H_DEBUG, H_APM, H_APM_SESSION,
    H_HID, H_HID_RESOURCE, H_HID_MEMORY, H_TIME, H_CLOCK, H_ZONE, H_FS,
    H_SD, H_VI, H_VI_APP, H_VI_SYSTEM, H_VI_MANAGER, H_BINDER,
    H_APPLET_EVENT, H_VSYNC_EVENT, H_BUFFER_EVENT, H_NV_EVENT, H_TRANSFER,
    H_NV
};
enum { H_NV_MAP = 1, H_NV_CTRL_GPU, H_NV_AS_GPU, H_NV_GPU, H_NV_CTRL };
typedef struct { uint64_t address; uint32_t size; } HBuffer;
typedef struct {
    uint32_t type, command, raw_size, num_handles;
    uint32_t handles[8];
    uint8_t data[256];
    HBuffer send[8], recv[8];
    uint32_t num_send, num_recv;
} HRequest;

static uint32_t h_u32(const void *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static uint64_t h_u64(const void *p) { uint64_t v; memcpy(&v, p, 8); return v; }
static void h_w32(void *p, uint32_t v) { memcpy(p, &v, 4); }
static void h_w64(void *p, uint64_t v) { memcpy(p, &v, 8); }
static uint32_t h_handle(SatchelVM *vm, uint32_t kind, uint32_t value) {
    for (unsigned i = 1; i < 128; i++) if (!vm->horizon.handles[i].kind) {
        vm->horizon.handles[i] = (SatchelHHandle){.kind=kind, .value=value};
        return 0x100 + i;
    }
    fault(vm, "Horizon handle table exhausted", kind); return 0;
}
static SatchelHHandle *h_object(SatchelVM *vm, uint32_t handle) {
    if (handle <= 0x100 || handle >= 0x180 || !vm->horizon.handles[handle-0x100].kind) return NULL;
    return &vm->horizon.handles[handle-0x100];
}
static bool h_buffer(SatchelVM *vm, HBuffer b, uint32_t minimum, bool write) {
    if (b.size < minimum || b.size > SATCHEL_NRO_MEMORY_SIZE) { fault(vm, "Invalid IPC buffer size", b.size); return false; }
    return access_ok(vm, b.address, b.size, write);
}
static bool h_parse(SatchelVM *vm, HRequest *r) {
    if (!access_ok(vm, SATCHEL_NRO_TLS, 256, true)) return false;
    const uint8_t *p = vm->memory + SATCHEL_NRO_TLS;
    uint32_t a=h_u32(p), b=h_u32(p+4), at=8;
    memset(r, 0, sizeof(*r)); r->type=a&0xFFFF;
    if (r->type==2) return true;
    if (r->type!=4 && r->type!=5) { fault(vm, "Unsupported HIPC message type", r->type); return false; }
    if (b>>31) {
        uint32_t d=h_u32(p+at); at+=4;
        if (d&1) at+=8;
        r->num_handles=((d>>1)&15)+((d>>5)&15);
        if(r->num_handles>8 || at+r->num_handles*4>256) { fault(vm,"Invalid IPC handle count",r->num_handles);return false; }
        for(unsigned i=0;i<r->num_handles;i++,at+=4) r->handles[i]=h_u32(p+at);
    }
    unsigned nx=(a>>16)&15, na=(a>>20)&15, nb=(a>>24)&15, nw=(a>>28)&15;
    if(nx+na>8 || nb+nw>8 || at+nx*8+(na+nb+nw)*12>256) { fault(vm,"Invalid IPC descriptors",a);return false; }
    for(unsigned i=0;i<nx;i++,at+=8) {
        uint32_t d=h_u32(p+at),lo=h_u32(p+at+4),size=d>>16;
        uint64_t address=lo|((uint64_t)((d>>12)&15)<<32)|((uint64_t)((d>>6)&15)<<36);
        if(size) r->send[r->num_send++]=(HBuffer){address,size};
    }
    for(unsigned i=0;i<na+nb+nw;i++,at+=12) {
        uint32_t size=h_u32(p+at),lo=h_u32(p+at+4),d=h_u32(p+at+8);
        uint64_t address=lo|((uint64_t)(d>>28)<<32)|((uint64_t)((d>>2)&15)<<36);
        if(!size) continue;
        if(i<na) r->send[r->num_send++]=(HBuffer){address,size};
        else r->recv[r->num_recv++]=(HBuffer){address,size};
    }
    uint32_t raw_end=at+(b&0x3FF)*4;
    at=(at+15)&~15u;
    if(raw_end>256 || at+16>raw_end || h_u32(p+at)!=0x49434653 || h_u32(p+at+4)!=0) { fault(vm,"Invalid CMIF request",at);return false; }
    if(h_u32(p+at+12)) { fault(vm,"Unsupported CMIF command high bits",h_u32(p+at+12));return false; }
    r->command=h_u32(p+at+8);r->raw_size=raw_end-at-16;
    memcpy(r->data,p+at+16,r->raw_size);
    /* Pointer-buffer size is advertised as zero, so nonzero C descriptors are
     * not used by this profile. Reject them explicitly rather than miscopying. */
    unsigned nc=(b>>10)&15; if(nc>2)nc-=2;else if(nc)nc=1;
    if(raw_end+nc*8>256) { fault(vm,"Invalid IPC receive descriptor",raw_end);return false; }
    for(unsigned i=0;i<nc;i++) {
        uint32_t lo=h_u32(p+raw_end+i*8),d=h_u32(p+raw_end+i*8+4),size=d>>16;
        if(size) {
            if(r->num_recv==8) {fault(vm,"IPC receive descriptor overflow",nc);return false;}
            r->recv[r->num_recv++]=(HBuffer){lo|((uint64_t)(d&0xFFFF)<<32),size};
        }
    }
    return true;
}
static void h_reply(SatchelVM *vm, uint32_t result, const void *data, uint32_t size, uint32_t handle, bool copy) {
    if(vm->fault)return;
    if(size>208 || !access_ok(vm,SATCHEL_NRO_TLS,256,true)) {fault(vm,"IPC response too large",size);return;}
    uint8_t *p=vm->memory+SATCHEL_NRO_TLS;memset(p,0,256);
    h_w32(p+4,(size+16+16+3)/4 | (handle?0x80000000u:0));
    if(handle) {h_w32(p+8,copy?2:32);h_w32(p+12,handle);}
    h_w32(p+16,0x4F434653);h_w32(p+24,result);
    if(size)memcpy(p+32,data,size);
    vm->x[0]=0;
}
static void h_reply_u64(SatchelVM *vm,uint64_t value) {h_reply(vm,0,&value,8,0,false);}
static void h_reply_object(SatchelVM *vm,uint32_t kind,uint32_t value) {uint32_t handle=h_handle(vm,kind,value);h_reply(vm,0,NULL,0,handle,false);}
static void h_unknown(SatchelVM *vm,uint32_t kind,uint32_t command) {
    vm->fault=true;
    snprintf(vm->error,sizeof(vm->error),"Horizon IPC not implemented: service %u command %u at PC 0x%llx",kind,command,(unsigned long long)vm->pc);
}
static void h_input(SatchelVM *vm, bool consume_pending) {
    SatchelHorizon *h=&vm->horizon;
    if(!h->hid_address)return;
    /* Legacy HID's controller array starts at0x9A00. Seven layouts per
     * controller,0x350 bytes each; defaultlayout6 uses 17-entry ring. */
    uint64_t seq=++h->input_sequence;
    uint64_t buttons=h->inputbuttons | (consume_pending?h->pendingbuttons:0);
    if(consume_pending)h->pendingbuttons=0;
    uint8_t *base=vm->memory+h->hid_address;
    unsigned controller=8; /* handheld, selected by CONTROLLER_P1_AUTO */
    uint8_t *c=base+0x9A00+controller*0x5000;
    h_w32(c,1u<<1);
    for(unsigned layout=0;layout<7;layout++) {
        uint8_t *l=c+0x28+layout*0x350;
        h_w64(l,seq);h_w64(l+8,1);h_w64(l+16,0);h_w64(l+24,16);
        h_w64(l+0x20,seq);h_w64(l+0x28,seq);h_w64(l+0x30,buttons);
        h_w64(l+0x48,3); /* connected and wired */
    }
}
static uint64_t h_block_offset(uint32_t xbyte,uint32_t y,uint32_t stride) {
    uint32_t width_gobs=(stride+63)/64;
    return (uint64_t)(y/128)*512*16*width_gobs+(uint64_t)(xbyte/64)*512*16+
        (uint64_t)((y%128)/8)*512+((xbyte%64)/32)*256+((y%8)/2)*64+((xbyte%32)/16)*32+(y%2)*16+xbyte%16;
}
static bool h_present(SatchelVM *vm,SatchelHBuffer *b,uint32_t transform) {
    SatchelHorizon *h=&vm->horizon;
    if(b->map==0 || b->map>=64 || !h->maps[b->map].allocated) {fault(vm,"Unmapped display buffer",b->map);return false;}
    SatchelHMap *map=&h->maps[b->map];
    if(b->format!=1 || !b->width || b->width>1280 || !b->height || b->height>720 || b->stride<b->width || b->stride>2048 || (transform&~2u)) {fault(vm,"Unsupported software display layout",b->format);return false;}
    uint32_t bytes=b->stride*4;
    uint64_t last=h_block_offset((b->width-1)*4+3,b->height-1,bytes);
    if(b->offset>map->size || last>=map->size-b->offset || !access_ok(vm,map->address+b->offset,(unsigned)(last+1),false)) {fault(vm,"Display buffer outside NV mapping",b->offset);return false;}
    if(!h->pixels) h->pixels=malloc(1280*720*4);
    if(!h->pixels){fault(vm,"Could not allocate display",0);return false;}
    const uint8_t *src=vm->memory+map->address+b->offset;
    for(uint32_t y=0;y<b->height;y++)for(uint32_t x=0;x<b->width;x++) {
        uint32_t sy=(transform&2)?b->height-1-y:y;
        uint64_t off=h_block_offset(x*4,sy,bytes);
        uint8_t *dst=h->pixels+(y*b->width+x)*4;
        memcpy(dst,src+off,4);dst[3]=255;
    }
    h->width=b->width;h->height=b->height;h->frame_count++;h->frame_ready=true;
    return true;
}
static void h_binder_transaction(SatchelVM *vm,const HRequest *r) {
    SatchelHorizon *h=&vm->horizon;
    if(r->raw_size<12 || h_u32(r->data)!=1 || !h->layer_open || r->num_send!=1 || r->num_recv!=1 ||
       !h_buffer(vm,r->send[0],16,false) || !h_buffer(vm,r->recv[0],512,true)) {fault(vm,"Invalid binder transaction",r->command);return;}
    const uint8_t *parcel=vm->memory+r->send[0].address;
    uint32_t length=h_u32(parcel),offset=h_u32(parcel+4),code=h_u32(r->data+4);
    if(offset<16 || offset>r->send[0].size || length>r->send[0].size-offset || length<8) {fault(vm,"Invalid parcel bounds",offset);return;}
    const uint8_t *p=parcel+offset;
    static const char token[]="android.gui.IGraphicBufferProducer";
    uint32_t chars=h_u32(p+4),token_size=(8+(chars+1)*2+3)&~3u;
    if(chars!=sizeof(token)-1 || token_size>length) {fault(vm,"Invalid binder interface",chars);return;}
    for(unsigned i=0;i<sizeof(token);i++) if(p[8+i*2]!=(uint8_t)token[i] || p[9+i*2]) {fault(vm,"Invalid binder interface",i);return;}
    p+=token_size;length-=token_size;
    uint8_t out[512]={0};uint8_t *q=out+16;uint32_t outsize=8;
    uint32_t slot=length>=4?h_u32(p):UINT32_MAX;
    if(code!=10 && !h->producer_connected) {fault(vm,"Buffer producer not connected",code);return;}
    switch(code) {
        case 10: /* connect CPU producer */
            if(length<12 || h_u32(p)!=0 || h_u32(p+4)!=2 || h->producer_connected) {fault(vm,"Invalid CPU producer connect",length);return;}
            h->producer_connected=true;h_w32(q,1280);h_w32(q+4,720);outsize=24;break;
        case 11:
            if(length<4 || h_u32(p)!=2) {fault(vm,"Invalid producer disconnect",slot);return;}
            h->producer_connected=false;break;
        case 14: { /* set preallocated buffer */
            if(length<16+364 || slot>=3 || h_u32(p+4)!=1 || h_u32(p+8)!=364 || h_u32(p+12)!=0) {fault(vm,"Invalid preallocated buffer",slot);return;}
            const uint8_t *f=p+16;SatchelHBuffer *b=&h->buffers[slot];
            uint32_t map=h_u32(f+40+4),off=h_u32(f+40+0x50),sz=h_u32(f+40+0x28);
            if(h_u32(f)!=0x47424652 || map==0 || map>=64 || !h->maps[map].allocated || off>h->maps[map].size || sz>h->maps[map].size-off || !sz) {fault(vm,"Invalid graphic-buffer mapping",map);return;}
            *b=(SatchelHBuffer){.configured=true,.width=h_u32(f+4),.height=h_u32(f+8),.stride=h_u32(f+12),.format=h_u32(f+16),.map=map,.offset=off,.size=sz};
            memcpy(b->flattened,f,364);break;
        }
        case 9: /* query native window attributes */
            if(length<4 || slot>2) {fault(vm,"Unsupported buffer query",slot);return;}
            h_w32(q,slot==0?1280:slot==1?720:1);outsize=12;break;
        case 3: { /* dequeue */
            if(length<20) {fault(vm,"Truncated dequeue request",length);return;}
            int chosen=-1;
            for(unsigned i=0;i<3;i++){unsigned s=(h->next_slot+i)%3;if(h->buffers[s].configured&&!h->buffers[s].dequeued){chosen=(int)s;break;}}
            if(chosen<0){fault(vm,"No available display buffer",0);return;}
            SatchelHBuffer *b=&h->buffers[chosen];
            if(h_u32(p+4)!=b->width || h_u32(p+8)!=b->height) {fault(vm,"Display dequeue dimensions mismatch",chosen);return;}
            b->dequeued=true;h->next_slot=((unsigned)chosen+1)%3;
            h_w32(q,(uint32_t)chosen);h_w32(q+4,1);h_w32(q+8,36);h_w32(q+12,0);
            h_w32(q+16,1);for(unsigned i=0;i<4;i++)h_w32(q+20+i*8,UINT32_MAX);
            outsize=60;break;
        }
        case 1: /* requestbuffer */
            if(length<4 || slot>=3 || !h->buffers[slot].configured) {fault(vm,"Unknown display slot",slot);return;}
            h_w32(q,1);h_w32(q+4,364);h_w32(q+8,0);memcpy(q+12,h->buffers[slot].flattened,364);outsize=384;break;
        case 7: { /* queue populated CPU framebuffer */
            if(length<12+84 || slot>=3 || !h->buffers[slot].dequeued || h_u32(p+4)!=84 || h_u32(p+8)!=0) {fault(vm,"Invalid display queue",slot);return;}
            const uint8_t *input=p+12;
            if(h_u32(input+48) && h_u32(input+52)!=UINT32_MAX) {fault(vm,"GPU fence waiting not implemented",h_u32(input+52));return;}
            const SatchelHBuffer *queued=&h->buffers[slot];
            uint32_t left=h_u32(input+12),top=h_u32(input+16),right=h_u32(input+20),bottom=h_u32(input+24);
            if(left||top||((right||bottom)&&(right!=queued->width||bottom!=queued->height))){fault(vm,"Display cropping not implemented",right);return;}
            if(!h_present(vm,&h->buffers[slot],h_u32(input+32)))return;
            h->buffers[slot].dequeued=false;h->current_slot=slot;
            h_w32(q,h->width);h_w32(q+4,h->height);outsize=24;break;
        }
        case 4:
            if(length<4 || slot>=3 || !h->buffers[slot].configured) {fault(vm,"Invalid detach slot",slot);return;}
            memset(&h->buffers[slot],0,sizeof(h->buffers[slot]));break;
        default:h_unknown(vm,H_BINDER,code);return;
    }
    h_w32(out,outsize);h_w32(out+4,16);h_w32(out+12,16+outsize);
    if(16+outsize>r->recv[0].size){fault(vm,"Binder reply exceeds output",outsize);return;}
    memcpy(vm->memory+r->recv[0].address,out,16+outsize);
    h_reply(vm,0,NULL,0,0,false);
}
static bool h_gpu_mapping(SatchelVM *vm,uint64_t address,uint64_t size) {
    for(unsigned i=0;i<96;i++){SatchelHGpuMapping *m=&vm->horizon.mappings[i];if(m->size&&address>=m->address&&address-m->address<=m->size&&size<=m->size-(address-m->address))return true;}
    return false;
}
static void h_nv_ioctl(SatchelVM *vm,const HRequest *r) {
    SatchelHorizon *h=&vm->horizon;
    if(r->raw_size<8){fault(vm,"Truncated NV ioctl",r->raw_size);return;}
    uint32_t fd=h_u32(r->data),req=h_u32(r->data+4),size=(req>>16)&0x3FFF,dir=req>>30;
    if(fd==0||fd>=16||!h->devices[fd].kind||!size||size>256){fault(vm,"Invalid NV ioctl",req);return;}
    uint8_t d[256]={0};
    if(dir&1){if(r->num_send!=1){fault(vm,"Missing NV ioctl input",req);return;}if(!h_buffer(vm,r->send[0],size,false))return;memcpy(d,vm->memory+r->send[0].address,size);}
    if(dir&2){if(r->num_recv!=1){fault(vm,"Missing NV ioctl output",req);return;}if(!h_buffer(vm,r->recv[0],size,true))return;}
    SatchelHDevice *dev=&h->devices[fd];uint32_t kind=dev->kind;
#ifdef SATCHEL_TRACE
    fprintf(stderr,"NV fd=%u kind=%u ioctl=%08x arg=%08x %08x %08x %08x %llx %llx %llx\n",fd,kind,req,h_u32(d),h_u32(d+4),h_u32(d+8),h_u32(d+12),(unsigned long long)h_u64(d+16),(unsigned long long)h_u64(d+24),(unsigned long long)h_u64(d+32));
#endif
    bool handled=false;
    if(kind==H_NV_MAP) {
        if(req==0xC0080101){ /* create */
            uint32_t length=h_u32(d);if(!length||length>SATCHEL_NRO_MEMORY_SIZE||(length&4095)){fault(vm,"Invalid NV map size",length);return;}
            for(unsigned i=1;i<64;i++)if(!h->maps[i].size){h->maps[i].size=length;h_w32(d+4,i);handled=true;break;}
        } else if(req==0xC0200104){ /* bind guest allocation */
            uint32_t id=h_u32(d),align=h_u32(d+12);uint64_t addr=h_u64(d+24);
            if(!id||id>=64||!h->maps[id].size||h->maps[id].allocated||align<4096||(align&(align-1))||(addr&4095)||!access_ok(vm,addr,h->maps[id].size,true)){fault(vm,"Invalid NV allocation",id);return;}
            h->maps[id].address=addr;h->maps[id].allocated=true;h->maps[id].flags=h_u32(d+8);h->maps[id].kind=d[16];handled=true;
        } else if(req==0xC008010E||req==0xC0080103){
            uint32_t id=h_u32(d+(req==0xC008010E?4:0));
            if(!id||id>=64||!h->maps[id].allocated){fault(vm,"Invalid NV map handle",id);return;}
            h_w32(d+(req==0xC008010E?0:4),id);handled=true;
        }
    } else if(kind==H_NV_CTRL_GPU) {
        /* Read-only properties describe the virtual compatibility device. No
         * render/compute engine is exposed; submissions remain unsupported. */
        switch(req){
            case 0x80044701:h_w32(d,0x1000);handled=true;break;
            case 0x80284702:h_w32(d,1);h_w32(d+4,1);h_w32(d+8,1);handled=true;break;
            case 0xC0B04705:
                if(h_u64(d)<0xA0||!h_u64(d+8)){fault(vm,"Invalid GPU characteristics query",h_u64(d));return;}
                memset(d+16,0,160);h_w64(d,0xA0);h_w32(d+16,0x120);h_w32(d+20,0xB);h_w32(d+24,0xA1);h_w32(d+28,1);h_w32(d+16+0x28,0x20000);h_w32(d+16+0x2C,0x20000);h_w32(d+16+0x34,0x30000);h_w32(d+16+0x5C,0xB197);handled=true;break;
            case 0xC0184706:h_w32(d+16,1);handled=true;break;
            case 0x80084714:memset(d,0,8);handled=true;break;
        }
    } else if(kind==H_NV_AS_GPU) {
        switch(req) {
            case 0x40284109:dev->initialized=true;dev->next_va=UINT64_C(0x100000000);handled=true;break;
            case 0xC0404108:
                h_w32(d+8,48);h_w64(d+16,0x100000000);h_w32(d+24,4096);h_w64(d+32,0x100000);
                h_w64(d+40,0x200000000);h_w32(d+48,65536);h_w64(d+56,0x100000);handled=true;break;
            case 0xC0184102: {
                uint64_t length=(uint64_t)h_u32(d)*h_u32(d+4),align=h_u64(d+16);
                if(!dev->initialized||!length||length>UINT64_C(0x1000000000)||!align||align>UINT64_C(0x100000000)||(align&(align-1))){fault(vm,"Invalid GPU virtual reservation",length);return;}
                uint64_t va=(dev->next_va+align-1)&~(align-1);if(va>UINT64_C(0x10000000000)-length){fault(vm,"GPU address space exhausted",va);return;}h_w64(d+16,va);dev->next_va=va+length;handled=true;break;
            }
            case 0x40044101: {
                uint32_t channel=h_u32(d);if(channel==0||channel>=16||h->devices[channel].kind!=H_NV_GPU){fault(vm,"Invalid NV channel",channel);return;}
                dev->channel=channel;handled=true;break;
            }
            case 0xC0284106: {
                uint32_t flags=h_u32(d),id=h_u32(d+8);uint64_t off=h_u64(d+16),len=h_u64(d+24),va=h_u64(d+32);
                if(flags==0x100){ /* remap subrange of existing CPU-backed VA */
                    if(id||!len||off>UINT64_MAX-va||!h_gpu_mapping(vm,va+off,len)){fault(vm,"Invalid GPU remapping",va);return;}handled=true;break;
                }
                if(!id||id>=64||!h->maps[id].allocated||off>h->maps[id].size){fault(vm,"Invalid GPU map object",id);return;}
                if(!len)len=h->maps[id].size-off;
                /* NV address-space mappings use GPU pages (64 KiB here).
                 * Legacy libnx maps a whole GPU page for several 4 KiB nvmap
                 * allocations. Only VA bookkeeping is rounded; CPU/display
                 * access remains bounded by the original nvmap byte size. */
                uint32_t page_size=h_u32(d+12);if(!page_size)page_size=4096;
                if((page_size!=4096&&page_size!=65536&&page_size!=131072)||(flags&~5u)){fault(vm,"Unsupported GPU mapping flags/pages",flags);return;}
                uint64_t rounded=((uint64_t)h->maps[id].size+page_size-1)&~(uint64_t)(page_size-1);
                if(!len||off>rounded||len>rounded-off){fault(vm,"GPU map range outside allocation",len);return;}
                if(!(flags&1)){va=(dev->next_va+65535)&~UINT64_C(65535);dev->next_va=va+len;}
                if(va>UINT64_C(0x10000000000)-len){fault(vm,"GPU mapping exceeds address space",va);return;}
                for(unsigned i=0;i<96;i++)if(!h->mappings[i].size){h->mappings[i]=(SatchelHGpuMapping){fd,id,va,len,off};handled=true;break;}
                h_w64(d+32,va);break;
            }
        }
    } else if(kind==H_NV_GPU) {
        switch(req){
            case 0x40044801: {
                uint32_t mapfd=h_u32(d);if(mapfd==0||mapfd>=16||h->devices[mapfd].kind!=H_NV_MAP){fault(vm,"Invalid channel NV map fd",mapfd);return;}dev->nvmap=mapfd;handled=true;break;
            }
            case 0xC020481A:
                if(!dev->nvmap||!h_u32(d)||h_u32(d)>0x10000){fault(vm,"Invalid GPFIFO allocation",h_u32(d));return;}
                dev->initialized=true;h_w32(d+12,1);h_w32(d+16,0);handled=true;break;
            case 0xC0104809:
                if(!dev->initialized||h_u32(d)!=0xB197){fault(vm,"Unsupported GPU object class",h_u32(d));return;}h_w64(d+8,1);dev->object_class=h_u32(d);handled=true;break;
            case 0xC010480B:
                if(!h_gpu_mapping(vm,h_u64(d),4096)){fault(vm,"Invalid zcull mapping",h_u64(d));return;}dev->zcull=h_u64(d);handled=true;break;
            case 0x40084714:
                if(!access_ok(vm,h_u64(d),4096,true))return;dev->userdata=h_u64(d);handled=true;break;
            case 0x4004480D:
                if(h_u32(d)!=50&&h_u32(d)!=100&&h_u32(d)!=150){fault(vm,"Invalid channel priority",h_u32(d));return;}dev->priority=h_u32(d);handled=true;break;
            case 0xC018480C: {
                uint32_t id=h_u32(d+16);uint64_t off=h_u64(d),length=h_u64(d+8);
                if(!id||id>=64||!h->maps[id].allocated||off>h->maps[id].size||length>h->maps[id].size-off){fault(vm,"Invalid error notifier",id);return;}dev->error_map=id;handled=true;break;
            }
        }
    }
    if(!handled){fault(vm,"NV ioctl not implemented",req);return;}
    if(dir&2)memcpy(vm->memory+r->recv[0].address,d,size);
    uint32_t error=0;h_reply(vm,0,&error,4,0,false);
}
static void h_service(SatchelVM *vm,SatchelHHandle *object,const HRequest *r) {
    SatchelHorizon *h=&vm->horizon;uint32_t kind=object->kind,cmd=r->command;
#ifdef SATCHEL_TRACE
    fprintf(stderr,"IPC service=%u cmd=%u bytes=%u\n",kind,cmd,r->raw_size);
#endif
    if(r->type==5){
        if(cmd==3){h_reply_u64(vm,0);return;} /* no pointer-buffer reservation */
        if(cmd==2||cmd==4){h_reply_object(vm,kind,object->value);return;}
        h_unknown(vm,kind,cmd);return;
    }
    switch(kind){
        case H_SM:
            if(cmd==0){h->sm_initialized=true;h_reply(vm,0,NULL,0,0,false);return;}
            if(cmd==1){
                if(r->raw_size<8){fault(vm,"Truncated service name",r->raw_size);return;}
                char name[9]={0};memcpy(name,r->data,8);
                if(!h->sm_initialized){h_reply(vm,0x415,NULL,0,0,false);return;}
                if(!name[0]){h_reply(vm,0x615,NULL,0,0,false);return;}
                static const struct{const char *name;uint32_t kind,value;} services[]={
                    {"appletOE",H_APPLET,0},{"apm:p",H_APM,0},{"apm",H_APM,0},{"hid",H_HID,0},
                    {"time:s",H_TIME,0},{"time:u",H_TIME,0},{"fsp-srv",H_FS,0},{"vi:m",H_VI,2},{"vi:s",H_VI,1},{"vi:u",H_VI,0},
                    {"nvdrv",H_NV,0},{"nvdrv:a",H_NV,0},{"nvdrv:s",H_NV,0}
                };
                for(unsigned i=0;i<sizeof(services)/sizeof(*services);i++)if(!strcmp(name,services[i].name)){h_reply_object(vm,services[i].kind,services[i].value);return;}
                vm->fault=true;snprintf(vm->error,sizeof(vm->error),"Horizon service not implemented: %s at PC 0x%llx",name,(unsigned long long)vm->pc);return;
            }break;
        case H_APPLET:
            if(cmd==0){h->applet_initialized=true;h->aruid=1;h->focus_message=true;h_reply_object(vm,H_PROXY,0);return;}break;
        case H_PROXY:{
            uint32_t target=0;
            switch(cmd){case 0:target=H_COMMON;break;case 1:target=H_SELF;break;case 2:target=H_WINDOW;break;case 3:target=H_AUDIO;break;case 4:target=H_DISPLAY;break;case 11:target=H_LIBRARY;break;case 20:target=H_FUNCTIONS;break;case 1000:target=H_DEBUG;break;}
            if(target){h_reply_object(vm,target,0);return;}break;
        }
        case H_COMMON:
            if(cmd==0){uint32_t handle=h_handle(vm,H_APPLET_EVENT,0);h_reply(vm,0,NULL,0,handle,true);return;}
            if(cmd==1){if(h->focus_message){h->focus_message=false;h_reply_u64(vm,15);}else h_reply(vm,0x680,NULL,0,0,false);return;}
            if(cmd==5||cmd==6){h_reply_u64(vm,0);return;}
            if(cmd==9){h_reply_u64(vm,1);return;}break;
        case H_WINDOW:
            if(cmd==1){h_reply_u64(vm,h->aruid);return;}
            if(cmd==10){h->foreground=true;h_reply(vm,0,NULL,0,0,false);return;}break;
        case H_FUNCTIONS:
            if(cmd==40){h_reply_u64(vm,1);return;}break;
        case H_SELF:
            if(cmd==13&&r->raw_size>=3){memcpy(h->focus_policy,r->data,3);h_reply(vm,0,NULL,0,0,false);return;}
            if(cmd==16&&r->raw_size>=1){h->focus_policy[3]=r->data[0];h_reply(vm,0,NULL,0,0,false);return;}
            if((cmd==11||cmd==12)&&r->raw_size>=1){h->notifications[cmd-11]=!!r->data[0];h_reply(vm,0,NULL,0,0,false);return;}
            if(cmd==40){h_reply_u64(vm,1);return;}break;
        case H_APM:if(cmd==0){h_reply_object(vm,H_APM_SESSION,0);return;}break;
        case H_HID:
            if(cmd==0){if(r->raw_size<8||h_u64(r->data)!=h->aruid){fault(vm,"Invalid HID applet resource",r->raw_size);return;}h_reply_object(vm,H_HID_RESOURCE,0);return;}
            if(cmd==124){uint32_t id=h_u32(r->data);if(r->raw_size<16||id>=8||h_u64(r->data+8)!=h->aruid){fault(vm,"Invalid HID assignment",id);return;}h->dual_mode[id]=true;h_reply(vm,0,NULL,0,0,false);return;}break;
        case H_HID_RESOURCE:
            if(cmd==0){uint32_t handle=h_handle(vm,H_HID_MEMORY,0);h->hid_handle=handle;h_reply(vm,0,NULL,0,handle,true);return;}break;
        case H_TIME:if(cmd==0||cmd==1||cmd==3||cmd==4){h_reply_object(vm,cmd==3?H_ZONE:H_CLOCK,cmd);return;}break;
        case H_CLOCK:if(cmd==0){h_reply_u64(vm,(uint64_t)time(NULL));return;}break;
        case H_FS:
            if(cmd==1){object->value=1;h_reply(vm,0,NULL,0,0,false);return;}
            if(cmd==18&&object->value){h_reply_object(vm,H_SD,0);return;}break;
        case H_VI:
            if(cmd==object->value){h_reply_object(vm,H_VI_APP,object->value);return;}break;
        case H_VI_APP:
            if(cmd==100 || (cmd==103&&object->value>=1)){h_reply_object(vm,H_BINDER,1);return;}
            if(cmd==101&&object->value>=1){h_reply_object(vm,H_VI_SYSTEM,0);return;}
            if(cmd==102&&object->value>=2){h_reply_object(vm,H_VI_MANAGER,0);return;}
            if(cmd==1010){if(r->raw_size<64||memcmp(r->data,"Default\0",8)){fault(vm,"Unsupported display name",0);return;}h->display_open=true;h_reply_u64(vm,1);return;}
            if(cmd==1020&&h->display_open){h->display_open=false;h_reply(vm,0,NULL,0,0,false);return;}
            if(cmd==5202&&h->display_open){uint32_t handle=h_handle(vm,H_VSYNC_EVENT,0);h_reply(vm,0,NULL,0,handle,true);return;}
            if(cmd==1102&&h->display_open){uint64_t out[2]={1280,720};h_reply(vm,0,out,sizeof(out),0,false);return;}
            if(cmd==2020&&h->display_open){
                if(r->raw_size<80||h_u64(r->data+64)!=1||h_u64(r->data+72)!=h->aruid||r->num_recv!=1||!h_buffer(vm,r->recv[0],0x100,true)){fault(vm,"Invalid display layer open",cmd);return;}
                uint8_t native[0x100]={0};h_w32(native,0x28);h_w32(native+4,16);h_w32(native+12,0x38);h_w32(native+24,1);
                memcpy(vm->memory+r->recv[0].address,native,sizeof(native));h->layer_open=true;h_reply_u64(vm,0x38);return;
            }
            if(cmd==2021&&h->layer_open){h->layer_open=false;h_reply(vm,0,NULL,0,0,false);return;}
            if(cmd==2101&&h->layer_open){if(r->raw_size<16||h_u64(r->data+8)!=1){fault(vm,"Invalid display scaling layer",cmd);return;}h->scaling=h_u32(r->data);h_reply(vm,0,NULL,0,0,false);return;}break;
        case H_BINDER:
            if(cmd==0||cmd==3){h_binder_transaction(vm,r);return;}
            if(cmd==1){
                if(r->raw_size<12||h_u32(r->data)!=1||h_u32(r->data+8)>1){fault(vm,"Invalid binder reference",cmd);return;}
                int32_t delta=(int32_t)h_u32(r->data+4);uint32_t *refs=h_u32(r->data+8)?&h->strong_refs:&h->weak_refs;
                if((delta!=1&&delta!=-1)||(delta<0&&!*refs)||*refs>1024){fault(vm,"Invalid binder refcount",*refs);return;}*refs=(uint32_t)((int64_t)*refs+delta);h_reply(vm,0,NULL,0,0,false);return;
            }
            if(cmd==2){if(r->raw_size<8||h_u32(r->data)!=1||h_u32(r->data+4)!=15){fault(vm,"Invalid binder native handle",cmd);return;}uint32_t handle=h_handle(vm,H_BUFFER_EVENT,0);h_reply(vm,0,NULL,0,handle,true);return;}break;
        case H_NV:
            if(cmd==3){
                SatchelHHandle *transfer=r->num_handles==2?h_object(vm,r->handles[1]):NULL;
                if(r->raw_size<4||!transfer||transfer->kind!=H_TRANSFER||h_u32(r->data)!=transfer->size){fault(vm,"Invalid NV transfer memory",cmd);return;}
                h->transfer_address=transfer->address;h->transfer_size=transfer->size;h->nv_initialized=true;h_reply(vm,0,NULL,0,0,false);return;
            }
            if(cmd==8&&h->nv_initialized){if(r->raw_size<8||h_u64(r->data)!=h->aruid){fault(vm,"Invalid NV applet resource",cmd);return;}h_reply(vm,0,NULL,0,0,false);return;}
            if(cmd==0&&h->nv_initialized){
                if(r->num_send!=1||r->send[0].size>63||!h_buffer(vm,r->send[0],1,false)){fault(vm,"Invalid NV path",cmd);return;}
                char path[64]={0};memcpy(path,vm->memory+r->send[0].address,r->send[0].size);
                static const char *paths[]={"","/dev/nvmap","/dev/nvhost-ctrl-gpu","/dev/nvhost-as-gpu","/dev/nvhost-gpu","/dev/nvhost-ctrl"};uint32_t devkind=0;
                for(unsigned i=1;i<6;i++)if(!strcmp(path,paths[i])){devkind=i;break;}
                if(!devkind){fault(vm,"Unsupported NV device",0);return;}
                for(unsigned i=1;i<16;i++)if(!h->devices[i].kind){h->devices[i].kind=devkind;uint32_t out[2]={i,0};h_reply(vm,0,out,8,0,false);return;}
                fault(vm,"NV device table exhausted",0);return;
            }
            if(cmd==1&&h->nv_initialized){h_nv_ioctl(vm,r);return;}
            if(cmd==2){uint32_t fd=h_u32(r->data);if(r->raw_size<4||!fd||fd>=16||!h->devices[fd].kind){fault(vm,"Invalid NV close",fd);return;}memset(&h->devices[fd],0,sizeof(h->devices[fd]));uint32_t out=0;h_reply(vm,0,&out,4,0,false);return;}
            if(cmd==4){uint32_t fd=h_u32(r->data),id=h_u32(r->data+4);if(r->raw_size<8||!fd||fd>=16||!h->devices[fd].kind||id>3){fault(vm,"Invalid NV event",id);return;}uint32_t handle=h_handle(vm,H_NV_EVENT,fd),out=0;h_reply(vm,0,&out,4,handle,true);return;}break;
    }
    h_unknown(vm,kind,cmd);
}

static bool horizon_ipc(SatchelVM *vm,unsigned svc) {
    SatchelHorizon *h=&vm->horizon;
    switch(svc){
        case 0x1F:{
            uint64_t address=vm->x[1];if(!access_ok(vm,address,4,false))return true;
            if(memcmp(vm->memory+address,"sm:\0",4)){fault(vm,"Unsupported named port",address);return true;}
            vm->x[1]=h_handle(vm,H_SM,0);vm->x[0]=0;return true;
        }
        case 0x21:{
            SatchelHHandle *object=h_object(vm,(uint32_t)vm->x[0]);
            if(!object){vm->x[0]=0xE401;return true;}
            HRequest request;if(!h_parse(vm,&request))return true;
            if(request.type==2){memset(object,0,sizeof(*object));vm->x[0]=0;return true;}
            h_service(vm,object,&request);return true;
        }
        case 0x13:{
            SatchelHHandle *object=h_object(vm,(uint32_t)vm->x[0]);uint64_t address=vm->x[1],size=vm->x[2];
            if(!object||object->kind!=H_HID_MEMORY){vm->x[0]=0xE401;return true;}
            if((address&4095)||size!=0x40000||vm->x[3]!=1||address>vm->memory_size||size>vm->memory_size-address||h->hid_address){vm->x[0]=0xD401;return true;}
            for(uint64_t p=address/4096;p<(address+size)/4096;p++)if(vm->memory_type[p]){vm->x[0]=0xD801;return true;}
            map_pages(vm,(uint32_t)address,(uint32_t)size,1);memset(vm->memory+address,0,(size_t)size);
            for(uint64_t p=address/4096;p<(address+size)/4096;p++)vm->memory_type[p]=6;
            h->hid_address=(uint32_t)address;object->address=address;object->size=size;h_input(vm,false);vm->x[0]=0;return true;
        }
        case 0x14:{
            SatchelHHandle *object=h_object(vm,(uint32_t)vm->x[0]);
            if(!object||object->kind!=H_HID_MEMORY||vm->x[1]!=object->address||vm->x[2]!=object->size){vm->x[0]=0xD401;return true;}
            map_pages(vm,(uint32_t)object->address,(uint32_t)object->size,0);h->hid_address=0;object->address=object->size=0;vm->x[0]=0;return true;
        }
        case 0x15:{
            uint64_t address=vm->x[1],size=vm->x[2];
            if((address&4095)||!size||(size&4095)||address<SATCHEL_NRO_HEAP||address>SATCHEL_NRO_HEAP+SATCHEL_NRO_HEAP_SIZE||size>SATCHEL_NRO_HEAP+SATCHEL_NRO_HEAP_SIZE-address||vm->x[3]!=0){vm->x[0]=0xD401;return true;}
            if(!access_ok(vm,address,(unsigned)size,true))return true;
            uint32_t handle=h_handle(vm,H_TRANSFER,0);SatchelHHandle *object=h_object(vm,handle);if(!object)return true;
            object->address=address;object->size=size;
            for(uint64_t p=address/4096;p<(address+size)/4096;p++){vm->permissions[p]=0;vm->memory_type[p]=14;vm->reprotect[p]=false;}
            vm->x[0]=0;vm->x[1]=handle;return true;
        }
        case 0x16:{
            SatchelHHandle *object=h_object(vm,(uint32_t)vm->x[0]);if(!object){vm->x[0]=0xE401;return true;}
            if(object->kind==H_TRANSFER)map_pages(vm,(uint32_t)object->address,(uint32_t)object->size,3);
            memset(object,0,sizeof(*object));vm->x[0]=0;return true;
        }
        case 0x17:{
            SatchelHHandle *object=h_object(vm,(uint32_t)vm->x[0]);
            if(!object||object->kind<H_APPLET_EVENT||object->kind>H_NV_EVENT){vm->x[0]=0xE401;return true;}
            object->signaled=false;vm->x[0]=0;return true;
        }
        case 0x18:{
            uint64_t address=vm->x[1],count=vm->x[2];if(!count||count>64||!access_ok(vm,address,(unsigned)count*4,false)){vm->x[0]=0xD401;return true;}
            for(unsigned i=0;i<count;i++){
                SatchelHHandle *object=h_object(vm,(uint32_t)read_memory(vm,address+i*4,4));
                if(!object||object->kind<H_APPLET_EVENT||object->kind>H_NV_EVENT){vm->x[0]=0xE401;return true;}
                if(object->kind==H_APPLET_EVENT)h_input(vm,true);
                bool ready=object->kind==H_APPLET_EVENT?h->focus_message:object->kind==H_VSYNC_EVENT||object->kind==H_BUFFER_EVENT||object->signaled;
                if(ready){object->signaled=true;vm->x[0]=0;vm->x[1]=i;return true;}
            }
            vm->x[0]=0xEA01;vm->x[1]=UINT32_MAX;return true; /* virtual event not signaled */
        }
        default:return false;
    }
}
