// BGM loop points, extracted from DC4.EXE's static table at 0x4B4AD8.
//
// The engine's Audio_PlaySound (0x43C5C0) walks a table of 24-byte records —
// { char name[16]; int32 loop_start; int32 loop_end } — when playing channel 0
// (BGM), and converts the two fields to byte offsets as
//   bytes = frames * channels * bits_per_sample / 8
// so the fields are frame counts (samples per channel). The same table is
// what the installer's file list references, which is why only the BGMs the
// game actually loops are listed.
//
// Not every track has a usable entry: 001.PCM's loop_end (5368829) is not a
// multiple of anything and 002's start (6721536) sits past some tracks' ends,
// so the caller falls back to looping the whole file when a point looks bogus.
#pragma once
#include <cstddef>
#include <string>

namespace dc4 {

struct BgmLoop {
    const char* name;
    int loop_start;   // frames; < 0 means "no loop point"
    int loop_end;     // frames; <= 0 means "loop to the end of the file"
};

// The engine's own table, in its original order.
const BgmLoop* bgm_loop_table(size_t* count);

// Looks up a track by file name ("001.pcm", "001.PCM" or "001").
bool bgm_loop_for(const std::string& name, int* loop_start, int* loop_end);

}  // namespace dc4
