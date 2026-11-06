# DAWg

DAWg is a cross-platform desktop digital audio workstation built with JUCE and C++26. Its reusable engine, project model, and command services are kept independent of JUCE so the realtime core can serve future products as well as the DAW application.

## Current foundation

- JUCE desktop application shell with explicit application composition
- A JUCE-free C++20 engine with a bounded transport command queue and deterministic silent rendering
- Immutable render snapshots published at audio block boundaries
- A JUCE-free project model with stable track identity and typed commands
- Version-one transactional project persistence with typed errors
- JUCE audio-device/callback adapters and a platform window manager for state, dialogs, monitor bounds, DPI, and native X11 behavior
- A display-free project/audio vertical-slice test that creates, edits, saves, reloads, and renders a project

The application currently initializes an audio device and outputs deterministic silence. Recording, clip playback, editing, mixing, and media management are later milestones; the foundation establishes the contracts they will consume.

## Requirements

- CMake 3.25 or newer
- A C++26-capable compiler and platform SDK for the application
- A C++23 standard library for persistence and a C++20 compiler for the reusable engine/project targets
- Git and network access for the first application configure, so CMake can fetch JUCE and nlohmann/json

JUCE documents CMake as a supported project workflow. Its published framework baseline is C++17 (C++20 for one optional Windows MIDI configuration); this project requests C++26 for the application target. Toolchain and platform support must be confirmed on the machine used to build it.

## Build

```sh
cmake -S . -B build -DDAWG_BUILD_APP=ON -DDAWG_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug
ctest --test-dir build --output-on-failure
```

The engine, project, persistence, controller, and vertical-slice tests can be
built without JUCE or a display server:

```sh
cmake -S . -B build-headless -DDAWG_BUILD_APP=OFF -DDAWG_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-headless
ctest --test-dir build-headless -L headless --output-on-failure
```

The window-manager integration test is labelled `window-manager` and requires
a supported desktop display. On Linux/X11, run it on the target host with the
appropriate `DISPLAY`; it covers close routing, modal/child windows, maximize,
fullscreen, monitor bounds, and DPI conversions.

On Windows, open the generated solution in Visual Studio or build with:

```powershell
cmake --build build --config Debug
```

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md), [docs/ROADMAP.md](docs/ROADMAP.md), and [docs/DEPENDENCIES_AND_LICENSING.md](docs/DEPENDENCIES_AND_LICENSING.md).

## Project status

Foundation stage complete. DAWg is not yet a usable recorder or full production
DAW; recording, playback, editing, mixing, and plugin workflows are the next
product milestones.
