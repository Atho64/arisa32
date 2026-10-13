# D.C.4 engine reimplementation — RE handoff document

This file is the single entry point for handing the project to another AI (or a
new engineer). It summarizes **everything reverse-engineered so far**, what is
implemented, how to build/test, and what remains. Deep details live in the
companion docs (`findings.md`, `opcode_table.md`, `vm-progress.md`,
`audio-progress.md`, `re-save-load.md`).

---

## 1. What this project is

`dc4engine` is a C++17 reimplementation of the CIRCUS adv32 engine powering
**D.C.4 ~ダーカー4~** (PC, DC4.EXE). It reads the *installed game's* assets
(MES scripts, GRP images, MUSIC, VOICE.DAT) from the original installation and
re-renders the game with its own engine. It never modifies game files and never
touches the original game's SAVE/LOAD menus (protects the user's real saves —
standing rule).

- Desktop frontend: `sdl_desktop/` (SDL2, 1280×720 logical).
- Android frontend: `android/` (JNI bridge + Java shell; see `android/README.md`).
- Core engine: `core/` (VM, scene renderer, audio, formats).
- Working dir conventions: RE artifacts in `work/`, disassemblies in
  `work/menu/*.txt`, IDA dumps in `.ida-mcp/`.

Original game data assumed at `F:\DC4 new\DC4\AdvData` (the desktop shell
takes the data dir as argv[1], or `DC4_ADVDATA` env for the .bat helpers).
IDA database: `F:\DC4 new\DC4\DC4.EXE.i64` (open in IDA with the MCP plugin).

## 2. Build / run / test

```
build.bat                     # MSVC x64 env + cmake + ninja, builds everything
build\desktop\dc4_desktop.exe "F:\DC4 new\DC4\AdvData" [--scene X --line N]
    --shot out.bmp            # headless single-frame screenshot
    --panel save|load|backlog|title|extras|config|gallery|music|scenes|movies
    --line N                  # advance to dialogue index N (0-based)
    --reveal N --effect-ms N  # partial text reveal / settled effect time
    --hover X,Y --down        # pointer state for hover/press art
    --debug                   # dump chips/sprites as they execute
build\desktop\mesdis.exe FILE.mes          # script disassembler
python tools\proto\crx_decode.py A.CRX     # CRX -> PNG
ctest in build\desktop (9 suites: mesvm, audio, scene, save_screen, save_host,
                        resource, media, system_menu, log_screen)
```

Desktop controls: click/space advance, right-click hide text, wheel/B backlog,
F5/F9 quick save/load, SAVE/LOAD buttons → 100 slots across ten pages, A auto,
S skip, V voice replay, Esc quit. Android: full touch shell (auto/skip/backlog/
save/load with picker) — see `android/README.md`.

Note: mesdis (a native Windows binary) needs a native path — MSYS `/f/...`
paths fail; use `F:\DC4 new\...`.

## 3. Formats (all RE'd, see findings.md for byte layouts)

| Ext | Meaning | Decoder |
|-----|---------|---------|
| .mes | VM bytecode scripts, version 0x2400AAB6 (D.C.4) | `core/src/mesfile.cpp`, `messtream.cpp` |
| .crx | images (32bpp BGRA, comp=2 RLE-ish, flags=0x1 flip alpha) | `core/src/crx.cpp`; `tools/proto/crx_decode.py` |
| .crm/.bst | character sprite archives (BST refs resolve into CRMs) | `core/src/crm.cpp` |
| .xpcm / VOICE.DAT | audio: XPCM blobs; VOICE.DAT = TOC + streamed blobs | `core/src/xpcm.cpp`, `voice.cpp` (Voice_LoadTOC 0x452590, lookup strips dir + `.pcm`, case-insensitive) |
| .dc4sav | our own emulator saves (v1-v4, Writer/Reader in `scene_state.cpp`) — **never** compatible with the original's | `core/src/scene_state.cpp` |

MES text is SJIS with a custom escape encoding (TEXT commands, `sjis_to_utf16`
in scene.cpp); in-game rendered text = white glyphs + 1px black dilation ring
(see Text rendering below).

