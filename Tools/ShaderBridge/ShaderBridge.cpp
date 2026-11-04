#include "ShaderBridge.h"
#include "spirv_msl.hpp"
#include <algorithm>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace {
bool overlaps(const void *a, size_t n, const void *b, size_t m) {
    const uintptr_t x = reinterpret_cast<uintptr_t>(a), y = reinterpret_cast<uintptr_t>(b);
    if (n > UINTPTR_MAX - x || m > UINTPTR_MAX - y) return true;
    return n && m && x < y + m && y < x + n;
}
bool framed(const std::vector<uint32_t> &words) {
    if (words.size() < 5 || words[0] != 0x07230203 || words[1] < 0x10000 ||
        words[1] > 0x10600 || (words[1] & 0xff) || words[3] == 0 ||
        words[3] > 65536 || words[4] != 0) return false;
    size_t count = 0;
    for (size_t i = 5; i < words.size();) {
        const size_t length = words[i] >> 16;
        if (!length || length > words.size() - i || ++count > 16384) return false;
        i += length;
    }
    return count != 0;
}
}

extern "C" SatchelShaderStatus satchel_shader_spirv_to_msl(const void *input, size_t length,
    char *output, size_t capacity, SatchelShaderInfo *info) {
    // Reject overlaps before clearing outputs, so aliasing cannot alter input.
    if (!info || !input || !output || capacity == 0 || capacity > SATCHEL_SHADER_MAX_MSL_BYTES ||
        length < 20 || length > SATCHEL_SHADER_MAX_SPIRV_BYTES || length % 4 ||
        overlaps(input, length, output, capacity) || overlaps(input, length, info, sizeof(*info)) ||
        overlaps(output, capacity, info, sizeof(*info))) return SATCHEL_SHADER_INVALID;
    *info = {}; output[0] = 0;
    try {
        std::vector<uint32_t> words(length / 4);
        std::memcpy(words.data(), input, length);
        if (!framed(words)) return SATCHEL_SHADER_INVALID;
        spirv_cross::CompilerMSL compiler(std::move(words));
        const auto entries = compiler.get_entry_points_and_stages();
        if (entries.size() != 1) return SATCHEL_SHADER_UNSUPPORTED;
        const auto model = entries[0].execution_model;
        SatchelShaderInfo result = {};
        switch (model) {
            case spv::ExecutionModelVertex: result.stage = SATCHEL_SHADER_VERTEX; break;
            case spv::ExecutionModelFragment: result.stage = SATCHEL_SHADER_FRAGMENT; break;
            case spv::ExecutionModelGLCompute: result.stage = SATCHEL_SHADER_COMPUTE; break;
            default: return SATCHEL_SHADER_UNSUPPORTED;
        }
        compiler.set_entry_point(entries[0].name, model);
        if (model == spv::ExecutionModelGLCompute) {
            if (!compiler.get_execution_mode_bitset().get(spv::ExecutionModeLocalSize) ||
                compiler.get_execution_mode_bitset().get(spv::ExecutionModeLocalSizeId)) return SATCHEL_SHADER_UNSUPPORTED;
            uint64_t total = 1;
            for (uint32_t i = 0; i < 3; ++i) {
                const uint32_t size = compiler.get_execution_mode_argument(spv::ExecutionModeLocalSize, i);
                if (!size || size > 1024) return SATCHEL_SHADER_UNSUPPORTED;
                result.local_size[i] = size; total *= size;
            }
            if (total > 1024) return SATCHEL_SHADER_UNSUPPORTED;
        }
        if (!compiler.get_specialization_constants().empty()) return SATCHEL_SHADER_UNSUPPORTED;
        const auto resources = compiler.get_shader_resources();
        // Pinned Compiler API traverses the current entry point's reachable CFG.
        // Preserve all declared resources; distinguish those statically accessed.
        const auto active = compiler.get_active_interface_variables();
        if (!resources.push_constant_buffers.empty() || !resources.sampled_images.empty() ||
            !resources.subpass_inputs.empty() || !resources.atomic_counters.empty() ||
            !resources.acceleration_structures.empty()) return SATCHEL_SHADER_UNSUPPORTED;
        auto bind = [&](const auto &list, uint32_t kind) {
            for (const auto &resource : list) {
                if (!compiler.get_type(resource.type_id).array.empty()) return false;
                if (!compiler.has_decoration(resource.id, spv::DecorationDescriptorSet) ||
                    !compiler.has_decoration(resource.id, spv::DecorationBinding) ||
                    compiler.get_decoration(resource.id, spv::DecorationDescriptorSet) != 0) return false;
                const uint32_t binding = compiler.get_decoration(resource.id, spv::DecorationBinding);
                if (binding >= SATCHEL_SHADER_MAX_BINDINGS || result.binding_count >= SATCHEL_SHADER_MAX_BINDINGS) return false;
                for (uint32_t i = 0; i < result.binding_count; ++i)
                    if (result.bindings[i].kind == kind && result.bindings[i].binding == binding) return false;
                result.bindings[result.binding_count++] = {kind, binding, active.count(resource.id) ? 1u : 0u};
                spirv_cross::MSLResourceBinding remap = {};
                remap.stage = model; remap.desc_set = 0; remap.binding = binding;
                remap.msl_buffer = binding; remap.msl_texture = binding; remap.msl_sampler = binding;
                compiler.add_msl_resource_binding(remap);
            }
            return true;
        };
        if (!bind(resources.uniform_buffers, SATCHEL_SHADER_BUFFER) || !bind(resources.storage_buffers, SATCHEL_SHADER_BUFFER) ||
            !bind(resources.separate_images, SATCHEL_SHADER_TEXTURE) || !bind(resources.storage_images, SATCHEL_SHADER_TEXTURE) ||
            !bind(resources.separate_samplers, SATCHEL_SHADER_SAMPLER)) return SATCHEL_SHADER_UNSUPPORTED;
        spirv_cross::CompilerMSL::Options options;
        options.platform = spirv_cross::CompilerMSL::Options::iOS;
        options.set_msl_version(2, 3);
        compiler.set_msl_options(options);
        compiler.rename_entry_point(entries[0].name, "satchel_main", model);
        const std::string source = compiler.compile();
        if (compiler.needs_swizzle_buffer() || compiler.needs_buffer_size_buffer() ||
            compiler.needs_view_mask_buffer() || compiler.needs_depth_clip_state_buffer() ||
            compiler.needs_dispatch_base_buffer() || compiler.needs_output_buffer() ||
            compiler.needs_patch_output_buffer() || compiler.needs_input_threadgroup_mem()) return SATCHEL_SHADER_UNSUPPORTED;
        if (source.size() >= SATCHEL_SHADER_MAX_MSL_BYTES || source.size() >= capacity) return SATCHEL_SHADER_CAPACITY;
        const auto &entry = compiler.get_cleansed_entry_point_name("satchel_main", model);
        if (entry.empty() || entry.size() >= sizeof(result.entry_point)) return SATCHEL_SHADER_UNSUPPORTED;
        std::memcpy(result.entry_point, entry.c_str(), entry.size() + 1);
        result.source_bytes = source.size();
        std::memcpy(output, source.c_str(), source.size() + 1);
        *info = result;
        return SATCHEL_SHADER_OK;
    } catch (...) {
        // Exception diagnostics may include translated shader payload; never export them.
        return SATCHEL_SHADER_COMPILER_ERROR;
    }
}
