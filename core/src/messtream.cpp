#include "dc4/messtream.h"

#include <cstring>

namespace dc4 {

std::string mes_decode_text(const uint8_t* data, size_t len) {
    std::string out;
    out.reserve(len);
    for (size_t i = 0; i < len; ++i)
        out.push_back(char(data[i] + 0x20));
    return out;
}

bool MesStream::next(MesCommand* out) {
    const size_t n = code_.size();
    size_t p = pos_;
    if (p >= n) return false;
    out->pos = uint32_t(p);
    const uint8_t op = code_[p];
    out->op = op;
    auto cstr_len = [&](size_t start) -> long {
        const void* f = memchr(code_.data() + start, 0, n - start);
        return f ? long(static_cast<const uint8_t*>(f) - code_.data() - start) : -1;
    };
    if (op == 0x00) {
        out->kind = CmdKind::Nop;
        out->size = 1;
        pos_ = p + 1;
        return true;
    }
    if (op <= 0x3A) {
        if (p + 3 > n) return false;
        out->kind = CmdKind::Param2;
        out->p1 = code_[p + 1];
        out->p2 = code_[p + 2];
        out->size = 3;
        pos_ = p + 3;
        return true;
    }
    if (op <= 0x47) {
        if (p + 2 > n) return false;
        long l = cstr_len(p + 2);
        if (l < 0) return false;
        out->kind = CmdKind::ParamStr;
        out->p1 = code_[p + 1];
        out->str.assign(reinterpret_cast<const char*>(&code_[p + 2]), size_t(l));
        out->size = 2 + size_t(l) + 1;
        pos_ = p + out->size;
        return true;
    }
    if (op <= 0x68) {
        long l = cstr_len(p + 1);
        if (l < 0) return false;
        out->kind = CmdKind::Str;
        out->str.assign(reinterpret_cast<const char*>(&code_[p + 1]), size_t(l));
        out->size = 1 + size_t(l) + 1;
        pos_ = p + out->size;
        return true;
    }
    if (op <= 0x6D) {
        long l = cstr_len(p + 1);
        if (l < 0) return false;
        out->kind = CmdKind::Text;
        out->str = mes_decode_text(&code_[p + 1], size_t(l));
        out->size = 1 + size_t(l) + 1;
        pos_ = p + out->size;
        return true;
    }
    // 0x6E-0xFF: four u16
    if (p + 9 > n) return false;
    out->kind = CmdKind::Words4;
    for (int i = 0; i < 4; ++i) {
        out->words[i] = uint16_t(code_[p + 1 + 2 * i] | (code_[p + 2 + 2 * i] << 8));
    }
    out->size = 9;
    pos_ = p + 9;
    return true;
}

}  // namespace dc4
