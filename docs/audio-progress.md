# Audio reconstruction — 2026-10-10

Implemented in `core/src/audio.cpp` and `core/src/scene_audio.cpp`.
This is a working audio subset, not a claim of complete adv32 compatibility.

## Evidence and corrections

The attached IDA database identifies `DC4.EXE`, 32-bit x86, image base
`0x400000`. The following are **static decompilation observations**, obtained
through the user's local IDA MCP on 2026-10-10. Complete responses are retained
under `work/ida-20261010-audio-<address>.json`; older dispatcher evidence is
`work/pseudo_dispatcher.c`. Existing IDA names are not all reliable:
`Audio_StopChannel` at `0x43D070` actually starts a sound by group.

| Evidence | Observation | Implementation |
| --- | --- | --- |
| `0x40C830`, `0x40C9D0`, `0x40CA20` | Audio IDs index filename tables in `GRP/NAMES.DAT`. Version is 7; after the 4-byte version and 224-byte header come a u32 BGM count and 48-byte records, then a u32 SE count and 36-byte records. The first 32 bytes of each record are a terminated filename. Limits are 400 and 600. | Validated table parser; numeric IDs are never assumed to be filenames. Case-insensitive resource lookup also works on case-sensitive filesystems. |
| `0x43B5A0`, `0x43D070`, `0x43ADC0` | BGM uses channel 0; looping SE uses 2; one-shot SE picks the first idle channel in 3..4, otherwise 5. Voice similarly picks 8..10, otherwise 11. Nonzero play flags request looping. | Twelve independent mixer slots and native allocation order. |
| `0x43C5C0`, `0x43CFE0` | SE names with prefixes `se\\201` route to channel 7; `se\\200`, `se\\202`, `se\\473` route to 6. | System effects can coexist with ordinary SE. |
| `0x43CE50`, `0x43CEC0`, `0x43B920` | Ordinary SE stop/status groups are 2..5; voice groups 8..11. Immediate BGM stop includes 0..1. | Group stops preserve system SE slots 6/7. |
| `0x43C220`, `0x43C2F0` | Outgoing BGM is copied from 0 to 1, preserving playback position, then faded. Default fade has 30 update steps, towards -3000 hundredths dB if current volume is at least -3000, otherwise -10000. | Replacement music preserves outgoing playback in slot 1 and fades it. |
| `0x43B6F0`, `0x43B600`, `0x43AE20` | Volume is DirectSound hundredths dB. At master 100%, group percentage maps to `50*p - 5000`; zero is -10000. Temporary changes use 30 steps. | Percentage volume and ramps in decibels. |
| `0x43B7A0`, `0x43B800`, `0x43AE50`, `0x43AE70` | These helpers call DirectSound `SetPan` / `GetPan`, not volume. Pan groups include 0..1, 2..7, and 8..11. | Signed pan, channel attenuation, and pan ramps. |
| `0x43C5C0`, table `0x4B4AD8` (previous evidence) | BGM loop start and end are source-rate frame offsets. End limits the playback buffer. | Both endpoints are used; the intro plays once, then the loop region repeats. Invalid endpoints fall back within the decoded buffer. |

The previous findings document describes `0x70` as a stop operation. The
dispatcher shows it is **BGM playback**, with a special stop ID. Likewise,
`0x13` specifically operates on BGM; its first operand is not a group selector.

## Implemented MES operations

| Opcode | Decoded behavior |
| --- | --- |
| `0x70 id, mode, _, _` | BGM by NAMES.DAT index. ID 255 stops. Modes 0/1 loop; mode 1 keeps the currently selected track without restarting. Modes 2+ play once. |
| `0x6F id, mode, _, _` | Alternate BGM start: mode 0 loops. The original's additional preload/streaming flag is not reproduced. |
| `0x71 id, loop, _, _` | SE by table index; signed ID -1 fades ordinary SE, -2 stops voice. SE0 resolves to the silent file in D.C.4, which also silences system effects. |
| `0x4C`, `0x4D`, `0x4E` + path | Voice archive lookup, with loose-file fallback. New voice replaces current voice; the last successfully decoded blob is retained for Replay. |
| `0x13 byte, flag` | Fade BGM. If flag is 255, a following `0x77` supplies the fade duration in its first word. |
| `0x05 percentage, _` | Temporarily scale BGM volume. 255 restores; 254 is the native queued-audio flush marker (audio here dispatches immediately). Starting another BGM restores saved volume. |
| `0xAF start, target, ticks, group` | Signed pan values multiplied by 100; group 0=BGM, 1=SE, 2=voice. Zero ticks applies the initial pan only. |

