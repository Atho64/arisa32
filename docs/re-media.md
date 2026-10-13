# MPEG playback and ending sequences

Updated 2026-10-11. This work reads the supplied AdvData resources and retained
IDA decompilation. It does not run the original SAVE/LOAD UI or modify original
game files. The original renderer/player was not observed playing these scenes
during this investigation; the native behavior below is static evidence.

## Native evidence

Retained raw results and disassemblies are under `work/media-re/`; the complete
MES dispatcher is `work/menu/vm_full.c`.

| Evidence | Observation | Implemented behavior |
| --- | --- | --- |
| VM case 82 / `0x42E629..0x42E6E8` | MES opcode `0x52` constructs `MOVIE/<name>.mpg`, stops three ordinary audio groups, enters `sub_453860`, then restores saved UI state. | `pending_movie()` yields to the host. The VM cannot advance until `finish_movie()`. |
| `movie-stop-audio.json`, `movie-stop-groups.json`; `0x4224C0`, `0x43CE50` | Movie entry stops channels 0..1, 2..5, 8..11. System effects 6/7 are excluded. | `SceneAudio::stop_for_movie()` updates logical audio and stops those groups. Prior scenario BGM is not restarted after the movie. |
| `movie-native.json`; `0x453860` | The movie routine pumps events in a blocking loop until input, completion, or window termination; it restores window/input state afterward. | Desktop/Android use a nonblocking player with completion and cancellation paths, then continue after the consumed opcode. |
| `ed_asa.txt` and the other `ed_*.mes` files | Route endings show nine `ED/ED_<route>_01..09` backgrounds. They start BGM ID 50 in mode 2, contain `0x15` waits and `0x43` input targets, then return to a route-specific scenario. | Existing scene audio plays the nonlooping ending song; the scene runner displays cards and yields for their waits. |
| VM case 21; `0x4292BA..0x4292FD` | `0x15 p1,p2` stores `200*p1` milliseconds. `p2` enables input checking. Special values 254/255 are separate wait modes. | Timed waits use `200*p1` ms. Headless execution can still settle them immediately; interactive hosts enable animation scheduling. |
| `med.txt`, `credits-call.c`; `0x409FC1..0x40A01B` | `MED` starts BGM 51, writes bank 3 index 0 = 125, and calls `0x1E(9,0)`, which creates/starts the credits controller. | `0x1E(9,0)` starts the portable PCK credit scroller. |
| `scroll-start.c`; `0x459850` | Duration is `1000*bank3[0]`. `ED/ed.PCK` page count determines speed `720*count/duration`. Two pages start at y=0 and y=720. | The 22 supplied 1280×720 CRX pages scroll at 126.72 pixels/second for this script. Only adjacent pages are decoded/cached. |
| `scroll-update.c`; `0x459380` | At the last page, scrolling stops. It waits for input or up to 5000 ms, then raises a black overlay by 0.005 per update. | Final page holds for five seconds, then fades over 200 updates interpreted at 60 Hz. The whole sequence is about 127.65 seconds. |
| `pck-index.c`, `pck-page.c`; `0x44E340`, `0x44F760` | The non-CRXB PCK format starts with a u32 count followed by `(offset,size)` pairs; entries point directly at image blobs. | Count, offsets, sizes, archive size, and 1280×720 CRX headers are validated before image decoding. |
| `native-var-read.c`, `effect-duration.json`; `0x40CE2D`, `0x40E1E0` | Effect speed is bank 99 index 12, stored at Buf[11]. Centre is 5. For base frames N: below 5 add `int((N/2)/5*(5-speed))`; above 5 subtract `int((N/2)/6*(speed-5))`, where N/2 is integer division. | Supported scene transitions use the native 0..11 curve. The effects toggle is separate from temporal scene scheduling, so turning effects off retains ending waits. |

`ED.MES` is progression/return logic, not a movie. `TED.MES` transfers to ED.
There is no basis for replacing these scripts with an invented ending MPG.

## Portable movie implementation

`core/src/movie.cpp` uses PL_MPEG: MPEG-1 video, MPEG Layer II audio, and MPEG
Program Stream demuxing. The same code builds for Windows and Android, avoiding
dependence on platform MPEG-1 codec availability. The MIT source and pinned
upstream revision/hash are in `third_party/pl_mpeg/README.md`.

The host opens a stem or `.mpg` name relative to AdvData/MOVIE. Absolute paths,
separators, traversal, NULs, and non-MPG extensions are rejected. The resource
resolver handles the shipped mixed case and selected translation overlays.
Playback reads compressed data from a file rather than loading an entire movie
or predecoding its soundtrack. Header limits are 1920×1088, known MPEG-1 frame
rate codes, a 1 GiB file, and four hours of reported duration. MPEG-2 sequence
extensions are rejected. These checks are not a claim of exhaustive decoder
fuzz hardening.

