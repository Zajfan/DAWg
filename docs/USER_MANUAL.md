# DAWg User Manual & Studio Reference Guide

## Welcome to DAWg
DAWg (Digital Audio Workstation) is a modern, professional music production and audio engineering environment developed by The No-hands Company. Engineered to the highest real-time performance standards, DAWg combines a lock-free, zero-allocation audio engine with automatic plugin delay compensation (PDC), hardware controller integration, and flagship virtual instruments and mixing processors.

Whether you are producing electronic dance music, recording a live band, mixing a film score, or mastering multi-channel audio, this manual guides you through every tool and workflow in DAWg.

---

## Table of Contents
1. [System Requirements & Audio Hardware Setup](#1-system-requirements--audio-hardware-setup)
2. [Workbench Navigation & User Interface](#2-workbench-navigation--user-interface)
3. [Arrangement Timeline & Track Architecture](#3-arrangement-timeline--track-architecture)
4. [Piano Roll & MIDI Sequencing](#4-piano-roll--midi-sequencing)
5. [Flagship Native Virtual Instruments (VSTi)](#5-flagship-native-virtual-instruments-vsti)
   - [Access Virus TI Polar Synth](#access-virus-ti-polar-synth)
   - [Buchla 259 Complex Wavefolder](#buchla-259-complex-wavefolder)
   - [Drum Pad Sampler & Wavetable Engines](#drum-pad-sampler--wavetable-engines)
6. [Flagship Native Mixing & Mastering Suite](#6-flagship-native-mixing--mastering-suite)
   - [ResoTamer Dynamic Resonance Suppressor](#resotamer-dynamic-resonance-suppressor)
   - [MasterPhase Multiband Dynamic EQ & Goniometer](#masterphase-multiband-dynamic-eq--goniometer)
   - [Binaural 3D Spatial Audio Stage](#binaural-3d-spatial-audio-stage)
7. [Audio Restoration & Post-Production Suite](#7-audio-restoration--post-production-suite)
   - [Harmonic-Percussive Stem Separation (HPSS)](#harmonic-percussive-stem-separation-hpss)
   - [Spectral Repair & Inpainting Brush](#spectral-repair--inpainting-brush)
   - [SMPTE LTC Audio Timecode Lock & Video Strip](#smpte-ltc-audio-timecode-lock--video-strip)
8. [Hardware Controller Integration (MCU, HUI, OSC & MIDI Learn)](#8-hardware-controller-integration-mcu-hui-osc--midi-learn)
9. [Project Persistence & Bundles (.dawg and .dawgz)](#9-project-persistence--bundles-dawg-and-dawgz)
10. [Keyboard Shortcuts & Quick Reference](#10-keyboard-shortcuts--quick-reference)

---

## 1. System Requirements & Audio Hardware Setup

### Linux Audio (PipeWire, JACK, ALSA)
DAWg runs natively on modern Linux systems. To achieve low latency without audio dropouts (underruns or "xruns"):

1. **Recommended Audio Subsystem**:
   - **PipeWire with pipewire-jack**: For modern Linux distributions (Fedora, Arch, Ubuntu 22.04+). PipeWire automatically handles application switching and low latency.
   - **JACK2**: For dedicated studio workstations requiring deterministic hardware routing.
   - **Direct ALSA**: For lowest possible round-trip latency when exclusive hardware access is preferred.

2. **Buffer Size & Latency Selection**:
   - **Tracking / Live MIDI Performance**: Choose **64** or **128** samples (1.3 ms to 2.7 ms at 48 kHz).
   - **General Production & Arranging**: Choose **256** samples (5.3 ms at 48 kHz).
   - **Heavy Mixing & Mastering with Lookahead Limiters**: Choose **512** or **1024** samples (10.6 ms to 21.3 ms).

3. **Real-Time Privileges**:
   Ensure your Linux user account belongs to the `audio` group and has real-time scheduling permissions configured in `/etc/security/limits.d/99-realtime.conf`:
   ```
   @audio - rtprio 95
   @audio - memlock unlimited
   ```

### macOS & Windows Setup
- **macOS**: Fully compatible with Apple CoreAudio. Select your audio interface directly from the Settings menu.
- **Windows**: Use native ASIO drivers provided by your interface manufacturer (e.g. Focusrite USB ASIO, RME Fireface USB, Universal Audio Thunderbolt). For internal soundcards, use ASIO4ALL or FlexASIO.

---

## 2. Workbench Navigation & User Interface

DAWg uses a unified single-window layout designed for ergonomic focus and minimum visual fatigue.

```
+------------------------------------------------------------------------+
| TRANSPORT BAR: Tempo | Meter | LTC Timecode | Play | Stop | Rec | Loop |
+---------+--------------------------------------------------------------+
|         | TIMELINE / ARRANGEMENT VIEW:                                 |
| TRACK   | - Track Headers (Color bar, Arm, Mute, Solo, Vol, Pan)       |
| INSPEC- | - Audio & MIDI Lanes with Clip Overviews                     |
| TOR     | - Video Thumbnail Strip (Scoring sync)                       |
| (toggle)+--------------------------------------------------------------+
|         | BOTTOM DOCK TABS:                                            |
|         | [Piano Roll] [Channel Rack] [Mixer] [Device Chain] [3D Stage]|
+---------+--------------------------------------------------------------+
```

### Transport Bar
- **Play / Pause**: Press `Spacebar` to toggle transport playback.
- **Stop & Rewind**: Press `Enter` or `Home` to stop and return the playhead to bar 1.
- **Recording Arm**: Press `R` to toggle master recording arm.
- **Loop Cycle**: Press `L` or `/` to toggle loop mode between in/out boundaries.
- **SMPTE LTC Clock**: Live readout showing Hours:Minutes:Seconds:Frames:Subframes (frame accurate to 24, 25, 29.97, and 30 fps).

### Bottom Dock & Floating Windows
The bottom section of the screen is an integrated multi-panel dock. You can switch between active panels using keyboard shortcuts or dock tabs:
- **Piano Roll**: Key `P`
- **Channel Rack / Step Sequencer**: Key `C`
- **Mixer Console**: Key `M`
- **Modulation Matrix Rack**: Key `Shift+M`
- **Binaural 3D Spatial Radar**: Key `3`
- **Melodyne Pitch Blob Editor**: Key `V`

Any docked view can be detached into its own independent floating window for multi-monitor studio setups by clicking the **[DETACH]** button in its header.

---

## 3. Arrangement Timeline & Track Architecture

### Track Types
1. **Audio Tracks**: Host recorded vocal tracks, guitars, stems, and sound effects with real-time waveform display, non-destructive slip editing, and crossfades.
2. **Instrument Tracks (VSTi)**: Host polyphonic synthesizers, drum machines, and sampler engines triggered by MIDI clips or external controllers.
3. **Submix / Group Tracks**: Collect multiple tracks into a unified bus (e.g. Drum Bus, Vocal Group) for collective compression and saturation.
4. **Aux Return Tracks**: Host shared spatial effects (e.g. Hall Reverbs, Stereo Tape Delays) fed via auxiliary track sends.
5. **Folder Tracks**: Nest related tracks to collapse and organize complex arrangements.

### Clip Editing Modes
- **Slip Editing (`Alt + Drag`)**: Move the underlying audio inside a clip boundary without altering the clip start or length on the timeline.
- **Fade Handles**: Hover over the top-left or top-right corners of any audio clip to drag exponential/logarithmic volume fade-ins and fade-outs.
- **Razor Tool (`X`)**: Slice any clip instantly at the playhead position or mouse cursor.
- **Phase-Locked Take Comping**: Expand track take lanes to swipe-comp the best vocal phrases from multiple takes into a seamless composite master lane.

---

## 4. Piano Roll & MIDI Sequencing

The DAWg Piano Roll combines fast note placement with deep expressive control.

### Core Features
- **Pencil & Paint Tool**: Click to insert notes, drag note edges to resize duration.
- **Scale Quantization & Key Highlighting**: Choose your key and scale (e.g. D Minor, F# Phrygian, C Major). Notes belonging to the scale are highlighted on the background grid, with automatic snap-to-scale.
- **Micro-Pitch Slide & 808 Pitch Glides**: Set the `Slide Semitones` parameter on any note to create seamless glide trajectories (essential for modern Trap 808s and synth leads).
- **Vibrato Envelope**: Adjust `Vibrato Depth`, `Vibrato Rate (Hz)`, and `Vibrato Delay (ms)` per note for lifelike virtual strings and expressive synth solos.
- **Step Sequencer**: Enable step record mode (`Ctrl+Shift+R`) and press keys on your MIDI keyboard to step-advance musical passages into the active clip.

---

## 5. Flagship Native Virtual Instruments (VSTi)

Unlike basic DAWs that provide only generic parameter sliders, DAWg features bespoke, animated hardware chassis interfaces for its flagship instruments.

### Access Virus TI Polar Synth
The **Virus TI Polar** is DAWg's flagship virtual-analog polyphonic synthesizer, modeled on the legendary German hardware synthesizer.

#### Hardware Interface
- **Chassis**: Arctic white aluminum faceplate with real white-wood end-cheeks.
- **Display**: Glowing ruby-red dot-matrix display showing parameter names and a real-time oscilloscope waveform trace.
- **Controls**: 16 knurled brushed-aluminum rotary encoders with red LED indicator arcs and 6 backlit pushbuttons.

#### DSP Architecture
- **Voice Allocation**: 16-voice polyphony with zero heap allocations on the audio thread.
- **Hypersaw Mode**: Oscillators 1 & 2 can be switched into Hypersaw mode, stacking up to 9 detuned sawtooth waves per voice with adjustable stereo panning spread for massive trance and synthwave leads.
- **Oscillator 2 FM & Sync**: Frequency-modulate Oscillator 1 from Oscillator 2, or hard-sync Oscillator 2 to Oscillator 1 for cutting aggressive timbres.
- **Dual Cascade ZDF Ladder Filters**:
  - Two 24 dB/oct Zero-Delay Feedback transistor ladder filters.
  - Configurable in Serial 4-pole, Serial 6-pole, Parallel, or Split routings.
  - Filter Balance control sweeps continuously between Filter 1 and Filter 2.
- **Analog Saturation & Distortion**:
  - Choose between Soft Clip, Hard Clip, Half-Wave Rectifier, Tube Triode Curve, and Digital Bitcrush.
- **Built-in Effects**:
  - Stereo tape delay with cross-feedback and tempo sync.
  - Studio plate/hall reverb tank with adjustable size and high-frequency damping.
- **Factory Presets**:
  - "Polar Hypersaw Lead"
  - "TrancePluck TI"
  - "Cyberpunk Reese Bass"
  - "Crystal Ambient Bells"
  - "Analog Poly Brass"
  - "Vicious Acid 303"

### Buchla 259 Complex Wavefolder
A modular West-Coast synthesis engine featuring:
- **Dual Oscillators**: Modulation VCO driving a Carrier VCO.
- **Non-Linear Diode Wavefolder**: Dynamically folds waveform crests inward to generate metallic, rich upper harmonics.
- **Vactrol Low Pass Gate (LPG)**: Opto-coupler decay simulation for organic wooden plucks and percussive bongs.

### Drum Pad Sampler & Wavetable Engines
- **16-Pad MPC-Style Drum Sampler**: Layer velocity-switched samples with round-robin playback and choke groups.
- **Vector Wave Sequencer**: Morph continuously between 4 wavetables using a 2D joystick pad.

---

## 6. Flagship Native Mixing & Mastering Suite

### ResoTamer Dynamic Resonance Suppressor
ResoTamer is a high-precision spectral suppressor designed to tame harsh cymbal rings, vocal room modes, and abrasive synth resonances without dulling the overall tone.
- **Real-Time 1024-Bin FFT Spectrogram**: Live frequency analysis plotting input vs suppressed spectrum.
- **Psychoacoustic ERB Masking Curve**: Automatically computes equivalent rectangular bandwidth thresholds to only suppress resonances that are perceptually offensive to the human ear.
- **Dynamic Cut Bands**: Interactive draggable cut nodes with selective Q bandwidth and attack/release sensitivity.

### MasterPhase Multiband Dynamic EQ & Goniometer
The ultimate master channel inspection and stereo imaging processor.
- **Lissajous Stereo Phase Radar**: High-refresh vector scope showing stereo field distribution (M/S geometry).
- **Phase Correlation Meter**: Continuous needle readout from -1.0 (pure out-of-phase cancellation) to +1.0 (pure mono compatibility).
- **4-Band Linkwitz-Riley Crossover**: Draggable vertical band divider handles allowing individual stereo width expansion or mono-collapsing below 120 Hz.

### Binaural 3D Spatial Audio Stage
- **SOFA HRTF Spatializer**: Place any audio track in a true 3D binaural sphere (Azimuth, Elevation, Distance) for immersive headphone listening.
- **2D Top-Down Radar**: Drag source coordinates in real time to simulate moving sound sources and room reflections.

---

## 7. Audio Restoration & Post-Production Suite

### Harmonic-Percussive Stem Separation (HPSS)
Right-click any audio clip on the timeline and choose **"Split Clip into 4 Stems (Drums, Bass, Vocals, Other)"**.
- **100% Native DSP**: Uses multi-resolution Short-Time Fourier Transform (STFT) separation with zero external python or heavy AI dependencies.
- **Instant Arrangement Routing**: Automatically creates 4 new grouped audio tracks and populates them with isolated stem clips aligned to the project tempo.

### Spectral Repair & Inpainting Brush
Open the **Spectral Repair Dialog** on any vocal or instrument recording:
- **2D Time-Frequency Spectrogram**: Zoom into frequency regions from 20 Hz to 20 kHz.
- **Lasso & Repair Brush**: Paint over microphone plosives, chair squeaks, or coughs.
- **Autoregressive Inpainting**: Reconstructs the missing spectral bins seamlessly using surrounding temporal and harmonic energy.

### SMPTE LTC Audio Timecode Lock & Video Strip
- **LTC Chase Lock**: Decode Linear Timecode audio received on any physical audio input channel to slave DAWg to analog tape decks or video playback decks.
- **Video Filmstrip Lane**: Synchronized video thumbnail strip displayed above the audio tracks for film scoring hit-point alignment.

---

## 8. Hardware Controller Integration (MCU, HUI, OSC & MIDI Learn)

### Mackie Control Universal (MCU) & HUI Support
Connect physical fader consoles (e.g. Mackie MCU Pro, Behringer X-Touch, PreSonus FaderPort):
- **Motorized Faders**: Bidirectional fader feedback tracking mixer levels in real time.
- **Rotary Encoders**: Adjust track pan, send levels, and macro parameters.
- **Transport Buttons**: Play, Stop, Rewind, Fast-Forward, Record, Loop.

### Universal MIDI Learn Overlay
1. Click the **[LEARN]** button in the transport header.
2. Click any knob, fader, or macro handle in DAWg.
3. Wiggle a physical knob or fader on your MIDI keyboard.
4. **Pickup / Soft Takeover**: The parameter will not jump abruptly; it smoothly engages when the physical hardware reaches the software value.

### Open Sound Control (OSC)
Broadcasts mixer and transport state over UDP to tablets running TouchOSC or Lemur for wireless booth control during recording sessions.

---

## 9. Project Persistence & Bundles (.dawg and .dawgz)

### Project Formats
- **.dawg Project File**: Clean, human-readable JSON project file containing complete track arrangements, MIDI notes, plugin states, and automation curves.
- **.dawgz Consolidated Archive**: A self-contained zip archive containing the `.dawg` session file along with all referenced audio assets, drum samples, and impulse responses. Perfect for sending sessions to collaborators or backing up finished productions.

### Project Templates
DAWg includes ready-to-play production templates with full MIDI sequences:
- **Cyberpunk Synthwave**: 110 BPM retro outrun session starring the Access Virus TI Polar Hypersaw lead and rolling 16th-note analog bass.
- **Modern Trap Production**: 140 BPM modern trap session featuring sliding 808 sub-bass, syncopated hi-hats, and minor pluck melodies.
- **Multi-Mic Acoustic Rock**: 124 BPM session with multi-mic drum kit comping, DI bass, and tube guitar chains.
- **Mastering Session**: Linear-phase mastering suite with true-peak limiting and loudness meter analysis.

---

## 10. Keyboard Shortcuts & Quick Reference

| Action | Shortcut |
| :--- | :--- |
| **Play / Pause** | `Spacebar` |
| **Stop & Return to Zero** | `Enter` or `Home` |
| **Toggle Record Arm** | `R` |
| **Toggle Loop Mode** | `L` or `/` |
| **New Project** | `Ctrl + N` |
| **Open Project** | `Ctrl + O` |
| **Save Project** | `Ctrl + S` |
| **Save Project As** | `Ctrl + Shift + S` |
| **Add Audio Track** | `Ctrl + T` |
| **Add Instrument Track** | `Ctrl + Shift + T` |
| **Export Master Bounce** | `Ctrl + B` |
| **Undo** | `Ctrl + Z` |
| **Redo** | `Ctrl + Shift + Z` |
| **Cut Clip at Cursor** | `X` |
| **Slip Edit Clip** | `Alt + Drag` |
| **Duplicate Selection** | `Ctrl + D` |
| **Toggle Piano Roll** | `P` |
| **Toggle Mixer Console** | `M` |
| **Toggle Channel Rack** | `C` |
| **Toggle Modulation Rack** | `Shift + M` |
| **Toggle 3D Spatial Radar**| `3` |
| **Toggle Pitch Blob Editor**| `V` |
| **Toggle Device Chain** | `D` |

---
*DAWg -- Built for serious music production by The No-hands Company.*
