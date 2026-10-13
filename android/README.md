# Arisa32 Android app (dc4engine)

The app opens to a persistent game library. Selecting **Play** opens the game's
original title screen, rendered by the same portable `dc4core` used on desktop.
Game assets and translation packs are supplied separately from the APK.

## Build

```bash
bash android/build_apk.sh          # -> android/Arisa32-debug.apk
```

Requires Android SDK build-tools 35.0.0, platform android-35, CMake 3.22.1,
NDK 27.3.13750724 and JDK 17+. Override defaults with `ANDROID_SDK`,
`ANDROID_NDK_HOME`, `JAVA_HOME` and `DC4_BUILD_DIR`. The packaging script clears
`DC4_BUILD_DIR`; use a dedicated build directory, never a source or game-data
folder. Gradle/Android Studio can also build `android/`.

The APK targets API 35, supports Android 7.0/API 24 onward, and contains
`arm64-v8a` and `armeabi-v7a` libraries. The standalone script compiles every
Java helper with UTF-8 source encoding. C++ exceptions must remain enabled for
checked scene snapshots.

## Add and choose games

- **Add game folder → Choose folder** uses Android's Storage Access Framework.
  Choose `AdvData`, its parent, or a translation root containing `MES`. The
  activity copies the selected game's files into app-owned storage on a worker
  thread, with progress and cancellation. It checks free space, entry counts,
  nesting and file sizes, and removes incomplete copies. `content://` URIs are
  never passed to the filesystem-based core. Original folders are read-only.
- **Use existing readable folder path** registers data in place without copying.
  Shared paths may require the explicitly selected **Storage access** grant:
  all-files access on Android 11+, legacy read permission on older releases.
  The ordinary folder picker requires neither grant.
- The complete game needs `MES/start.mes`, `MES/win_size.mes`,
  `MES/dc4_kyo20190204a1.mes`, `GRP/SYSTEM/MESWIN.CRX`, original system-menu
  scripts/artwork, and the remaining `GRP`, `MUSIC`, `MOVIE`, `NAMES.DAT` and
  `VOICE.DAT` resources. Missing required resources produce a diagnostic.
- On an empty library, an already readable app-specific `AdvData`,
  `/storage/emulated/0/DC4/AdvData`, `/sdcard/DC4/AdvData`, or a valid `data_dir`
  intent extra is registered automatically. Startup still shows the library.
- Entries can be renamed or removed. Removing an entry retains game files and
  emulator saves. Re-adding identical scripts reconnects the same save identity.

The catalogue is atomically persisted in `files/library.properties`. Game
identity is SHA-256 over sorted relative MES script names and contents, so moving
unchanged data does not detach its saves. Interrupted catalogue writes recover
from their backup.

```bash
adb install -r android/Arisa32-debug.apk
adb shell am start -n org.dc4engine/.Dc4Activity \
  --es data_dir /storage/emulated/0/Android/data/org.dc4engine/files/AdvData
adb logcat -s dc4
```

## Languages and fonts

**App language** selects English, Indonesian or Japanese for the library,
Android settings and confirmation dialogs. This is independent of **Game
language**, which selects original Japanese or an imported English/Indonesian
translation pack. The original title/config/gallery/music artwork keeps its
language unless the selected pack supplies translated assets.

Use **Game settings → Import language pack**, select its language and text
encoding (Windows-31J/CP932 or UTF-8), then choose its folder. The pack must
contain actual MES scripts; empty placeholder directories are rejected. Select
it under **Game language** after import. `GRP`, `MES` and `MOVIE` resolve from
the overlay first, with case-insensitive matching and per-file base fallback.
Audio, saves and unrelated paths are not redirected. UTF-8 uses strict decoding
with CP932 fallback for untranslated strings. The local `eng_data` and `id_Data`
folders inspected during development contain no translation assets; the app
does not generate or bundle translated dialogue.

