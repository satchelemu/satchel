/* Bounded single-process Horizon memory model. SVC register ABI follows libnx
 * kernel/svc.s; result codes/layouts follow switchbrew.org/wiki/SVC.
 * Unsupported IDs are returned as InvalidEnum, not invented successful values.
 */
static bool kernel_range(SatchelVM *vm, uint64_t address, uint64_t length) {
    return address < vm->memory_size && length <= vm->memory_size - address;
}
static bool horizon_kernel(SatchelVM *vm, unsigned svc) {
    uint64_t a = vm->x[0], b = vm->x[1], c = vm->x[2];
    if (svc == 0x29) { /* GetInfo */
        uint64_t value = 0; uint32_t rc = 0;
        switch (b) {
            case 0: value = 1; break; /* one virtual CPU */
            case 1: value = UINT64_MAX; break; /* priority range */
            case 2: case 14: value = 0x3000000; break;
            case 3: case 15: value = 0xE00000; break;
            case 4: value = SATCHEL_NRO_HEAP; break;
            case 5: value = SATCHEL_NRO_HEAP_SIZE; break;
            case 6: value = SATCHEL_NRO_MEMORY_SIZE; break;
            case 7: { for (unsigned p=0;p<vm->memory_size/4096;p++) if(vm->memory_type[p]) value+=4096; break; }
            case 8: value = 0; break; /* debugger detached */
            case 12: value = SATCHEL_NRO_BASE; break;
            case 13: value = SATCHEL_NRO_MEMORY_SIZE - SATCHEL_NRO_BASE; break;
            default: rc = 0xF001; break; /* unsupported info type */
        }
        if (!rc && b != 8 && c != 0xFFFF8001) rc = 0xE401;
        if (!rc && vm->x[3]) rc = 0xF001;
        vm->x[0] = rc; vm->x[1] = rc ? 0 : value;
        return true;
    }
    if (svc == 6) { /* QueryMemory: out buffer x0, queried address x2 */
        if (!access_ok(vm,a,40,true)) return true;
        uint64_t base, size; uint32_t type=0, attr=0, perm=0;
        if(c >= vm->memory_size) { base=vm->memory_size; size=UINT64_MAX-base+1; type=0x10; }
        else {
            unsigned page=(unsigned)(c/4096), start=page,end=page+1;
            type=vm->memory_type[page];attr=vm->memory_attributes[page];perm=vm->permissions[page];
            while(start && vm->memory_type[start-1]==type && vm->memory_attributes[start-1]==attr && vm->permissions[start-1]==perm)start--;
            while(end<vm->memory_size/4096 && vm->memory_type[end]==type && vm->memory_attributes[end]==attr && vm->permissions[end]==perm)end++;
            base=(uint64_t)start*4096;size=(uint64_t)(end-start)*4096;
        }
        uint8_t info[40]={0};memcpy(info,&base,8);memcpy(info+8,&size,8);memcpy(info+16,&type,4);memcpy(info+20,&attr,4);memcpy(info+24,&perm,4);
        memcpy(vm->memory+a,info,40);vm->x[0]=0;vm->x[1]=0;return true;
    }
    if (svc == 2 || svc == 3) { /* SetMemoryPermission / SetMemoryAttribute */
        uint32_t rc=0;
        if(a&4095)rc=0xCC01;
        else if(!b || (b&4095))rc=0xCA01;
        else if(!kernel_range(vm,a,b))rc=0xD401;
        else if(svc==2 && c!=0 && c!=1 && c!=3)rc=0xD801;
        else if(svc==3 && (c!=8 || (vm->x[3]&~UINT64_C(8))))rc=0xF001;
        if(!rc)for(uint64_t page=a/4096;page<(a+b)/4096;page++) {
            if(!vm->reprotect[page] || (vm->permissions[page]&4) || (vm->memory_attributes[page]&1)) {rc=0xD401;break;}
        }
        if(!rc)for(uint64_t page=a/4096;page<(a+b)/4096;page++) {
            if(svc==2)vm->permissions[page]=(uint8_t)c;
            else vm->memory_attributes[page]=(vm->memory_attributes[page]&~8)|(uint8_t)vm->x[3];
        }
        vm->x[0]=rc;return true;
    }
    if (svc == 0x25) { /* GetThreadId */
        vm->x[0] = (b == 0xFFFF8000 || b == 0x80) ? 0 : 0xE401; vm->x[1] = (b == 0xFFFF8000 || b == 0x80) ? 1 : 0;return true;
    }
    if(svc==0x10){vm->x[0]=0;return true;} /* current virtual CPU */
    return false;
}
