// MES scenario script container (CIRCUS adv32, D.C.4 dialect)
// Format recovered from DC4.EXE via IDA: Mes_LoadFile 0x438300, Mes_SplitBuffer 0x438200.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace dc4 {

struct MesLabel {
    uint32_t offset;  // into code body (after version dword), low 24 bits
    uint32_t flags;   // high byte of the raw dword (e.g. 0x80 on some labels)
    uint16_t id;
};

struct MesFile {
    uint32_t version = 0;                  // 0x00AAB6 = DC4, 0x00ABB6 = Plus Harmony
    std::vector<MesLabel> labels;
    std::vector<uint8_t> code;             // includes 4-byte version prefix at [0..3]
    size_t code_base = 0;                  // offset of code (post-version) within file

    bool load(const uint8_t* data, size_t size, std::string* err = nullptr);
    bool load_file(const std::string& path, std::string* err = nullptr);

    const MesLabel* find_label(uint16_t id) const;
};

// Text-opcode constants from MesTextTool (script_info.cpp, "dc4" alias)
enum : uint8_t {
    MES_ENC_STRING_MIN = 0x69,   // XOR-0x20 obfuscated strings
    MES_ENC_STRING_MAX = 0x6D,
    MES_STRING_MIN     = 0x48,   // plain SJIS strings
    MES_STRING_MAX     = 0x68,
    MES_TEXT_OUT       = 0x5D,
};

}  // namespace dc4
