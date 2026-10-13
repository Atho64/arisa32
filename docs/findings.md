# D.C.4 / adv32 (CIRCUS engine) — Reverse-Engineering Findings

Living document. Sources: direct analysis of the game folder (`G:\DC4 new\DC4`),
GARbro `ArcFormats/Circus/*`, cokkeijigen/MesTextTool, lifegpc/msg-tool,
cokkeijigen/CircusEnginePatchs (`DC3WY.md`), web research (2026-09-26).

## Game / engine identity
- D.C.4 ~Da Capo 4~ (CIRCUS, 2019-05-31). Engine community name: **adv32** (VNDB).
- Exe: `DC4.EXE` / `oriDC4.EXE`, 927,232 bytes, 32-bit x86 PE, built ~2019-05-04, no packer.
- Tech stack (from imports/strings): **Direct3D 9** (`d3d9.dll`, `d3dx9_43.dll`,
  `D3DXCreateEffectFromResource` → HLSL effects), **DirectSound** (`DSOUND.dll`),
  built-in **Ogg Vorbis**, **DirectShow/COM** for MPEG video (per DC3WY notes),
  `IMM32`, GDI `GetGlyphOutlineA/W` + `CreateFontA` for text rasterization,
  glyph-sheet cache files `AdvData\grp\FONT0.IMG … FONT5.IMG`, `WINMM`, `WININET`
  (clear-bonus URL), registry `Software\CIRCUS\DC4`, reference to `\circus_spec.txt`.
- Logical resolution: **1280×750** (from CRX headers of full-screen assets).
- Cracked variants in folder: `adadjha.exe`, `dc4crackdvdaaa.exe` (byte-patched copies
  of oriDC4.EXE; disc/key check bypassed by user). Our reimplementation never
  implements those checks at all.

## Data layout (all loose files under `AdvData/`, no master archive)
```
AdvData/
  GRP/           CRX images: BG, BST (sprites), CURSOR, ED, EFF (effects), FACE,
                 MSK, SYSTEM (UI/chips/fonts cache), NAMES.DAT
  MES/           ~330 *.mes compiled scenario scripts (SJIS text + bytecode)
  MOVIE/         MPEG-1 .mpg (OP1/OP2/OP3 + songs)
  MUSIC/         NNN.pcm  (XPCM = header + Ogg Vorbis)
  SE/            NNN.pcm  (XPCM)
  VOICE.DAT      1.08 GB voice container (TOC at start, offset/size pairs)
  savedata/      DC400.dat (game saves), system.dat (global/system data)
```

## Formats

### CRX image (magic `CRXG`; variants `CRXD`, `CRXJ`, `CRXB` seen as code refs in exe)
Per GARbro `ImageCRX.cs` (CRXG):
```
0x00  "CRXG"
0x04  int16 OffsetX        0x06 int16 OffsetY
0x08  uint16 Width         0x0A uint16 Height
0x0C  uint16 Compression   (1..3; 1 = custom LZ77, 2/3 = zlib)
0x0E  uint16 CompressionFlags  (bit 0x10: int32 compressed-size field present)
0x10  int16  Colors        (0 = 24bpp, 1 = 32bpp, else 8bpp palette, 0x102 = 4-byte pal entries)
0x12  uint16 Mode          (alpha post-processing selector)
--- 8bpp: palette (3 or 4 bytes/entry, ≤256 entries)
--- Compression >= 3: int32 count, count*0x10 chunk table (parsed, ignored by GARbro)
--- flags & 0x10: int32 compressed size
--- compressed pixel data
```
- V1: custom LZ77, 64 KB ring window, 8-bit flag stream, 3 control tiers + 0x7F long form.
- V2/V3: zlib stream; ≥24bpp rows have a filter byte: 0=horiz delta, 1=vert delta,
  2=diag delta, 3=RTL vertical, 4=RLE-per-channel, other=raw. 8bpp rows raw w/ stride pad.
- 32bpp alpha post-pass: A,B,G,R → B,G,R,A with alpha ^= 0xFF unless Mode==2 (no flip)
  or Mode==1 (skip).
- Team repacker writes PNG+JSON sidecar (`InnerX, InnerY, Width, Height, Version:2,
  Flags, Bpp, Clips[]` — Clips = sprite-sheet chip rects, matches `*chip.CRX` files).

### XPCM audio (MUSIC/SE `*.pcm`, likely VOICE.DAT entries)
```
0x00 "XPCM"   0x04 dword (≈samples? or bytes)   0x0C dword data length (== filesize-16)
0x08 word / 0x0A word: channels/codec fields (GARbro AudioPCM.cs has full map)
0x10 payload — in DC4: raw Ogg Vorbis stream (OggS…, 44.1 kHz stereo confirmed)
```
- vgmstream calls the raw/AD-PCM variants "XPCM/AD-XPCM" and the Vorbis one **vc-XPCM**.
- FFmpeg has an `xpcm` demuxer; vgmstream decodes all variants.

### VOICE.DAT — SOLVED (IDA: Voice_LoadTOC 0x452590, Voice_Lookup 0x4526A0)
```
u32 count                       (0x41C5 = 16837 entries)
count × (u32 offset, u32 size)  legacy index — loader seeks over it, then overwrites
count × 64-byte entries:        the array the loader actually keeps ("Base")
  char name[56]                 zero-padded, uppercase, e.g. "SE\0000SILENT24.PCM",
                                "DC4_ASA20190429B\DC4_ASA20190429B_ASA001.PCM"
  u32 offset                    absolute file offset of the XPCM blob
  u32 size                      bytes of the blob
<pad to 0x800> then data blobs
```
- Loader `_read`s count, reads 8*count pairs into a malloc(count<<6) buffer, then
  reads 64*count entries over it, then `qsort` by name (CompareFunction).
