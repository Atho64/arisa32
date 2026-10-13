# Arisa32 Android redesign — implementation log

Requested: 2026-10-13 (Asia/Jakarta).

## Brief

Rework the Android application around the supplied Arisa wallpaper, polish the
library and supporting app screens, and rename the application **Arisa32**.
Document each implementation stage and its validation here as work proceeds.

The artwork source is the user attachment
`C:/Users/Atho/AppData/Local/Temp/codex-clipboard-27132c01-5b69-4529-b80e-856855f84e16.png`.

## Compatibility decisions

- Rename the visible application, launcher icon and build output to Arisa32.
- Retain the installed application ID `org.dc4engine`, native library/JNI names,
  signing key, catalogue identity and save paths so an update retains existing
  games, language packs, fonts and saves.
- D.C.4 remains the name of an actual game in the library. Its authored in-game
  graphics are game content; the Android app shell receives the redesign.
- Use the supplied artwork as a bundled wallpaper with a contrast scrim and
  responsive cropping. Controls must remain legible over the image.

---

## Step log

### 1. Inspect the current Android shell — complete

- Inspected manifest, resource inventory, Activity library/settings/import flows,
  game catalogue, and standalone APK packaging script.
- Identified that the shell previously used plain dark cards, system default dialogs,
  and the launcher label was "DC4".
- Found that `build_apk.sh` compiled Java before generating `R.java` from resources,
  ignoring resource compilation errors and emitting `dc4-debug.apk`.

### 2. Define and implement the visual system (adv32 / Circus Engine theme) — complete

- **Aesthetic Direction**:
  - Purged generic "vibecoded" tropes (poetic slogans, washed-out pastel sparkles, bubbly curves).
  - Adopted an authentic visual novel engine / emulator UI style reflecting the **adv32 / CIRCUS Engine** runtime heritage (clean, functional, information-dense, dark slate and navy).
- **Palette**:
  - Background Ink: `#0C121C` (deep engine charcoal/navy)
  - Solid Emulator Panel: `#F5131B26` (high opacity 96% panel with crisp borders)
  - Primary Text: `#E6EDF5`
  - Secondary/Muted: `#8899AC`
  - Accent Engine Cyan: `#38BDF8`
  - Accent Circus Gold: `#F59E0B`
  - Border Lines: `#33455A73` (subtle 1dp technical stroke)
- **Wallpaper Presentation**:
  - User-provided Arisa wallpaper converted to WebP (`res/drawable-nodpi/arisa_wallpaper.webp`).
  - Rendered with a high-contrast dark gradient scrim on the left (to keep text 100% crisp and readable) while displaying the character art cleanly on the right backdrop.
- **UI System (`ArisaUi.java`)**:
  - Replaced 22–24dp bubbly corners with sharp, professional 8–10dp radiuses.
  - Buttons: 6dp rounded, uppercase bold labels (`LAUNCH`, `MANAGE`, `+ ADD GAME`).
  - Badges: 4dp technical badges (`ADV32 · CIRCUS`, `JP (ORIGINAL)`, `EN PATCH`, `ID PATCH`).
  - Dialogs: `ArisaUi.DialogBuilder` with custom header eyebrow (`ADV32 / CIRCUS ENGINE`).

### 3. Application branding and resource assets — complete

- **Application Name**:
  - Explicitly maintained as **Arisa32** in `strings.xml`, manifest, and output APK.
- **Icons & Drawables**:
  - Crest emblem: `res/drawable/arisa_mark.xml` and adaptive icon `res/mipmap-anydpi-v26/ic_launcher.xml` redesigned with a geometric Circus star and Arisa32 engine crest in slate, cyan, and Circus gold.
  - Dialog shape: `res/drawable/arisa_dialog.xml` (12dp radius, dark slate fill, technical border).
- **Themes & Styles**:
  - `ArisaTheme` (`styles.xml`) with dark `#0B1017` bars, cyan highlight `#38BDF8`.
  - `ArisaDialog` alert dialog theme.

### 4. Activity overhaul and interaction flows — complete

- **Library View (`showLibrary`)**:
  - Title: **Arisa32** with technical eyebrow `ADV32 · CIRCUS ENGINE RUNTIME`.
  - Section header: `GAME LIBRARY` with description `CIRCUS Visual Novel Runtime & Script VM`.
  - Removed all vibecoded/fluffy text ("STORIES, AT YOUR PACE", "Your games. Your own little world", "Your first story awaits").
  - Functional empty state: `NO GAMES FOUND` with clear call to action to select an `AdvData` folder or MES scenario files.
  - Game cards display: Title, `ADV32 · CIRCUS` badge, language tag, path, and direct action buttons `LAUNCH` and `MANAGE`.
  - Footer: `Arisa32 · adv32 Runtime v0.2.0 (CIRCUS Engine)`.
- **Settings & Sheets**:
  - `showAppSettings`: Grouped into `PREFERENCES`, `GAME STORAGE`, and `ENGINE SPECIFICATION` describing the CIRCUS adv32 scenario VM, CRM/CRX graphics, and XPCM audio pipeline.
  - Clear directory selection between SAF folder import and direct path linking.
- **In-Game Overlay & Controls**:
  - Floating `⋮` bubble for settings in-game: clamped to upper viewport so it never obscures dialogue or choice prompts.
  - Progress dialog in `runFileTask`: In-app styled progress card featuring `ProgressBar` and live byte transfer updates (`progressMessage`).

### 5. Standalone packaging script modernization — complete

- Updated `android/build_apk.sh`:
  - Enforced safe build directory `C:/dc4build/arisa32` with safety checks against root deletion.
  - Added resource staging to prevent path-with-space issues with AAPT2.
  - **AAPT2 Compile & Link**: Generates `R.java` with `--java` into `$OUT/gen/r` before Java compilation.
  - **Javac**: Compiles all Java sources including generated `R.java`.
  - **D8**: Compiles classes to `classes.dex` targeting min-api 24.
  - **Asset & Native Assembly**: Packs `arm64-v8a` and `armeabi-v7a` native libraries, aligns `resources.arsc`
    uncompressed.
  - **Sign**: Signs output using `android/debug.keystore` (alias `dc4`).
  - Output artifacts: `android/Arisa32-debug.apk` (and alias `android/dc4-debug.apk`).

### 6. Validation and test results — complete

1. **Standalone APK Build**:
   - `android/build_apk.sh` executed cleanly.
   - Generated `android/Arisa32-debug.apk` (8,684,539 bytes).
2. **AAPT2 Badging Verification**:
   - Package: `org.dc4engine`
   - VersionCode: `2`, VersionName: `0.2.0`
   - Application Label: `Arisa32`
   - Launcher Icon: `res/mipmap-anydpi-v26/ic_launcher.xml`
   - Native ABIs: `arm64-v8a`, `armeabi-v7a`
3. **Automated Test Suites**:
   - Java archive & library checks: `LibraryChecks` passed **60/60 checks** (`PASS: 60 archive/catalogue checks`).
   - C++ Core tests: `resource_test` and `scene_test` passed **100%** with zero failures.
