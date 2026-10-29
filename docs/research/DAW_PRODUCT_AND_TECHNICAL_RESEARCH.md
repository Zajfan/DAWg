# DAWg Product and Technical Research

**Status:** Research synthesis and decision brief. This is not an approved product specification.  
**Research date:** 2026-10-29  
**Repository:** [Zajfan/DAWg](https://github.com/Zajfan/DAWg)

## 1. Purpose

Before building more DAWg features, decide what kind of music-making experience it should provide and what technical commitments that experience requires.

This research looks at representative DAW workflows, audio-engine architecture, plug-in interoperability, audio quality, licensing, and C++26/toolchain risk. It uses vendor manuals and technical standards for verifiable product behavior. Vendor feature descriptions explain what a product offers; they do not prove that users find it easy or enjoyable. Personal workflow evidence from the project owner is kept separate from market-wide claims.

## 2. User context and product question

The project owner describes:

- **eJay:** the first DAW, used in the 1990s; fun to play with and make many tracks, but extremely limited.
- **FruityLoops / FL Studio:** FL Studio 9 felt enjoyable and creative. The owner later found modern FL Studio harder to navigate and less comfortable.
- **Reason:** never felt like home.

The product question is not simply “How do we make a DAW with many features?” It is:

> Can DAWg make it easy to get from a musical idea to an arranged, recordable, mixable project, while keeping the interface understandable as the project grows?

### Timeline clarification

Image-Line dates FruityLoops’ origin to 1997 and FL Studio 9’s release to September 9, 2009. The owner recalled FL Studio 9 as “around the early 2000s”; that date appears to be off by several years. The useful product signal is the remembered workflow and feeling, rather than the exact calendar year. [Image-Line history](https://www.image-line.com/fl-studio-news/fl-studio-25-years-x-lfu) · [FL Studio 9 announcement](https://www.image-line.com/fl-studio-news/fl-studio-9-released)

## 3. DAW workflow landscape

DAWs make different choices about what the user thinks they are manipulating: patterns, clips, tracks, devices, or a customizable set of surfaces. The comparison below describes representative workflow models, not an overall ranking.

| Product / model | Main mental model | What it makes easy | Friction or design risk to watch |
|---|---|---|---|
| **eJay (owner’s experience)** | Loop/sample blocks assembled into songs | Quick play, experimentation, and finishing many small ideas | Limited depth can block recording, detailed editing, routing, and growth |
| **FL Studio 9 and current FL Studio** | Step-sequenced patterns and instruments arranged on a flexible Playlist | Start with a beat or pattern; reuse and arrange ideas freely; route instruments independently | Flexible relationships between Channel Rack, Playlist, and Mixer are powerful but can feel ambiguous. Image-Line’s manual explicitly notes that the default model may confuse people accustomed to a conventional instrument → track → mixer relationship. |
| **Reason** | A modular device rack with a sequencer and mixer | See and alter device chains; patch devices and routing as part of sound design | Rack and sequencer can feel like separate worlds. Reason 14 now places more track, device, level, and send information in a track panel; this is a major change from older versions and needs hands-on evaluation. |
| **Ableton Live** | Two complementary surfaces: Session clips for improvisation and Arrangement timeline for fixed song structure | Explore, launch, and combine ideas before recording a performance into a linear arrangement | Two views and their state can add a second system to learn; switching between them must remain clear. |
| **Bitwig Studio** | Two interacting sequencers: Arranger and Clip Launcher | Keep nonlinear idea exploration and linear song construction available in one DAW; copy ideas between them | The two sequencers have distinct data and control rules. Flexibility comes with an additional mental model. |
| **REAPER** | A traditional timeline and mixer made highly customizable through actions, layouts, scripts, and screensets | Adapt the application to specific workflows and hardware; automate repeated actions | The user may have to configure the application before it feels like theirs. Customization can move complexity from the product into setup. |
| **Cubase** | Track/project window with inspector, zones, editors, media, and mixer surfaces | Organize a large production around tracks, with detailed editing and mixing tools close at hand | Many zones, panels, and specialized tools can crowd the screen or require navigation between views. |
| **Logic Pro** | Tracks, regions, editors, mixer, and optional Live Loops grid | Combine conventional recording and arranging with a cell-based idea grid; tracks map to mixer channel strips | It is Apple-platform specific. Its breadth and deep editor set still require careful hierarchy and progressive disclosure. |
| **Ardour** | Multitrack recording, editing, routing, mixing, and optional cue/clip performance in a desktop production session | Strong audio recording and routing model, including open formats and Linux-first workflows | Complex routing and dense engineering concepts may be a poor default for a user seeking immediate playfulness. |
| **Pro Tools** | Session, track, edit, and mix workflows oriented toward recording and professional post/mix work | Precise audio editing, large-session routing, control surfaces, and established studio interchange | Avid’s AAX ecosystem, licensing/access requirements, and specialist workflow may be more than DAWg needs early on. |

### Sourced product behavior

- FL Studio’s online manual names the Channel Rack, Piano Roll, Mixer, Playlist, and Browser as main work surfaces. It explains that Playlist tracks can hold different clip types and are not necessarily bound to mixer channels. The manual also documents a track mode for a more conventional one-track/one-channel arrangement. [FL Studio workflow](https://www.image-line.com/fl-studio-learning-content/fl-studio-online-manual/html/basics_workflow.htm) · [Playlist](https://www.image-line.com/fl-studio-learning-content/fl-studio-online-manual/html/playlist.htm)
- Reason’s current materials describe its redesigned track-centered workflow, with devices, signal chains, levels, and sends accessible from the track panel while preserving the Rack. The owner’s historical experience predates this redesign. [Reason 14 overview](https://www.reasonstudios.com/new-in-14) · [Reason 14 manual](https://docs.reasonstudios.com/reason14/working-with-the-rack)
- Ableton explicitly distinguishes Session View’s improvisational clip launching from Arrangement View’s linear song timeline. [Ableton Session View](https://www.ableton.com/en/live-manual/12/session-view/) · [Arrangement View](https://www.ableton.com/en/manual/arrangement-view/)
- Bitwig describes its Arranger and Launcher as two sequencers with separate data that can copy clips and scenes between them. [Bitwig: One DAW, Two Sequencers](https://www.bitwig.com/userguide/latest/one_daw_two_sequencers/)
- REAPER documents extensive customization through layouts, actions, toolbars, screensets, and scripting. [REAPER About](https://www.reaper.fm/about.php)
- Cubase’s Project window combines track list, events, inspector, zones, editors, and mixer-related views. [Cubase Project Window](https://www.steinberg.help/r/cubase-pro/15.0/en/cubase_nuendo/topics/project_window/project_window_overview_r.html)
- Logic supports a conventional Tracks area and Live Loops cells/scenes, and allows switching between or displaying both views. [Logic Live Loops](https://support.apple.com/guide/logicpro/live-loops-overview-lgcpf46ffc88/10.7/mac/11.0) · [Logic tracks](https://support.apple.com/guide/logicpro/tracks-overview-lgcpd9638760/10.7/mac/11.0)
- Ardour documents audio and MIDI recording, non-destructive editing, plugins, automation, routing, and a nonlinear cue sequencer. [Ardour features](https://ardour.org/features.html)
- Pro Tools describes session mixing, track organization, routing, metering, automation, and AAX plug-ins. Avid’s AAX SDK access and commercialization involve its developer process. [Avid mixing](https://www.avid.com/pro-tools/features/audio-mixing) · [AAX SDK](https://developer.avid.com/aax/)

## 4. Initial product hypotheses

These are opportunities to evaluate, not decisions to lock in before the owner has tried them.

### Hypothesis A: Keep the first action playful

A new project should allow a user to make sound quickly: record audio, drag in a loop, or tap in a rhythm without first configuring a large session. That preserves the immediacy the owner remembers from eJay and early FruityLoops.

### Hypothesis B: Give every track a legible identity

A conventional relationship—track → clips/regions → track processing → mixer channel—may make projects easier to understand. DAWg could still allow flexible routing, sends, buses, and shared patterns, but the default should reveal those relationships clearly.

### Hypothesis C: Let ideas grow into a timeline

A simple pattern/loop workspace could feed a traditional linear arrangement without forcing the user to learn two unrelated sequencer systems. We should prototype and compare:
1. one timeline that accepts loop/pattern clips;
2. a separate idea grid that can be captured into the timeline;
3. one track-centered view with contextual editing panels.

### Hypothesis D: Progressive disclosure beats a permanent “beginner mode”

Keep recording, playback, clip placement, and core track controls visible. Reveal advanced routing, detailed automation, plug-in setup, and specialized editors when requested. Provide stable navigation, explicit panel names, saved workspace layouts, and a one-action reset to the default layout.

**Inference:** The owner’s account suggests a desire for early creative immediacy without the later navigation burden. It does not yet establish whether DAWg should be primarily pattern-first, audio-recording-first, or deliberately hybrid. A short workflow evaluation is needed before choosing.

## 5. Engine and application architecture

### JUCE is a platform and audio toolkit, not the complete DAW engine

JUCE provides cross-platform application, audio I/O, audio file, DSP, MIDI, and plug-in APIs. Its AudioProcessorGraph can connect processors as nodes, and AudioPluginFormatManager can instantiate supported plug-ins. Those are useful foundations, but neither is a complete project/arrangement engine. [JUCE features](https://juce.com/features/) · [AudioProcessorGraph API](https://docs.juce.com/develop/classjuce_1_1AudioProcessorGraph.html) · [Plugin format manager API](https://docs.juce.com/master/classjuce_1_1AudioPluginFormatManager.html)

**Recommended starting architecture:** use JUCE for application/platform integration, device I/O, audio file support, DSP building blocks, and selected plug-in format adapters. Keep DAWg’s project model and playback/recording engine under DAWg ownership:

- **Project model:** songs, tracks, clips, media references, tempo, routing, automation, and undoable edits.
- **Transport and timeline:** musical time, sample time, playhead, loop/punch ranges, count-in, and synchronization.
- **Audio graph/mixer:** track inputs, inserts, sends, buses, master, channel layouts, and latency compensation.
- **Media engine:** background disk reads for playback, waveform/peak generation, record buffering, and file writing.
- **Plug-in host service:** scanning, identification, instantiation, parameter automation, state save/restore, latency, UI lifecycle, failure reporting, and later optional process isolation.
- **UI:** a message-thread editor that issues commands and reads published state; it should not own audio-thread data.

JUCE’s audio-device callback runs on a high-priority audio thread. Blocking work there risks dropouts; file operations and large project edits need another path. JUCE’s own documentation warns that callback locks block the audio thread. [AudioIODeviceCallback](https://docs.juce.com/master/classjuce_1_1AudioIODeviceCallback.html) · [AudioDeviceManager callback lock](https://docs.juce.com/master/classjuce_1_1AudioDeviceManager.html)

### C++26

Keep the user-requested C++26 target, but treat it as a toolchain requirement rather than a sound-quality feature. JUCE’s modules declare a C++17 baseline, and JUCE’s published requirements still list C++17, with C++20 needed for the optional Windows MIDI Services configuration. CMake 3.25 recognizes CXX_STANDARD 26; compiler support remains partial and compiler-dependent. GCC describes its C++26 mode as experimental; Clang lists a subset of C++26 features. [JUCE requirements](https://github.com/juce-framework/JUCE) · [CMake CXX_STANDARD](https://cmake.org/cmake/help/latest/prop_tgt/CXX_STANDARD.html) · [GCC C++ status](https://gcc.gnu.org/projects/cxx-status.html) · [Clang C++ status](https://clang.llvm.org/cxx_status)

**Implication:** keep C++26 isolated to DAWg application code where practical, avoid depending on newly standardized features until toolchains agree, and add build checks for the actual OS/compiler matrix before declaring a platform supported.

### Audio device and driver choices

JUCE provides a common device layer but the available backends differ by platform. Its current audio-device module documents WASAPI and DirectSound on Windows, ALSA on Linux, and optional JACK. ASIO is a separate Windows option and enabling it creates additional SDK/license obligations. macOS audio uses CoreAudio. [JUCE audio device module](https://github.com/juce-framework/JUCE/blob/master/modules/juce_audio_devices/juce_audio_devices.h)

**Decision still needed:** first supported OS(es), expected audio interface, and whether low-latency ASIO/JACK support is needed at the first public milestone. A plain device abstraction can get DAWg started, but it cannot validate professional low-latency behavior across devices.

## 6. Plug-in formats and ecosystem

A “plugin system” is not one feature. It includes discovery, scanning, validation, loading, editor windows, automation, state serialization, MIDI/event handling, channel layouts, latency reporting, preset access, crash recovery, and version compatibility.

| Format | Practical role | Evidence and tradeoff |
|---|---|---|
| **VST3** | Broad desktop cross-platform baseline | Steinberg released VST 3.8 under MIT, including MIDI 2.0 interfaces. JUCE lists VST3 hosting. Good candidate for an early desktop host target after basic playback/mixing. |
| **AU / AUv3** | Apple ecosystem | JUCE lists AU and AUv3 hosting; Apple formats are platform-specific. Requires a macOS/iOS plan and platform-specific build/release work. |
| **LV2** | Open plug-in ecosystem, especially relevant to Linux | JUCE lists LV2 hosting; Ardour also documents LV2. Strong fit if Linux is in scope. |
| **CLAP** | Open, cross-platform plugin format | CLAP publishes an MIT license and C ABI/spec. JUCE’s listed built-in host formats do not include CLAP, so support likely means a separate adapter or dependency. |
| **AAX** | Pro Tools ecosystem | Avid’s SDK access is gated by developer terms; commercialization requires contacting Avid for necessary tools/license. Not a reasonable first host target without a deliberate Pro Tools compatibility goal. |
| **VST2** | Legacy compatibility | Steinberg says new VST2 hosting/plugin distribution requires an older signed license; do not make VST2 a new DAWg dependency. |

Sources: [JUCE plug-in support](https://juce.com/features/) · [VST 3.8 licensing](https://steinbergmedia.github.io/vst3_dev_portal/pages/VST%2B3%2BLicensing/VST3%2BLicense.html) · [CLAP license and overview](https://www.cleveraudio.org/1-feature-overview/) · [Avid AAX SDK](https://developer.avid.com/aax/) · [Steinberg VST 2 licensing FAQ](https://steinbergmedia.github.io/vst3_dev_portal/pages/FAQ/Licensing.html)

**Provisional direction:** design the processor and project state model so formats can be added, but defer third-party plug-in hosting until stable audio playback, recording, project saving, and crash-safe state handling exist. Decide the first format after target OS and user plug-in requirements are known. Do not promise VST3+AU+LV2+CLAP in version one just because they are possible in principle.

## 7. Audio quality and delivery

The DAW’s export quality will depend on its signal path, algorithms, and export options—not the number “26” in the language standard and not a marketing claim about a mixer engine.

### Quality factors DAWg should control

- **Internal headroom and clipping:** choose a floating-point mix path and clear metering behavior. Never hide overloads behind silent clipping or automatic normalization.
- **Sample-rate conversion:** define when conversion occurs, use a high-quality resampler, and test it with material that exposes aliasing and transient loss.
- **Time-stretch and pitch-shift:** expose the algorithm and quality/CPU tradeoff; these operations are not neutral and can sound different.
- **Automation and gain changes:** smooth parameter changes to prevent zipper noise and clicks.
- **Plug-ins:** their algorithms, oversampling, channel layout, latency, and implementation often affect the audible output more than the DAW’s empty summing path.
- **Export bit depth and dither:** preserve floating-point or high-resolution output while editing; dither only when reducing bit depth for a final integer output, and avoid applying it repeatedly.
- **Loudness and true peak:** meter them and offer export checks or profiles, but do not force one target across music, broadcast, game audio, and podcasts.
- **Repeatability:** offline renders, stem export, sample-rate conversion, plug-in state, and project version should be reproducible and documented.

Ableton’s audio fact sheet describes sample-rate conversion and time-warping as non-neutral processes, and recommends dither when rendering to a lower bit depth. Reason documents floating-point internal processing and supported I/O formats. These support the conclusion that DAWg should prioritize transparent signal handling and user-visible export controls over “better-sounding engine” claims. [Ableton Audio Fact Sheet](https://www.ableton.com/en/manual/audio-fact-sheet/) · [Reason Audio Basics](https://docs.reasonstudios.com/reason12/audio-basics)

EBU R 128 is one standards-based example for broadcast loudness and true-peak measurement; its −23 LUFS target is for broadcast program normalization, not a universal target for every song or streaming service. [EBU R 128](https://tech.ebu.ch/publications/r128)

**Recommendation:** establish an internal render-quality test suite with known input signals and reference renders. It should cover summing/headroom, channel routing, automation ramps, SRC, time-stretch, integer export/dither, loudness/true-peak analysis, and offline-versus-realtime consistency.

## 8. Licensing and project sustainability

JUCE’s framework modules are dual-licensed under AGPLv3 and a commercial JUCE license. The repository currently fetches JUCE 9.0.4 but DAWg’s own distribution/license plan has not been chosen. This is a product-level decision, not a later packaging detail. [JUCE LICENSE.md](https://github.com/juce-framework/JUCE/blob/master/LICENSE.md)

Other relevant licensing signals:

- VST 3.8 is MIT-licensed; preserve license notices and respect trademarks if branding is used.
- CLAP is MIT-licensed.
- AAX has Avid terms and gated SDK/commercialization steps.
- ASIO support in JUCE is disabled by default; enabling it brings Steinberg ASIO SDK obligations.

**Decision gate:** choose whether DAWg is intended to remain personal/open-source under AGPL-compatible terms, use a paid JUCE license for a closed/commercial path, or evaluate a different platform layer before deep integration. This research is not legal advice; review the actual current license terms before distribution.

## 9. Recommended product decision process

### Stage 1: Define the first producer and first success

Decide whether the first target is primarily:

- a loop/pattern-driven creator who wants to play with ideas quickly;
- an audio recorder who wants dependable multitrack capture and editing;
- a songwriter/producer who alternates between loops, instruments, and live takes;
- or a generalist who wants one tool to cover all three over time.

The initial coding conversation selected audio recording as the first milestone. The owner’s earlier examples point toward loop/pattern play as a meaningful creative origin. DAWg can support both in the long-term plan, but the first-run experience still needs one clear center.

### Stage 2: Compare three interaction prototypes

Use wireframes or small click-through prototypes before adding engine complexity:

1. **Pattern-first:** step/pattern canvas → timeline arrangement → mixer.
2. **Track-first:** create audio/instrument tracks → record/drag clips → arrange → mix, with a fast loop/pattern panel.
3. **Idea-grid hybrid:** launch a grid of clips/scenes, then capture the performance into a familiar timeline.

For each prototype, evaluate the same tasks:

- Start a new project and make a recognizable loop.
- Record an external audio source.
- Add a second part and arrange a verse/chorus.
- Find the track’s mixer controls and understand where its sound goes.
- Add or remove an effect.
- Save, close, and reopen without missing media.
- Export a WAV with a deliberate format and loudness/peak check.

Record task completion, hesitation, misclicks, and the words the user uses to describe the controls. This tests “feels like home” more directly than a feature checklist.

### Stage 3: Lock architecture decisions from demonstrated needs

| Decision | Candidate direction | What evidence should settle it |
|---|---|---|
| Primary arrangement model | One clear default timeline; add pattern/idea capture where tests show it helps | Prototype task results |
| Track-to-mixer relationship | Explicit default mapping with optional advanced routing | Whether users can predict signal flow |
| Audio engine | DAWg-owned project/transport/mix model on top of JUCE device/audio APIs | Playback/recording prototype and latency tests |
| Plug-in support | Architecture-ready; choose first format after OS and existing plug-in needs are known | User’s devices and target platforms |
| Audio quality | Float internal path, robust SRC/export options, measured loudness/true peak, tested dither | Defined export use cases and repeatable render tests |
| Platform | Select initial desktop OS matrix; verify C++26 toolchains early | Actual development machine and distribution intent |
| License | Decide before distributing builds | Open-source vs closed/commercial intent |

## 10. Current recommendations and unresolved decisions

### Recommended now

1. Keep audio recording as the first engineering milestone, as previously selected.
2. Pause additional feature implementation until one primary workflow model is chosen.
3. Treat “eJay immediacy,” “FruityLoops patterns,” and “modern DAW depth” as design inputs, not a request to clone an existing product.
4. Prototype a track-centered default with fast access to loops/pattern creation, then compare it with a true idea grid.
5. Keep JUCE for cross-platform app/audio plumbing while keeping DAWg’s core project, transport, and media behavior under project control.
6. Add a licensing decision and build/toolchain matrix to the next project decision review.

### Not decided yet

- First supported OS and desktop hardware.
- Whether the core user is an electronic/loop producer, a recording musician, or both.
- Whether first projects open in a timeline, pattern workspace, or idea grid.
- How much of the application remains visible in one workspace versus context-sensitive panels.
- Which plug-in formats are required in the first public release.
- JUCE license and DAWg license/distribution model.
- Minimum audio interface, latency, track-count, and project-size targets.
- Which export presets (music, broadcast, game, podcast) are important first.

## 11. Source map and limitations

This report used public vendor documentation and standards pages for product behavior and technical/license claims. Principal sources are linked inline above. Research was performed on 2026-10-29.

This is a desk-research synthesis, not a statistically representative survey or hands-on usability study. Vendor descriptions are used to map workflows, not to endorse the products. The strongest DAWg-specific evidence is the owner’s direct history with eJay, FL Studio, and Reason. The report makes no claim that other users share the same preferences. A hands-on comparison of current versions—especially Reason 14 and the current FL Studio—remains necessary before selecting a final workflow.
