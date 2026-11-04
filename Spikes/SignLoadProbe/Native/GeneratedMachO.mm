#import "SignerBridge.h"
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <vector>
#include <cstring>

// Entire image assembled from Mach-O structures and two ARM64 instructions.
// No compiler, linker, input image, or external code is used at runtime.
BOOL SPGenerateDylib(NSString *path, BOOL ios, NSError **error) {
    std::vector<uint8_t> bytes(0x8100, 0);
    auto header = (mach_header_64 *)bytes.data();
    *header = {MH_MAGIC_64, CPU_TYPE_ARM64, CPU_SUBTYPE_ARM64_ALL, MH_DYLIB, 0, 0,
        MH_NOUNDEFS | MH_DYLDLINK | MH_TWOLEVEL, 0};
    size_t cursor = sizeof(*header);
    auto command = [&](const void *p, size_t size) {
        memcpy(bytes.data() + cursor, p, size);
        cursor += size;
        header->ncmds++;
        header->sizeofcmds += size;
    };
    segment_command_64 text = {};
    text.cmd = LC_SEGMENT_64; text.cmdsize = sizeof(text) + sizeof(section_64);
    strcpy(text.segname, "__TEXT"); text.vmsize = 0x8000; text.filesize = 0x8000;
    text.maxprot = text.initprot = 5; text.nsects = 1;
    section_64 section = {};
    strcpy(section.sectname, "__text"); strcpy(section.segname, "__TEXT");
    section.addr = 0x4000; section.offset = 0x4000; section.size = 8; section.align = 14;
    section.flags = S_REGULAR | S_ATTR_PURE_INSTRUCTIONS | S_ATTR_SOME_INSTRUCTIONS;
    command(&text, sizeof(text));
    memcpy(bytes.data() + cursor, &section, sizeof(section)); cursor += sizeof(section);
    header->sizeofcmds += sizeof(section);
    segment_command_64 link = {};
    link.cmd = LC_SEGMENT_64; link.cmdsize = sizeof(link); strcpy(link.segname, "__LINKEDIT");
    link.vmaddr = 0x8000; link.vmsize = 0x4000; link.fileoff = 0x8000; link.filesize = 0x100;
    link.maxprot = link.initprot = 1; command(&link, sizeof(link));
    auto dylibCommand = [&](uint32_t type, const char *name) {
        size_t size = (sizeof(dylib_command) + strlen(name) + 1 + 7) & ~7;
        std::vector<uint8_t> data(size, 0);
        auto c = (dylib_command *)data.data(); c->cmd = type; c->cmdsize = (uint32_t)size;
        c->dylib.name.offset = sizeof(*c); c->dylib.current_version = c->dylib.compatibility_version = 0x10000;
        strcpy((char *)data.data() + sizeof(*c), name); command(data.data(), size);
    };
    dylibCommand(LC_ID_DYLIB, "@rpath/generated-probe.dylib");
    dylibCommand(LC_LOAD_DYLIB, "/usr/lib/libSystem.B.dylib");
    build_version_command build = {LC_BUILD_VERSION, sizeof(build), (uint32_t)(ios ? PLATFORM_IOS : PLATFORM_MACOS),
        ios ? 0x110000u : 0x0d0000u, ios ? 0x110000u : 0x0d0000u, 0};
    command(&build, sizeof(build));
    uuid_command uuid = {}; uuid.cmd = LC_UUID; uuid.cmdsize = sizeof(uuid);
    arc4random_buf(uuid.uuid, sizeof(uuid.uuid)); command(&uuid, sizeof(uuid));
    // Export trie: root edge followed by a regular export at VM offset 0x4000.
    const char *symbol = "_probe_value";
    size_t edgeSize = strlen(symbol) + 1;
    std::vector<uint8_t> trie = {0, 1};
    trie.insert(trie.end(), symbol, symbol + edgeSize);
    trie.push_back((uint8_t)(trie.size() + 1));
    for (uint8_t b : {4, 0, 0x80, 0x80, 1, 0}) trie.push_back(b);
    memcpy(bytes.data() + 0x8000, trie.data(), trie.size());
    dyld_info_command dyld = {}; dyld.cmd = LC_DYLD_INFO_ONLY; dyld.cmdsize = sizeof(dyld);
    dyld.export_off = 0x8000; dyld.export_size = (uint32_t)trie.size(); command(&dyld, sizeof(dyld));
    symtab_command sym = {LC_SYMTAB, sizeof(sym), 0x8040, 1, 0x8050, 0x20}; command(&sym, sizeof(sym));
    dysymtab_command dysym = {}; dysym.cmd = LC_DYSYMTAB; dysym.cmdsize = sizeof(dysym);
    dysym.nextdefsym = 1; dysym.iundefsym = 1; command(&dysym, sizeof(dysym));
    nlist_64 n = {}; n.n_un.n_strx = 1; n.n_type = N_SECT | N_EXT; n.n_sect = 1; n.n_value = 0x4000;
    memcpy(bytes.data() + 0x8040, &n, sizeof(n)); strcpy((char *)bytes.data() + 0x8051, symbol);
    const uint32_t instructions[] = {0x52800540, 0xd65f03c0}; // mov w0, #42; ret
    memcpy(bytes.data() + 0x4000, instructions, sizeof(instructions));
    return [[NSData dataWithBytes:bytes.data() length:bytes.size()] writeToFile:path options:NSDataWritingAtomic error:error];
}