## 4. VM (MesVM) — opcode semantics

Authoritative operand signatures: `docs/opcode_table.md` (from fetcher
sub_438790). Dispatcher: `MesVM_ExecuteOpcode` sub_428640, jumptable at
0x428DBC, **default case = no-op** (0x430EDA). The full VM disassembly is
dumped at `work/menu/vm_dis.txt` (9696 instructions).

Implemented in `core/src/mesvm.cpp`: labels 0x4B, jumps 0x54, transfers
0x4F/0x50 (+0x51 entry label), choices 0x09/0x0A/0x5C, checkpoint 0x1C,
skip scopes 0x20, variables 0x82/0x83/0x84(sum)/0x85/0x86(compare+0x02
companion). Key variable banks: `var[bank][idx]`; bank 9/99/100 etc. carry
game state (9,7 = "in ADV scene", 99,x = config values, 3,0 = temp).

Everything else (drawing/audio/input ops) is *host-owned*: the VM yields the
command and `SceneRunner::advance_to_next_text` (scene.cpp) interprets it.

### Menu/widget command subset (used by all system menus)

Recovered from MESMENU/gotitle/main scripts and verified in-game; implemented
in `sdl_desktop/ui_script.cpp` (`dc4ui::ScriptUi`) and the scene chip path:

- `0x3C slot "SYSTEM\NAME"` — decode image into sheet slot; **0x3D registers
  UI sheets at index slot+37** (MesVM case 61), which is what 0x77's sheet id
  then refers to (start.mes registers MESWIN=37, ADVMENUCHIP=38, RIGHTCHIP=39).
- `0x49 "SYSTEM\BG"` — commit background (slot 1 was prepared by 0x3C).
- `0x72 x1,y1,x2,y2` — mouse hit rect for a widget.
- `0x54 "tag"` — in a widget block: the action tag; in script flow: jump to
  the 0x4B label of that name. Same opcode, context decides.
- `0x73 dst_x,dst_y,src_x,src_y` — chip blit position + source offset.
- `0x77 w,h,sheet,state` — companion of 0x7A/0x73: size, sheet id, and the
  art state: **99 = normal, 1 = hover, 2 = pressed**.
- `0x17 a,b` — widget animation/opacity params (native sub_412D40; the
  standard pair is 201,200; 255,200 for disabled-looking items). The following
  `0x91 65535,1,65535,1` block is consumed alongside it (input-mask params).
- `0x7A x,y,sx,sy` + `0x77 w,h,sheet,state` — free (non-widget) chip blit.
- Widget art pattern (3 states): `0x73 dst,sx,sy / 0x77 w,h,sheet,99` then
  the same pair with state 1 and 2 (hover/pressed crops usually adjacent).

### Event-loop ops (menus use them; NOT yet emulated — host drives instead)

- `0x1A n,0` — frame yield / event poll variants (n selects sub-mode).
- `0x24 n,0` — UI event pump (native 0x42B547).
- `0x1E n,x` — n=18: CG gallery capture (CG_CheckOrCapture); also triggers
  the save-select UI helper (0x437810) and metadata update (0x40B6F0) when a
  save-select screen is open. The old SaveSelect_Update address was incorrect.
- `0x04 n,0` — section marker used between menu page setups.
- Menu scripts are endless event loops (`4B "menu1"` … conditional jumps back);
  the native engine re-runs the draw section every frame, redrawing widget
  states from variables. Our approach: parse the layout statically (exact
  positions come from the script), drive interaction from the shell, and map
  widget tags to shell actions.

## 5. Scene rendering (SceneRunner, core/src/scene.cpp)

- 1280×720 RGBA CPU frame; layer commit model: 0x3C loads slot 1 background →
  0x49 commits it (releases old characters, clears date HUD chips).
- Character sprites: slots 4-8 = sprite indexes 0-4 (0x3D loads BST);
  **slots 19-23 are preload scratch and are never drawn** (phantom-sprite bug
  fixed). 0x8B transform (mode = last word; 0 = position (w0,w1) on layer
  10·(w2+1); 0x10|n scale/anchor; 0x80|n hide), 0x8E slide motion,
  0x8C effect-layer fade (dismissal of BST\CUT cut-ins; alpha 255→0 at
  510/(w1·12) per 60Hz frame; mode&0x80 → fade, else instant) via sub_447B00.
