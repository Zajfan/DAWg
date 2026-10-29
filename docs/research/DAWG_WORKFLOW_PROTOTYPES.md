# DAWg First-Session Workflow Prototypes

**Status:** Comparative product prototype; no workflow has been selected.  
**Date:** 2026-10-30  
**Related research:** [DAW Product and Technical Research](DAW_PRODUCT_AND_TECHNICAL_RESEARCH.md)

## Purpose

Compare three ways DAWg could help a new project move from first idea to recorded, arranged, and exported audio:

1. Pattern-first
2. Track-first
3. Idea-grid hybrid

The goal is not to choose by feature count. Use the same creative tasks in each prototype and observe which structure feels easiest to understand, where it slows down, and whether the user can predict what the DAW will do.

The owner’s remembered eJay and FruityLoops experience points to playful immediacy. Later frustration with modern FL Studio and with Reason points to a need for a stable, understandable home base. Audio recording has already been selected as the first engineering milestone. The prototypes must therefore let the user explore ideas while still exercising a real recording workflow.

## Shared test brief

**Project:** Create a short electronic/songwriter idea called “First Light.”

**Starting conditions:** New empty project; audio interface already selected; headphones connected; no plug-ins installed or required; default tempo 100 BPM, 4/4, 48 kHz; one simple stock drum sound and one stock bass instrument available. These are test fixtures, not locked product decisions.

**Tasks in every prototype:**

1. Start playback and make an 8-bar drum groove.
2. Add a bass part and create a variation for the second half.
3. Record a four-bar guitar or vocal take beginning at bar 9.
4. Make an intro and verse structure from the material already created.
5. Add reverb to the recorded track, then answer: “Where is the reverb, and where does this track go?”
6. Save, close, reopen, and confirm the audio is present.
7. Export a WAV and identify its sample rate, bit depth, and destination.

Use the same device and project data in all three. Give the participant only the task brief; do not coach unless they are stuck. Record completion time, errors, help requests, verbalized uncertainty, and whether the participant can explain track-to-mixer routing.

## Prototype A — Pattern-first

### The promise

**Make a groove immediately.** The first surface is a step sequencer and pattern editor. A song timeline is always available as the place to arrange recorded patterns and audio.

### Main screen regions

| Region | Contents |
|---|---|
| Header | Project name, tempo, meter, play/stop/record, loop, save status |
| Pattern palette | Named patterns such as Drums A, Drums B, Bass A; add/duplicate/rename |
| Pattern editor | Step grid with instrument lanes, velocity, bar length, swing, and audition |
| Song timeline | Horizontal bars; each pattern or recorded audio clip appears on its own named track |
| Track strip | Instrument/audio identity, arm, monitor, mute/solo, level, destination channel |
| Mixer drawer | Track channels, sends, master, meters; opens without replacing the timeline |

### Task path

- Add a drum pattern from the pattern palette and enter steps in the grid.
- Duplicate the pattern, change a few hits, and name it “Drums B.”
- Add a bass pattern in a piano-roll editor; the pattern card stays visible while editing.
- Place patterns into the timeline at bars 1 and 5.
- Create or select an audio track, arm it, and record at bar 9.
- Add reverb from the track strip’s effect slot; show whether it is an insert or send.
- Arrange intro/verse by moving and duplicating pattern and audio clips on the timeline.
- Save and export through the standard project/export commands.

### Expected strengths

- Lowest friction for beat and loop creation.
- Pattern reuse and variation are explicit concepts.
- Preserves the remembered “play first” impulse associated with eJay and early FruityLoops.
- A clear pattern editor can make rhythm editing faster than starting in an empty timeline.

### Expected risks

- Audio recording may feel like an exception to the primary pattern model.
- Users may wonder whether editing one pattern changes every occurrence or only one clip.
- Two objects—pattern source and timeline occurrence—must have understandable identity and linking behavior.
- A crowded pattern palette can become another place users must search.

### Questions this prototype must answer

- Does a pattern feel like a musical object or like an extra layer to manage?
- Can the user predict whether a pattern edit affects every use?
- Does recording a live take feel as natural and visible as creating a pattern?

## Prototype B — Track-first

### The promise

**Every sound has a clear home.** The song timeline is the primary workspace. A track can hold recorded audio or clips from a pattern/step editor, but the same track remains the organizing unit.

### Main screen regions

| Region | Contents |
|---|---|
| Header | Project name, tempo, meter, play/stop/record, loop, save status |
| Track list | Named audio/instrument tracks with arm, monitor, mute/solo, meter, and destination |
| Timeline | Audio, MIDI, and loop clips aligned to bars; section markers for intro/verse/chorus |
| Context editor | Piano roll, waveform, or step sequencer opens for the selected clip while the song context remains visible |
| Inspector | Selected track or clip properties, inserts, sends, gain, color, and clear routing destination |
| Mixer drawer | Full channel strips and master; each strip visibly maps to a track |

### Task path

- Create a drum instrument track from an obvious “Add Track” action.
- Open that track’s step editor in the context editor; create an 8-bar groove.
- Duplicate the clip on the timeline, open its copy in the step editor, and make a variation.
- Add a bass instrument track and create a MIDI clip using a piano roll.
- Add an audio track, select its input, arm, monitor, and record beginning at bar 9.
- Select the audio track; add reverb in the inspector and see its destination channel.
- Use section markers and timeline edit operations to build intro and verse.
- Save and export using a single, consistent project workflow.

### Expected strengths

- Recording, clip editing, and arrangement share one organizing model.
- Track identity and mixer destination are directly visible.
- The timeline remains a stable home base as projects grow.
- The user can alternate between piano roll, step editor, and waveform without moving the underlying material to a separate mode.

### Expected risks

