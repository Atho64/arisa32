# MES VM recovery, 2026-10-10

The scene player now has a control-flow interpreter in `MesVm`, rather than
reading both sides of every condition and choice in file order. This is a
D.C.4 dialect implementation; compatibility with other adv32 games is not yet
established.

## Evidence identity and boundary

The user-supplied IDA MCP endpoint was `http://127.0.0.1:13337/mcp`. Its live
`server_health` reported module `DC4.EXE`, image base `0x400000`, ready Hex-Rays,
and database `DC4.EXE.i64`. No binary patches, renames, or saved
database changes were requested or made in this pass.

Full direct-MCP decompilations, including address markers, are retained as
`work/ida-20261010-vm-0xADDRESS.json`. These are static observations, not an
execution trace of the original application. The large dispatcher is also
available in the earlier `work/pseudo_dispatcher.c` and
`work/disasm_dispatcher.txt`; its decompiler warns about a bad stack value,
so small helper functions and the actual scripts were used to corroborate
the implemented behaviors.

The current `AdvData/MES` directory has **512** `.mes` files, of which **469**
have `dc4_` chapter names. Older findings referring to 330 files describe an
earlier corpus.

## Recovered behavior

| Behavior | Static evidence | Implemented result |
| --- | --- | --- |
| Signed word operands | `0x438400`, instruction at `0x438413` | VM arithmetic and comparisons sign-extend 16-bit immediates; `0xFFFF` means `-1`. Graphics commands retain their raw words. |
| Named label | `0x438420`, comparisons at `0x43844B`, `0x43847D` | **`0x4B` declares a label**. Search starts at code offset 3, uses case-sensitive comparison, and takes the first duplicate. |
| Named jump | dispatcher `0x54`; `0x41FC90`, `0x41FCC3` | **`0x54` jumps to a `0x4B` label**. Earlier docs describing `0x4B` as a goto were incorrect. |
| Compare and scoped branch | dispatcher `0x86` at `0x42AFF2`; `0x4385A0` | Read left variable, compare against signed immediate or packed variable reference. Operators 0..5 are `==`, `!=`, `>`, `<`, `>=`, `<=`; bit `0x80` selects an indirect right operand. Following `0x02` supplies the scope ID. |
| False branch / else | `0x4385A0`, `0x4385BB`..`0x4385D0`; dispatcher `0x20` | False comparison skips through the next `0x20` with matching **p2 scope ID**, irrespective of nesting or p1. Reached `0x20` with p1=0 performs the same skip; p1!=0 is a marker. |
| Variable read | `0x40CCD0` | Byte banks 0..10 contain 100 variables each. Banks 96/97/98/100 hold 400/1200/650/150 bytes. Bank 93 holds 20 signed dwords; bank 99 is mapped engine state. |
| Variable write | `0x40D2C0` (misnamed `VM_Random` in the saved IDA database) | Byte writes truncate modulo 256. Supported system state preserves dwords, selected audio sentinels map 255 to -1, and bank99/index217 aliases the selected bank93 entry. |
| Assignment | dispatcher `0x82`; `0x40EB30`, `0x40EB60` | Operand3=0 assigns immediate; 1 adds; 2 subtracts. |
| Copy / sums | dispatcher `0x83`..`0x85`; `0x40EB90` | Copy another bank/index, sum a following list ending at `0x02`, or add another variable. |
| Checkpoint companion | dispatcher `0x1C` at `0x42EF92`; `0x41DEA0` | Consume the following string command as checkpoint metadata. It must not execute as a jump during normal playback. Scene checkpoint restoration remains unimplemented. |
| Choices | dispatcher `0x09`, `0x5C` at `0x429881`, `0x0A` at `0x4299CC`; `0x414BD0` | Reset choices, pair each target string with the following encoded caption, then wait for selection at `0x0A` p1=0. Selection jumps to that target label. Captions are not emitted as dialogue. |
| Script transfer | dispatcher `0x4F`/`0x50`; `Mes_RunScriptByName` at `0x4260E0`, reset PC at `0x426150` | Yield transfer to host once and suspend until the next file is bound. Persistent variables survive. `0x51` selects an optional entry label in the destination. |

