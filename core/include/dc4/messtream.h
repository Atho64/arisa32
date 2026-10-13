// MES command-stream walker (CIRCUS adv32, D.C.4)
// Mirrors the engine's fetcher (sub_438790). Verified against all 330 game
// scripts: 50,707/50,707 labels land on command boundaries.
#pragma once
#include "dc4/mesfile.h"
#include <string>
#include <vector>

namespace dc4 {

enum class CmdKind {
    Nop,        // 0x00
    Param2,     // 0x01-0x3A : two u8 params
    ParamStr,   // 0x3B-0x47 : u8 param + raw string
    Str,        // 0x48-0x68 : raw string (paths, names)
    Text,       // 0x69-0x6D : message text (stored SJIS - 0x20 per byte)
    Words4,     // 0x6E-0xFF : four u16 params
};

struct MesCommand {
    CmdKind kind = CmdKind::Nop;
    uint32_t pos = 0;      // offset of the opcode byte within `code`
    uint8_t op = 0;
    uint8_t p1 = 0, p2 = 0;             // Param2 / ParamStr param
    uint16_t words[4] = {};             // Words4
    std::string str;                    // Str / ParamStr / Text (decoded)
    size_t size = 0;                    // total bytes consumed incl. opcode
};

// Decodes stored text (real SJIS byte - 0x20) into real SJIS bytes.
std::string mes_decode_text(const uint8_t* data, size_t len);

class MesStream {
public:
    explicit MesStream(const MesFile& mes) : code_(mes.code) {
        // code_ includes the 3-byte B6 AA 00 header; command offsets are
        // relative to its start (label offsets already account for it).
        base_ = 0;
        pos_ = 3;  // first command after the B6 AA 00 header
    }

    // Reads the next command. Returns false at end of code.
    bool next(MesCommand* out);

    uint32_t pos() const { return pos_; }
    void seek(uint32_t label_offset) { pos_ = label_offset; }

private:
    const std::vector<uint8_t>& code_;
    size_t base_ = 0;
    size_t pos_ = 0;
};

}  // namespace dc4
