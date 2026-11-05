# Linux/X11 maximize fix — 2026-11-05

Base: `275cdf7`. Implementation, regression, and this report are committed together.
Worktree: `/home/zajferx/.codex/worktrees/097f/DAWg`.

## Cause and change

On the active GNOME Shell X11 host (`DISPLAY=:0`, two monitors, 3840×1080),
the root advertised `_NET_WM_STATE` and both maximize atoms. Temporary X11
diagnostics showed `XSendEvent` succeeded, but `ManagedWindow` inherited
`ResizableWindow` flags that omitted `windowHasMaximiseButton`. JUCE wrote
Motif functions `0x6` (move/resize); GNOME omitted maximize from the window's
allowed actions and ignored the request. Enabling that flag for resizable
Linux/BSD windows changed the functions to `0x16`, and GNOME applied both
maximize atoms. The existing client-message action, target, and masks were
already correct.

The adapter now explicitly sets the client-message display and flushes the
request, observes property notifications on its own X11 connection, and
checks both maximize atoms before reporting success. Restore requires both
atoms absent. A monotonic one-second deadline accommodates asynchronous
compositor updates and bounds ignored requests; `poll` waits on the X11
socket instead of arbitrary sleeps. It also accounts for events already
queued inside Xlib and interrupted waits. It does not dispatch JUCE
callbacks or consume events from JUCE's connection.

EWMH capability gating and manager message-thread guards are preserved.
No engine changes, manager changes, or final CMake changes were required.
All temporary diagnostic output was removed.

## Verification

Configured a Release build with local JUCE 9.0.4 sources and the temporary
sibling VersaTone symlink:

```sh
cmake -S . -B build-x11-maximize -G Ninja -DDAWG_BUILD_TESTS=ON \
  -DCMAKE_BUILD_TYPE=Release \
  -DFETCHCONTENT_SOURCE_DIR_JUCE=/tmp/dawg-task5-build/_deps/juce-src \
  -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF
cmake --build build-x11-maximize --target \
  dawg_window_registry_tests dawg_window_manager_integration_tests --parallel 2
DISPLAY=:0 ctest --test-dir build-x11-maximize --output-on-failure
cmake --build build-x11-maximize --target DAWg --parallel 2
```

- Red: before production fixes, the new native maximize/restore/re-maximize
  regression failed four maximize assertions; the original four manager
  maximize assertions also failed. Registry passed. CTest exited `8`.
- Green: both requested test targets built successfully; full CTest on
  `DISPLAY=:0` passed **2/2**, with integration executed (not skipped), exit `0`.
- DAWg Release build/link: exit `0`.
- GUI smoke: launched the built DAWg, waited up to ten seconds for its own
  visible X11 window using `xdotool search --sync --pid`, then requested close
  with `wmctrl`. DAWg exited normally with code `0`.
- `git diff --check` and unchanged relative CMake path checks passed.
- Existing CMake bundle-ID warning and informational serial-LTO compilation
  warning remain. No failing checks remain on the acceptance host. Bare Xvfb
  was not used for acceptance; other platform adapters were not exercised.

Removed this task's `build-x11-maximize/` directory and temporary sibling
symlink. Borrowed pre-existing JUCE caches were left intact. No push or merge.
