#include "GameShaderBridge.h"
#include "spirv_msl.hpp"
#include <cstdlib>
#include <cstring>
#include <vector>

extern "C" int satchel_game_spirv_to_msl(const void *input, size_t length,
                                    char **output, size_t *output_length) {
    if (!output || !output_length) return -1;
    *output = nullptr;
    *output_length = 0;
    if (!input || length < 20 || length % 4 || length > 64 * 1024 * 1024) return -1;
    try {
        std::vector<uint32_t> words(length / 4);
        std::memcpy(words.data(), input, length);
        if (words[0] != 0x07230203 || words[3] == 0 || words[3] > 4 * 1024 * 1024) return -1;
        bool atomics = false;
        for (size_t i = 5; i < words.size();) {
            size_t count = words[i] >> 16;
            if (!count || count > words.size() - i) return -1;
            atomics |= (words[i] & 0xffff) == 60;
            i += count;
        }
        spirv_cross::CompilerMSL compiler(std::move(words));
        auto options = compiler.get_msl_options();
        // dmsh_build does not pass --msl-ios. Preserve the pinned CLI default
        // even for source ultimately compiled by iOS, to retain source parity.
        options.msl_version = atomics ? 30100 : 20300;
        options.argument_buffers = true;
        options.texture_buffer_native = true;
        options.pad_fragment_output_components = true;
        options.ios_support_base_vertex_instance = true;
        compiler.set_msl_options(options);
        const auto source = compiler.compile();
        if (source.size() > 64 * 1024 * 1024) return -2;
        auto *bytes = static_cast<char *>(std::malloc(source.size() + 1));
        if (!bytes) return -2;
        std::memcpy(bytes, source.c_str(), source.size() + 1);
        *output = bytes;
        *output_length = source.size();
        return 0;
    } catch (...) { return -3; }
}
extern "C" void satchel_game_msl_free(void *output) { std::free(output); }
