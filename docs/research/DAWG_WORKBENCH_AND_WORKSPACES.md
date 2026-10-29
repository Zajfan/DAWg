# DAWg Workbench and Workspace System

**Status:** Architecture and product proposal; not yet an implementation specification.  
**Date:** 2026-10-30  
**Related:** [DAW workflow prototypes](DAWG_WORKFLOW_PROTOTYPES.md) · [product and technical research](DAW_PRODUCT_AND_TECHNICAL_RESEARCH.md)

## Owner direction

The owner selected the track-first visual only because it resembles the familiar FL Studio working pattern. That is a familiarity signal, not a preference or a decision to make track-first the DAWg default.

The owner wants DAWg to feel like a personal workbench: people should be able to make it suit their work instead of being locked into one preferred layout. DAWg should explore the pattern-first, track-first, and idea-grid models as available workspaces, and allow users to arrange their own workspaces from the same editor panels.

## Product proposal

**Separate the music project from the way it is displayed.**

DAWg should have one project model and several views/workspaces over it. Arrangement, step/pattern editing, idea-grid launching, piano roll, waveform/take editing, and mixing are panels that can be combined and rearranged. A named workspace saves the panel arrangement and view settings, not another copy of the song.

This makes the user's "workbench" personal while keeping edits, audio, instruments, and routing consistent no matter which workspace is open.

### One music model; multiple views

| Project concept | What it owns | Views that can edit or show it |
|---|---|---|
| Track | Name, color, track type, input/monitor state, record arm, default playback behavior | Track list, timeline, launcher, mixer/inspector |
| Clip source | Audio or MIDI events, loop length, source tempo, content identity | Step editor, piano roll, waveform/editor, browser |
| Clip placement | A reference to content plus song or scene position and per-placement properties | Arrangement timeline, scene grid, clip list |
| Scene | A named set of clip-slot references and launch behavior | Idea grid/launcher, section overview |
| Mixer channel / bus | Processing chain, fader, pan, metering, sends, input/output routing | Mixer, track inspector, routing graph |
| Automation | Time-varying values linked to a parameter or control | Timeline lanes, clip editor, automation panel, mixer |
| Workspace | Panel types, docking tree, visibility, size, selection/focus, view-specific settings | Workspace switcher and layout editor |

A clip can be shown in more than one view. A scene grid and song timeline must not create duplicate audio just because the same idea is visible in both. The project model owns the content; views own how the user reaches it.

### Clip and pattern identity rules

The relationship between reusable patterns and arrangement clips must be visible and predictable.

- A launcher slot and an arrangement placement can refer to the same clip source.
- Each placement can have its own position, length, start offset, loop range, gain, and local automation.
- The UI shows a linked/independent indicator for content edits.
- “Duplicate linked” keeps the shared source. “Make independent” creates a copy for a variation.
- Editing shared content explains which linked uses will change. “Make independent” is one undoable action.
- Deleting a slot or placement removes that reference; it does not silently delete the underlying source while another use still refers to it.
- Recording a launcher performance writes clip placements and captured automation into the arrangement. It does not render duplicate audio unless the user explicitly chooses to bounce/render.

**Open interaction decision:** which duplicate action should be the default in the timeline? Prototype both “duplicate linked” and “duplicate independent”; make the current behavior visible in either case.

### Arrangement and launcher playback

Two sequencers can be useful, but simultaneous control of the same track would be confusing. DAWg should make the active playback source explicit per track.

- Arrangement playback is the normal default.
- Launching a scene or clip temporarily takes control of the affected track.
- The track visibly indicates that Launcher playback overrides Arrangement playback.
- A single “Return to Arrangement” action restores the timeline source.
- Recording a launcher performance captures clip launch/stop, scene, and automation events into the arrangement.
- Captured arrangement clips remain editable after recording.

