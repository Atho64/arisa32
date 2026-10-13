# dc4engine

A C++17 reimplementation of the D.C.4 / CIRCUS adv32 visual novel runtime. It
reads the installed game's MES scripts, CRX/CRM artwork and XPCM audio. Game
assets are not included. This is a work in progress, not yet a complete adv32
emulator or a replacement for the original game's save files.

## Build and run on Windows

Install Visual Studio or Visual Studio Build Tools with **Desktop development
with C++** and **C++ CMake tools for Windows**, then run:

```bat
build.bat
run.bat
```

The build helper detects Visual Studio and creates a Release x64 build in
`build/desktop`. An older `build/CMakeCache.txt`, including a cache from another
drive, is left untouched. SDL2 is included in `third_party`; its DLL is copied
beside `build/desktop/dc4_desktop.exe` automatically.

The launcher looks for `../DC4/AdvData` beside this repository. To use another
installation, set `DC4_ADVDATA` or invoke the executable directly:

```bat
set "DC4_ADVDATA=D:\Games\DC4\AdvData"
run.bat
build\desktop\dc4_desktop.exe "D:\Games\DC4\AdvData"
```

The player starts the opening scenario, `dc4_kyo20190204a1.mes`. The executable
also discovers a nearby `DC4/AdvData` directory when no path is supplied. Game
files are read in place; the player does not modify them.

## Reading controls

| Input | Action |
| --- | --- |
| Left click, Space, Enter | Finish the typewriter text, then advance |
| Right click, H, OFF/Close button | Hide/show the message window |
| A, AUTO button | Auto advance after text and voice finish |
| S, SKIP button; hold Ctrl | Skip dialogue |
| V, VOICE button | Replay the current voice |
| F5, QUICK SAVE button | Write the quick-save snapshot |
| F9, QUICK LOAD button | Restore the quick-save snapshot |
| SAVE / LOAD buttons | Open the original-style screen with 100 slots |
| 1–9 / 0 in Save/Load | Select slots 1–10 on the current page, down each column |
| Left/Right, Page Up/Down in Save/Load | Change page (ten pages, wrapping) |
| Tab in Save/Load | Switch between Save and Load |
| B, BACKLOG button, wheel up | Open the current session's reading history |
| Wheel, arrows, Page Up/Down in backlog | Scroll history |
| Click a choice, or press 1–9 | Select an option |
| F11, Alt+Enter | Toggle fullscreen |
| F1 | Show controls |
| Esc | Close backlog, cancel auto/skip, otherwise exit |

Auto and Skip stop at choices. Clicking a menu button does not advance the
dialogue. Backlog is a reading history for the current process, not a save or
rewind mechanism. Loading a snapshot resets that history to the restored line.
The original-style system menus (Title, Config, Gallery, Music, Scene mode,
Extras) are implemented from the original MES layouts; the End navigation and
AUTO shortcut stay disabled until those features exist, and report their status
in the window title.

Snapshots use dc4engine's own format. The quick save and 100 manual slots live
in `build/desktop/saves` beside the executable, with a `DC4_SAVE_DIR` override.
The game installation's original `AdvData/savedata` directory is not used.
Selecting an occupied manual slot asks before replacing it; the quick save has its
own file. Saving leaves the screen open; loading restores the scene and closes it.
Empty Load slots are inactive. Optional thumbnail and dialogue-preview sidecars identify occupied
slots; a missing preview does not prevent loading. Original D.C.4 save files
are not compatible. The screen uses the original SAVEBACK/LOADBACK, SLCHIP and
SYSTEMGOTO artwork. NEW loads the newest numbered emulator save, and QLOAD
loads its quicksave. The AUTO shortcut and End navigation remain disabled.
Slots 1–10 retain their previous filenames. Native chapter labels
are represented by dialogue excerpts; overwrite prompts use the platform dialog.

The scene always uses a 1280×720 logical canvas. Resizing or fullscreen keeps
the 16:9 aspect ratio, with black borders where necessary. Pointer hit testing
uses the same logical coordinates. Windows MS Gothic is the default Japanese
font; `--font FILE` or `DC4_FONT` can select another TrueType font/collection.

## Screenshots and development commands

```bat
rem Jump to another scenario (a basename or a full path is accepted).
run.bat scene dc4_kyo20190204a2

rem Reproducible screenshots at native resolution, with no window or audio.
build\desktop\dc4_desktop.exe --scene dc4_kyo20190204a2 --line 0 --shot work\scene.bmp
build\desktop\dc4_desktop.exe --scene dc4_kyo20190204a2 --line 0 --reveal 12 --shot work\partial.bmp
build\desktop\dc4_desktop.exe --scene dc4_kyo20190204a1 --line 24 --panel save --shot work\save-menu.bmp
build\desktop\dc4_desktop.exe --scene dc4_kyo20190204a1 --line 24 --panel load --save-page 10 --shot work\load-page10.bmp

rem Inspect title artwork, test the letterboxed renderer, or test audio.
run.bat title
build\desktop\dc4_desktop.exe --title --window-size 1000 800 --shot work\title.bmp
run.bat audio

rem Print all options.
build\desktop\dc4_desktop.exe --help
```

`--line` is a zero-based dialogue index and is valid interactively too. A
screenshot uses fully revealed text and a fixed wait-animation frame by
default; `--reveal`, `--wait-frame`, and `--hover X Y --down` make partial text
and button states reproducible. `--panel save`, `load`, or `backlog` captures an
overlay without writing any snapshot. Screenshot errors and unreachable line indices
produce a nonzero exit code. Create the output directory before capturing.
`--no-audio` disables device initialization for interactive debugging.

For a configured developer environment, the equivalent build commands are:

```sh
cmake -S . -B build/desktop -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/desktop --parallel
ctest --test-dir build/desktop --output-on-failure
```

Ten CTest suites cover the VM, audio, media/movie, scene, resources, and the
confirmation, backlog, save and system-menu controllers; synthetic fixtures need no game assets, and
several suites accept an AdvData path for optional real-asset regressions.

The core is shared with the Android frontend. Non-Windows desktop CMake expects
an installed SDL2 development package (2.0.18 or newer), but the desktop shell
still needs a complete platform Shift-JIS decoder there. Windows desktop is
the currently validated build target.

## Repository layout and remaining work

- `core/`: portable script VM, formats, audio and scene renderer.
- `sdl_desktop/`: desktop input, display, session backlog and screenshots.
- `android/`: Android/JNI frontend; see its own README for setup.
- `tools/`: script disassembly, asset conversion and reverse-engineering tools.
- `tests/`: synthetic runtime regression tests.
- `docs/findings.md`, `docs/opcode_table.md`: recovered engine behavior and its evidence.

The runtime is being checked against D.C.4's original scripts and renderer.
Unimplemented areas still include original-save compatibility, End/AUTO menu
navigation, native confirmation artwork for save overwrites, complete coverage
of visual effects and every adv32 opcode (roughly 39% of opcode instances in
the shipped scripts still fall through to the host-owner default — see
`docs/RE-HANDOFF.md` §12), and real Android device testing.
Compatibility with other adv32 games is not established.
