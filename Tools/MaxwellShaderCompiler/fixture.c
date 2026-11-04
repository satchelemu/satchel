#include <stdint.h>
#include <stdio.h>
#include <string.h>
extern int satchel_shader_probe(const uint8_t *, int, uint8_t *, int);
extern int satchel_shader_runtime_features(void);
int main(int argc, char **argv) {
    if (satchel_shader_runtime_features() != 0) return 4;
    uint64_t words[4] = {0, UINT64_C(0xe30000000007000f), 0, 0};
    uint8_t output[65536];
    int count = satchel_shader_probe((const uint8_t *)words, sizeof(words), output, sizeof(output));
    uint32_t magic = 0; memcpy(&magic, output, sizeof(magic));
    if (count < 20 || magic != UINT32_C(0x07230203)) return 1;
    if (satchel_shader_probe((const uint8_t *)words, 0, output, sizeof(output)) != -1) return 2;
    if (satchel_shader_probe((const uint8_t *)words, sizeof(words), output, 1) != -2) return 3;
    if (argc == 2) {
        FILE *file = fopen(argv[1], "wb");
        if (!file) return 5;
        int failed = fwrite(output, 1, (size_t)count, file) != (size_t)count;
        if (fclose(file) || failed) return 6;
    }
    printf("Native C -> AOT shader translator: %d SPIR-V bytes; 4 assertions passed, dynamic-code features disabled.\n", count);
    return 0;
}
