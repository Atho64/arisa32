# Save/Load screens — retained native evidence

Updated 2026-10-11. This investigation reads the original MES/CRX/EXE files,
retained decompilation, and a user-supplied original Load screenshot. It does
not execute the original Save/Load UI or
read/write the user's saves. The emulator continues to use its own `.dc4sav`
files; original physical IDs below are evidence, not an emulator storage rule.

## Evidence boundary and corrections

The exact layout below is observed in retained disassembly of `SAVE.mes` and
`LOAD.mes`. Native behavior is observed statically in retained IDA pseudocode,
not through original runtime interaction. Native names in old dumps are
analyst labels and can be misleading:

- `SaveSelect_Update` is **0x40B6F0**, in
  `work/menu/dec_40B6F0.json`. The earlier handoff's **0x437810** address is a
  different UI helper called before it.
- `sub_40C6A0` reads exactly **356324 / 0x56FE4 bytes from one slot file**.
  The earlier guess that `DC400.dat` contains all 100 slots is incorrect.
- The function labeled `Save_WriteSlot` at **0x40C770** actually opens a file
  read-only and copies its data into live state. The existing names should not
  be used to infer read/write direction.
- REA MCP was unavailable in this session; the parent performed the read-only
  registration diagnosis. No REA Evidence IDs were produced. Sources below
  therefore cite retained filenames, MES byte offsets, native addresses, and
  direct EXE byte locations.

`work/save-load-re/extract_evidence.py` reproducibly materializes the relevant
pseudocode and decodes the four original CRX resources into this new workspace
folder. It never writes to the game directory. `evidence.json` records hashes,
CRX headers, and native rectangle constants. EXE SHA-256:
`28ce64e5da0df7f512aee216fe083cf9465daa0f9ba00390cf8cd5087b2e9578`.

## Assets and complete screen layout

Coordinates are original 1280×720 logical pixels. Rectangles below use left,
top, right, bottom. MES widget art state **99=normal, 1=hover, 2=pressed**.
The literal crop sizes are independent of the hit area.

| Resource under `AdvData/GRP/SYSTEM` | Decoded size | Use |
|---|---:|---|
| `saveback.CRX` | 1280×720 | Complete Save background at (0,0) |
| `loadback.CRX` | 1280×720 | Complete Load background at (0,0) |
| `slchip.CRX` | 447×627 | Page buttons, slot backgrounds/outlines, top shortcuts, Return |
| `SYSTEMGOTO.CRX` | 530×132 | Save, Load, Config, Title, End bottom navigation |

SAVE byte offsets 261–336 load SLCHIP to sheet 19, SAVEBACK to sheet 20,
SYSTEMGOTO to sheet 23, then draw sheet 20 full screen. LOAD 323–398 substitutes
LOADBACK. These are `0x3C` scratch-sheet IDs, not the `0x3D` +37 sheet registry.
The CRX headers and independent decoded PNGs are retained in `work/save-load-re`.

The original background is a translucent navy overlay: LOADBACK pixel
(500,500) is RGBA (0,32,70,217). The supplied original screenshot confirms the
title scene remains visible beneath it. The renderer must composite over a
fresh scene frame on every draw; reusing the previous overlaid frame would
progressively darken it. The row rectangles are native-created;
they are not already painted into SAVEBACK/LOADBACK. No visible slot-number
label was found in the full metadata builder or shared list renderer.

## Pages and row selection

SAVE offsets 1647–1728 / LOAD 1709–1790 register these ten `0x7E` hit rectangles,
in this order:

| Visible row index | Rectangle |
|---:|---|
| 0 | (152,95,602,201) |
| 1 | (152,212,602,318) |
| 2 | (152,329,602,435) |
| 3 | (152,446,602,552) |
| 4 | (152,563,602,669) |
| 5 | (617,95,1067,201) |
| 6 | (617,212,1067,318) |
| 7 | (617,329,1067,435) |
| 8 | (617,446,1067,552) |
| 9 | (617,563,1067,669) |

Thus rows run **down the left column first, then down the right**. The page is
zero-based `var[99,0]`; entry copies it from `var[100,22]`, and `pageset` stores
it back there. There are ten pages, 0–9. Native row metadata ID is
`3 + page * 10 + visible_index` (0x40B7F3–0x40B7FA). The 100 ordinary physical
IDs are 3–102 (`DC403.dat` through `DC4102.dat`, `%02d` is minimum width).
The first three physical IDs are special shortcuts, not numbered rows.

SAVE 9944–10316 / LOAD 10318–10690 show previous/next wrapping 0↔9 and page 1–10
assigning values 0–9. The current page has a free selected-art blit instead of
an active page widget.

