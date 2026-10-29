# DAWg Roadmap

## 0.1.0 — Foundation

- [x] JUCE application shell and CMake build entry point
- [x] Audio device initialization and status display
- [x] Define real-time audio constraints
- [ ] Confirm builds on the target operating systems and compilers
- [ ] Add audio device settings UI

## 0.2.0 — Transport and audio playback

- [ ] Play, stop, and return-to-start controls
- [ ] Import WAV files
- [ ] Single-track playback with stable seeking
- [ ] Show clip duration and a basic waveform

## 0.3.0 — Recording

- [ ] Select input device and channels
- [ ] Record input to WAV through a preallocated buffer and worker thread
- [ ] Place takes on the arrangement timeline
- [ ] Handle device loss and disk-write errors clearly

## 0.4.0 — Multitrack arrangement

- [ ] Create, rename, reorder, and remove audio tracks
- [ ] Move, trim, split, and duplicate clips
- [ ] Add timeline zoom and horizontal scrolling
- [ ] Save and reopen projects

## 0.5.0 — Basic mixing

- [ ] Track gain and pan
- [ ] Mute and solo
- [ ] Master output and level meters
- [ ] Export stereo mix to WAV

## Later

- Non-destructive edits, fades, automation, plugin hosting, MIDI, virtual instruments, undo/redo, and expanded format support.
