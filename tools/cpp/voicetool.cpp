// voicetool — VOICE.DAT TOC inspector / extractor (D.C.4)
// Usage:
//   voicetool <AdvData\VOICE.DAT> stats          TOC summary
//   voicetool <VOICE.DAT> find <name>            lookup like the engine (e.g. VOICE\asa011)
//   voicetool <VOICE.DAT> extract <name> <out>   extract raw XPCM blob
//   voicetool <VOICE.DAT> extractoggs <name> <out.ogg>  extract payload (voice: strip hdr)
#include "dc4/voice.h"
#include "dc4/xpcm.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace dc4;

int main(int argc, char** argv) {
    if (argc < 3) {
        printf("usage: voicetool <VOICE.DAT> stats|find|extract|extractogg [args]\n");
        return 1;
    }
    VoiceArchive va;
    std::string err;
    if (!va.open(argv[1], &err)) {
        printf("FAIL: %s\n", err.c_str());
        return 1;
    }
    std::string cmd = argv[2];
    if (cmd == "stats") {
        const auto& es = va.entries();
        printf("entries: %zu\n", es.size());
        size_t named = 0;
        uint32_t max_off = 0;
        for (auto& e : es) {
            if (e.name[0]) ++named;
            if (e.offset > max_off) max_off = e.offset;
        }
        printf("named: %zu, max offset: %u (0x%X)\n", named, max_off, max_off);
        for (size_t i = 0; i < 5; ++i)
            printf("  [%zu] %-50s off=%u size=%u\n", i, es[i].name, es[i].offset, es[i].size);
        return 0;
    }
    if (cmd == "find" && argc >= 4) {
        const VoiceEntry* e = va.find(argv[3]);
        if (!e) { printf("not found: %s\n", argv[3]); return 1; }
        printf("%s off=%u size=%u\n", e->name, e->offset, e->size);
        return 0;
    }
    if ((cmd == "extract" || cmd == "extractogg") && argc >= 5) {
        const VoiceEntry* e = va.find(argv[3]);
        if (!e) { printf("not found: %s\n", argv[3]); return 1; }
        std::vector<uint8_t> blob;
        if (!va.read_entry(*e, &blob, &err)) { printf("FAIL: %s\n", err.c_str()); return 1; }
        std::vector<uint8_t>* outp = &blob;
        std::vector<uint8_t> payload;
        if (cmd == "extractogg") {
            if (!xpcm_payload(blob.data(), blob.size(), &payload)) {
                printf("FAIL: not XPCM\n");
                return 1;
            }
            outp = &payload;
        }
        FILE* f = fopen(argv[4], "wb");
        if (!f) { printf("cannot write %s\n", argv[4]); return 1; }
        fwrite(outp->data(), 1, outp->size(), f);
        fclose(f);
        printf("wrote %s (%zu bytes) from %s\n", argv[4], outp->size(), e->name);
        return 0;
    }
    printf("unknown command\n");
    return 1;
}