Page-button hit rectangles are `(802+43*p,20,845+43*p,60)` for `p=0..9`.
Destinations are `(802+43*p,20)`. SLCHIP source x values and literal widths:

| Page | Source x | Width |
|---:|---:|---:|
| 1 | 2 | 41 |
| 2 | 44 | 41 |
| 3 | 86 | 41 |
| 4 | 128 | 41 |
| 5 | 170 | 40 |
| 6 | 211 | 41 |
| 7 | 253 | 41 |
| 8 | 295 | 41 |
| 9 | 337 | 40 |
| 10 | 378 | 39 |

Normal art uses source y=206, height=40. Hover, pressed, and current-page art
use y=246, height=43 (the orange page row). SAVE 422–1540 / LOAD 484–1602.

Previous hit=(772,28,798,56): normal dst (772,28), src (2,294), size 25×30;
hover/pressed dst (773,28), src (28,294), size 24×30.
Next hit=(1234,28,1260,56): normal dst (1234,28), src (53,294), size 25×30;
hover/pressed same destination, src (78,294), size 24×30.

## Native row art and metadata

`SaveSelect_Update` creates mode 3 for Save and mode 4 for Load. Native
`CG_CheckOrCapture` 0x409E90 case 6 sets `(arg != 0)+3` and requests the rebuild.
SAVE 1737/1740 runs `1E 20,0` then `1E 6,0`; LOAD 1799/1802 uses `1E 6,1`.

The normal/hover/pressed background source rectangles are loaded respectively
from **0x4AE44C**, **0x4AE45C**, **0x4AE46C**, with destination local (0,0).
All three initial EXE values are **(2,21,449,126)**: pink art in all states.
Direct file offsets are 0xACA4C, 0xACA5C, 0xACA6C. These right coordinates exceed
the decoded sheet width 447 by 2 px; clipping is appropriate. No evidence here
establishes a blue-row hover state, despite blue art existing lower in SLCHIP.
Runtime changes to these mutable globals were not observed.

| Row component | Destination relative to row origin | Source / content |
|---|---|---|
| Thumbnail outline, normal | (14,14) | SLCHIP (16,127,145,202) |
| Thumbnail outline, active | (14,14) | SLCHIP (148,127,277,202) |
| Saved screenshot | (16,16) | Screenshot (0,0,125,71) |
| Timestamp | (160,12) | `YYYY MM/DD HH:MM:SS` |
| Chapter | (160,50) | Script-name title lookup, or raw script name |

The screenshot capture writer 0x4218E0 requests a **128×72 PNG**. The row
builder draws the top-left 125×71 crop, identically in all three states.
Native calls use text-local (80,6)/(80,25); `sub_436280` doubles those
coordinates at 0x436956/0x436A33/0x4369BD. They are not an 80 px indent.

Empty timestamp is exactly `---- --/-- --:--:--`; empty chapter is `No Data`.
Both are prefixed `@s` in the native text stream. Occupied timestamp/chapter
use `@i`. Retained `work/native-font.c` function 0x43E6C0 maps `@s` to 20 px
(0x43EF57), `@i` to 18 px (0x43EF6D), `@y` to 16 px, and `@r` to 12 px. The
chapter name changes to 16 px above 30 SJIS bytes including its prefix, then
12 px above 34. Native text color argument 135 selects palette index 7 plus a
style flag. The user-supplied screenshot shows timestamp ink near native
y=119 and No Data ink near y=158 in the first row. The core agent's normalized
comparison found the initial reimplementation 9 px too high and corrected
the baseline. That screenshot establishes practical empty-row placement;
complete occupied-font pixel equivalence remains unverified.

Behavior is materially different between modes:

- **Save, empty or occupied:** assigns action tag `selsave`, active pink row
  and active thumbnail outline. Empty uses the placeholder strings.
- **Load, occupied:** same `selsave` action and active outline.
- **Load, empty/unreadable/truncated:** no `selsave` tag, placeholders, and
  normal background/outline in every state. It cannot start a Load action.

Sources: 0x40B80A–0x40BD01 (Load), 0x40BD16–0x40C113 (Save),
`work/menu/dec_40B6F0.json`; `dec_0x436280.json` for placement.

Metadata reader 0x40C6A0 zeroes the buffer, opens `%s\\DC4%02d.dat` read-only,
requires exactly 0x56FE4 bytes, and returns -1 otherwise. Its buffer contains
the script name at +8. Timestamp byte offsets in that returned buffer are:
month +0x1873A, day +0x1873B, hour +0x1873C, minute +0x1873D,
year-minus-2000 +0x1873E, seconds +0x1873F.
These derive from the stack-local offsets in 0x40B6F0 and are
not a promise of emulator/original format compatibility. Chapter lookup is
case-insensitive against pairs at 0x4B34E8/0x4B34EC; the retained list is
`work/menu/chapter_table.txt`.