- Lookup: strip everything before first `\`, append ".pcm", `_stricmp` binary search,
  returns entry+56 (offset). Names are case-insensitive-sorted after qsort.
- Verified: entry 0 = `0000SILENT24.PCM` @ 0x128000 size 0x158A4 == loose SE copy.

### XPCM audio — SOLVED (Audio_OpenAndDecode 0x452A40); two header variants, per-file
- **Vorbis variant (16-byte header)** — used by MUSIC and most VOICE entries:
  `"XPCM"` + u32 decoded_pcm_size + u32 0x00000505 (u16 tag=5 ⇒ Ogg Vorbis,
  u16 secondary=5) + u32 data_len(=filesize−16) + raw Ogg Vorbis stream.
  Verified by extraction: `SYSTEM\SYSTEM_ASA011.PCM` → OggS at +16.
- **PCM variant (28-byte header)** — some voices (e.g. `0000SILENT24.PCM`):
  `"XPCM"` + u32 data_size + u32 0 + WAVEFORMATEX(16: tag=1, ch=1, 44100,
  88200 B/s, align=2, 16-bit) + raw PCM16.
- Loader tries extensions `.pcm` → `.wav` → `.ogg`; codec ids in engine: 0 raw,
  1 PCM, 2 ADPCM (sub_4527E0), 5 vorbis (`dword_4F9654=5`).
- Note: datalen field at +12/+4 is authoritative; entry.size in VOICE.DAT TOC
  covers header+payload.

### MES scenario script container — SOLVED (Mes_LoadFile 0x438300, Mes_SplitBuffer 0x438200)
```
u32 count                      number of labels (main.mes: 0x80)
u32[count] label_offsets       low 24 bits = code offset; HIGH BYTE = flags
                               (0x80 seen on some labels, e.g. main.mes label 7)
u16[count] label_ids
u32 version                    0x2400AAB6 for DC4 (MesTextTool reads low u16 0xAAB6;
                               Plus Harmony 0xABB6); first label offset is always 3
byte code[...]                 VM bytecode
```
- Mes_SplitBuffer splits into: dword offsets array (object+219948), u16 ids array
  (object+0), code buffer (object+115944, first 4 bytes = version u32).
- Version check: byte[0]==0xB6 && byte[1]+byte[2]<<8==0xAA, else MessageBox
  "MESファイルを更新してください".

### MES VM dispatcher — STRUCTURE KNOWN (MesVM_ExecuteOpcode = sub_428640, 38,495 bytes)
- One giant function at 0x428640, called from 4 sites (0x432171, 0x434595,
  0x434652, 0x4356e2).
- Primary jump table at 0x428DBC: **182 cases = opcodes 0x00..0xB5**; 139 valid
  handlers, 43 invalid (shared stub 0x430EDA). Invalid: 0x01,0x02,0x08,0x0D,0x0E,
  0x12,0x14,0x19,0x1F,0x23,0x27,0x29–0x31,0x3A,0x3B,0x42,0x48,0x53,0x57,0x5D,0x60,
  0x62,0x66,0x69,0x6C,0x6E,0x77,0x8A,0x91–0x93,0x97,0x98,0xA3,0xA6,0xB5? (see
  work/jumptable.json; 0x5D per MesTextTool is the *text* opcode handled by the
  text engine, not this table).
- 8 secondary jump tables inside (17/16/4/4/17/6/6/5 cases) for sub-dispatch.
- VM state: global `Buf` (engine context); current command buffer `Buf+0x40E78`;
  script object holds code at +115944, label offsets +219948, label ids +0.
- Per-handler disassembly + 250 callee decompilations dumped to
  work/disasm_dispatcher.txt, work/pseudo_dispatcher.c, work/callees.json,
  work/opcode_summary.txt.
- Key subsystem functions identified & renamed in IDA (45 names, see
  work/rename_batch.json): Mes_LoadAndSplit, Voice_LoadTOC, Audio_PlaySound,
  Cmd_ParseParams (@cmd/@re1/@w1 param syntax), SaveData_Process (SAVEDATA%02d::),
  SysData_Load (SYSDATA::), Sprite_BstParseName (bst\ + _B suffix rules),
  PCK_Read (.pck archives), SystemDat_Load/Save, Save_Write/ReadSlot/Preview,
  Gfx_QueueDraw/Gfx_DrawLayer/Gfx_EffectSetup, VM_VarRead/VM_VarWrite, VM_Random.
### MES command stream — FULLY DECODED & VERIFIED (fetcher sub_438790; 50,707/50,707 labels consistent)
The "bytecode" is a variable-length command stream. The VM fetcher (sub_438790)
groups opcodes exactly as MesTextTool described:
```
op 0x00         nop — no advance
op 0x01–0x3A    u8, u8 operands                       (3 bytes total)  "uint8x2"
op 0x3B–0x47    u8 param + NUL-terminated raw string  (uint8str)
op 0x48–0x68    NUL-terminated raw string             (string) — paths, names
op 0x69–0x6D    NUL-terminated TEXT string            (encstr) — decoded by
                sub_4076B0: each stored byte +0x20 = real SJIS byte
                (verified: 62 ad → 82 CD = み; 61 22 → 81 42 = 。)
op 0x6E–0xFF    u16 × 4                               (9 bytes)  "uint16x4"
op 0x77 ('w')   wait modifier — fetcher reads its 4×u16 and attaches it to the
                CURRENT command (never reaches the dispatcher → "invalid" case)
