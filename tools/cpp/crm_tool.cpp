// crm_tool — CRXB archive / CRXD differential image CLI (D.C.4)
// Usage:
//   crm_tool list <archive.crm>
//   crm_tool get  <archive.crm> <entry> [out.png]
//   crm_tool ref  <GRP dir> <BST reference> [out.png]
//   crm_tool all  <GRP dir>                 decode every BST ref in a file list
//   crm_tool name <script BST reference>       (shows the resolved parts)
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include "dc4/crm.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <set>
#include <string>
#include <vector>

using namespace dc4;

static int decode_ref(const std::string& grp, const std::string& ref,
                      const char* png) {
    std::vector<uint8_t> rgba;
    CrxInfo info;
    std::string err;
    if (!bst_load(grp, ref, &rgba, &info, &err)) {
        printf("FAIL %s: %s\n", ref.c_str(), err.c_str());
        return 1;
    }
    printf("%-32s %4ux%-4u bpp=%d off=(%d,%d)\n", ref.c_str(), info.width, info.height,
           info.bpp, info.offset_x, info.offset_y);
    if (png && !stbi_write_png(png, info.width, info.height, 4, rgba.data(), info.width * 4)) {
        printf("PNG write failed\n");
        return 1;
    }
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        printf("usage: crm_tool list <archive.crm>\n"
               "       crm_tool get <archive.crm> <entry> [out.png]\n"
               "       crm_tool ref <GRP dir> <BST reference> [out.png]\n"
               "       crm_tool all <GRP dir> <list.txt>\n"
               "       crm_tool name <BST reference>\n");
        return 1;
    }
    std::string cmd = argv[1];
    if (cmd == "name") {
        BstRef r = bst_parse_name(argv[2]);
        printf("marker=%c entry=%s archive=%s valid=%d\n",
               r.marker ? r.marker : '-', r.entry.c_str(), r.archive.c_str(),
               int(r.valid));
        return 0;
    }
    if (cmd == "ref" || cmd == "all") {
        if (cmd == "ref" && argc < 4) { printf("crm_tool ref needs a reference\n"); return 1; }
        if (cmd == "all") {
            if (argc < 4) { printf("crm_tool all needs a list file\n"); return 1; }
            std::ifstream in(argv[3]);
            std::string line;
            int ok = 0, bad = 0;
            while (std::getline(in, line)) {
                while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
                    line.pop_back();
                if (line.empty()) continue;
                if (decode_ref(argv[2], line, nullptr) == 0) ++ok; else ++bad;
            }
            printf("decoded %d ok, %d failed\n", ok, bad);
            return bad ? 1 : 0;
        }
        return decode_ref(argv[2], argv[3], argc > 4 ? argv[4] : nullptr);
    }

    CrmArchive arc;
    std::string err;
    if (!arc.open(argv[2], &err)) { printf("FAIL: %s\n", err.c_str()); return 1; }

    if (cmd == "list") {
        printf("%s: %zu entries, canvas %ux%u\n", argv[2], arc.entries().size(),
               arc.canvas_w(), arc.canvas_h());
        for (const auto& e : arc.entries()) printf("  %#010x %s\n", e.offset, e.name.c_str());
        return 0;
    }
    if (cmd != "get") { printf("unknown command %s\n", cmd.c_str()); return 1; }
    if (argc < 4) { printf("crm_tool get needs an entry name\n"); return 1; }

    const CrmArchive::Entry* e = arc.find(argv[3]);
    if (!e) { printf("entry %s not found\n", argv[3]); return 1; }
    std::vector<uint8_t> rgba;
    CrxInfo info;
    if (!crxd_decode(arc, e->offset, &rgba, &info, &err)) {
        printf("FAIL: %s\n", err.c_str());
        return 1;
    }
    printf("%s!%s: %ux%u bpp=%d off=(%d,%d) -> %zu bytes RGBA\n", argv[2], e->name.c_str(),
           info.width, info.height, info.bpp, info.offset_x, info.offset_y, rgba.size());
    if (argc > 4) {
        if (!stbi_write_png(argv[4], info.width, info.height, 4, rgba.data(), info.width * 4)) {
            printf("PNG write failed\n");
            return 1;
        }
        printf("wrote %s\n", argv[4]);
    }
    return 0;
}