- Starting from an empty track list may feel less playful than a dedicated idea surface.
- Too many track types or inline editors could make the timeline visually dense.
- The distinction between linked pattern and independent clip still needs a simple rule.
- The instrument browser and track setup must not become a configuration wall.

### Questions this prototype must answer

- Can the user make a groove quickly without the timeline feeling like “work”?
- Is it obvious how to open the right editor for each selected clip?
- Can the track list remain compact and legible with both audio and instrument tracks?

## Prototype C — Idea-grid hybrid

### The promise

**Collect alternatives first; turn them into a song when ready.** A small grid of scenes and clips sits beside the timeline. The user can launch combinations, then capture a performance into the arrangement.

### Main screen regions

| Region | Contents |
|---|---|
| Header | Project name, tempo, meter, play/stop/record, global quantization, capture status |
| Idea grid | Rows are tracks; columns are scenes such as Intro, Verse, Chorus; cells hold audio or MIDI clips |
| Timeline | Linear song arrangement; capture performance writes a continuous arrangement of launched clips |
| Selected clip editor | Step editor, piano roll, or waveform for a grid cell or arrangement clip |
| Track inspector | Track instrument/input, effects, sends, and destination; consistent across grid and timeline |
| Mixer drawer | Full channels and master; tracks remain mapped to channels |

### Task path

- Create a drum row and add an 8-bar clip to the first scene.
- Add a second scene, duplicate the drum clip, and vary it.
- Create a bass row and add a bass clip to each scene.
- Launch the first scene, then record a four-bar live take into the selected track.
- Launch scenes in order and capture the performance into the timeline.
- Build intro/verse by editing the captured arrangement or moving scene blocks directly.
- Add reverb from the track inspector and confirm routing in the mixer drawer.
- Save and export using the same flow as the other prototypes.

### Expected strengths

- Makes alternatives easy to audition before committing to a fixed arrangement.
- Connects pattern, sample, instrument, and recorded audio in the same idea space.
- Capturing a performance can translate improvisation into a timeline faster than arranging every clip manually.
- Could bridge playful creation and conventional song structure.

### Expected risks

- Two sequencers can create two sources of truth, especially if clips diverge after capture.
- Scene launch, global quantization, capture, and timeline editing add concepts before they may be needed.
- The grid takes screen space and may reduce the visible timeline on smaller displays.
- The capture operation needs unmistakable feedback: what was recorded, where it landed, and whether the grid remains linked.

### Questions this prototype must answer

- Does launching and capturing save meaningful effort for this user?
- Is the grid useful before there are multiple sections, or does it delay starting?
- Can users understand the relationship between a grid cell, its source clip, and its captured timeline occurrence?

## Comparison rubric

Rate each item from 1 (poor) to 5 (excellent) after each run. Add a note and observed evidence for every rating; do not score from memory alone.

| Criterion | What to observe |
|---|---|
| Time to first sound | Time from new project to hearing a playable groove |
| Musical momentum | Whether the participant keeps experimenting or stops to configure |
| Learnability | Number of unexplained concepts needed to finish the tasks |
| Recording confidence | Whether input, monitoring, record arm, and take outcome are clear |
| Arrangement confidence | Whether intro/verse structure can be created without losing material |
| Signal-flow clarity | Whether the participant correctly identifies insert/send and output destination |
| Edit predictability | Whether duplicate/link/variation behavior matches expectation |
| Navigation effort | Number of view changes, searches, and backtracking actions |
| Recovery confidence | Whether save/reopen preserves the work and makes status clear |
| Export confidence | Whether format, destination, and completion are visible and understandable |
| Small-screen resilience | Whether essential controls remain usable at 1280×720 and 1920×1080 |
| Comfort / “feels like home” | Participant’s own rating and words; do not infer from task speed |

Do not calculate a single weighted winner yet. The owner’s comfort rating matters alongside task completion. A fast but uncomfortable workflow is not a successful result.

## Prototype hypotheses to validate

- **A (Pattern-first)** may win on immediate rhythm creation and lose on recording identity.
- **B (Track-first)** may win on recording, routing, and continuity and lose on playful first contact.
- **C (Idea-grid hybrid)** may win on exploring alternatives and lose on conceptual clarity or screen space.
- A fourth outcome is possible: track-first as the permanent home base, with a compact step editor and optional idea grid that can be opened when needed. Treat this as a hypothesis, not a compromise selected in advance.

## Decision rule

Choose a primary workflow only after the owner tries all three against the same task brief. The first version should prioritize the strongest, most comfortable path while retaining architecture room for the others. A workflow is not “best” because it has the most functions; it is best for DAWg if it makes the owner’s real work feel clear, quick, and dependable.

## What this prototype does not decide

- The final visual style, color palette, typography, or branding.
- Whether DAWg should ultimately be pattern-first, track-first, or hybrid.
- The first supported operating system or audio driver.
- Plug-in formats, licensing, or final engine architecture.
- A claim of audio-quality superiority.

## Visual mockup step

The owner connected the 12ui account and three visual concepts were generated for comparison. Selected representatives and review notes are described in [DAWG_WORKFLOW_VISUALS.md](DAWG_WORKFLOW_VISUALS.md). The images are exploratory concepts, not an approved design or implemented interface. The current GitHub file integration supports UTF-8 text only, so generated PNGs are supplied separately as a review bundle rather than committed as repository assets.


## Owner clarification: layout flexibility

The owner selected the track-first visual because it is familiar from time spent in FruityLoops/FL Studio; this is not a preference ranking or final choice. The product direction is to explore all three layouts and more as customizable workspaces over the same project. See [DAWG_WORKBENCH_AND_WORKSPACES.md](DAWG_WORKBENCH_AND_WORKSPACES.md).