## Return, shortcuts, navigation, confirmation

Return hit=(1160,628,1286,684), clipped to the 1280 px viewport. Draw at (1160,628),
size 102×38; normal source (0,431), hover/pressed (103,431). Script uses `17 255,200`,
yet the Return control remains active: that parameter alone does not mean
disabled.

Top shortcuts draw at x=348, 476, 604, all y=12. SAVE paints only disabled crops
at (0,576), (120,576), (240,576), size 119×51. LOAD checks the metadata of the special
physical slot first, then either paints that disabled crop or registers:

| Destination / hit size | Script action | Physical ID | Normal source / size | Hover+pressed source |
|---|---|---:|---|---|
| (348,12), 119×51 | `new` | 1 | (2,325), 119×53 | (2,379) |
| (476,12), 119×51 | `auto` | 0 | (130,325), 119×53 | (130,379) |
| (604,12), 119×51 | `ql` | 2 | (258,325), 120×53 | (258,379) |

**Naming inconsistency:** the left resource reads AUTO and the middle reads
NEW; their MES tags are `new` and `auto`, respectively. Do not infer behavior
from tag names alone. Save success explicitly also writes physical 1. The
emulator's "latest numbered save" shortcut and absent autosave are intentional
host choices; they are not a recovered original storage implementation.

Bottom SYSTEMGOTO controls use hit and draw origins x=724, 833, 942, 1051, 1160;
y=686..719, size 106×33. Source x=0, 106, 212, 318, 424 selects Save, Load,
Config, Title, End. Rows y=0, 33, 66, 99 are normal, hover, pressed/selected,
disabled. Current mode is
painted selected. Script state can disable navigation depending on title/ADV
context. Unsupported emulator navigation must remain disabled rather than
pretend to perform that native action.

Ordinary Save selection runs the character's SYSTEM_*012 voice and requests
a confirmation dialog at (422,248), type 7 (`SAVE` 10918–10948). Accepted callback
runs `18 5,0`, `18 4,0`, sets `var[99,254]=1`, runs `18 4,0`, then jumps to
`menu`. **Successful Save remains on the Save page**, updating both the selected
and special 1 save. Cancel restores selection bookkeeping and returns to `menu`.

Load ordinary selection uses SYSTEM_*013 and the same dialog origin, type 8
(`LOAD` 11508–11538). Special shortcuts also request type 8. Accepted callback
checks `1E 19,0` again; failure goes to `noload` and returns to `menu`, success
performs a black/mask transition then `18 0,0`, the state load path. Cancel
stays in the Load screen. No implementation should proceed on missing data.

The dialog request is not proof a dialog always appears: native opcode 0x59
at 0x42E9B3 checks a per-dialog system flag and may directly invoke the callback.
The retained code identifies sub_418960 as its dialog construction path;
this pass does not recover its complete dialog chip geometry or configured
default. Native confirmation asset/pixel matching remains open.

## Reimplementation boundary and remaining unknowns

Answered statically: four screen resources, page/row/nav/return geometry,
page count and wrap, original physical-slot mapping, empty-row interactivity,
row and thumbnail crops, text strings/sizes/placement, metadata and screenshot
entry points, and Save/Load continuation behavior.

Unresolved: full native font/color equivalence, mutable row-constant
runtime changes, full confirmation art/default setting, special slot 0 capture
policy, whether and where a latest-save NEW badge is used outside this builder.
The user supplied an original empty Load screenshot during this pass. Its
client area is approximately 1266×713 inside a titlebar window, so comparison
normalizes that client area to 1280×720. It confirms the empty two-column Load
layout, pink rows, disabled AUTO/QLOAD, active NEW, and translucent background.
No original UI automation was performed. Other states remain statically
established rather than screenshot-verified. The supplied reference is retained
as `work/save-load-re/user-original-load.png`.

The shared `SaveScreen` controller accepts 100 emulator slot records and
returns zero-based actions. Desktop storage adds 1 to preserve its existing
slot filenames. The desktop supplies its own timestamps, excerpts and
128×72 thumbnails. Excerpts replace the native chapter lookup by design.
The current desktop host uses an SDL confirmation before overwriting an
occupied numbered slot; new saves and loads run immediately. Native Save/Load
confirmation art and the original configurable dialog policy are not yet
reproduced.

`tests/save_screen_test.cpp` covers controller transitions, distinct physical
controls sharing an action, disabled Load rows, all 100 slot indices, native
crop geometry via synthetic CRX art, invalid metadata, and failed asset loads.
The no-argument suite needs no installed game. Passing the original AdvData
path runs additional read-only real-asset Save/Load render checks; this passed
on 2026-10-11. Test metadata and QA previews are fabricated, not original saves.