- Transitions: 0x0F(32) crossfade (snapshot before), 0x0F(21,x) MSK wipe masks
  (slot 21, `MSK\*.CRX`, never rendered as content), 0xB4 commit trigger,
  0x36/0x37 mark transition frames.
- Camera: 0x99 duration, 0x9D apply (sub_425530 family).
- Date HUD chips (0x7A/0x77) render on eyecatches and clear on bg commit.
- COMU phone chat (opcode 0xAB + 0x6D messages): fully implemented — see the
  COMU section in findings.md (layout constants, name table, stamp grid,
  per-message text style white+black 1px stroke).

## 6. Log screen (backlog) — ORIGINAL LOOK, DONE ✅

Native code RE'd (all addresses verified in IDA):
- `Backlog_InitGraphics` 0x405CE0 — screen setup:
  - copies the log text store from `Buf+0x4B80`;
  - BKLOGBACK drawn fullscreen (0,0,1282,722); the "Log" title, header rule,
    left rule, scrollbar track and arrows are **baked into the background**;
  - BKLOGCHIP supplies dynamic pieces; scroll thumb art crop (1200,38)-(1224,68)
    drawn at x=1232, `y = 82 + 582·(current/total)`; scrollbar hidden ≤4 rows;
- row renderer `sub_404EE0` 0x404EE0:
  - rows start at y=82, **pitch 150**, newest last (screen opens scrolled to
    the bottom);
  - row bar: chip crop **(0,220)-(1203,343)** at **(20, top+14)** (1203×123);
  - speaker face: grid cell `id` at crop `(151·(id%8), 121·(id/8)+370)` size
    150×120 at **(20, top+18)**; **the pink name plate is baked into each cell**;
  - speaker→id resolver `sub_401240`: global table `SubStr` 0x4AE008 (32-byte
    SJIS entries, runtime-filled); empty name = -1 (narration, no face);
    names not in the table → dynamic text at `x = 48−4·bytes` (or
    `75−3·bytes` if ≥20 bytes);
  - line text: x=480, first line top = top+22 (glyphs land ~top+31), 36px
    line pitch, white + dark outline (same style as the message window);
  - シーンジャンプ button: region (328,top+28)-(488,top+62), art crops
    (362,52)-(488,87) normal / (492,52)-(618,87) hover+pressed;
  - ボイス再生 button (only rows with voice): region (328,top+68)-(488,top+101),
    art (362,12)-(488,46) / (492,12)-(618,46);
- widgets `sub_4010F0` 0x4010F0:
  - サムネイル checkbox: region (327,44)-(356,74), box art (0,6)-(29,37)
    (hover 31,6), label art (76,0)-(310,38) at (356,38), check art
    (310,0)-(341,32) when enabled (global Buf[40]);
  - 戻る button: region (1100,672)-(1216,712), art (640,8)-(744,47)
    (hover 640,47)-(744,89);
- face grid order (BKLOGCHIP cells 0-28, names verified off the art):
  一登 有里栖 有里咲 二乃 諳子 ひより 詩名 末羽 ちょ子 CHOCO 杉並 叶方 元 泉
  KotoRI 琴里 アリス ちょえし 梓 ララ リリ ルル 紗希 尚紀 憂次 十和子 三美
  正佳 可帆 (29 = blank face, 30-31 empty).

Implementation: `core/include/dc4/log_screen.h`, `core/src/log_screen.cpp`
(`dc4ui::LogScreen`, shared by desktop and Android), rows in
`Backlog` (main.cpp) now carry speaker + full text + voice entry + dialogue
index. Interactions: wheel/keys/scrollbar-drag scroll, 戻る/right-click/Esc
close, checkbox toggles thumbnails, voice button replays the row's voice
(`play_voice` → VOICE.DAT), **scene jump rebuilds the opening scene settled at
the row's dialogue index** (shared `build_scene` lambda in main.cpp, same
mechanism as comu.bat `--line` jumps). Verified against the original
screenshot: `work/menu/compare-row.png` (positions match ≤3px).