Each game remembers its content language and dialogue font. Import a `.ttf`,
`.otf` or `.ttc` through Android's document picker, or select **System default**.
Fonts are size-limited, checked for a supported font signature and loaded by
Android before installation. A changed font applies on the next game launch.
Changing content language returns to the library; save progress first. Separate
language save directories prevent accidentally mixing translated script offsets.

## Original screens and engine controls

The native MES-driven title, extras, seven-page config, CG gallery, music room,
scene replay, backlog and numbered save/load screens share the original game
assets and portable controllers. Menu availability follows recorded progress.
The native config controls dialogue/auto timing, read/unread skipping, stopping
at choices, voice waiting, window opacity, audio groups/character volumes and
supported effects. Android retains its landscape/fullscreen presentation for
platform-specific window controls.

A draggable **⋮** bubble opens engine-only extras: original config, language and
font settings, save transfer, original title, or library. It stays inside the
upper part of the window, away from the message-bar buttons. Its position is
remembered. Android Back opens the same extras while playing. Returning to the
library joins rendering, stops audio and destroys the current engine before
another game can start.

Scene touches use the same centered 16:9 viewport as rendering. Press/release
must hit the same original control or choice; cancelled gestures and secondary
fingers cannot activate them. Dialogue taps reveal or advance. Auto/skip follow
native config and pause at a choice until the player selects it. Backlog rows
support scrolling, voice replay without closing the log, and snapshot-based
scene jumps. Slot menus have 100 numbered slots with timestamps, 128×72 previews
and dialogue excerpts; overwrites require a confirmation and queue the write
onto the render thread.

MPEG-1 Program Stream movies and MPEG Layer II sound decode through portable
`pl_mpeg` and the core mixer, including scripted movie completion and tap-to-skip.
COMU, camera animation, transitions and credits use the shared scene renderer.
Other video codecs are outside this decoder's scope.

## Saves and portable archives

Emulator data lives under:

```
files/games/<base-script-sha256>/saves/<jp|en|id>/
```

This contains `slot1.dc4sav` through `slot100.dc4sav`, `quicksave.dc4sav`, an
`autosave.dc4sav` if present, preview sidecars, `system-profile.dat` for native
settings/unlocks and `read-lines.txt` for read history. Existing legacy
`files/saves` slots are copied once to the first registered game's Japanese
save directory; the original legacy copies remain intact.

**Game settings → Export saves / Import saves** uses Android document providers
and a versioned ZIP archive. Exports include manual/quick/automatic saves,
previews, native progress/profile and read history. Import closes the running
game and replaces manual/quick slots for the selected content language. Included
profile/read-history data is restored on next launch. Older slot-only archives
preserve the existing profile, read history and automatic save. The selected
base-game fingerprint and content-script fingerprint must match the manifest.
These are emulator archives, not original Windows-game save files.

Import rejects traversal/absolute paths, unknown or duplicate entries, oversized
content, invalid scene-save versions/lengths/checksums, orphan previews and
malformed profile/read-history records. Limits include 310 entries, 32 MiB per
file and 256 MiB total extracted archive data, with tighter native save and
preview limits. All validation occurs in a sibling staging directory before the
existing save directory is renamed to a backup. Failed swaps roll back; startup
recovers interrupted swaps. Export a backup before replacing a save set.

## Lifecycle and validation

Live input, rendering, original-menu changes and save actions run on one render
thread. Lifecycle transitions stop/join it before native pause or shutdown.
Backgrounding pauses the audio device and movie decoder while retaining scene,
VM, mixer and decoder state in memory; returning resumes that state. Process
termination starts at the library, so permanent progress still needs a save.
File import/export runs on a separate worker while native rendering is stopped.

The Java shell compiles against Android API 35. The JVM helper harness in
`tests/android/LibraryChecks.java` passed 60 checks covering archive
round trips, metadata preservation, malformed input rejection, recovery and
catalogue identity/persistence. Portable resource and in-memory snapshot tests
live in `tests/resource_test.cpp` and `tests/scene_test.cpp`. Android hardware QA,
APK signing/alignment and full route coverage must be reported separately from
these compile/helper tests; passing them alone does not establish complete
playability or device-specific storage, font and lifecycle behavior.
