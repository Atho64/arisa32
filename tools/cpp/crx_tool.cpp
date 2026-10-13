// crx_tool — CRX image decoder CLI (D.C.4)
// Usage: crx_tool <file.crx> <out.png>   decode to PNG
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include "dc4/crx.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace dc4;

int main(int argc, char** argv) {
    if (argc < 3) {
        printf("usage: crx_tool <file.crx> <out.png>\n");
        return 1;
    }
    FILE* f = fopen(argv[1], "rb");
    if (!f) { printf("cannot open %s\n", argv[1]); return 1; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> buf(size_t(sz > 0 ? sz : 0));
    if (std::fread(buf.data(), 1, buf.size(), f) != buf.size()) { fclose(f); return 1; }
    fclose(f);

    std::vector<uint8_t> rgba;
    CrxInfo info;
    std::string err;
    if (!crx_decode(buf.data(), buf.size(), &rgba, &info, &err)) {
        printf("FAIL: %s\n", err.c_str());
        return 1;
    }
    if (!stbi_write_png(argv[2], info.width, info.height, 4, rgba.data(), info.width * 4)) {
        printf("PNG write failed\n");
        return 1;
    }
    printf("%s: %ux%u bpp=%d comp=%u mode=%u -> %s\n", argv[1],
           info.width, info.height, info.bpp, info.compression, info.mode, argv[2]);
    return 0;
}
