// VOICE.DAT container (CIRCUS adv32, D.C.4)
// Format recovered from DC4.EXE via IDA: Voice_LoadTOC 0x452590, Voice_Lookup 0x4526A0.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace dc4 {

struct VoiceEntry {
    char name[56];     // zero-padded, e.g. "SE\0000SILENT24.PCM"
    uint32_t offset;   // absolute file offset of XPCM blob
    uint32_t size;     // blob size in bytes

    std::string str() const { return std::string(name); }
};

// Reads only the TOC (a few MB); data is streamed separately.
class VoiceArchive {
public:
    bool open(const std::string& path, std::string* err = nullptr);
    const std::vector<VoiceEntry>& entries() const { return entries_; }

    // Lookup like the engine: strip path before last '\\', append ".pcm",
    // case-insensitive compare. Returns nullptr if not found.
    const VoiceEntry* find(const std::string& name) const;

    // Extract the raw XPCM blob for an entry into `out` (size bytes).
    bool read_entry(const VoiceEntry& e, std::vector<uint8_t>* out, std::string* err = nullptr) const;

private:
    std::string path_;
    std::vector<VoiceEntry> entries_;
};

}  // namespace dc4