`AudioEngine` has a separate bounded movie PCM ring. The render thread decodes
roughly 300 ms ahead; the device callback consumes and resamples stereo audio
to 44.1 kHz. Video follows the number of audio frames consumed. The callback
never decodes or allocates. Queue underrun produces silence; it does not mix in
the background music. A short video tail can continue after soundtrack end.
Without an audio device, video uses the host clock and the soundtrack is
decoded silently. `decoded_audio_frames()` and `audio_peak()` provide decoder
validation without opening a sound device.

Gallery playback suspends existing mixer positions and ramps and resumes them
on close. Scenario movie playback first executes the native group stops, so
it resumes a silent scene unless the following script starts new audio. Movie
volume uses its own user setting, multiplied by master volume. Script volume
and saved mixer state do not overwrite user volume preferences.

`set_device_paused()` stops/restarts the miniaudio device while retaining PCM,
positions, ramps and the movie queue. `MoviePlayer::set_paused()` also freezes
its clocks. Hosts reset their elapsed-time origin on resume. Saving during a
movie or a timed sequence is rejected rather than creating an incomplete
resume point; normal in-memory lifecycle pause does not require a save.

## Host contracts

- Keep `SceneRunner::set_animation_enabled(true)` for an interactive host.
  `set_effects_enabled(false)` disables visual animations only.
- A false return from `advance_to_next_text()` can mean a movie, timed wait,
  choice, script end, or error. Inspect the corresponding accessors.
- For nonempty `pending_movie()`, open `MoviePlayer` once; while it is active,
  update/present the movie instead of advancing the scene. On completion,
  cancellation, or open failure, close it, call `finish_movie()`, and advance.
- During `waiting_for_time()`, update the scene. Once `script_ready()` is true,
  call `advance_to_next_text()` once. `finish_script_wait()` is the explicit
  input path and honors the current scripted `0x43` target.
- Native menu transfers stop at `scene_ended()` with `next_script()`, so hosts
  can open the shared menu instead of executing its UI bytecode as dialogue.

## Verification

`media_test` includes a synthetic 64×48 MPEG-1/MP2 clip generated from FFmpeg's
test pattern and sine source. FFmpeg is not needed to run the engine/tests.
The test covers decoded video/audio, completion, cancel/reopen, pause, invalid
names, absent files, malformed/truncated files, movie VM blocking and resume,
timed wait boundaries, effects-disabled waits, snapshot rejection during
media, mixer isolation/restoration, volume separation, ducking and music seek.
Existing `audio_test` and `scene_test` also passed after integration.

The optional original-asset test takes an AdvData path; a second argument
decodes each supplied movie through EOF. No proprietary fixtures are committed.

| Supplied movie | Decoder duration (s) | Frames decoded through EOF |
| --- | ---: | ---: |
| dc4_mv001.mpg | 26.9603 | 809 |
| DC4_OP1.mpg | 97.9645 | 2938 |
| DC4_OP2.mpg | 107.908 | 3237 |
| DC4_OP3.mpg | 110.7 | 3322 |
| KotoRI_uta01.mpg | 22.656 | 680 |
| KotoRI_uta02.mpg | 13.9667 | 420 |

All six decoded successfully at 1280×720. A later decode smoke run measured
nonzero MP2 output for every file, with peaks from 0.0969 to 0.9839 over their
first 3.2 seconds. `DC4_OP1` also independently probes as MPEG-1 + stereo MP2 at
48 kHz (`op1-probe.json`). The original-asset test traversed ED_ASA's waits and
MED's PCK scroller. Retained logs: `full-decode-tests.txt` and
`decode-smoke-tests.txt`.

## Remaining limits

- This is decoded-output and static-code validation, not an original-player
  audiovisual timing comparison or an Android hardware performance guarantee.
  Device-specific audio output latency is not measured or compensated.
- `0x15(254,...)` is not a fully reconstructed generic audio/effect join; the
  implemented route endings use ordinary timed waits. Native first-view skip
  restrictions are not enforced by the portable explicit cancel interface.
- Scrolling credits use a 60 Hz interpretation of native per-update fades.
  Native first-view input flags and the alternate `0x1E(9,1)` slideshow mode
  are not reconstructed by the MED implementation.
- Per-character voice controls cover the twelve named menu slots; ASA/ASU and
  HYR/HIY share controls. Unmapped guest suffixes use the other-character group.
  Exact native guest subgroup classification is not established.
- Music ducking is configurable and functional, with a portable 100 ms attack
  and 300 ms release; those envelope times are not claimed as native timing.
- The player supports the supplied MPEG-1/MP2 media, not arbitrary modern video
  formats, subtitle tracks, movie seeking, or hostile-input isolation.
