/* Original synthetic AArch64 workload. This is NOT a Switch executable.
 * ABI: a private SVC yields a 160x240 RGBA framebuffer, then returns input.
 * All gameplay and pixel drawing execute inside the guest interpreter.
 */
typedef unsigned int u32;
typedef unsigned long long u64;
#define W 160
#define H 240
#ifdef NATIVE_REFERENCE
extern volatile u32 reference_pixels[W * H];
extern volatile int reference_state[7];
#define PIXELS reference_pixels
#define STATE reference_state
#else
#define PIXELS ((volatile u32 *)0x100000)
#define STATE ((volatile int *)0x200000)
#endif
#define INLINE static __attribute__((always_inline)) inline

INLINE void rect(int x, int y, int w, int h, u32 color) {
    for (int j = y; j < y + h; j++) {
        if (j < 0 || j >= H) continue;
        for (int i = x; i < x + w; i++) {
            if (i >= 0 && i < W) PIXELS[j * W + i] = color;
        }
    }
}

/* Host may replace this function for differential testing only. */
#ifdef NATIVE_REFERENCE
extern void reference_yield(int *touch, int *reset);
INLINE void present(int *touch, int *reset) { reference_yield(touch, reset); }
#else
INLINE void present(int *touch, int *reset) {
    register u64 a __asm__("x0") = 0;
    register u64 b __asm__("x1") = 0;
    __asm__ volatile("svc #0xF001" : "+r"(a), "+r"(b) : : "memory");
    *touch = (int)a;
    *reset = (int)b;
}
#endif

void guest_main(void) {
    int x = 80, y = 160, dx = 1, dy = -2, paddle = 80;
    int bricks = 0xFFFFFF, score = 0, lives = 3, frame = 0, touch = 80, reset = 0;
    for (;;) {
        if (reset) {
            x = 80; y = 160; dx = 1; dy = -2;
            bricks = 0xFFFFFF; score = 0; lives = 3; frame = 0;
        }
        paddle = touch;
        if (paddle < 18) paddle = 18;
        if (paddle > 142) paddle = 142;
        if (lives > 0 && bricks != 0) {
            x += dx; y += dy;
            if (x <= 5) { x = 5; dx = 1; }
            if (x >= 154) { x = 154; dx = -1; }
            if (y <= 5) { y = 5; dy = 2; }
            if (dy > 0 && y >= 213 && y <= 218 && x >= paddle - 19 && x <= paddle + 19) {
                y = 213; dy = -2; dx = x < paddle ? -1 : 1;
            }
            if (y > 238) { lives--; x = 80; y = 160; dy = -2; }
            for (int row = 0; row < 4; row++) {
                for (int col = 0; col < 6; col++) {
                    int mask = 1 << (row * 6 + col);
                    int bx = 8 + col * 24, by = 26 + row * 13;
                    if ((bricks & mask) && x >= bx - 2 && x <= bx + 23 && y >= by - 2 && y <= by + 11) {
                        bricks &= ~mask; score += 10; dy = -dy;
                    }
                }
            }
        }
        rect(0, 0, W, H, 0xFF1B1310);
        rect(3, 3, 1, 231, 0xFF46342A);
        rect(156, 3, 1, 231, 0xFF46342A);
        rect(3, 3, 154, 1, 0xFF46342A);
        for (int row = 0; row < 4; row++) {
            for (int col = 0; col < 6; col++) {
                if (bricks & (1 << (row * 6 + col))) {
                    u32 color = row < 2 ? 0xFF83EED1 : 0xFF84C6FF;
                    rect(8 + col * 24, 26 + row * 13, 21, 10, color);
                }
            }
        }
        rect(paddle - 17, 219, 34, 4, 0xFF83EED1);
        rect(x - 2, y - 2, 5, 5, 0xFFF2F5F6);
        for (int i = 0; i < lives; i++) rect(8 + i * 7, 10, 4, 4, 0xFF83EED1);
        if (lives == 0) rect(50, 116, 60, 3, 0xFF8080FF);
        if (bricks == 0) rect(50, 116, 60, 3, 0xFF83EED1);
        frame++;
        STATE[0] = score; STATE[1] = lives; STATE[2] = frame;
        STATE[3] = x; STATE[4] = y; STATE[5] = paddle; STATE[6] = bricks;
        present(&touch, &reset);
    }
}
