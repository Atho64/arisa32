#include "dc4/crm.h"
#include "dc4/resource.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <map>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dirent.h>
#endif

namespace dc4 {

namespace {

std::string upper(std::string s) {
    for (char& c : s) c = char(std::toupper((unsigned char)c));
    return s;
}

std::string base_name(const std::string& p) {
    size_t k = p.find_last_of("/\\");
    return k == std::string::npos ? p : p.substr(k + 1);
}

bool read_file(const std::string& path, std::vector<uint8_t>* out) {
    FILE* f = fopen(resolve_asset(path).c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    out->assign(size_t(sz > 0 ? sz : 0), 0);
    bool ok = sz <= 0 || fread(out->data(), 1, out->size(), f) == out->size();
    fclose(f);
    return ok;
}

}  // namespace

bool CrmArchive::open(const std::string& path, std::string* err) {
    data_.clear();
    entries_.clear();
    if (!read_file(path, &data_)) {
        if (err) *err = "cannot read " + path;
        return false;
    }
    if (data_.size() < 0x10 || memcmp(data_.data(), "CRXB", 4) != 0) {
        if (err) *err = path + ": not CRXB";
        data_.clear();
        return false;
    }
    uint32_t count;
    memcpy(&count, data_.data() + 8, 4);
    memcpy(&canvas_w_, data_.data() + 0x0C, 2);
    memcpy(&canvas_h_, data_.data() + 0x0E, 2);
    if (size_t(0x10) + size_t(count) * 32 > data_.size()) {
        if (err) *err = path + ": entry table out of range";
        data_.clear();
        return false;
    }
    entries_.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t* e = data_.data() + 0x10 + 32 * i;
        Entry en;
        memcpy(&en.offset, e, 4);
        memcpy(&en.id, e + 4, 4);
        const char* n = reinterpret_cast<const char*>(e + 8);
        size_t len = 0;
        while (len < 24 && n[len]) ++len;
        en.name.assign(n, len);
        if (upper(en.name).size() > 4 &&
            upper(en.name).compare(upper(en.name).size() - 4, 4, ".CRX") == 0)
            en.name.resize(en.name.size() - 4);
        entries_.push_back(en);
    }
    return true;
}

const CrmArchive::Entry* CrmArchive::find(const std::string& name) const {
    std::string want = upper(base_name(name));
    if (want.size() > 4 && want.compare(want.size() - 4, 4, ".CRX") == 0)
        want.resize(want.size() - 4);
    for (const Entry& e : entries_)
        if (upper(e.name) == want) return &e;
    return nullptr;
}

bool CrmArchive::range_at(uint32_t offset, const uint8_t** data, size_t* size) const {
    if (offset >= data_.size()) return false;
    // Entry size is the gap to the next entry, ordered by offset; the last one
    // runs to EOF. An offset with no table entry (the shared base blob) runs to
    // the first entry that starts after it.
    uint32_t next = uint32_t(data_.size());
    for (const Entry& e : entries_)
        if (e.offset > offset && e.offset < next) next = e.offset;
    *data = data_.data() + offset;
    *size = next - offset;
    return true;
}

// ---------------------------------------------------------------------------

namespace {

// A decoded image block: raw pixels in the file's channel order.
struct Block {
    std::vector<uint8_t> pixels;
    std::vector<uint8_t> palette;
    CrxInfo info;
    int stride = 0;
};

bool decode_crxd(const uint8_t* blk, size_t blk_size, const CrmArchive* arc,
                 const std::string& dir, Block* out, int depth, std::string* err);

// Decodes whatever image block starts at `blk` (plain CRXG, or CRXD which
// resolves its own base/diff chain).
bool load_block(const uint8_t* blk, size_t blk_size, const CrmArchive* arc,
                const std::string& dir, Block* out, int depth, std::string* err) {
    if (blk_size < 4) {
        if (err) *err = "image block too small";
        return false;
    }
    if (memcmp(blk, "CRXD", 4) == 0)
        return decode_crxd(blk, blk_size, arc, dir, out, depth, err);
    if (memcmp(blk, "CRXG", 4) == 0)
        return crx_decode_raw(blk, blk_size, &out->pixels, &out->info, &out->stride,
                              &out->palette, err);
    if (err) *err = "unknown image block magic";
    return false;
}

// Loads the block at an absolute offset in the archive.
bool load_block_at(const CrmArchive& arc, uint32_t offset, Block* out, int depth,
                   std::string* err) {
    const uint8_t* p;
    size_t n;
    if (!arc.range_at(offset, &p, &n)) {
        if (err) *err = "offset out of range";
        return false;
    }
    return load_block(p, n, &arc, ".", out, depth, err);
}

// Unpacks one CRXD block: base image plus a diff, blended additively. The
// base is named by offset (in this archive) with a loose filename fallback;
// the diff is inline or, for "CRXJ", another CRXD at a given offset.
bool decode_crxd(const uint8_t* blk, size_t blk_size, const CrmArchive* arc,
                 const std::string& dir, Block* out, int depth, std::string* err) {
    if (depth > 8) {
        if (err) *err = "CRXD chain too deep";
        return false;
    }
    if (blk_size < 0x28 || memcmp(blk, "CRXD", 4) != 0) {
        if (err) *err = "not CRXD";
        return false;
    }
    uint32_t base_off;
    memcpy(&base_off, blk + 8, 4);
    char base_name_buf[21];
    memcpy(base_name_buf, blk + 0x0C, 20);
    base_name_buf[20] = 0;

    // --- base -------------------------------------------------------------
    Block base;
    bool ok = false;
    if (arc && base_off) ok = load_block_at(*arc, base_off, &base, depth + 1, err);
    if (!ok && base_name_buf[0]) {
        std::vector<uint8_t> blob;
        if (read_file(dir + "/" + base_name_buf, &blob))
            ok = load_block(blob.data(), blob.size(), arc, dir, &base, depth + 1, err);
    }
    if (!ok) {
        if (err) *err = std::string("CRXD base not found: ") + base_name_buf;
        return false;
    }

    // --- diff -------------------------------------------------------------
    Block diff;
    if (memcmp(blk + 0x20, "CRXG", 4) == 0) {
        ok = load_block(blk + 0x20, blk_size - 0x20, arc, dir, &diff, depth + 1, err);
    } else if (memcmp(blk + 0x20, "CRXJ", 4) == 0 && blk_size >= 0x2C) {
        uint32_t doff;
        memcpy(&doff, blk + 0x28, 4);
        // The CRXJ target is a CRXD wrapper whose own *inline diff* carries the
        // real payload; its base reference is authoring metadata. Composing the
        // whole wrapper instead would add a second full body and wrap the
        // channels (verified against nin_b.CRM NIN_B1_AB02/NIN_B2_B11).
        const uint8_t* wrap = nullptr;
        size_t wrap_size = 0;
        if (arc) arc->range_at(doff, &wrap, &wrap_size);
        if (wrap && wrap_size >= 0x20 && memcmp(wrap, "CRXD", 4) == 0)
            ok = load_block(wrap + 0x20, wrap_size - 0x20, arc, dir, &diff,
                            depth + 1, err);
        if (!ok) {
            // Fallback: some CRXJ targets may be direct image blocks.
            if (arc) ok = load_block_at(*arc, doff, &diff, depth + 1, err);
            if (!ok) {
                if (err) *err = "CRXJ diff not found";
                return false;
            }
        }
    } else {
        if (err) *err = "unsupported CRXD diff type";
        return false;
    }
    if (!ok) return false;
    if (diff.info.bpp != base.info.bpp) {
        if (err) *err = "CRXD bpp mismatch";
        return false;
    }

    // --- blend over the intersected rectangle ------------------------------
    int x0 = std::max<int>(diff.info.offset_x, base.info.offset_x);
    int y0 = std::max<int>(diff.info.offset_y, base.info.offset_y);
    int x1 = std::min<int>(diff.info.offset_x + diff.info.width,
                           base.info.offset_x + base.info.width);
    int y1 = std::min<int>(diff.info.offset_y + diff.info.height,
                           base.info.offset_y + base.info.height);
    if (x0 < x1 && y0 < y1) {
        const int ps = base.info.bpp / 8;
        int bx = x0 - base.info.offset_x, by = y0 - base.info.offset_y;
        int dx = x0 - diff.info.offset_x, dy = y0 - diff.info.offset_y;
        for (int row = 0; row < y1 - y0; ++row) {
            uint8_t* d = base.pixels.data() + size_t(by + row) * base.stride + bx * ps;
            const uint8_t* s = diff.pixels.data() + size_t(dy + row) * diff.stride + dx * ps;
            int n = (x1 - x0) * ps;
            for (int i = 0; i + ps <= n; i += ps) {
                if (ps == 4) {
                    // 32bpp raw layout is (A,B,G,R): subtract the alpha byte,
                    // add B/G/R. Adding bytes 0..2 instead wrapped the red
                    // channel and turned every face diff teal.
                    d[i] = uint8_t(d[i] - s[i]);
                    for (int c = 1; c < 4; ++c)
                        d[i + c] = uint8_t(d[i + c] + s[i + c]);
                } else {
                    for (int c = 0; c < 3; ++c)
                        d[i + c] = uint8_t(d[i + c] + s[i + c]);
                }
            }
        }
    }
    *out = std::move(base);
    return true;
}

}  // namespace

bool crxd_decode(const CrmArchive& arc, uint32_t offset,
                 std::vector<uint8_t>* rgba, CrxInfo* info, std::string* err) {
    Block b;
    if (!load_block_at(arc, offset, &b, 0, err)) return false;
    *info = b.info;
    crx_to_rgba(b.info, b.stride, b.pixels, b.palette, rgba);
    return true;
}

bool crxd_decode(const uint8_t* data, size_t size, const uint8_t* base_data,
                 size_t base_size, std::vector<uint8_t>* rgba, CrxInfo* info,
                 std::string* err) {
    Block b;
    if (base_data && base_size) {
        if (!load_block(base_data, base_size, nullptr, ".", &b, 0, err)) return false;
    } else if (!load_block(data, size, nullptr, ".", &b, 0, err)) {
        return false;
    }
    *info = b.info;
    crx_to_rgba(b.info, b.stride, b.pixels, b.palette, rgba);
    return true;
}

BstRef bst_parse_name(const std::string& name) {
    BstRef r;
    size_t cut = std::string::npos;
    for (size_t i = 0; i < name.size(); ++i) {
        char c = name[i];
        if (c == ',' || c == '*' || c == '@') { cut = i; break; }
    }
    if (cut == std::string::npos) {
        r.entry = name;
    } else {
        r.marker = name[cut];
        r.entry = name.substr(0, cut);
        r.archive = name.substr(cut + 1);
    }
    // Drop any path prefix; the archive base never carries one in practice.
    r.entry = base_name(r.entry);
    r.archive = base_name(r.archive);
    if (r.archive.size() > 4 &&
        upper(r.archive).compare(r.archive.size() - 4, 4, ".CRM") == 0)
        r.archive.resize(r.archive.size() - 4);
    // Sprite_BstParseName appends "_B" to the entry when the slot previously
    // used that outfit; the stored name then has it as a suffix.
    if (r.entry.size() > 2 && r.entry.compare(r.entry.size() - 2, 2, "_B") == 0)
        r.entry.resize(r.entry.size() - 2);
    r.valid = !r.entry.empty();
    return r;
}

std::string find_file_ci(const std::string& dir, const std::string& name) {
    // Keep returning a basename: callers resolve the final path at file-open.
    // This also lets overlay-only files participate in existing asset lookup.
    if (asset_exists(dir + "/" + name)) return name;
    // Directories are scanned once and cached; the game resolves thousands of
    // asset names per playthrough and Windows/Android differ on case rules.
    static std::map<std::string, std::map<std::string, std::string>> cache;
    auto it = cache.find(dir);
    if (it == cache.end()) {
        std::map<std::string, std::string> idx;
#ifdef _WIN32
        WIN32_FIND_DATAA fd;
        std::string pattern = dir + "\\*";
        HANDLE h = FindFirstFileA(pattern.c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                idx[upper(fd.cFileName)] = fd.cFileName;
            } while (FindNextFileA(h, &fd));
            FindClose(h);
        }
#else
        if (DIR* d = opendir(dir.c_str())) {
            while (struct dirent* de = readdir(d))
                idx[upper(de->d_name)] = de->d_name;
            closedir(d);
        }
#endif
        it = cache.emplace(dir, std::move(idx)).first;
    }
    auto f = it->second.find(upper(name));
    return f == it->second.end() ? std::string() : f->second;
}

