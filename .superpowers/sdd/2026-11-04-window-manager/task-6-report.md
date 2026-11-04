# Task 6 report — full verification and handoff

Date: 2026-11-04

Implementation commit verified: `fa75dc3` (`feat(app): create DAWg main window through window manager`)

Final reviewed baseline: `c686726`, including the prior fix commit
`bd9a655`. Current scoped follow-up head: `dfa50a2`.

Result: `DONE_WITH_CONCERNS`

The only Task 6 source changes are the focused test-harness correction in `CMakeLists.txt` and `Tests/WindowManagerIntegrationTests.cpp`; no production source change was required.

## Commands and results

1. Checked the isolated worktree prerequisites:

   ```bash
   ls -ld ../VersaTone ../VersaTone/engine build build-window-tests 2>&1 || true
   ```

   The managed worktree had no build directories. A temporary `../VersaTone` symlink was present and resolved to the repository engine; it was used for configuration and removed during cleanup.

2. Configured the requested fresh test build:

   ```bash
   cmake -S . -B build-window-tests -DDAWG_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
   ```

   Exit code `0`. CMake configured successfully. It emitted the existing JUCE warning that `com.DAWg Project.DAWg` contains spaces in its bundle ID.

3. Built the requested integration and registry targets:

   ```bash
   cmake --build build-window-tests --target dawg_window_manager_integration_tests dawg_window_registry_tests --parallel 2
   ```

   Exit code `0`; both targets were built successfully.

4. Ran the integration executable directly under a fresh Xvfb display:

   ```bash
   timeout 30s xvfb-run -a ./build-window-tests/dawg_window_manager_integration_tests
   ```

   Exit code `1`. The first actual failure occurred before any test assertion:

   ```text
   X Error of failed request:  BadAtom (invalid Atom parameter)
   Major opcode of failed request:  18 (X_ChangeProperty)
   Atom id in failed request:  0x0
   ```

   The subsequent JUCE leak assertions are abort-cleanup fallout.

5. Ran the complete CTest set under Xvfb, as required:

   ```bash
   xvfb-run -a ctest --test-dir build-window-tests --output-on-failure
   ```

   Exit code `8`:

   ```text
   1/2 Test #1: dawg.window_registry ..............   Passed
   2/2 Test #2: dawg.window_manager_integration ...***Failed
   50% tests passed, 1 tests failed out of 2
   X Error of failed request:  BadAtom (invalid Atom parameter)
   Major opcode of failed request:  18 (X_ChangeProperty)
   Atom id in failed request:  0x0
   ```

   Registry passed (`1/2`). Integration failed before assertions with the same JUCE/X11 `BadAtom`. The harness fix is preserved: `JUCE_MODAL_LOOPS_PERMITTED=1` is enabled and `runDispatchLoopUntil` replaces the permanently stopping helper. No production change was justified.

6. Ran a bounded non-sanitized Release DAWg configuration and link:

   ```bash
   timeout 180s cmake -S . -B build-window-release -DDAWG_BUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=Release
   timeout 180s cmake --build build-window-release --target DAWg --parallel 2
   ```

   The Release executable was produced at `build-window-release/DAWg_artefacts/Release/DAWg`; a fresh incremental build verification completed successfully. CMake emitted only the existing JUCE bundle-ID warning about spaces in `com.DAWg Project.DAWg`.

7. Checked the architectural boundary:

   ```bash
   rg -n "juce_|JUCE" ../VersaTone/engine/include ../VersaTone/engine/src || true
   rg -n "DocumentWindow|MainWindow" Source/App Source/Window
   git diff --check
   git status --short
   ```

   No engine JUCE matches, no direct `DocumentWindow`/`MainWindow` matches, and no whitespace errors were reported. Before cleanup, status contained only the generated `build-window-tests/` and `build-window-release/` directories.

8. Removed temporary verification state:

   ```bash
   rm -rf build-window-tests build-window-release
   if [ -L ../VersaTone ]; then unlink ../VersaTone; fi
   git status --short
   ```

   Both build directories and the sibling symlink were absent afterward; the worktree status was empty.

## Concerns

