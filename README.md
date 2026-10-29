# DAWg

DAWg is a cross-platform desktop digital audio workstation built with JUCE and C++26. The first development milestone focuses on recording and arranging audio.

## Current foundation

- JUCE desktop application shell
- CMake build with JUCE fetched at a pinned release
- Audio device initialization and safe silent output callback
- Basic device status and restart control
- Architecture notes and a staged audio-first roadmap

The application currently initializes an audio device and outputs silence. Recording, playback, editing, mixing, and project persistence are planned work.

## Requirements

- CMake 3.25 or newer
- A C++26-capable compiler and platform SDK
- Git and network access for the first configure, so CMake can fetch JUCE

JUCE documents CMake as a supported project workflow. Its published framework baseline is C++17 (C++20 for one optional Windows MIDI configuration); this project requests C++26 for the application target. Toolchain and platform support must be confirmed on the machine used to build it.

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug
```

On Windows, open the generated solution in Visual Studio or build with:

```powershell
cmake --build build --config Debug
```

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md), [docs/ROADMAP.md](docs/ROADMAP.md), and [docs/DEPENDENCIES_AND_LICENSING.md](docs/DEPENDENCIES_AND_LICENSING.md).

## Project status

Early foundation stage. The code is not yet a usable recorder or DAW.
