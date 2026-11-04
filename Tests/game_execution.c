#include "Interpreter.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static uint8_t *file(const char*path,size_t*size){FILE*f=fopen(path,"rb");assert(f);fseek(f,0,SEEK_END);*size=(size_t)ftell(f);rewind(f);uint8_t*b=malloc(*size);assert(b);assert(fread(b,1,*size,f)==*size);fclose(f);return b;}
static uint64_t rgb_hash(const uint8_t*p,size_t size){uint64_t h=UINT64_C(14695981039346656037);for(size_t i=0;i<size;i++)if(i%4!=3){h^=p[i];h*=UINT64_C(1099511628211);}return h;}
int main(int argc,char**argv){
 assert(argc==4);size_t size;uint8_t*bytes=file(argv[1],&size);char error[256];SatchelVM*v=satchel_create_nro(bytes,size,error,sizeof error);free(bytes);assert(v);
 const uint32_t inputs[]={0,1,0,1u<<14,0,1,0,1u<<13,0,1,0,0};
 unsigned matched=0,presentations=0;char path[1024];snprintf(path,sizeof path,"%s/frame-00.rgba",argv[2]);uint8_t*expected=file(path,&size);assert(size==1280*720*4);
 uint64_t expected_hash=rgb_hash(expected,size);clock_t start=clock();
 while(matched<12 && satchel_instructions(v)<UINT64_C(5000000000)){
  SatchelResult r=satchel_frame(v,0,0,1000000);
  if(r==SATCHEL_FAULT||r==SATCHEL_EXITED){fprintf(stderr,"Game stopped after %llu instructions: %s\n",(unsigned long long)satchel_instructions(v),satchel_error(v));return 2;}
  if(r==SATCHEL_WAIT){struct timespec pause={0,1000000};nanosleep(&pause,NULL);continue;}
  if(r!=SATCHEL_YIELD)continue;
  presentations++;uint64_t hash=satchel_display_checksum(v);
  printf("frame=%u reference=%u instructions=%llu rgb=%016llx seconds=%.3f\n",presentations,matched,(unsigned long long)satchel_instructions(v),(unsigned long long)hash,(double)(clock()-start)/CLOCKS_PER_SEC);fflush(stdout);
  snprintf(path,sizeof path,"%s/frame-%02u.rgba",argv[3],presentations);FILE*out=fopen(path,"wb");assert(out);fwrite(satchel_display_pixels(v),1,satchel_display_width(v)*satchel_display_height(v)*4,out);fclose(out);
  if(!matched && hash!=expected_hash && presentations<5){
   const uint8_t *display=satchel_display_pixels(v);
   for(size_t i=0;i<size;i++)if(i%4!=3)assert(display[i]==0);
   continue;
  } /* queue setup can present cleared buffers */
  assert(satchel_display_width(v)==1280 && satchel_display_height(v)==720);
  for(size_t i=0;i<size;i++)if(i%4!=3 && satchel_display_pixels(v)[i]!=expected[i]){fprintf(stderr,"RGB mismatch frame %u byte %zu: actual %u expected %u\n",matched,i,satchel_display_pixels(v)[i],expected[i]);return 3;}
  assert(hash==expected_hash);matched++;free(expected);expected=NULL;
  if(matched<12){satchel_set_buttons(v,inputs[matched]);snprintf(path,sizeof path,"%s/frame-%02u.rgba",argv[2],matched);expected=file(path,&size);expected_hash=rgb_hash(expected,size);}
 }
 assert(matched==12);
 satchel_set_buttons(v,1u<<10);SatchelResult result=SATCHEL_BUDGET;
 uint64_t exit_limit=satchel_instructions(v)+100000000;
 while(satchel_instructions(v)<exit_limit){result=satchel_frame(v,0,0,1000000);if(result==SATCHEL_FAULT||result==SATCHEL_EXITED)break;}
 if(result!=SATCHEL_EXITED){fprintf(stderr,"Guest exit failed: %s\n",satchel_error(v));return 4;}
 assert(satchel_exit_code(v)==0);satchel_destroy(v);puts("PASS: original downloaded game matches all RGB bytes across12native-referenceframes, includingcontroller moves, bothplayers and clean Plus-button exit");
}
