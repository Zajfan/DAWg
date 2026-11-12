# Task 1 implementation report: JUCE-free audio assets and clips

Status: **DONE_WITH_CONCERNS**

## Delivered

The JUCE-free project boundary now models registered audio assets and clips:

- `MediaAssetId` and `ClipId` are `std::uint64_t`.
- `AudioAsset` owns its UTF-8 path and carries `AudioAssetPathKind`, sample rate, channel count, and frame count.
- `AudioClip` references an asset and stores timeline start, source offset, and sample length.
- `ProjectSnapshot` is normalized format version 2 and owns ordered asset and clip collections.
- `Project` registers assets and clips with independent monotonic high-water marks. It rejects invalid metadata, duplicate explicit IDs, unknown assets, and invalid source ranges without mutating state or consuming an allocated ID.
- Snapshot reconstruction accepts only version 2, validates all track/asset/clip IDs and clip ranges, preserves each collection's order, and resumes each ID domain independently.
- `AddAudioClip` is a project command. `ProjectController::addAudioAsset` and successful clip commands advance render generation without changing render track count; rejected operations and generation overflow are checked before mutation.

No media decoding, persistence schema migration, engine rendering, JUCE, or UI behavior was added.

## Changed files

- `project/include/dawg/project/ProjectTypes.h`
- `project/include/dawg/project/ProjectSnapshot.h`
- `project/include/dawg/project/ProjectCommand.h`
- `project/include/dawg/project/Project.h`
- `project/src/Project.cpp`
- `project/src/ProjectCommand.cpp`
- `services/commands/include/dawg/commands/ProjectController.h`
- `services/commands/src/ProjectController.cpp`
- `tests/project/ProjectTests.cpp`
- `tests/services/ProjectControllerTests.cpp`
- `services/persistence/src/ProjectFile.cpp`
- `tests/services/ProjectFileTests.cpp`

The two persistence files have only the minimum aggregate-initializer compatibility updates (`assets` and `clips` explicitly initialized empty). They deliberately retain their version-1 persistence behavior; persistence migration is outside this task.

## Test-driven development evidence

### RED

Before production model/controller changes, the project and controller tests were extended to require the new IDs, snapshot collections, asset/clip methods, clip command, controller registration seam, serialization coverage, and rejection behavior. The initial prescribed focused command could not start because `build/headless` did not exist. A headless configure was then attempted, but its unconditional `nlohmann/json` FetchContent dependency was not cached and configuration did not finish in the restricted environment. Direct focused compilation was used to observe the expected missing media-interface failures before production implementation.

### GREEN

After implementation, focused JUCE-free direct builds and executables passed with the repository warning policy:

```sh
g++ -std=c++20 -Wall -Wextra -Wpedantic -Werror -Iproject/include \
  tests/project/ProjectTests.cpp project/src/Project.cpp project/src/ProjectCommand.cpp \
  -o /tmp/dawg_project_tests
/tmp/dawg_project_tests
# PASS: 9 project behavior tests

g++ -std=c++20 -Wall -Wextra -Wpedantic -Werror \
  -Iproject/include -Iengine/include -Iservices/commands/include \
  tests/services/ProjectControllerTests.cpp project/src/Project.cpp \
  project/src/ProjectCommand.cpp services/commands/src/ProjectController.cpp \
  engine/src/AudioEngine.cpp engine/src/RenderSnapshotPublisher.cpp \
  engine/src/TransportCommandQueue.cpp -pthread \
  -o /tmp/dawg_project_controller_tests
timeout 20s /tmp/dawg_project_controller_tests
# PASS: 7 render publication and controller behavior tests
```

`git diff --check` also passed before finalization.

## CMake limitation

The requested focused CMake command was not used as final evidence. The initially absent `build/headless` required configuration, and top-level configuration unconditionally enters the persistence subdirectory, which FetchContent-downloads `nlohmann/json`. That dependency was not cached in the restricted environment, so configuration/build attempts were unsuitable for bounded verification. A later partial `cmake --build build/headless --target dawg_project_tests` did build the project target, but the broader focused CMake/CTest attempt was intentionally stopped on request. The direct commands above compile the exact project/controller/engine sources exercised by the two focused tests.

## Self-review

- The public model remains JUCE-free and uses owned values only.
- Version 2 is enforced only by `Project::fromSnapshot`; no persistence schema or media I/O was introduced.
- Clip range validation subtracts only after confirming `sourceOffset <= frameCount`.
- Asset, clip, and track ID high-water marks are independent and preserve exhaustion semantics.
- Controller generation is checked before asset registration and command mutation; only track add/remove changes render track count.
- Tests cover owned snapshot isolation, sparse loaded IDs, invalid metadata/references/ranges, duplicate IDs, rejected-operation immutability, and controller generation behavior.

## Concern

The repository's persistence implementation and persistence tests still represent format version 1 by design. Their aggregate initializers were made explicit solely to keep the new snapshot layout source-compatible; they will need an intentional version-2 persistence migration in the later persistence task. This retained boundary is the reason for `DONE_WITH_CONCERNS`.
