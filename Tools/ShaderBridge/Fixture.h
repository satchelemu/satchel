#ifndef SATCHEL_SHADER_BRIDGE_STORE_FIXTURE_H
#define SATCHEL_SHADER_BRIDGE_STORE_FIXTURE_H
#include <stdint.h>
/* Original SPIR-V 1.0 compute fixture. One invocation stores 0x13579bdf in
 * the first uint of descriptor-set0/binding0. No game or SDK shader data. */
static const uint32_t satchel_shader_fixture[] = {
    0x07230203,0x00010000,0,13,0,
    (2u<<16)|17,1, /* OpCapability Shader */
    (3u<<16)|14,0,1, /* Logical GLSL450 */
    (5u<<16)|15,5,10,0x6e69616d,0, /* GLCompute main */
    (6u<<16)|16,10,17,1,1,1, /* LocalSize */
    (3u<<16)|71,3,3, /* BufferBlock */
    (5u<<16)|72,3,0,35,0, /* member offset */
    (4u<<16)|71,5,34,0, /* DescriptorSet */
    (4u<<16)|71,5,33,0, /* Binding */
    (2u<<16)|19,1, /* void */
    (4u<<16)|21,2,32,0, /* uint */
    (3u<<16)|30,3,2, /* struct uint */
    (4u<<16)|32,4,2,3, /* ptr uniform struct */
    (4u<<16)|59,4,5,2, /* uniform variable */
    (3u<<16)|33,6,1, /* function type */
    (4u<<16)|32,7,2,2, /* ptr uniform uint */
    (4u<<16)|43,2,8,0, /* index zero */
    (4u<<16)|43,2,9,0x13579bdf,
    (5u<<16)|54,1,10,0,6,
    (2u<<16)|248,11,
    (5u<<16)|65,7,12,5,8,
    (3u<<16)|62,12,9,
    (1u<<16)|253,
    (1u<<16)|56
};
#endif
