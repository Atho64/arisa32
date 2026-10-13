#include "dc4/voice.h"

#include <cstdio>
#include <cstring>

namespace dc4 {

bool VoiceArchive::open(const std::string& path, std::string* err) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) {
        if (err) *err = "cannot open " + path;
        return false;
    }
    uint32_t count = 0;
    if (fread(&count, 4, 1, f) != 1 || count == 0 || count > 1000000) {
        fclose(f);
        if (err) *err = "bad entry count";
        return false;
    }
    // Legacy index: count*(offset,size) pairs — skipped by the engine loader.
    if (fseek(f, long(count) * 8, SEEK_CUR) != 0) {
        fclose(f);
        if (err) *err = "seek over index failed";
        return false;
    }
    entries_.resize(count);
    if (fread(entries_.data(), 64, count, f) != count) {
        fclose(f);
        if (err) *err = "short TOC read";
        return false;
    }
    fclose(f);
    path_ = path;
    return true;
}

static char lower(char c) { return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c; }

static bool ieq(const std::string& a, const char* b) {
    size_t i = 0;
    for (; i < a.size(); ++i) {
        if (!b[i] || lower(a[i]) != lower(b[i])) return false;
    }
    return b[i] == 0;
}

const VoiceEntry* VoiceArchive::find(const std::string& name) const {
    // Engine: everything before (and including) the first backslash is dropped,
    // ".pcm" is appended, case-insensitive compare.
    std::string key = name;
    size_t slash = key.find('\\');
    if (slash != std::string::npos) key = key.substr(slash + 1);
    key += ".pcm";
    for (const auto& e : entries_)
        if (ieq(key, e.name)) return &e;
    return nullptr;
}

bool VoiceArchive::read_entry(const VoiceEntry& e, std::vector<uint8_t>* out, std::string* err) const {
    FILE* f = fopen(path_.c_str(), "rb");
    if (!f) {
        if (err) *err = "cannot reopen " + path_;
        return false;
    }
    if (fseek(f, long(e.offset), SEEK_SET) != 0) {
        fclose(f);
        if (err) *err = "seek failed";
        return false;
    }
    out->resize(e.size);
    bool ok = fread(out->data(), 1, e.size, f) == e.size;
    fclose(f);
    if (!ok && err) *err = "short data read";
    return ok;
}

}  // namespace dc4