All **469 checkpoint commands** in the current corpus are followed by `0x54`;
all **74 choice-definition commands** are followed by `0x6C`. All observed
comparison modes (0, 1, 2, 3, 4, 130) are covered. These are bounded static
corpus observations, not claims about every adv32 dialect.

## Verification

`tests/mesvm_test.cpp` tests byte arithmetic, signed immediates, all six
comparisons and indirect operands, matching scope IDs, duplicate and
case-sensitive labels, checkpoint companions, both choice branches and their
join, repeated choices, persistent variables and entry labels across script
transfers, malformed streams, missing scope markers, and pure jump loops.

The normal CTest target uses synthetic fixtures, so it needs no game assets.
An optional argument to `mesvm_test` enables local game-data regressions:

```powershell
.\build\desktop\mesvm_test.exe 'D:\Games\DC4\AdvData\MES'
```

Verified with MSVC 14.51 on this date:

- `dc4_kyo20190204a1.mes` follows the normal ending into
  `DC4_KYO20190204A2`, skipping the conditional `SCE_END` transfer that the
  previous linear walker incorrectly reached.
- `dc4_dc99990101a.mes` pauses for two choices. Selecting each independently
  reaches its first dialogue at offset 4718 or 4944 respectively; both reach
  the common dialogue at 5094 without executing the other option's branch.
- `dc4_sor20190504a.mes` repeats its five-choice password menu after a wrong
  answer; the fourth option reaches the successful dialogue at 27447. This
  intentional game loop is preserved.
- A smoke pass starts every one of the **469 chapter files independently**
  with cleared variables and rotates choices when a menu repeats. It emits
  **48,059 dialogue commands**, handles **25 choice waits**, and reaches
  **469 script transfers**, with **zero VM errors**. The test stops at each
  transfer. It does not prove full-route continuity or explore every branch.

The result is retained in `work/vm-regressions-20261010.txt`. The initial
direct-build test binary was `build/vm-tests/mesvm_test.exe`; the shared build
also registers the same source as `mesvm_test`.

## Round 3: sprite motion and scene transitions, 2026-10-10 (later)

Implemented in `SceneRunner` after corpus-context analysis (781,046 commands
over all 512 files, 0 fetch failures; `work/opusage.py`, `work/opcontext.py`):

| Behavior | Static evidence | Corpus evidence | Implemented result |
| --- | --- | --- | --- |
| Sprite slide-in/out | `0x42DA46` calls `sub_447620`/`sub_447700` (per-sprite `Gfx` motion entries; `(end-start)/duration` velocity, then a redraw entry) | 13,045 uses; `0x8E` never carries a `0x77` extension; w0 small (1–64), w1/w2 signed offsets, w3 = sprite slot | `0x8E w0..w3` queues an offset decaying linearly to zero over `w0` 60 Hz ticks from the 0x8B position; update()/finish_animations() interpolate. Headless screenshots settle instantly. |
| Background crossfade | `0x0F(32)` handler `0x4301F6` sets effect flags (`sub_412B10..`); `0x40EF50` enables the effect engine (`this+2 = 1`) and stores 4 words (`sub_41C5E0`) | every background swap: `0x0F(2) 0x3C bg 0x49` (instant) then `0x0F(32) 0x3C bg 0x37 0x49 bg [chars] 0xB4` (crossfade) — 0x37 never appears without 0x49 following; 0xB4 is always `w[0]==0` | `0x0F(32)` snapshots the composed old frame; `0xB4` after the commit starts a 500 ms old→new crossfade; `0x0F(2)` stays an instant cut. `0x21 p1` selects the commit effect (MesVM 0x4294DF); p1=0 bypasses 0x425F50 setup. |
| Layer commit markers | dispatcher `0x36`/`0x37` both reach `0x4227E0` (redraw all 150 `Gfx` layers + the extra surface) | 0x37 precedes 0x49 100% of the time | `0x37` commits slot 1 early (same cleanup as the 0x49 commit); `0x36` is a redraw boundary only. |
| Voice-timing registry | `0x40E5E0`: up to 100 entries per table, 40-byte keys, strupr'd, updated in place; `MesVM case 71 (0x47)` passes table index + 2 words | 0x47 keys are exactly `so/sn/my/rn/rr/mt/st/wk/yk/al` ×6 `0xAE` variants per character per script; `0xAE w3` is always 255 | Not needed for playback fidelity; recorded here as the tables' semantics (per-character speech-balloon anchors by voice variant). |
| Wipe masks | `0x0F(21,x)` handlers route to `sub_421B30` (two half-screen effect layers) | `0x3C 21 "MSK\MASKxx"` always pairs with `0x0F(21,x)` or `SYSTEM\EFFCHANGESCREEN`; masks are gradient wipes, never scene content (2,399 uses) | `0x3C 21 MSK\*` loads into a dedicated `mask_` member instead of the layer map; the mask-wipe animation itself is still a hard cut. |

