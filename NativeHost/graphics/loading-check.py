#!/usr/bin/env python3
"""Exercise the patched ReadFile against synthetic images outside the repository."""
from pathlib import Path
import argparse, os, struct, subprocess
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--out', type=Path, required=True)
p.add_argument('--engine', type=Path, default=Path.home()/'SatchelDeps/NX2Apple/engine')
a=p.parse_args(); a.out=a.out.resolve(); root=Path(__file__).resolve().parents[2]
if a.out.is_relative_to(root): p.error('Use a private output directory outside the repository')
a.out.mkdir(parents=True,exist_ok=True)
for name in ['hle_fs.c','hle_svc.c','romfs_image.h']:(a.out/name).write_bytes((a.engine/'dm/host'/name).read_bytes())
for name in ['host-time.patch','loading.patch']:
 subprocess.run(['patch','-p1','--fuzz=0','-i',Path(__file__).with_name(name)],cwd=a.out,check=True)
size=10*1024*1024+37
for variant in range(2):
 payload=bytes((i+variant*31)%251 for i in range(65536))
 with (a.out/f'image{variant}.img').open('wb') as f:
  f.write(struct.pack('<8sQQQ',b'DMROMFS1',32+size,49,2))
  for off in range(0,size,len(payload)):f.write(payload[:min(len(payload),size-off)])
  f.write(struct.pack('<QQIHBB',1,1,0,0,0,0))
  f.write(struct.pack('<QQIHBB',32,size,0,1,1,0)+b'x')
