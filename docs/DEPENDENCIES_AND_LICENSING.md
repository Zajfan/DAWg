# Dependencies and Licensing

## JUCE

CMake fetches JUCE 9.0.4 from the official JUCE repository and pins that tag for repeatable builds. JUCE is dual-licensed under AGPLv3 and a commercial JUCE license. Before distributing DAWg, choose a JUCE license and review its current terms against the intended distribution model.

## nlohmann/json

The persistence service fetches nlohmann/json from the official
[repository](https://github.com/nlohmann/json) at exactly `v3.11.3`
(`9cca280a4d0ccf0c08f47a99aa71d1b0e52f8d03`) with a shallow Git checkout.
It links `nlohmann_json::nlohmann_json` privately to
`dawg_services_persistence`; the engine and project model have no JSON or
JUCE dependency. Public persistence headers expose only project values and
standard-library types.

nlohmann/json is MIT-licensed (copyright 2013–2022 Niels Lohmann in the pinned
release). Preserve its copyright and permission notice in copies or
substantial portions of the software, including distributions that contain
it. The upstream release includes the full notice in `LICENSE.MIT` and the
library headers. No DAWg project license is selected by adding this dependency.

## Project license

No license has been selected for DAWg itself. Add a project license only after choosing one. The project license does not replace JUCE's license requirements.

## C++ standard

DAWg requests C++26 for the application. The reusable engine and project model
request C++20; persistence and its consumers require C++23 for `std::expected`. JUCE's published minimum C++ standard is C++17, with C++20 required for the optional Windows MIDI Services configuration. C++26 compiler and standard-library support varies by toolchain; confirm it on each target platform.

## Dependency boundaries

The JUCE and X11 dependencies stop at `platform/juce` and the `DAWg` application
target. `dawg_engine_core`, `dawg_project_model`, and
`dawg_services_commands` are display-free and can be built with
`DAWG_BUILD_APP=OFF`. `dawg_services_persistence` depends on nlohmann/json only
at its filesystem boundary and exposes standard-library project values to its
callers. This separation is intentional: another future host can reuse the
engine and project services without importing DAWg's desktop framework.
