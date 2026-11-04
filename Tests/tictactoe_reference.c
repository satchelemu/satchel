#include <assert.h>
#include <stdlib.h>
#include "NativeSwitch/switch.h"
#define main downloaded_game_main
#include GAME_SOURCE
#undef main

static uint32_t pixels[1280*720];
static unsigned frame;
static const char *directory;
static const uint32_t inputs[] = {0,1,0,1u<<14,0,1,0,1u<<13,0,1,0,0};
void gfxInitDefault(void) {}
bool appletMainLoop(void) { return frame < sizeof(inputs)/sizeof(inputs[0]); }
void hidScanInput(void) {}
u32 hidKeysDown(int controller) { (void)controller;return inputs[frame] & ~(frame?inputs[frame-1]:0); }
void *gfxGetFramebuffer(u32 *width,u32 *height){*width=1280;*height=720;return pixels;}
void gfxFlushBuffers(void) {}
void gfxSwapBuffers(void){
 char path[1024];snprintf(path,sizeof path,"%s/frame-%02u.rgba",directory,frame);
 FILE*f=fopen(path,"wb");assert(f);assert(fwrite(pixels,1,sizeof pixels,f)==sizeof pixels);fclose(f);frame++;
}
void gfxWaitForVsync(void){}
void gfxExit(void){}
int main(int argc,char**argv){assert(argc==2);directory=argv[1];int result=downloaded_game_main(0,NULL);assert(result==0);assert(frame==12);puts("PASS: generated12frames from author's unmodified C game source with scripted controls");}