Ableton documents that Session clips and Arrangement clips are independent and mutually exclusive on a track, with Session playback taking precedence and a “Back to Arrangement” action. Bitwig also has independent Arranger and Launcher data, supports copying in either direction, and captures Launcher performances into Arranger tracks. These are proven interaction patterns, but DAWg should make the active source and link behavior more visible than a hidden mode change. [Ableton Session View](https://www.ableton.com/en/manual/session-view/) · [Bitwig two-sequencer model](https://www.bitwig.com/userguide/latest/one_daw_two_sequencers/) · [Bitwig Launcher recording](https://www.bitwig.com/userguide/latest/recording_launcher_clips/)

### Track-to-mixer flexibility

A simple default should not become a permanent restriction.

- Creating an audio or instrument track automatically creates a clear, predictable route to a mixer channel.
- Users may rename or reorder mixer channels independently from timeline tracks.
- Multiple tracks may route to a bus or shared mixer channel.
- Tracks may use sends, direct outputs, submixes, or hardware outputs.
- Multichannel instruments and recorded inputs can expose multiple routing endpoints where supported.
- Selecting a track shows its full signal path and destination, even when the full mixer is hidden.
- The mixer remains a project object, not a panel-specific display mode.

The starter experience can feel simple while retaining a real routing graph underneath. Advanced routing should be available in a routing panel/graph and remain inspectable from the selected track.

## Workspaces users can build

### Built-in starting workspaces

These should be editable starting points, never separate product modes:

| Workspace preset | First-visible panels |
|---|---|
| **Pattern Lab** | Step editor, pattern/source browser, compact tracks, song timeline |
| **Recording** | Arrangement, track list with input/monitor/arm, meters, take lanes, mixer |
| **Song Arrangement** | Arrangement, section markers, track inspector, browser |
| **Idea Grid** | Scene launcher, selected clip editor, arrangement, capture controls |
| **Mixing** | Mixer, routing, meters, selected-track inspector, compact arrangement |
| **Sound Design** | Instrument/device editor, modulation/automation, clip context, browser |
| **Blank Workbench** | Empty panel host with an Add Panel action |

The first-run path could offer a short picker and previews instead of assuming one “correct” default. The owner can always change workspaces later. A user can rename, duplicate, modify, lock, reset, and delete their own workspace.

### Layout controls

A user should be able to:

- dock panels on each side of the main work surface;
- split, resize, tab, show/hide, and float panels;
- move a floating panel to another monitor;
- restore a lost panel from a searchable **Add Panel** menu;
- save and switch named workspaces with a shortcut;
- copy a built-in workspace before customizing it;
- lock a workspace against accidental layout changes;
- reset one workspace or restore the shipped defaults;
- optionally import/export workspace presets.

A command search provides a stable route to actions even when the user has moved or hidden panels. The panel catalog should use task names (“Step Editor”, “Track Routing”, “Take Lanes”) instead of internal class names.

### Global and project workspace data

Use both scopes, with clear ownership:

- **Global workspace**: the user’s reusable panel layout and preferred view settings across projects.
- **Project workspace**: optional arrangement saved with the project, useful when a project is shared or opened on another machine.
- **Project remembers last workspace**: a small preference; users can instead force their preferred global default at open.
- **Portable geometry**: store relative proportions and dock relationships, then clamp/reflow to current screen size. Do not rely on absolute pixel coordinates that strand panels off-screen on a different monitor.
- **Versioned serialization**: unknown panel types or obsolete layout versions should be skipped with a clear recovery option, not block opening the project.

REAPER documents per-project track-view screensets and global window screensets; Cubase supports global and project workspaces. DAWg can adopt the distinction while making layout portability and recovery part of the initial design. [REAPER User Guide](https://www.reaper.fm/userguide.php) · [Cubase Workspaces](https://www.steinberg.help/r/cubase-pro/15.0/en/cubase_nuendo/topics/customizing/customizing_workspaces_c.html) · [Cubase Workspace Types](https://www.steinberg.help/r/cubase-pro/15.0/en/cubase_nuendo/topics/customizing/customizing_workspace_types_c.html)

## Technical architecture proposal

Keep the editor layout system separate from music data:

`ProjectDocument` stores tracks, clip sources, placements, scenes, routing, automation, media, and musical time. `WorkspaceDocument` stores panel types/instances, dock relationships, proportions, visible tabs, focus, and per-panel view state. UI edits issue commands to the project model; audio rendering consumes project state and does not depend on which panels are open.

A serialized workspace could start with:

- schema version, stable workspace ID, display name, scope, and optional key binding;
- a versioned dock/split tree with stable panel type IDs;
- panel instance IDs, visibility, selected-tab state, and normalized proportions;
- per-panel view state such as zoom, selected track, or editor mode;
- display-independent recovery hints for floating windows.

Do not serialize live JUCE component objects or pointers. Reconstruct components from stable IDs and validated configuration. Keep workspace changes undoable where practical, but distinguish layout undo from musical-edit undo.

### JUCE implication

JUCE supplies useful GUI building blocks such as `TabbedComponent`, `MultiDocumentPanel`, `SidePanel`, and `StretchableLayoutManager`. Its documented component set does not present a complete DAW-style docking workspace manager. **Inference:** DAWg should plan to own a thin, versioned docking/layout layer or evaluate a separate docking dependency; JUCE remains the platform/audio toolkit rather than the workspace system itself. Confirm APIs against the exact JUCE version before implementation. [JUCE module and GUI API index](https://docs.juce.com/)

## Feasibility and implementation order

**Yes, the three layouts and more are feasible.** The distinction is in cost:

- Showing the same editor panels in different presets: low-to-moderate UI work.
- Free dock/float/split customization, saved layouts, multi-monitor recovery, and project portability: substantial UI/state work.
- Making launcher and arrangement editing operate on predictable shared clip identities: core project-model work that should precede the launcher.
- Capturing launcher performances, automation, and source switching reliably: substantial sequencing/transport work.
- Letting every individual item in a panel rearrange freely: high complexity and likely unnecessary. Make panels modular first; refine their internal controls from user feedback.

Recommended sequence:

1. Define project objects and stable IDs for tracks, clip sources, placements, scenes, and mixer channels.
2. Build the application shell around a panel host from the start; keep the arrangement and selected editor as separate panels.
3. Implement project-independent panel commands and state; add Arrangement and Pattern Lab as built-in layouts.
4. Add user save/restore, duplicate, reset, and global/project workspace scope before there are many panels.
5. Add clip source linking/independence, then the Idea Grid and explicit active-source indicators.
6. Implement performance capture and routing flexibility.
7. Add more panels/workspace presets as user needs justify them; avoid a permanent mode that hides unrelated capabilities.

### Acceptance criteria for the first workspace milestone

- Switching workspaces never changes audio, clips, routing, automation, or project timing.
- Each workspace can show/hide/rearrange panels without losing editor state.
- A missing panel can be restored by name.
- Arrangement, Pattern Lab, and Recording presets expose the same underlying tracks and media.
- Track inspector always shows destination routing, even when the mixer panel is closed.
- Layout saves/reopens; opening on a smaller or different display recovers safely.
- A custom workspace can be duplicated and reset without changing the project.
- Critical tasks remain reachable through named commands/search if a panel has been moved.

## Decision status

**Direction accepted for further design:** explore configurable workspaces over one shared project model. The owner has not selected a default, final layout set, clip-link default, or implementation dependency. B remains a familiarity reference, not the chosen layout.
