# Arisa32

> [!IMPORTANT]
> ### 🤖 Full LLM Generated / 100% Dibuat oleh AI (LLM)
> Seluruh kode sumber, arsitektur sistem, reverse-engineering opcode MES adv32, dekoder aset, antarmuka Android/JNI, serta dokumentasi pada repositori ini **dibuat secara penuh oleh Large Language Model (LLM)**.
>
> ### 🎮 Kompatibilitas Game / Game Compatibility: D.C.4 Only
> Untuk saat ini runtime engine **hanya mendukung dan diuji khusus untuk D.C.4 (Da Capo 4)**. Judul visual novel lain yang menggunakan engine CIRCUS adv32 (seperti D.C.3, Dal Segno, dsb.) **belum didukung**.
>
> ### 📦 Nama Proyek / Repository: Arisa32
> Nama proyek dan repositori ini adalah **Arisa32** (pengganti penamaan internal sebelumnya `dc4engine`). Target paket Android menggunakan identifier `org.dc4engine` demi menjaga kompatibilitas save data dan path aplikasi.

A portable C++17 reimplementation of the CIRCUS adv32 visual novel engine runtime specifically for **D.C.4 (Da Capo 4)** on Windows (SDL2) and Android (NativeActivity + JNI). It reads the original game's installed MES scripts, CRX/CRM artwork, and XPCM audio directly in place.

> [!NOTE]
> **Game assets are not included.** A legitimate copy of D.C.4 (Da Capo 4) is required. Game files (`AdvData`) are read in place and are never modified by the engine.

---

## Platforms

1. **Windows Desktop** (C++17 + SDL2)
   - Native 1280×720 letterboxed renderer with aspect ratio preservation.
   - Text rendering via Windows font rasterizer (MS Gothic by default or TrueType font override).
   - High-fidelity audio playback for BGM, SE, and voice streams (XPCM).
   - Original-style Save / Load system (100 slots + quicksave) and reading backlog.
   - Title screen, config menus, gallery, and scene selector.

2. **Android App** (NativeActivity + JNI + Java UI)
   - Built-in persistent Game Library manager with folder import/linking via Storage Access Framework (SAF).
   - Clean adv32 / Circus Engine UI theme (`ArisaUi`) with Arisa crest icon.
   - Save archive import/export system (`.zip` backup/restore).
   - Integrated full-screen touch controls, dialogue backlog, and fast-forward skipping.
   - Targets Android API 35 (compatible with Android 7.0 / API 24+; `arm64-v8a` & `armeabi-v7a`).

---

## Build and Run on Windows

Install Visual Studio or Visual Studio Build Tools with **Desktop development with C++** and **C++ CMake tools for Windows**, then run:

```bat
build.bat
run.bat
```

The build helper detects Visual Studio and creates a Release x64 build in `build/desktop`. SDL2 headers and import libraries are included in `third_party/sdl2`; its runtime DLL is copied beside `build/desktop/dc4_desktop.exe` automatically.

The launcher automatically looks for `../DC4/AdvData` beside this repository. To use another installation directory, set `DC4_ADVDATA` or invoke the executable directly:

```bat
set "DC4_ADVDATA=D:\Games\DC4\AdvData"
run.bat
build\desktop\dc4_desktop.exe "D:\Games\DC4\AdvData"
```

The player starts the opening scenario, `dc4_kyo20190204a1.mes`.

---

## Build for Android

To build the standalone debug APK on Linux, macOS, or Windows (via Git Bash / MSYS2):

```bash
bash android/build_apk.sh          # -> generates android/Arisa32-debug.apk
```

Prerequisites:
- Android SDK build-tools 35.0.0 & platform `android-35`
- Android NDK 27.3+
- CMake 3.22.1+
- JDK 17+

Prebuilt binaries (`Arisa32-debug.apk`) are also provided on the [GitHub Releases](../../releases) page for manual sideloading. See [`android/README.md`](android/README.md) for full details on permissions, storage setup, and ADB installation.

---

## Reading Controls (Desktop)