```
- The fetcher returns the new pc and a 0x2024-byte command struct:
  [0]=opcode, [1..2048]=string copy, [2049..2052]=4 params, [2053..2056]=peeked
  'w' params. String ptr also saved to script object +224768 (dword 56192).
- Verification (tools/proto/mes_scan.py): walking from every one of the 50,707
  labels in all 330 .mes files with this model yields 100% consistent command
  boundaries; ~10M+ text chars decode as valid SJIS.
- Example (med.mes): `0x82 (99,195,0,0)` (vars), `0x70 (255,0,0,0)` (color),
  `0x3C p1 "bg\black"` + `0x49 "bg\black"` (load layer + set image),
  `0x4B "skiped"` (flag), `0x0F (2,0)` (layer select), `0x3C p0 "ED"` + `0x4F "ED"`.
- 0x6A/0x6B/0xB0 reset the 11-layer sprite stack (dispatcher prologue 0x428CB1);
  the prologue also manages 51 sprite slots (Cmd_ParseParams @cmd/@w1 syntax) and
  stack depth 11 with overflow MessageBox.
- Remaining per-opcode SEMANTIC naming = mechanical (work/opcode_summary.txt has
  call profiles per handler block).

### MES text dialect (from MesTextTool `script_info.cpp`, DC4 = "dc4" alias)
- Version `0xAAB6`, "offset2" layout (`head[0]*6+4`), requires head[1]-area `== 3`.
- Opcode byte groups: `0x00–0x3A`, `0x3B–0x47`, `0x48–0x68` (strings),
  `0x69–0x6D` (encoded strings, **XOR 0x20**), `0x6E–0xFF`; text output opcode `0x5D`.
- Text encoding: cp932 (Shift-JIS). Engine string `"MESファイルを更新してください"`
  (0x491560) and `"MESファイルバッファがとれません"` (0x491580) locate the loader.
- msg-tool (`src/scripts/circus`) implements export/import/**create** for this format.

### NAMES.DAT
`int32 count` + `count * 32 bytes` fixed-width uppercase names (START, MAIN, SAVE, …).

### Movies
Plain MPEG-1 `.mpg` — decode via FFmpeg; engine used DirectShow.

### Save data (TBD)
`AdvData/savedata/DC400.dat` (356 KB, 100 slots × ~3.5 KB?), `system.dat` (21 KB).
Slot name format in exe: `SAVEDATA%02d::`.

## Engine internals from CircusEnginePatchs `DC3WY.md` (sibling exe, D.C.3 WY)
- DRM: DVD check (NOP'd conditional before MessageBox) + `InstKey` key check
  (three string refs; pass path jmp at `0x429434` in dc3wy.exe — offsets are
  game-specific, technique transfers).
- Video: `ComPlayVideo` 0x444920 / `ComStopVideo` 0x444640; DirectShow via COM;
  current video path in global char[MAX_PATH] at `0x4E65F8`.
- Audio: play 0x431870 / stop 0x432490; global `IDirectSoundBuffer*` array
  `0x4A95EC`; filename found at `esp+0x12C` on entry.
- WndProc 0x40FC20 (via `RegisterClassA` ref).
- Text: `GetGlyphOutlineA` hooked for GBK fonts; glyph cache FONT*.IMG.
- Ruby markers: fullwidth ｛／｝ (0x816F/0x815E/0x8170); older engines used single bytes.
- Chapter titles table in-binary at 0x49DF58 (game-specific).
- Backlog avatar: name-string compare sets grid line/row.

## Public landscape
- **No public adv32 reimplementation exists** (checked GitHub topics, GARbro,
  vgmstream, asmodean, arc_conv/arc_unpacker, galgamewiki, Chinese hanhua scene).
- Chinese fan translations of DC4 exist (2DFan “step-2-16k” patch, ai2.moe
  Plus Harmony 内嵌汉化) — tooling not public; MesTextTool/msg-tool/GARbro are
  the open pieces.
- This project will be the first open adv32 runtime + first public full opcode doc.

## CRXB ("CRM") image archive + CRXD differential images (GRP/BST/*.CRM, GRP/FACE/*.CRM)

**CRXB header** (GARbro `ArcCRM.cs` agrees):
```
0x00 char[4] "CRXB"
0x04 u32     version (1)
0x08 u32     entry count
0x0C u16     0x0400 (constant across files, not a size)
0x0E u16     0x0800 (0x0352 in RCC_A.crm)
0x10 entries[count], 32 bytes each:
       0x00 u32        absolute file offset of the entry data
       0x04 u32        id, +0x80 per entry
       0x08 char[24]   name, e.g. "NIN_B1_A08.CRX"
```
Entry size = distance to the next *offset* (the table is **not** offset-sorted);
the last one runs to EOF. A shared base image blob sits between the table and
the first entry and is referenced by offset only.

**CRXD block** (the payload of most entries) — GARbro `ImageCRXD.cs` agrees:
```
0x00 char[4] "CRXD"
0x04 u32        0x11 in every DC4 file
0x08 u32        base image offset inside this archive (0 = none)
0x0C char[20]   base file name, used when the base is a loose .CRX
0x20 char[4]    "CRXG" -> inline diff image follows (plain CRXG)
                "CRXJ" -> u32 at 0x28 = offset of another CRXD holding the diff
