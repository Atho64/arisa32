// CRXB ("CRM") Circus image archive + CRXD differential image decoding.
//
// CRXB layout (GRP/BST/*.CRM, GRP/FACE/*.CRM):
//   0x00 char[4] "CRXB"
//   0x04 u32     version (1)
//   0x08 u32     entry count
//   0x0C u16     canvas width   (1024)
//   0x0E u16     canvas height  (2048)
//   0x10 entries[count], 32 bytes each:
//        0x00 u32    absolute file offset of the entry's data
//        0x04 u32    id (increments by 0x80 per entry; unused by us)
//        0x08 char[24] name, e.g. "NIN_B1_A08.CRX"
//   Data blocks start right after the table. An entry's size is the distance
//   to the next entry; the last one runs to EOF. A base CRXG blob may sit
//   between the table and the first entry and is addressed by offset alone.
//
// CRXD layout (the payload of most entries):
//   0x00 char[4] "CRXD"
//   0x04 u32     flags (0x11 in every DC4 file)
//   0x08 u32     base offset inside this archive, or 0
//   0x0C char[20] base file name (used when the base is a loose file)
//   0x20 "CRXG" + a plain CRXG image -> the diff, decoded with no canvas
//   0x20 "CRXJ" + u32 offset -> the diff lives in another CRXD at that offset
// Decoding adds the diff to the base (RGB) and subtracts it (alpha).
#pragma once
#include "dc4/crx.h"

#include <cstdint>
#include <string>
#include <vector>

namespace dc4 {

class CrmArchive {
public:
    struct Entry {
        uint32_t offset = 0;
        uint32_t id = 0;
        std::string name;   // without the ".CRX" suffix
    };

    bool open(const std::string& path, std::string* err = nullptr);
    void close() { data_.clear(); entries_.clear(); }
    bool is_open() const { return !data_.empty(); }
    const std::vector<Entry>& entries() const { return entries_; }
    uint16_t canvas_w() const { return canvas_w_; }
    uint16_t canvas_h() const { return canvas_h_; }

    // Case-insensitive entry lookup by name (with or without ".CRX").
    const Entry* find(const std::string& name) const;
    // Byte range of the entry starting at the given absolute offset, or of the
    // unnamed base blob when offset is not a table entry.
    bool range_at(uint32_t offset, const uint8_t** data, size_t* size) const;

private:
    std::vector<uint8_t> data_;
    std::vector<Entry> entries_;
    uint16_t canvas_w_ = 0, canvas_h_ = 0;
};

// Decodes a CRXD differential image. base_offset/base_data let the caller
// supply the base explicitly; otherwise the archive is consulted by offset and
// then the loose file named inside the CRXD header. Output is RGBA.
bool crxd_decode(const CrmArchive& arc, uint32_t offset,
                 std::vector<uint8_t>* rgba, CrxInfo* info, std::string* err = nullptr);

bool crxd_decode(const uint8_t* data, size_t size,
                 const uint8_t* base_data, size_t base_size,
                 std::vector<uint8_t>* rgba, CrxInfo* info, std::string* err = nullptr);

// Resolves a script BST/CUT reference ("BST\NIN_B1_A08,BST\NIN_B") to an
// archive plus entry name, applying the engine's Sprite_BstParseName rules:
// the first of ',', '*' or '@' splits the entry from the archive base, and a
// trailing "_B" on the entry is the outfit-inheritance marker.
struct BstRef {
    std::string entry;     // e.g. "NIN_B1_A08"
    std::string archive;   // e.g. "NIN_B" (empty when the ref names a loose CRX)
    char marker = 0;       // ',', '*', '@' or 0
    bool valid = false;
};
BstRef bst_parse_name(const std::string& name);

// Case-insensitive filename lookup inside a directory (the game ships mixed
// case: "GRP/BST/nin_b.CRM" is referenced from scripts as "BST\\NIN_B").
// Returns the real on-disk name, or "" when absent.
std::string find_file_ci(const std::string& dir, const std::string& name);

// Loads a script BST/CUT reference (e.g. "BST\\NIN_B1_A08,BST\\NIN_B") to
// RGBA, opening either the named .CRM archive or a loose .CRX.
bool bst_load(const std::string& grp_dir, const std::string& ref,
              std::vector<uint8_t>* rgba, CrxInfo* info, std::string* err = nullptr);

}  // namespace dc4