2026-10-13 backlog correction, checked against the user's original 二乃 row:
the speaker portrait/name stays visible on scene-jump hover. A separate 128×72
scene preview appears at (184, top+38); it must never replace the 150×120 face.
`capture_log_row()` now serves both shells, retaining authored Japanese line
breaks and capturing a fresh, downsampled whole-scene image before transition
and message overlays. English/Indonesian text still reflows. Japanese lines
use 36px spacing and only wrap at the row boundary when unusually long.
Scene jumps now restore retained VM/scene snapshots (the rebuild description
above describes the earlier implementation). Verification: nine CTest suites
pass, including new synthetic log geometry/text/capture regressions; both
Android ABIs compile. Current reference render: `work/backlog-fix/hover-row.png`.

### Save/Load screen — implemented 2026-10-11

`core/include/dc4/save_screen.h` and `core/src/save_screen.cpp` implement
`dc4ui::SaveScreen`, used by both SDL and Android/JNI. Exact source rectangles,
native addresses, and evidence limits are in `docs/re-save-load.md`.

- Original SAVEBACK/LOADBACK, SLCHIP and SYSTEMGOTO art; backgrounds blend over
  a freshly drawn scene with the message window hidden. Hosts must reset the
  input framebuffer before every draw to avoid repeated dimming.
- Ten pages of ten slots, column-major (five left, five right). The controller
  returns zero-based indices 0–99; both hosts store `slot1.dc4sav` through
  `slot100.dc4sav`, preserving the original emulator slots 1–10.
- Empty Load rows are inactive. Press and release must hit the same physical
  control; cancellation, mode/page changes and focus loss clear pending input.
- Save stays open after writing; Load closes after successful restoration.
  Occupied writes ask through the platform dialog. New writes and loads are
  immediate; native confirmation artwork/settings are not reproduced.
- NEW loads the most recent numbered emulator save; QLOAD uses its quicksave.
  AUTO and Config/Title/End are disabled until those features exist. Metadata
  uses emulator timestamps, thumbnails and excerpts instead of native chapter
  records. The original save format is still unsupported.
- Desktop keyboard: 1–9/0 selects within the page, Left/Right or Page Up/Down
  changes page, Tab switches mode, Esc/right-click returns. Screenshot option
  `--panel load --save-page 10` selects a page without reading original saves.

Validation: all five CTest suites pass, the optional installed-asset screen
test and real-scene snapshot round trip pass, and native-resolution/page-10/
letterboxed screenshots were inspected. The user-supplied original Load
capture confirmed layout and revealed the corrected nine-pixel text baseline
offset (`work/save-load-qa/original-load-reference.png`). Android Java and
both ARM64/ARMv7 libraries compile; the updated APK is signed and alignment
checked. Device touch/dialog/lifecycle behavior remains untested.

## 7. System menus — ALL IMPLEMENTED ✅ (audited from code, 2026-10-13)

`core/src/system_menu.cpp` (698 lines, `dc4ui::SystemMenu`) implements all six
pages as **static layout parsers** over the original MES scripts. Screenshot
verification (2026-10-13, `work/audit-test/`): every page renders real art —
title 8,944 unique colors, config 3,040, gallery 5,187, music 3,780,
scenes 4,636, extras 8,590. All 8 CTest suites pass.

| Page | Script(s) | State | Evidence |
|--------|-----------|-------|----------|
| Title / main menu | `main.mes` (`run("main","main")`) | **DONE** — logo, 7 buttons (PROLOGUE/START/CONTINUE/LOAD/EXTRA/CONFIG/END), 5-character art, copyright; CONTINUE greyed when no save |
| Extras | `main.mes` label `ex_main` | **DONE** — renders (8,590 colors) |
| Config | `config.mes` label `menu1` (7 pages via `--config-page`/var 100,54) | **DONE** — 4 tabs (システム/テキスト/サウンド/個別ボイス), sliders (effect speed, music seek), toggles, right-click mode, cursor style |
| Gallery (CG mode) | `cg_asa.mes` (+ `cg_cmn/hiy/miu/nin/sii/sor/tiy`) | **DONE** — variant browsing via wheel, CG-unlock flags (var 97 ← seen_cg, persisted in profile) |
| Music | `music.mes` label `menu` (+ `qmovie` variant) | **DONE** — 42-track list from NAMES.DAT, scroll, play/stop/seek, unlock from seen_music (var 96) |
| Scene mode | `sce_*.mes` | **FIXED 2026-10-13** — global unlock IDs translate through authored route-local selectors; replay startup sets bank9[7], SCE_END returns to the selected tab/page. All 469 selections and replay VM exits tested; see findings.md. |
| Window-size | `win_size.mes` | DONE (load_ui_script) |

