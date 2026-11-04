/* Link-only evidence. Never executed or packaged as an iPhone application. */
#include <SatchelShaderProbe/SatchelShaderProbe.h>
#include <SatchelShaderProbe/SatchelShaderFixture.h>
#include <SatchelCapturedFlat/CapturedFlat.h>
#include <stdint.h>
static uintptr_t volatile entries[]={ (uintptr_t)satchel_shader_runtime_features,(uintptr_t)satchel_shader_probe,(uintptr_t)satchel_shader_fixture_compile,(uintptr_t)satchel_captured_flat_compile };
int main(void){return entries[0]&&entries[1]&&entries[2]&&entries[3]?0:1;}