Verification: all 3 CTest suites pass, the 469-chapter smoke is unchanged
(48,059 dialogues, 0 errors), headless screenshots are pixel-identical to the
pre-change baseline, and `--effect-ms` samples show the crossfade ramping
brightness 32→105→193→197 over its 500 ms (black→sakura-tree opening scene).

## Opcode coverage audit, 2026-10-13

A corpus-wide audit (`mesdis.exe` over all 512 MES files) measured the gap
between opcodes the scripts actually use and opcodes with an explicit code
path:

- **128 distinct opcodes, 762,261 total instances** across the shipped scripts.
- Direct VM cases in `mesvm.cpp`: 18 (control flow: 0x02, 0x09, 0x0A, 0x1C,
  0x1D, 0x20, 0x4B, 0x4F, 0x50, 0x51, 0x54, 0x5C, 0x5F, 0x82–0x86).
- Host-interpreted ops: 27 in `scene.cpp` (0x0F, 0x15, 0x1E, 0x36, 0x37, 0x3C,
  0x3D, 0x43, 0x49, 0x52, 0x54, 0x6A, 0x6D, 0x72, 0x73, 0x77, 0x7A, 0x8B,
  0x8C, 0x8E, 0x95, 0x99, 0x9D, 0xAB, 0xB4) and 35 in `system_menu.cpp`
  (menu/widget ops including 0x69–0x7F, 0x87).
- Combined coverage: **≈60.9% of opcode instances** have a code path.
- **76 opcodes (~39% of instances) reach the default host-owner path** and are
  skipped. Highest frequency: `0xAE`×56,280 paired with `0x47`×56,280
  (per-character voice-variant anchors — `sub_444F50`/`sub_40E5E0`, the
  `so/sn/my/rn/rr/mt/st/wk/yk/al` speech tables; judged benign for playback),
  `0x03`×49,529 (Mes_GetLabelAddr helper), `0x06`×49,226, `0x3F`×25,936
  (Sprite_BstPath2), `0x4D`×16,024 (`sub_4224C0`), `0xB0`×14,008.

All system screens and representative scenes render correctly with these
skipped, so the skipped set is believed to be visually benign or redundant —
but complex effects-heavy scenes remain the natural place to look for missing
behavior. Reproduce the stats by running mesdis over `AdvData/MES/*.mes` and
counting the second column.

## Remaining limits

- Bank99 mirrors a subset of engine state. Reads needing engine services
  (save previews, real input state, or UI modes) are not fully wired to the
  host. System defaults persist to the emulator `profile.dc4profile` (not the
  original SYSTEM.DAT); configuration/audio side effects are applied through
  `apply_settings()`. Audio registry banks94/95 only
  support the registry-independent special record50.
- `0x1C` checkpoint targets are consumed correctly, and scene-state snapshots
  exist as the emulator's own `.dc4sav` format (see §5/re-save-load.md);
  `0x1D` restore is wired through the emulator save system. Original D.C.4
  save files remain incompatible by design. A conventional subroutine
  call/return stack has not been established by evidence here.
- `0x00` is treated as stopped script playback. The original fetcher does
  not advance at that byte; its surrounding UI scheduling remains a host
  concern. Other graphics/audio/wait commands are yielded to the host.
- The stream decoder exposes `0x77` extension words as a separate command;
  the original fetcher attaches them to the previous command. The verified
  control-flow instructions above do not require those extensions.
- Missing labels, malformed companions, and runaway pure jumps produce
  diagnostics. These are deliberate emulator safeguards, not reproductions
  of the original application's error dialogs or missing-label fallthrough.
- Static analysis establishes control-flow structure. The optional tests
  establish execution of this reimplementation on local scripts. Neither
  establishes pixel accuracy, audio fidelity, or original-engine timing.