- Bare Xvfb causes JUCE 9.0.4 to issue `X_ChangeProperty` with atom `0` during integration startup; the suite needs a display environment with the required X11/window-manager support to execute its assertions. The requested harness correction is committed, but this Xvfb run remains blocked before assertions.
- JUCE continues to warn about the pre-existing bundle identifier containing spaces.

## Final whole-branch review fix pass

Fix commit: `bd9a655` (`fix(window): resolve final manager review findings`)

The pass added regression coverage before production edits, then fixed the
review findings without touching the reusable engine:

- `ManagedWindow` keeps the close-request callback callable across repeated
  native attempts until the application closes the window.
- Contradictory minimum/maximum dimensions are rejected by both
  `WindowRegistry` and `WindowManager` before registration or peer creation.
- Parent children are brought into the manager-controlled z-order, and modal
  dialogs using the default `{ 0, 0, 900, 560 }` placement are centered around
  their parent.
- Linux maximize capability and requests now require the three maximize-related
  EWMH atoms to be advertised by `_NET_SUPPORTED`; bare Xvfb therefore reports
  maximize as unsupported.

### TDD and verification results

1. Added the repeated-close and contradictory-limit regression tests, plus
   modal-centering/child-placement coverage. The deterministic red check ran:

   ```bash
   /tmp/dawg-window-fixes-red/dawg_window_registry_tests
   ```

   Exit `1`, with the expected failures that contradictory limits were still
   registered. The integration test initially needed a test-only JUCE API
   correction (`isFocused()` is not a `ManagedWindow` API); after that correction
   the target compiled before production edits.

2. After implementation:

   ```bash
   cmake --build /tmp/dawg-window-fixes-red --target \
     dawg_window_registry_tests dawg_window_manager_integration_tests --parallel 2
   ```

   Exit `0`.

   ```bash
   /tmp/dawg-window-fixes-red/dawg_window_registry_tests
   ```

   Exit `0`.

3. Focused CTest under bare Xvfb:

   ```bash
   xvfb-run -a ctest --test-dir /tmp/dawg-window-fixes-red --output-on-failure
   ```

   Exit `8`: the deterministic registry test passed; the JUCE integration test
   failed before assertions with X11 `BadAtom` from `X_ChangeProperty`, followed
   by JUCE leak assertions. One bounded retry with Mutter on Xvfb produced the
   same startup failure, so this remains an environment limitation rather than
   a test assertion result. No practical Linux-native maximize assertion could
   run in that display environment.

4. Production link verification:

   ```bash
   timeout 180s cmake -S . -B /tmp/dawg-window-fixes-release \
     -DDAWG_BUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=Release
   timeout 180s cmake --build /tmp/dawg-window-fixes-release \
     --target DAWg --parallel 2
   ```

   Both exited `0`; the Release DAWg executable linked successfully. The only
   configure warning was the existing JUCE bundle-ID warning about spaces.

## Scoped re-review follow-up

Follow-up commit: `dfa50a2` (`fix(window): verify native maximize state`)

This follow-up corrects the post-review close-count assertion so both the
manager-backed and direct `ManagedWindow` repeated-request checks remain at
`2` after explicit close handling. It also adds native-state assertions to the
supported maximize integration path and changes Linux `setMaximized()` to wait
for a synchronized `_NET_WM_STATE` result before returning success. The manager
already records `WindowState::maximized` only after that native call succeeds.

Exact follow-up verification:

- `cmake -S . -B /tmp/dawg-window-followup -DDAWG_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release` — exit `0`; existing JUCE bundle-ID spaces warning.
- `cmake --build /tmp/dawg-window-followup --target dawg_window_registry_tests dawg_window_manager_integration_tests --parallel 2` — exit `0`.
- `/tmp/dawg-window-followup/dawg_window_registry_tests` — exit `0`.
- `timeout 30s xvfb-run -a /tmp/dawg-window-followup/dawg_window_manager_integration_tests` — exit `1` before assertions with JUCE/X11 `BadAtom` from `X_ChangeProperty`; this is the same bare-Xvfb environmental blocker.
- `timeout 60s cmake --build /tmp/dawg-window-followup --target DAWg --parallel 2` — exit `124` while Release LTO was still linking. The Release link is therefore not claimed as passed for this follow-up.

The follow-up touched no engine files. Temporary build output and the sibling
VersaTone symlink are removed after handoff; the worktree is expected to remain
clean.