s=(a.out/'hle_fs.c').read_text(); start=s.index('HLE_FORCE("_ZN2nn2fs8ReadFileEPm'); end=s.index('HLE_FORCE("_ZN2nn2fs9WriteFile',start)
function=s[start:end]
macros='\n'.join(line for line in s.splitlines() if line.startswith(('#define IMG_', '#define FH_')))
fixture=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <mach/mach.h>
static uint64_t now_ns(void) { return clock_gettime_nsec_np(CLOCK_UPTIME_RAW); }
#define LOG(...) ((void)0)
#include "HostPause.h"
#include "romfs_image.h"
static int fail_map;
static void *test_map(void *a, size_t n, int p, int f, int fd, off_t off) {
    if (fail_map) { errno = ENOMEM; return MAP_FAILED; }
    return mmap(a,n,p,f,fd,off);
}
static _Atomic unsigned mapped_success, mapped_failure;
static kern_return_t test_read_overwrite(vm_map_read_t task, vm_address_t source, vm_size_t size, vm_address_t target, vm_size_t *copied) {
    kern_return_t result=vm_read_overwrite(task,source,size,target,copied);
    atomic_fetch_add(result==KERN_SUCCESS && *copied==size ? &mapped_success : &mapped_failure,1);
    return result;
}
#define mmap test_map
#define vm_read_overwrite test_read_overwrite
#include "RomFSRead.h"
#undef vm_read_overwrite
#undef mmap
#define HLE_FORCE(sym,desc,ret,name,...) ret name(__VA_ARGS__)
typedef uint32_t Result;
#define RES_OK 0
#define RES_FAIL 1
static struct { romfs_img *img; } g_mnt[1];
static struct { int fs; } g_flag;
static FILE *g_fslog;
static char *g_fdpath[4096];
static pthread_mutex_t g_fslog_lock = PTHREAD_MUTEX_INITIALIZER;
static uint64_t g_t0_ns;
static Result err_res(const char *op, const char *path) { return RES_FAIL; }
static _Thread_local int protect_again;
static _Thread_local void *protected_buffer;
static _Thread_local size_t protected_size;
static _Atomic unsigned faults;
static void on_fault(int sig, siginfo_t *info, void *context) {
    uintptr_t a=(uintptr_t)info->si_addr, b=(uintptr_t)protected_buffer;
    if (a < b || a-b >= protected_size || mprotect(protected_buffer, protected_size, PROT_READ|PROT_WRITE)) _exit(90);
    atomic_fetch_add(&faults,1);
}
static void dm_guest_write_prepare(void *buf, size_t size) {
    if (buf==protected_buffer && size) {
        assert(!mprotect(buf,protected_size,PROT_READ|PROT_WRITE));
        if (protect_again) { protect_again=0; assert(!mprotect(buf,protected_size,PROT_READ)); }
    }
}
'''
fixture='#include <time.h>\n'+fixture+macros+'\n'+function+r'''
static void check_read(uint64_t offset, size_t size, unsigned char *buffer) {
    unsigned char *expected=malloc(size ? size : 1); assert(expected);
    size_t want=offset>=g_mnt[0].img->e[1].b ? 0 : g_mnt[0].img->e[1].b-offset;
    if (want>size) want=size;
    assert(pread(g_mnt[0].img->fd,expected,want,(off_t)(32+offset))==(ssize_t)want);
    size_t got=SIZE_MAX;
    assert(h_fs_ReadFile(&got,FH_OUT(IMG_HANDLE(0,1)),(int64_t)offset,buffer,size)==RES_OK);
    assert(got==want && !memcmp(buffer,expected,want)); free(expected);
}
static void *worker(void *arg) {
    unsigned char *buf=malloc(65536); assert(buf);
    uint64_t state=(uintptr_t)arg+1;
    for (int i=0;i<1500;i++) { state=state*6364136223846793005ull+1; check_read(state%(10*1024*1024),1+(state>>32)%65536,buf); }
    free(buf); return NULL;
}
int main(int argc,char **argv) {
    struct sigaction act={0}; act.sa_sigaction=on_fault; act.sa_flags=SA_SIGINFO; sigemptyset(&act.sa_mask);
    assert(!sigaction(SIGBUS,&act,NULL) && !sigaction(SIGSEGV,&act,NULL));
    unsigned char *buf=malloc(5*1024*1024); assert(buf);
    for (int image=1;image<argc;image++) {
        g_mnt[0].img=romfs_image_open(argv[image]); assert(g_mnt[0].img);
        for (unsigned i=0;i<64;i++) check_read(i*16381,1+i*13,buf);
        check_read(3*1024*1024+123,2*1024*1024,buf);
        check_read(7*1024*1024+123,2*1024*1024,buf);
        check_read(0,5*1024*1024,buf);
        check_read(10*1024*1024+30,100,buf);
        check_read(11*1024*1024,100,buf);
        size_t got=SIZE_MAX;
        assert(h_fs_ReadFile(&got,FH_OUT(IMG_HANDLE(0,1)),-1,buf,100)==RES_OK && got==0);
        assert(h_fs_ReadFile(&got,0,0,buf,100)==RES_FAIL && got==0);
        errno=0; assert(h_fs_ReadFile(&got,FH_OUT(4000),0,buf,100)==RES_FAIL && errno==EBADF);
        errno=0; assert(h_fs_ReadFile(&got,FH_OUT(IMG_HANDLE(0,1)),0,(void *)1,100)==RES_FAIL && errno==EFAULT);
        fail_map=1; check_read(5*1024*1024,12345,buf); fail_map=0;
        protected_size=(size_t)getpagesize();
        protected_buffer=mmap(NULL,protected_size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0); assert(protected_buffer!=MAP_FAILED);
        protect_again=1; check_read(123,protected_size,protected_buffer);
        // Kernel copies must preserve shared guest aliases and subsequent writes.
        vm_address_t alias=0; vm_prot_t current=0, maximum=0;
        assert(vm_remap(mach_task_self(), &alias, protected_size, 0, VM_FLAGS_ANYWHERE,
                        mach_task_self(), (vm_address_t)protected_buffer, FALSE, &current, &maximum, VM_INHERIT_NONE)==KERN_SUCCESS);
        check_read(321,protected_size,protected_buffer);
        assert(!memcmp((void *)alias,protected_buffer,protected_size));
        ((unsigned char *)protected_buffer)[0]^=123;
        assert(*(unsigned char *)alias==*(unsigned char *)protected_buffer);
        assert(vm_deallocate(mach_task_self(),alias,protected_size)==KERN_SUCCESS);
        assert(!munmap(protected_buffer,protected_size)); protected_buffer=NULL;
        pthread_t th[8];
        for (uintptr_t i=0;i<8;i++) assert(!pthread_create(&th[i],NULL,worker,(void *)i));
        for (int i=0;i<8;i++) assert(!pthread_join(th[i],NULL));
        if (satchel_romfs_key_ok) assert(atomic_load(&satchel_romfs_readers)==1);
        romfs_image_close(g_mnt[0].img);
    }
    if (satchel_romfs_key_ok) { void *r=pthread_getspecific(satchel_romfs_key); assert(!pthread_setspecific(satchel_romfs_key,NULL)); satchel_romfs_destroy(r); }
    assert(atomic_load(&satchel_romfs_readers)==0);
    assert(atomic_load(&faults)==0);
    if (atoi(getenv("DM_ROMFS_MAPPED"))) {
        assert(atomic_load(&mapped_success)>20000);
        assert(atomic_load(&mapped_failure)>=4);
    } else assert(atomic_load(&mapped_success)==0 && atomic_load(&mapped_failure)==0);
    puts("ReadFile parity passed: 24000 concurrent reads, boundaries, negative offset, null/bad fd, invalid destination, map failure, write-protection race, shared aliases, reopen identity, thread cleanup");
    free(buf);
}
'''
(a.out/'loading-check.c').write_text(fixture)
exe=a.out/'loading-check'
subprocess.run(['xcrun','clang','-std=gnu11','-O2','-Wall','-Wextra','-Wno-unused-parameter','-Wno-unused-variable','-I',str(Path(__file__).parent),'-I',str(a.out),a.out/'loading-check.c','-o',exe],check=True)
for mode in ['0','1']:
 subprocess.run([exe,a.out/'image0.img',a.out/'image1.img'],env=dict(os.environ,DM_ROMFS_MAPPED=mode,DM_HOST_PAUSE='0',DM_HOST_TRACE=''),check=True)
