# Task 6 report — full verification and handoff

Date: 2026-11-04

Implementation commit verified: `fa75dc3` (`feat(app): create DAWg main window through window manager`)

Result: `DONE_WITH_CONCERNS`

No production source or test changes were made during Task 6. The report and SDD ledger are the only files added after verification.

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

3. Built the complete test-enabled tree:

   ```bash
   cmake --build build-window-tests --parallel 2
   ```

   Exit code `2`. The engine, registry test, integration test, and all DAWg sources compiled. The final DAWg link failed because the host lacks the ASan runtime referenced by GCC:

   ```text
   /usr/bin/ld: cannot find /usr/lib64/libasan.so.8.0.0: No such file or directory
   collect2: error: ld returned 1 exit status
   ```

4. Rebuilt the independently linkable test targets:

   ```bash
   cmake --build build-window-tests --target dawg_window_registry_tests dawg_window_manager_integration_tests --parallel 2
   ```

   Exit code `0`; both targets were built successfully.

5. Ran the complete CTest set:

   ```bash
   ctest --test-dir build-window-tests --output-on-failure
   ```

   Exit code `8`:

   ```text
   1/2 Test #1: dawg.window_registry ..............   Passed
   2/2 Test #2: dawg.window_manager_integration ...***Failed
   50% tests passed, 1 tests failed out of 2
   FAIL: parent-close modal completion runs exactly once
   FAIL: parent-close modal completes with result zero
   FAIL: escape-enabled dialog completion runs exactly once
   FAIL: escape closes the dialog handle
   FAIL: escape-disabled dialog completion runs exactly once
   ```

   This is a test-harness ordering issue, not a production window-manager defect: the earlier close-routing test calls `stopDispatchLoop()`, after which JUCE's message manager cannot restart its dispatch loop. The later modal callbacks remain queued and are not delivered. The manager's modal lifecycle implementation was not changed to mask this test-only condition.

6. Ran the exact existing-configuration application command:

   ```bash
   cmake --build build --config Debug --parallel 2
   ```

   Exit code `1`: `build` does not exist in this isolated worktree.

7. Verified the application target through a temporary non-sanitized Release configuration:

   ```bash
   cmake -S . -B build-window-release -DDAWG_BUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=Release && cmake --build build-window-release --target DAWg --parallel 2
   ```

   Exit code `0`; the DAWg target linked successfully at 100%. The same existing JUCE bundle-ID warning appeared during configuration.

8. Checked the architectural boundary:

   ```bash
   rg -n "juce_|JUCE" ../VersaTone/engine/include ../VersaTone/engine/src || true
   rg -n "DocumentWindow|MainWindow" Source/App Source/Window
   git diff --check
   git status --short
   ```

   No engine JUCE matches, no direct `DocumentWindow`/`MainWindow` matches, and no whitespace errors were reported. Before cleanup, status contained only the generated `build-window-tests/` and `build-window-release/` directories.

9. Removed temporary verification state:

   ```bash
   rm -rf build-window-tests build-window-release
   if [ -L ../VersaTone ]; then unlink ../VersaTone; fi
   git status --short
   ```

   Both build directories and the sibling symlink were absent afterward; the worktree status was empty.

## Concerns

- Debug application linking cannot complete on this host until the system ASan runtime providing `/usr/lib64/libasan.so.8.0.0` is installed or repaired.
- The integration test must stop and restart its event-pump strategy: it currently permanently stops JUCE's dispatch loop before the modal lifecycle assertions. This should be corrected in a follow-up test-harness change; no production fix was justified by this verification run.
- JUCE continues to warn about the pre-existing bundle identifier containing spaces.
