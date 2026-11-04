/* Test-only surface for compiling the author's unmodified Tic-Tac-Toe source
 * as an independent native framebuffer reference. Never linked into the app. */
#include <stdbool.h>
#include <stdint.h>
typedef uint32_t u32;
#define KEY_PLUS (1u << 10)
#define KEY_MINUS (1u << 11)
#define KEY_DLEFT (1u << 12)
#define KEY_DUP (1u << 13)
#define KEY_DRIGHT (1u << 14)
#define KEY_DDOWN (1u << 15)
#define KEY_JOYCON_RIGHT 1u
#define CONTROLLER_P1_AUTO 10
void gfxInitDefault(void);
bool appletMainLoop(void);
void hidScanInput(void);
u32 hidKeysDown(int controller);
void *gfxGetFramebuffer(u32 *width, u32 *height);
void gfxFlushBuffers(void);
void gfxSwapBuffers(void);
void gfxWaitForVsync(void);
void gfxExit(void);