| Input | Action |
| --- | --- |
| Left click, Space, Enter | Finish the typewriter text, then advance |
| Right click, H, OFF/Close button | Hide/show the message window |
| A, AUTO button | Auto advance after text and voice finish |
| S, SKIP button; hold Ctrl | Skip dialogue |
| V, VOICE button | Replay the current voice line |
| F5, QUICK SAVE button | Write quick-save snapshot |
| F9, QUICK LOAD button | Restore quick-save snapshot |
| SAVE / LOAD buttons | Open 100-slot Save/Load screen |
| 1–9 / 0 in Save/Load | Select slots 1–10 on current page |
| Left/Right, Page Up/Down in Save/Load | Change page (10 pages, wrapping) |
| Tab in Save/Load | Switch between Save and Load screens |
| B, BACKLOG button, Mouse wheel up | Open reading backlog history |
| Mouse wheel, Arrow keys, Page Up/Down | Scroll backlog history |
| Click a choice, or press 1–9 | Select an option |
| F11, Alt+Enter | Toggle fullscreen |
| F1 | Show controls dialog |
| Esc | Close backlog, cancel auto/skip, or exit |

---

## Save Data & Snapshots

- Snapshots use Arisa32's internal portable format (`.sav` snapshot files with metadata sidecars).
- Quick saves and the 100 manual slots live in `build/desktop/saves/` on Windows, or in app-isolated external files storage on Android.
- **Original Windows D.C.4 save files (`AdvData/savedata`) are not compatible.**
- The original game's save directories are never altered.

---

## Development & Diagnostics

```bat
rem Jump directly to a specific scenario:
run.bat scene dc4_kyo20190204a2

rem Headless reproducible screenshot at native 1280x720:
build\desktop\dc4_desktop.exe --scene dc4_kyo20190204a2 --line 0 --shot work\scene.bmp

rem Inspect title screen or test audio:
run.bat title
run.bat audio

rem Print all CLI options:
build\desktop\dc4_desktop.exe --help
```

To run the automated CTest regression test suites:
```sh
cmake -S . -B build/desktop -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/desktop --parallel
ctest --test-dir build/desktop --output-on-failure
```

---

## Repository Layout

- `core/`: Portable C++17 script VM, MES interpreter, CRX/CRM decoders, XPCM audio, and scene renderer.
- `sdl_desktop/`: SDL2 desktop frontend, window management, audio playback, backlog, and screenshot capture.
- `android/`: Android frontend (NativeActivity, JNI glue, `ArisaUi` custom widget library, Game Library activity).
- `tools/`: Disassemblers (`mesdis`), asset inspection utilities (`crx_tool`, `crm_tool`, `voicetool`), and analysis helpers.
- `tests/`: Automated unit and regression tests for VM opcodes, save states, audio, and resource loading.
- `docs/`: Reverse-engineering documentation, opcode dispatch tables, and engine findings.

---

## Current Status & Limitations

- **D.C.4 Specific**: Tested against D.C.4's original assets and scripts. Compatibility with other CIRCUS adv32 games is not implemented.
- **Opcode Coverage**: Core gameplay, dialogue flow, branching choices, voice playback, background transitions, CG displays, BGM, and system menus are functional. Roughly 39% of less common opcodes fall through to default stub behavior (see `docs/RE-HANDOFF.md` §12).
- **Save Compatibility**: Saves cannot be imported from or exported to the original D.C.4 Windows binary.
- **Work in Progress**: Not an official product of CIRCUS. This is an independent, clean-room reverse-engineering project created entirely with LLMs for preservation and research purposes.

---

## Legal & Trademark Notice

- **D.C.4 ~Da Capo 4~** and **CIRCUS / adv32** are trademarks and copyrighted intellectual property of **CIRCUS**.
- This project is an independent, clean-room reimplementation developed strictly for educational, interoperability, and preservation purposes.
- No copyrighted game assets, scripts (`.mes`), images (`.crx`/`.crm`), or audio streams (`.xpcm`/`VOICE.DAT`) are bundled or distributed with this repository. Users must own and provide their own legitimate copy of the game.