```
Decode: unpack the base, unpack the diff (raw pixels, no canvas), then over
the intersecting rectangle add the diff to RGB and subtract it from alpha
(byte-wise wraparound). Verified: 2295/2295 unique BST references across all
512 scripts decode to RGBA. `crm_tool list/get/ref/all/name` exercises it.

## BST character-sprite naming (Sprite_BstParseName, 0x41E940)

Script references look like `BST\NIN_B1_A08,BST\NIN_B`. Delimiters are `,` `*`
and `@` (constants at 0x48C774/778/790/794); the first one splits
**entry** from **archive**. The engine may also append `_B` to the entry when
the slot previously used that outfit, so a trailing `_B` is stripped too.
Resolution: `GRP/BST/<archive>.CRM`, entry `<entry>.CRX`; with no delimiter it
is a loose file `GRP/BST/<entry>.CRX`. Filenames are matched case-insensitively
(disk is `nin_b.CRM`, scripts say `NIN_B`).

Entry names are `<CHARA>_<OUTFIT><POSE>_<EXPR><FRAME>`; 17 frames per
expression set. `*`/`@` are outfit-variant separators, not wildcards.

Validated: 23345 BST commands in 512 scripts, 2295 unique names, **0 misses**.

## Sprite slots (sub_41E830 + MesVM case 60 / case 139)

`sub_41E830(slot)` maps script slots onto the graphics table:
slot 1/2/3 → layers 0/1/2, **slot 4..8 → character bodies 10/20/30/40/50**,
slots 9..13 → faces (+2), 25/26 → cut-ins 63/65, 31/32 → 75/76.

- `0x3C slot, name` — load. Slot 19..23 additionally run `Sprite_BstParseName`
  then `Sprite_BstPath2`; slots 4..8 only write the engine's 67-byte name
  registry and set a dirty flag (something later renders them); `--` reuses the
  slot's previous name; a leading `*` means "atoi index".
- `0x8B p0,p1,p2,p3` — transform. `p3 & 0x10` selects "set", `p3 & 0xF` is the
  **character index minus one** (so `0x8B ... ,16` targets slot 4). `p0` =
  scale in percent (`sub_4443C0` stores it raw), `(p1,p2)` = the sprite's
  **centre** on the 1280x720 screen — the default `8B 100, 640, 360, 16` is
  that centre. `sub_444450` stores the target, `sub_4442C0` reads the animated
  current value.
- Histogram over all scripts: scale ∈ {78, 100, 65, 128}, x ∈ {384..640},
  y ∈ {128..448}.

**Screen size is 1280x720**, not 750: MESWIN.CRX and the system sheets are
1280x720 and backgrounds are 1280x720 / 1600x900 / 1920x1080 (16:9).

## BGM loop points

`Audio_PlaySound` (0x43C5C0) channel 0 walks a static table at **0x4B4AD8** of
42 records, 24 bytes each: `{ char name[16]; int32 loop_start; int32 loop_end }`
(`001.PCM` … `042.PCM`). Fields are frame counts (samples per channel); bytes =
frames × channels × bits/8. Extracted to `core/src/bgm.cpp` (`bgm_loop_for`).
001.PCM loops 0.26 s → 121.74 s of its 128.6 s. Channel map in the same
function: 0 = BGM, 2..5 = SE, 6/7 = the `se\2xx` preset table, 8..11 = voice.

`Audio_StopChannel`/`sub_43C4F0(group, action)`: group 0 → channel 0,
1 → channels 2..5, 2 → 8..11. This is what MES opcode 0x70 calls.

## Message window / system bar placement (win_size.mes, main.mes)

The UI is **not** baked into MESWIN.CRX at its on-screen position — the CRX is
a *sheet* and the scripts blit pieces out of it.

**Sheet registry**: `0x3D slot, "SYSTEM\NAME"` loads `GRP/SYSTEM/NAME.CRX` and
registers it at index **slot + 37** (MesVM case 61: `v60 = v656[2049] + 37`).
`start.mes` registers 0=MESWIN → 37, 1=ADVMENUCHIP → 38, 2=RIGHTCHIP → 39,
4=MOUSEGES → 41, 5=EMOTION → 42. RIGHTCHIP is exactly 486x514, matching the
largest rect win_size.mes blits from sheet 39.

**Chip opcodes** (all 4 x u16):
- `0x73 dst_x, dst_y, src_x, src_y` then `0x77 w, h, sheet, state` — one state
  of a widget. state 99 = normal, 1 = hover, 2 = pressed.
- `0x7A dst_x, dst_y, src_x, src_y` then `0x77 w, h, sheet, state` — a chip
  drawn outright (no widget), e.g. the bar capsule.
- `0x72 x1, y1, x2, y2` — hit rect; `0x54 ":action"` — the action tag.
- `0x17 a, b` — opacity/alpha pair.

**The bottom bar** (from win_size.mes, identical in main.mes): capsule
`7A 400,695 <- 397,392` 836x24 from sheet 37, then 11 buttons all at y=695,
h=24, each dst_x = hit_x1 + 4:

| action  | hit x     | w   | sheet src            |
|---------|-----------|-----|----------------------|
| :voice  | 400..470  | 70  | 400,442 (hover 466)  |
| :auto   | 470..537  | 67  | 470,442              |
| :rapid  | 537..619  | 82  | 537,442              |
| :backlog| 619..708  | 89  | 619,442              |
| :qs     | 708..822  | 114 | 708,442              |
| :ql     | 822..937  | 115 | 822,442              |
| :ds     | 937..1010 | 73  | 937,442              |
| :dl     | 1010..1082| 72  | 1010,442             |
| :option | 1082..1171| 89  | 1082,442             |
| :clear  | 1171..1234| 63  | 1171,442             |
| :n      | 1234..1280| 46  | 1234,416 (hover 392) |

Sheet 37's own layout (MESWIN.CRX, 1280x720): top-left cat logo + gears y 3..31,
SKIP/AUTO y 72..134, **name plate y 197..240 and 270..313 (x 337..908)**, bar
capsule y 384..440, button rows y 444..459 (normal) and 469..484 (hover),
message-window background y 546..720.

**The cyan parallelogram at (338,197) is the choice box, not a name plate.**
Confirmed with a 1:1 (1280x720) real screenshot: the speaker name is *plain
outlined text* above the window, no plate behind it. The piece is presumably
blitted per-choice by whichever script emits the selection (TBD).

**What the renderer must compose** (verified against real screenshots):
- message-window background = sheet 37 rect (0,540)-(1280,720) at its natural
  position, ~82% alpha;
- speaker name = plain outlined text, 18px, at x=283 baseline 540;
- the bar and its buttons blitted to y=695 per the table above;
- dialogue = 21px glyphs advancing 21px, column x=283 width 525 (25 per line),
  up to 2 lines, baselines ≈ 602 and 638, 1px dark outline under white glyphs.

Text opcode **0x6A is the speaker name** and 0x6B/0x6C/0x6D are the message
line; the name must be consumed separately or the engine renders the speaker
as if it were the line.

## More VM opcodes identified (round 2)

| op | handler | meaning |
|----|---------|---------|
| 0x52 | case 82, `0x42e629` | **play movie**: builds `MOVIE\<name>.mpg` via the path builder and plays it through DirectShow (`sub_453860`) |
| 0x13 | case 19, `0x42af90` | **audio group stop/fade**: `sub_43C3F0(audio, group, flag)` with group 0 = BGM (channel 0), 1 = SE (channels 2..5), 2 = voice (channels 8..11) |
| 0x6B | case 107, `0x42f043` | dialogue **accumulates** into the message buffer at `this+12516`; when the same line continues a voice it is prefixed with `@c%d` (voice-continuation tag), then `Cmd_ParseParams` handles inline tags |
| 0x6D | case 109, `0x430890` | secondary text sink — `sub_4581D0` routes to sub_457200 / sub_458330 depending on a mode (backlog/log) |

## Scene chip sheets and the date-HUD (calendar) — SOLVED (2026-10-11)

The in-game date/weather HUD (the "2月 4(月)" eyecatch calendar) is **script-driven**,
not native. Every chapter script contains a cascade right after its eyecatch
background commit, e.g. `dc4_kyo20190204a2.mes`:

- `3C 19, "SYSTEM\EYECATCH_CHIP"` — loads the 630x394 chip sheet into **slot 19**
  (the sheet contains month/day digits, 月( ), weekday kanji, 30 weather
  circles, 30 moon-phase circles);
- a cascade of `7A dst_x, dst_y, src_x, src_y` + attached `77 w, h, sheet,
  state` blits, one per HUD piece, with `86/82/02` variable branches selecting
  the src per date/weekday (bank3[91]/[77]/[78]/[92] = day, weather, moon,
  month): month digits at (1168,22)/(1132,22), 月 at (1216,28), weather circle
  64x64 at (1140,76), moon circle at (1204,76), plus the forecast chain
  (992,11)/(1004,92)/(1056,164)/(1128,216)/(1212,228).

**Sheet-handle table**: the native keeps 64 image handles on the VM object at
`+0x634` (MesVM case 122 reads `*(this + 4*(sheet & 0x3F) + 1588)`).
`0x3D "SYSTEM\NAME"` registers at **slot + 37** (37..42 for start.mes);
`0x3C slot, path` registers at the **raw slot id** — so a chapter script's
slot 19/20/22/23 SYSTEM loads (EYECATCH_CHIP, EXCHIP, CGASACHIP, TITLECHIP01/02,
CONFIGCHIP*) are chip-sheet sources, NOT full-screen layers. Only the `0x49`
commit of slot 1 builds the visible background.

**Chip draw semantics** (validated by pixel-exact template match of the 月 chip
at SAD 0.006 against a 1:1 original screenshot): chips blit **top-left anchored
at dst + the 0x88 offset**, 1:1, from the sheet src rect. `sub_417D20` builds a
retained item (position via `sub_444260`, half-extents via `sub_444450`, UV via
`sub_444340`) queued into the z-map of the object at `VM+0x6210` keyed by its
float at +172; `sub_412BA0` supplies the global dst offset from `VM+0x384/0x388`
(set by `0x88 a,b` = `sub_412BC0` — the a2 cascade sets (0,2) then (0,0)).

**Chip lifetime**: the calendar shows on the eyecatch and is gone during
dialogue (verified against original screenshots), so the retained chips are
cleared by the next background commit — `sub_425DB0` (0x49's workhorse) tears
the layer state down via `sub_41F440`/`sub_413360` family calls. Implemented
model: `scene_chips_` accumulates 0x7A/0x77 pairs, renders after sprites, and
clears on a real 0x49 commit. BST loads into a raw slot replace that table
entry (`sheets_.erase(slot)` in `load_bst`).

Corpus check (all 512 scripts): 0x3C slots are 1 = backgrounds, 4..8/19..23
BST refs, 25..27 CUT refs, 19/20/21/22/23 SYSTEM chip sheets, 21 MSK masks,
26..29 SYSTEM\EFF* particle overlays (native-driven, never script blitted),
31/32 FACE refs (graphics table 75/76 — semantics still open), 0 = script
names for transfers.

## 0x8B sprite transform — SOLVED (2026-10-11, verified against live captures)

Case 139 (`0x42D215`), operands p0..p3 (all u16, signed where noted):

- **p3 = 0**: current-position write. `sub_401040 -> sub_444260` stores
  `(p0, p1)` (signed) on the character body layer `10*(p2+1)` and snapshots it
  (`sub_444300` -> fields [242],[243]). p2 selects the character. This is what
  actually moves a sprite at scale 1 (all slide-ins use cur_y = -40).
- **p3 & 0x10** (place): `sub_4443C0` stores current scale = p0 percent
  (bytes 128/132, animated toward bytes 356 via the `sub_448F00` stepper);
  `sub_444450` stores (p1, p2) at bytes 152/156 — the **zoom anchor in frame
  space**, not a screen position; `sub_4443E0` snapshots it.
- **p3 & 0x20**: `sub_4449B0(p0)`; **p3 & 0x40**: the ±20px nudge family
  (`sub_444470`/`sub_4477E0`/`sub_41DE40`, p0 selects 0..3 presets);
  **p3 negative**: clears the name registry and queues `sub_447B00` with
  `12*p1` ticks; **255**: nothing.

**Placement model** (verified to ~1.5px on three independent live captures —
a2 bedroom 78%, a2 street two-sprite 100%): the BST entry's composed canvas
lives at inner offset `I`, size `C`, inside a registration frame that is
always **1280 tall** (`I_y + C_h = 1280` in every entry; the canvas bottom is
the character's feet). The native pins the frame point `(tgt_x, tgt_y)` at
screen `(cur_x + tgt_x, cur_y + tgt_y)`:

```
screen_centre = (cur_x + tgt_x, cur_y + tgt_y) + (I + C/2 - (tgt_x, tgt_y)) * s
```

Equivalently `centre = cur + tgt*(1-s) + (I + C/2)*s`: at scale 1 the place
operands cancel entirely and the mode-0 position alone moves the sprite; the
default `8B 100, 640, 360, 16..20` at every script start only resets the zoom
anchor, leaving characters at their authored frame position. The 0x8E slide
offsets decay the mode-0 position linearly to zero (`sub_447620`), and the
per-layer camera state (bytes 36-48: scale + pivot, defaults (640,360);
`sub_444A50`) composes on top — bg layers get pivot (512-x, 288-y), character
layers (512-0.8x, 288-0.8y), the 80% being the 1280->1600 bg over-scan.

**Supporting evidence**: the draw animator `sub_448F00` steps bytes [8,12]
(current pos) toward bytes [208,212] with velocity entries, and steps scale
bytes [128,132] toward [356]; the BST load path `sub_423090` parses the
character registry (`sub_40DBA0`, table at VM+0x5688/0x56A8, count at
+0x19EA8) and stores the same-name registered position into bytes [152,156].

**CRM chain fix (same session)**: entries whose diff is a **CRXJ** point at a
CRXD *wrapper* whose own inline CRXG is the real diff payload — composing the
whole wrapper (our old reading) added a second full body and wrapped every
channel (white -> cyan, faces teal). `crm.cpp` now decodes the wrapper's
inline diff directly; NIN_B1_AB02/NIN_B2_B11 and every other chained entry
decode correctly.

**CRM blend fix (same session)**: the 32bpp raw pixel layout is **(A, B, G, R)**
— confirmed by solving `needed_diff = native_render - base` against a live
capture (needed = (p3, p2, p1) exactly). The old blend added raw bytes 0..2
(alpha, B, G) and *subtracted* byte 3 (R), wrapping the red channel and turning
every face diff teal. The blend now subtracts byte 0 and adds bytes 1..3.
24bpp rows (plain B,G,R) were already correct.

**0x8C dismisses the CUT cut-ins (2026-10-11, a2 pocket-watch scene).** The
memory cut-ins (`3C 25, BST\CUT007` -> sprite 12) are effect layers, not
regular sprites: native keeps a single effect-layer object at VM+0x688 that
the `40 n,"param"` ops (Gfx_SetLayerParam) configure right after the load.
`8C 0,3,0,128` (MesVM case 140 at 0x42D6CF) is the dismissal: mode word =
last operand, `mode & 0x80` fades that effect layer out through its wipe mask
via `sub_447B00` — alpha 255 -> 0 at a rate of 510/(w1*12) per 60 Hz frame,
i.e. fully transparent halfway through (w1=3 -> 36 frames, gone in ~0.3 s).
The queued entry targets VM+0x688, the direction flag stays 0 at this call
site, and the fourth arg is `VM_VarRead` (0x421E40, a skip/wait state check):
while skipping, no animation is queued and the op instead unloads the mask
slot (`Sprite_BstPath("-", 21, 0)`) — the cut-in vanishes instantly. Without
this op our engine kept the cut-in on screen forever (a2: it lingered through
「あっ……」 and 「いい朝なんだから…」 instead of disappearing after
「そして、念じる」). Implemented as a per-sprite fade clock (alpha drives the
existing blit alpha; `finish_animations` dismisses the layer for
skip/headless), targeting the last-loaded slot 25-27 sprite.

Known residual fidelity gap: BST expression frames A00..A15 are a native
animation cycle (blinking/mouth); the engine composites the single static
frame named by the script, so mid-dialogue expressions differ.

**Preload sheet slots 19-23 are never drawn (2026-10-11, a2 street scene).**
Before lines carrying `@bN` blink tags the script preloads alternate
expression BSTs into sheet slots 19..23 (e.g. `3C 19, BST\NIN_B1_AB02`,
`3C 20, BST\NIN_B1_AB16` next to the current `3C 6, BST\NIN_B1_AB01`); the
tags swap preloaded content into the character's current slot mid-line. The
original renders only the current-slot sheet. Registering a preload used to
create/clobber a visible sprite (slots 19-23 shared sprite indices 0-4 with
body slots 4-8), which stacked two extra stale-transform copies of the
character across the screen. `load_slot` now routes BST loads on 19-23 to
`load_preload` (decodes into the sheet table only) and treats `3C n, "-"` /
`"--"` there as sheet-table clears/refreshes. Slots 25-27 (`BST\CUT*`
cut-ins) remain real foreground sprites.

## COMU phone chat — SOLVED (2026-10-11)

The in-game smartphone ("ＣＯＭＵ") is driven by two opcodes. `0xAB` (MesVM
case 171 at 0x430710) manages the phone through a manager object at VM+0x6220
plus a global active flag at Buf+0x109B58; `0x6D` strings (case 109 →
sub_4581D0) append chat entries while a group is open. Modes observed in the
corpus: 99 = create (`AB 99, 2` in start.mes), 65535 = destroy/clear (every
scenario start), 1 and 7 = show + open a message group, 0 = group end (the
queued messages commit — sub_458CC0 consumes the queue; the phone stays
visible), 98 = hide. `15 0, 1` after each group is the native tap-wait.

Layout comes from the type-2 UI constructor sub_458940: a bg\black dim layer
at 0.5 alpha, the phone chip (COMUCHIP.CRX crop 0,0–372,518) at (440,16), and
five visible message slots from (468,80) with an 81px pitch. sub_458370 parses
each entry: `"「name」text"` selects the contact row by the 11-name SJIS table
at 0x492850..0x4928A8 — 有里栖, 二乃, 諳子, ひより, 詩名, 未羽, ちよ子, 有里咲,
一登, 杉並, 叶方 — unknown senders fall back to row 1; the row chip
(COMUCHIP crop x420..740, y 81·contact..+81) carries avatar, name plate and
the blank bubble, and the text draws at x540 in dark gray, wrapped at 28 SJIS
bytes. `"@dN"` stamps crop comustamp.CRX at (80·((N−1)/8), 64·((N−1)%8)),
79×63 per cell (8 columns, 64px rows) — verified @d1 = ごはんだよ and
@d2 = 了解 against dc4_sen20181213d. The timeline persists across AB 98 and
clears with AB 65535; the queue holds 20 (type 1) / 200 (type 2) entries.
Implemented in `scene.cpp` (`comu_command`/`comu_message`/`draw_comu`); save
format v4 adds the chat state. Native message-by-message reveal pacing
(this[557]/@t) is not reproduced — all committed entries show at once.

## Choices

There are **no hit rects in any chapter script** (0x72 only appears in
sce_*, config, start, main, cg_*, LOAD/SAVE — the menu screens). So choices
are not script-driven widgets; they go through the VM's variable/branch
system, and the `com1`/`sel1` strings seen in chapter scripts are menu/label
tags (`0x4B` = goto label), not UI actions.

Implementing choices therefore needs the VM's variable model (the
`0x02/0x20/0x86/0x99/0x9D` family) and the branch opcodes — the next big
piece of RE.

## Next-page gear (open)

The small gear shown right after the text while waiting for input is **not**
from MESWIN (its (70,0)-(180,32) strip holds four 27x32 frames, but they are
decorative window gears with soft alpha), nor from MOUSEGES.CRX (mouse-cursor
and input-help art) nor EMOTION.CRX (colour-cycled emotion animation frames).
Source still unidentified — likely a Direct3D primitive or another asset.

## Voice auto-play

`Audio_PlaySound` channel 0 plays BGM; `0x4D "VOICE\..."` (case 77) is the
only opcode that names a voice file directly. SE/BGM track *numbers* are
resolved through the VM variable system, which is why chapter scripts contain
no audio names.

## Save/Load screen reconstruction — 2026-10-11

The shared desktop/Android `dc4ui::SaveScreen` now uses original art and
geometry. See `docs/re-save-load.md` for the complete retained evidence,
source rectangles and distinction between native behavior and host choices.

Corrections to older notes: native save metadata/widget construction is
`0x40B6F0`, not `0x437810`; row text is rendered by `0x436280`. `DC400.dat` is
one original slot file, not a container for all saves. The native ordinary
slots map to physical IDs 3–102, with separate special slots 0–2. None of
these files are accessed by emulator Save/Load.

The UI has ten pages, each with five left-column and five right-column rows.
SAVEBACK/LOADBACK are translucent and must blend over a fresh scene frame;
SLCHIP provides the pink rows, page controls, shortcut buttons and Return.
SYSTEMGOTO supplies the footer. Empty Load rows have no action. Save returns
to its page after writing; successful Load leaves the screen. The supplied
original Load screenshot verified foreground geometry and prompted a nine-
pixel correction to our row text baseline. Reference and verification images
are retained in `work/save-load-qa/` and `work/save-load-re/`.

Both hosts expose 100 emulator-owned slots while preserving existing slot
filenames 1–10. NEW selects the newest numbered save, QLOAD restores the
quicksave, and unsupported AUTO/Config/Title/End actions stay disabled. The
native chapter line is represented by a saved dialogue excerpt. Overwriting
asks through a platform dialog; native confirmation graphics and settings
remain unresolved. The original save format is still unsupported.

Validation includes eight passing CTest suites, installed-asset rendering,
real-scene save/restore, page-10 and letterbox captures, Java/JNI compilation,
and both Android ARM ABIs. Android hardware behavior remains unverified.

## System menu reconstruction — 2026-10-13 (audit)

`dc4ui::SystemMenu` (`core/src/system_menu.cpp`) statically parses the original
menu scripts and implements all six pages: Title (`main.mes`), Extras
(`ex_main`), Config (`config.mes` `menu1`, seven pages), Gallery (`cg_asa`),
Music (`music.mes`), and Scene mode (`sce_asa`). Widget art follows the
99=normal / 1=hover / 2=pressed state convention from §4. Sliders map into a
`SystemConfig` struct the hosts apply to audio and the SceneRunner and persist
to `profile.dc4profile`. Gallery/Music/Scene unlock state comes from scene
variables 97/98 and is synced both ways via `sync_progress()` /
`mark_cg_seen()` / `mark_music_seen()` / `mark_scene_seen()`. Every page was
screenshot-verified against the installed game on 2026-10-13; see
`docs/RE-HANDOFF.md` §7 for the per-page evidence table.


## Backlog portrait, preview and authored lines — 2026-10-13

The user supplied matching original/emulator crops of the first 二乃 line in
`dc4_kyo20190204a2.mes` (text command at 8091). The original shows the portrait
and name together with a separate landscape scene preview. The emulator's
hover path incorrectly replaced the face with a 150×120 center crop.

The corrected row keeps the face at (20, top+18) and draws a 128×72 whole-scene
preview at (184, top+38) on scene-jump hover when thumbnails are enabled. Both
frontends now call `capture_log_row()`. The renderer refreshes the current scene
cache before thumbnail sampling, excludes message/transition overlays, and
does not advance VM or animation clocks. Android previously captured no preview.

The MES explicitly breaks after 良い and starts the next line with a fullwidth
space followed by んですか？. Removing that newline and rewrapping at 545px
incorrectly moved い to the second line. Japanese log text now preserves the
authored breaks/indentation and uses 36px line pitch; the supplied original
light-glyph bands begin about 35 screenshot pixels apart. The 720px fallback
width is the available row column, not a recovered native wrap constant. Latin
translation reflow retains its existing 545px width and 24px pitch.

`tests/log_screen_test.cpp` verifies face persistence, preview geometry/toggle/
scrolling, exact Japanese line breaks, Latin reflow, and fresh scene capture
including all four corners and excluding the message window. Nine CTest suites
pass. Both arm64-v8a and armeabi-v7a native libraries compile; no device runtime
claim is made. Fresh image: `work/backlog-fix/hover-row.png`.

## Scene Mode selection and replay return — 2026-10-13

The menu host incorrectly assigned a global `sceN` unlock ID directly to
bank99[100] and jumped to `sce_set`. The original `sceN` labels translate that
ID to a route-local selector first. For example, `sce_com.mes` offsets
47773..47790 translate `sce370` to selector 0, which launches
`DC4_SEN20181213B`; `sce_asa` happens to use matching global/local IDs.
The independent NAMES.DAT catalogue regression reproduced 386 wrong or empty
launches out of 469 entries, including Nino's first entry incorrectly opening
`DC4_NIN20190501C` instead of `DC4_MIA20190301A`.

Selection now executes the authored label. Both hosts use `initialize_scene()`
to seed progression and the replay flag bank9[7] (`scemode.mes` offset 12).
Chapter replay endings transfer to `SCE_END` (e.g. `dc4_mid20190301a1.mes`
offsets 7186..7286), which the scene runner now yields to the host as a menu
transition. `resume_scenes()` restores the selected route and page, including
after configuration visits. Fresh normal-story initialization clears the flag.

Evidence is local MES parsing and emulator execution, not a live original-game
comparison or REA Evidence record. `tests/system_menu_test.cpp` checks all 469
menu selections against NAMES.DAT, executes their replay VM paths to SCE_END
(48,668 dialogue commands; choices cycle to avoid intentional choice loops),
renders one startup from each of eight tabs, and runs one complete replay
through SceneRunner. Movie completion is simulated in this test. Captures:
`work/scene-mode-fix/sce*-replay.png`; before/after logs:
`work/scene-mode-before.txt`, `work/scene-mode-after.txt`.
All nine CTest suites pass with installed assets. Both Android native ABIs
compile; Android device behavior has not been exercised for this fix.

## Missing action confirmations — 2026-10-13

The shared ADVMENUCHIP dialog now gates new-game starts, Scene Mode replay,
movie playback, settings reset, saving empty slots, overwriting occupied slots,
loading slots and shortcuts, quick save/load, and backlog scene jumps. Existing
Title/Quit confirmations use the same typed prompts. System-menu actions are
deferred before mutation, so cancelling a replay preserves the selected page
and cancelling settings reset preserves the current values. Japanese, English,
and Indonesian messages are supplied by the engine; they are not claimed as
an exact transcription of every original dialog string.

SaveScreen now returns a slot action once after Yes. The desktop's duplicate
overwrite check previously reopened a dialog after the shared controller had
already accepted it; that second check is removed. Number-key slot selection
uses the same confirmation path as pointer input. F5/F9 and backlog jumps use
the host overlay, which now also renders above backlog/save overlays. Modals
capture input, handle Enter/Y, Escape/N and right-click on desktop, and pause
automatic story advancement. Android uses the shared art for quick actions
and backlog jumps, freezes their underlying frame, and cancels a pending
dialog on hardware Back or app suspension. Existing Android library/app
management dialogs remain platform dialogs.

The checkbox suppresses only the accepted action kind for the lifetime of its
controller (not across app restarts). No does not suppress future prompts.
Cancelled/reopened dialogs clear armed presses; invisible legacy button hit
rectangles were removed. Confirmation fallback rendering remains available
when Save/Load artwork fails to load.

Validation: ten passing CTest suites with installed assets; new
`confirm_dialog_test` covers every prompt kind, cancellation, drag/release,
suppression isolation, empty/missing saves, one-shot acceptance and modal
navigation. Menu tests verify deferred replay/reset actions and still check all
469 replay selections. Java and arm64-v8a/armeabi-v7a native builds pass.
Visual checks: `work/confirmation-fix/confirm-scene.png`, `desktop-en.png`,
`desktop-id-save.png`. Android device interaction was not tested in this pass.