`SceneAudio::dispatch` returns true for supported instructions, including when
the output engine is null. It retains audio state across script transitions.
`reset_state()` is for an actual restart/new game, not every MES load.

## Decoder and callback fixes

- All playback uses the source sample rate. D.C.4's supplied SE set contains
  22,050, 24,000, 44,100 and 48,000 Hz files; playing everything at 44,100 Hz
  changed pitch and duration. Mono is upmixed; fractional positions interpolate.
- XPCM raw PCM8/PCM16 and Vorbis are accepted; malformed or truncated payloads
  are rejected. Ogg version normalization walks page lengths and segment tables,
  avoiding accidental modification of `OggS` bytes inside compressed packets.
- Decoder work and allocation happen outside the mixer lock. Buffer replacement
  is synchronized with the callback; old buffers are released outside the lock.
  The previous callback could read a vector while another thread cleared it.
- The same mixer runs without an audio device for deterministic tests.
  Device initialization and shutdown now clean up failed starts and repeated calls.

## Validation

Release build via `build.bat` succeeded. CTest passed both `mesvm_test` and
`audio_test`. `audio_test ..\DC4\AdvData` additionally passed against the supplied
game data: **101 BGM names, 600 SE names, all 148 supplied SE files decoded**, BGM
001 Vorbis playback, system voice lookup and replay. No audio device is opened
by these tests.

The deterministic checks cover loop boundaries, source-rate duration,
interpolation, mono upmix, saturation, native volume mapping, pan ramps including
ramps staged before voice starts, outgoing BGM fades, SE allocation/stop groups,
malformed input and concurrent voice replacement while rendering.

## Emulator quicksave audio state

`SceneAudioSnapshot` version 1 is an emulator-owned save structure. It does not
describe or import the original game's save format. The parent save system
serializes its fields; it must not serialize an in-memory C++ struct layout.

- BGM: table ID, mode, alternate opcode flag, active/inactive status, and a
  fractional source-frame position. Inactive music stays inactive on restore.
- Looping SE: table ID, native channel (2, 6 or 7), and fractional source-frame
  position. Multiple entries cannot claim the same channel.
- Voice: last voice resource name, retained for Replay. Restore stays silent by
  default; `restore_snapshot(snapshot, true, error)` requests replay from the
  beginning. No decoded PCM or archive blobs go into the snapshot.
- Controls: logical BGM percentage and saved temporary percentage, plus each
  group's current/target volume in dB and pan in DirectSound units. Remaining
  ramp lengths are output frames, preserving unfinished transitions.

`capture_snapshot()` obtains all mixer positions and controls under one lock.
`restore_snapshot()` validates the version, IDs, finite ranges, unique channels,
resource names and source positions; it reads/decodes the required assets before
atomically replacing mixer channels. An invalid snapshot, missing asset or
unsupported version returns an explanatory error without changing current
playback or logical scene audio state. Filesystem names are resolved inside the
game data directory. Old PCM buffers are destroyed outside the callback lock.

The dispatcher also records logical tracks, looping effects, last voice and
controls when there is no audio engine. Headless snapshots retain those values
without requiring an output device; source positions do not advance without a
mixer clock. Headless restore performs the same asset/position validation.

Additional tests passed in the standalone audio build: after saving and restoring
real BGM/looped-SE playback with volume and pan transitions, the next 256 mixed
stereo frames are identical. Tests also cover fractional seeks, restore rollback,
duplicate channels, non-finite controls, out-of-source positions, inactive BGM,
finished voice silence and explicit replay, logical headless state, old versions
and missing source data.

Short one-shot SE, already outgoing BGM crossfades and the progress of a voice
utterance are deliberately transient and are not resumed by this compact
snapshot. The retained voice name remains available to replay after loading.

## Remaining limits

Native durations are update counts; this implementation converts them using a
60 Hz clock and interpolates continuously. Exact native timer cadence, fade
completion behavior and simultaneous mixed-voice staging need runtime audio
comparison. `0x4E` currently has replacement-voice semantics; the original can
stage multiple voice slots within a VM batch. Character-specific voice volume,
pitch adjustments, voice continuation tags, skip-dependent audio suppression,
and per-character automatic panning are not yet reconstructed. Decoding is
whole-file rather than streamed. PCM ADPCM/other XPCM codecs are unsupported.
These limits are separate from the verified basic playback and routing above.