bool bst_load(const std::string& grp_dir, const std::string& ref,
              std::vector<uint8_t>* rgba, CrxInfo* info, std::string* err) {
    BstRef r = bst_parse_name(ref);
    if (!r.valid) {
        if (err) *err = "empty BST reference";
        return false;
    }
    if (!r.archive.empty()) {
        std::string file = find_file_ci(grp_dir, r.archive + ".CRM");
        if (file.empty()) {
            if (err) *err = "no archive " + r.archive + ".CRM in " + grp_dir;
            return false;
        }
        CrmArchive arc;
        if (!arc.open(grp_dir + "/" + file, err)) return false;
        const CrmArchive::Entry* e = arc.find(r.entry);
        if (!e) {
            if (err) *err = "no entry " + r.entry + " in " + file;
            return false;
        }
        return crxd_decode(arc, e->offset, rgba, info, err);
    }
    std::string file = find_file_ci(grp_dir, r.entry + ".CRX");
    if (file.empty()) {
        if (err) *err = "no file " + r.entry + ".CRX in " + grp_dir;
        return false;
    }
    std::vector<uint8_t> buf;
    if (!read_file(grp_dir + "/" + file, &buf)) {
        if (err) *err = "cannot read " + file;
        return false;
    }
    return crx_decode(buf.data(), buf.size(), rgba, info, err);
}

}  // namespace dc4
