# SDD ledger — plan: docs/superpowers/plans/2026-11-04-window-manager.md

## Setup

- Worktree: `/home/zajferx/.codex/worktrees/097f/DAWg`
- Base commit: `0106c6568065ece0516de5738c64bc215e392832`
- Plan and spec read before dispatch.

## Preflight plan scan

| Scope | Shared output/input or self-check | Finding | Ruling |
|---|---|---|---|
| Task 1 ↔ Task 2 | `Tests/WindowRegistryTests.cpp`, `WindowRegistry`, `WindowTypes` | Task 1 intentionally references the not-yet-created API; Task 2 implements the same signatures. | Clean TDD boundary; dispatch in order. |
| Task 1 | Test file and direct compiler command | The focused command is expected to fail for the missing header, then Task 2 supplies it. | Clean. |
| Task 2 ↔ Task 3 | `CMakeLists.txt`, optional test target | Task 2 adds the deterministic target; Task 3 adds the integration target under the same option. | Clean; retain both CTest labels. |
| Task 2 | Registry types, implementation, CTest target | Tests cover lifecycle, clamping, close callbacks, display placement, and conversions implemented by the task. | Clean. |
| Task 3 ↔ Task 4 | `CMakeLists.txt`, `Tests/WindowManagerIntegrationTests.cpp`, `WindowManager` | Task 3 is the red test phase; Task 4 supplies the backend sources and completes the target. | Clean TDD boundary; no production implementation in Task 3. |
| Task 3 | JUCE initialisation, return code `77`, manager behaviors | Desktop-peer absence is skipped; all other failures return non-zero. | Clean. |
| Task 4 ↔ Task 5 | `CMakeLists.txt`, integration tests, `WindowManager`/`WindowOptions` | Task 5 consumes the public manager and close callback delivered by Task 4. | Clean. |
| Task 4 | Managed window, registry, native state, modal lifecycle | JUCE maximize is not portable through `ResizableWindow`; the plan uses the native-state seam and capability reporting required by the spec. | Ruling: keep platform adapters; unsupported backends must return failure, never fullscreen-as-maximize. |
| Task 5 ↔ Task 6 | DAW target, CTest, `Main.cpp` | Task 5 wires the app; Task 6 verifies the combined branch and architectural boundary. | Clean. |
| Task 5 | Main-window ownership and shutdown | `DawApplication` owns the manager and closes the main handle before manager destruction. | Clean. |
| Task 6 | Full test/build/grep checks | Verification does not mutate source except for a focused regression fix if needed. | Clean. |

No additional plan conflicts were found. The only preflight ruling is the native maximize adapter decision recorded above.

## Task 1 review loop

- Review verdict: Needs fixes.
- Important: modal test must assert no callback is delivered before the returned completion callback is invoked, then prove one delivery.
- Minor: remove the unused `WindowRegistry` in the conversion test so warning-enabled builds remain clean.
- Task 1: fix round 1/5 (2 open findings; commits `6026b2b` onward).
- Task 1: fix round 1/5 (2 addressed, 0 open; commits `6026b2b`..`b3d952d`).
- Task 1: complete (commits `6026b2b`..`b3d952d`, review clean).

## Task 2 ruling

- Ruling: use `Source/Window` as the registry test include directory and remove the accidental root `Source/WindowRegistry.h` shim — the approved file map places all window implementation under `Source/Window`, and no root facade is requested. Cost if wrong: a later include-path cleanup would be required, but no public API behavior changes.
- Task 2: complete (commit `9f32763`, review clean).
- Task 2 concern recorded: isolated worktree lacks the sibling `../VersaTone/engine`, so CMake verification required a temporary symlink that was removed; existing JUCE bundle-ID warning is unrelated to the diff.
- Task 3 review verdict: Needs fixes.
- Task 3 fix round 1 findings: pass a concrete `WindowHandle` to `closeRequested` (compile blocker); exercise the callback attached to the manager-created window rather than only a synthetic adapter; add invalid-handle and unknown-display public-manager no-mutation assertions.
- Task 3: fix round 1/5 (3 addressed, 0 open; commits `eeb8955`..`65f2eb8`).
- Task 3: complete (commits `eeb8955`..`65f2eb8`, review clean).
- Task 4 initial implementation: commit `9d7e79e`; scoped review identified release-blocking modal lifecycle, maximize initialization, message-thread ownership, and report-integrity findings plus display/native-source follow-ups.
- Task 4 fix round 1/5: dispatch an implementer against the existing commit; preserve the commit and add focused modal parent-close/escape coverage before re-review.
- Task 4 fix round 1/5: existing follow-up commits `933dac1`, `6f699a0`, and `de4d980` addressed the original review findings; controller-added commit `317a971` defers parent-close modal ownership until JUCE delivers its callback and consolidates the callback regression flush.
- Task 4 focused verification: `/tmp/dawg-build-window-tests` build and CTest passed (`2/2`); temporary worktree build directories, `/tmp` build, and `../VersaTone` symlink were removed. `Source/App/Main.cpp` remains an unrelated pre-existing dirty change and was not included.
- Task 5: complete (commit `fa75dc3`, review clean; verification was interrupted before the focused test could run).
- Task 6 verification: fresh Debug configuration succeeded, registry and integration binaries built, the DAWg Debug link was blocked by the host's missing `/usr/lib64/libasan.so.8.0.0`, and CTest reported the existing test-harness event-loop ordering failure in the modal assertions. A temporary non-sanitized Release build linked `DAWg` successfully. Architectural boundary checks and `git diff --check` passed; all build artifacts and the temporary `../VersaTone` symlink were removed, leaving the worktree clean. No source changes were justified.

## Final whole-branch review fix pass

- Ruling: reject contradictory maximum dimensions when a non-zero maximum is smaller than the normalized minimum; apply the same validation in `WindowRegistry` and `WindowManager`, before mutating or creating state.
- TDD red phase: `dawg_window_registry_tests` failed with the two expected contradictory-limit assertions before production edits. The new integration regression/placement tests compiled after a test-only JUCE API correction.
- Fix commit: `bd9a655` (`fix(window): resolve final manager review findings`). It keeps close-request callbacks reusable, centers default-placement modal dialogs around parents, applies manager-controlled parent/child z-order, gates Linux maximize on advertised EWMH atoms, and rejects invalid limits.
- Green verification: both focused targets built successfully; `dawg_window_registry_tests` exited `0`; Release `DAWg` configured and linked successfully.
- Environment concern: focused CTest under bare Xvfb exited `8` because JUCE aborts at startup with X11 `BadAtom`; a bounded Mutter-on-Xvfb retry reproduced the same pre-assertion failure. The integration target therefore could not execute its assertions here. No engine files changed.
- Final cleanup: temporary `/tmp/dawg-window-fixes-red`, `/tmp/dawg-window-fixes-release`, and the isolated-worktree `../VersaTone` symlink were removed.