Config wiring is complete end-to-end: sliders/toggles map to a `SystemConfig`
struct (volume/pan/text speed/auto wait/effects/window opacity/51 per-character
volumes) that `apply_settings()` in main.cpp pushes into the audio engine and
SceneRunner, and persists to `profile.dc4profile` in the emulator save dir.

Progress sync: `sync_progress()` copies vars 97/98 (CG/scene seen flags) and
100,100..149 from the live scene into the menu, persisted across sessions;
`mark_scene_seen()` records each visited scene. Gallery unlock = var 97 > 0.

Bottom navigation: SYSTEMGOTO controls (Save/Load/Config/Title/End at
x=724,833,942,1051,1160, y=686..719) are wired: Save/Load → `open_slots`,
Config → Config page, Title → confirm dialog then Title page, Quit → confirm.
Disabled targets (AUTO, unimplemented End navigation) stay disabled by design.

The right-click ADV menu (`advmenu.mes`) is NOT a parsed script screen — its
widget tags are wired directly in the shells, and the confirm dialog uses
native ADVMENUCHIP art (`core/src/confirm_dialog.cpp`, 431×223 frame).

Confirmation follow-up (2026-10-13): scene replay/new game/movie/reset,
save/load and quick actions, and backlog jumps now wait for Yes. Cancelling
preserves the underlying screen; accepted overwrite actions execute once.
Shared typed prompts support Japanese/English/Indonesian and per-action,
session-only checkbox suppression. Desktop keyboard paths and Android touch/
hardware Back use the modal gate. Ten CTest suites pass; see the confirmation
section in `findings.md` for coverage and runtime limits.

## 8. Text rendering

`FontRenderer` (core/include/dc4/scene.h): one face, `rasterize_line` renders
white glyphs + dark 1px outline (dilation ring; blend order: black pass, then
white pass) into a coverage buffer. Message window = 22px dialogue / 18px
speaker name. COMU chat text: white fill + 1px black ring, ring color 48
(final iteration agreed with the user; don't sample colors from
dark-pixel-filtered screenshots — it only ever sees the stroke).

## 9. Tooling

