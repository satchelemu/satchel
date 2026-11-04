/* Compile against the selected NX2Apple dm/host include directory. */
#include "romfs_image.h"
#include <stdio.h>

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    romfs_img *image = romfs_image_open(argv[1]);
    if (!image) { puts("FAIL NX2Apple host rejected image"); return 1; }
    printf("PASS NX2Apple host opened image entries=%llu\n", (unsigned long long)image->n);
    romfs_image_close(image);
    return 0;
}
