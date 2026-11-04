#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Pinned dmsh_build XC options, not the restricted fixture API. Output belongs
   to the caller until satchel_game_msl_free. Error diagnostics never contain MSL. */
int satchel_game_spirv_to_msl(const void *spirv, size_t length, char **msl, size_t *msl_length);
void satchel_game_msl_free(void *msl);
#ifdef __cplusplus
}
#endif