- **IDA MCP**: `python tools/proto/mcp_client.py call <tool> '<json>'` (server
  http://127.0.0.1:13337/mcp). Addresses are hex STRINGS ("0x405CE0");
  long outputs truncated → curl the `/output/<id>.json` URL it prints.
  Useful tools: decompile, disasm, xrefs_to, lookup_funcs, search_text.
- Full VM disasm: `work/menu/vm_dis.txt`; menu script disasms: `work/menu/*.txt`.
- PE parsing of DC4.EXE for .rdata strings (COMU name table at 0x492850 was
  read this way; see findings.md).

## 10. Key native addresses (quick reference)

| Addr | Meaning |
|------|---------|
| 0x428640 | MesVM_ExecuteOpcode (jumptable 0x428DBC; default = nop) |
| 0x438790 | command fetcher (operand signatures) |
| 0x42D215 / 0x42D6CF / 0x42DA46 | 0x8B transform / 0x8C effect fade / 0x8E slide |
| 0x447B00 | effect-layer fade impl |
| 0x430710 | 0xAB COMU manager |
| 0x458940 / 0x4587A0 / 0x458370 / 0x458CC0 | COMU ctor / stamp slot / parse / render |
| 0x492850 | COMU contact name table (SJIS, .rdata) |
| 0x405CE0 / 0x404EE0 / 0x4010F0 / 0x401240 | Log init / row draw / widgets / name→face-id (SubStr 0x4AE008) |
| 0x40B6F0 / 0x436280 | SaveSelect_Update (metadata/widget builder) / row text renderer |
| 0x437810 | Save-select UI helper, distinct from metadata builder |
| 0x444240 / 0x444260 / 0x444340 | layer visible-flag / position / rect(src crop) |
| 0x4440C0 | Gfx_SetLayerParam (rect defaults to full image when unset) |
| 0x428E8B | 0x1E handler: CG_CheckOrCapture + SaveSelect_Update |
| 0x4B319C / 0x4B31A0 | default 0x17 widget params (201,200) |

## 11. Conventions & constraints

- **Never** operate the original game's SAVE/LOAD menus or write into its
  savedata (protects the user's real saves). Emulator saves live in their own
  directory (DC4_SAVE_DIR / app files dir on Android).
- The user commits explicitly ("ok commit") — do not commit unasked.
- PC control allowed only for DC4-related windows.
- Compare renders against original screenshots; the user supplies them (or
  crops from the game). Log-screen reference: the 2026-10-11 backlog
  screenshot; our verification renders: `work/menu/nb25.png`, `nb26.png`,
  `compare-row.png`.
- After each completed screen: update this handoff + findings.md, then move to
  the next screen (easiest first, subagents welcome for parallel RE).

## 12. Known gaps / next steps

**Audited from code + live execution, 2026-10-13.** The previous list (config /
title / gallery / music / scene modes) is obsolete — all six system-menu pages
are implemented and screenshot-verified (§7). Remaining real gaps:

1. **Opcode coverage gap (measured).** The 512 shipped MES scripts use 128
   distinct opcodes (762,261 instances). Direct VM cases: 18 (`mesvm.cpp`).
   Host-interpreted ops add 27 (`scene.cpp`) and 35 (`system_menu.cpp`).
   Overall ≈60.9% of opcode *instances* have an explicit code path; 76 opcodes
   (~39% of instances) fall through to the default host-owner path — the
   highest-frequency are `0xAE`×56,280 + `0x47`×56,280 (a paired opcode,
   `sub_444F50`/`sub_40E5E0`, voice-variant anchors per vm-progress.md — likely
   benign), `0x03`×49,529 (label-addr helper), `0x06`×49,226,
   `0x3F`×25,936 (Sprite_BstPath2), `0x4D`×16,024 (`sub_4224C0`),
   `0xB0`×14,008. Scenes and menus render correctly with these skipped, but a
   visual audit of complex scenes (effects-heavy transitions, sprite batches)
   is the next fidelity check. Opcode usage stats reproducible via
   `build/desktop/mesdis.exe` over `AdvData/MES/*.mes`.
2. **Save/Load fidelity follow-up**: native confirmation art (currently the
   platform SDL dialog / ADVMENUCHIP-style art in confirm_dialog.cpp), chapter
   labels (emulator uses dialogue excerpts), autosave policy, and real Android
   device testing (APK builds and signs; device touch/lifecycle untested).
3. **Original-save compatibility**: still unimplemented by design — emulator
   saves are `.dc4sav` only.
4. **Menu event-loop ops** (0x1A/0x24) are bypassed by design (static layout
   parsing). A script-variable evaluator would only be needed if a screen
   requires script-driven dynamic redraw (0x86/0x02/0x20 already understood).
5. **0x8C exact mask-wipe animation** — deferred ("for later"); masks load but
   wipes are hard cuts.
6. **Credits (MED/PCK)**: `scene.cpp` has credits scaffolding (credits_pages_,
   credits_archive_, per re-media.md) — verify end-to-end against ed_*.mes.
7. **0x21** (commit-effect selector) has no explicit handler; falls through to
   the host default — verify against crossfade-heavy scenes.

Housekeeping: 120 files modified/untracked in git as of 2026-10-13 (includes 3
new Android sources: FolderImporter.java, GameLibrary.java, SaveArchive.java).
Also note many scratch files at repo root (`scratch_*.py/png`, `test_*.bmp`) —
candidates for a cleanup pass.
