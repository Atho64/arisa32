#include "dc4/bgm.h"

#include <cctype>
#include <cstring>

namespace dc4 {

// Generated from DC4.EXE (see bgm.h for the layout).
static const BgmLoop kTable[] = {
    {"001.PCM", 11261, 5368829},
    {"002.PCM", 6721536, 12229120},
    {"003.PCM", 99327, 2826090},
    {"004.PCM", 112640, 3110912},
    {"005.PCM", 18432, 3665920},
    {"006.PCM", 24064, 3281152},
    {"007.PCM", 14336, 2272256},
    {"008.PCM", 22528, 3726848},
    {"009.PCM", 88831, 4198400},
    {"010.PCM", 1126400, 3407872},
    {"011.PCM", 345600, 3277824},
    {"012.PCM", 20480, 3362816},
    {"013.PCM", 24576, 3528704},
    {"014.PCM", 67584, 3654400},
    {"015.PCM", 458752, 3645440},
    {"016.PCM", 19968, 4764160},
    {"017.PCM", 1004544, 3935232},
    {"018.PCM", 740352, 3608576},
    {"019.PCM", 24576, 2446336},
    {"020.PCM", 2108366, 4176529},
    {"021.PCM", 695307, 3554821},
    {"022.PCM", 1214463, 3824339},
    {"023.PCM", 28672, 4452352},
    {"024.PCM", 793599, 3871744},
    {"025.PCM", 16384, 3342336},
    {"026.PCM", 528384, 3356541},
    {"027.PCM", 6144, 3117056},
    {"028.PCM", 24576, 4186355},
    {"029.PCM", 40960, 3376128},
    {"030.PCM", 901120, 5129032},
    {"031.PCM", 959430, 3781632},
    {"032.PCM", 941671, 9250405},
    {"033.PCM", 1401045, 4174848},
    {"034.PCM", 458752, 6701056},
    {"035.PCM", 29696, 4071018},
    {"036.PCM", 35840, 4267296},
    {"037.PCM", 821248, 3563520},
    {"038.PCM", 34816, 5858408},
    {"039.PCM", 30720, 3733556},
    {"040.PCM", 796672, 4260864},
    {"041.PCM", 642048, 3270740},
    {"042.PCM", 717824, 3588096},
};

const BgmLoop* bgm_loop_table(size_t* count) {
    if (count) *count = sizeof(kTable) / sizeof(kTable[0]);
    return kTable;
}

bool bgm_loop_for(const std::string& name, int* loop_start, int* loop_end) {
    std::string up;
    for (char c : name) up.push_back(char(std::toupper((unsigned char)c)));
    if (up.size() > 4 && up.compare(up.size() - 4, 4, ".PCM") == 0)
        up.resize(up.size() - 4);
    if (up.size() < 3) up = std::string(3 - up.size(), '0') + up;
    up += ".PCM";
    for (const BgmLoop& e : kTable) {
        if (std::strcmp(e.name, up.c_str()) == 0) {
            if (loop_start) *loop_start = e.loop_start;
            if (loop_end) *loop_end = e.loop_end;
            return true;
        }
    }
    return false;
}

}  // namespace dc4
