# DAWg Workflow Visual Comparison

**Status:** Early visual exploration for review; no interface or workflow has been approved.  
**Generated:** 2026-10-30  
**Task brief:** [DAWG_WORKFLOW_PROTOTYPES.md](DAWG_WORKFLOW_PROTOTYPES.md)

Three 1440×900 concepts were generated to make the workflow differences easier to compare. Each image represents one interaction model. The generated PNG files are supplied separately in the DAWG Workflow Mockups review bundle; the repository holds this comparison note and the task-based prototype brief.

The images are exploratory. Their colors, icons, controls, example content, and layout details are provisional; treat them as workflow sketches rather than implementation specifications.

## A. Pattern-first

The step sequencer is the primary workspace. A named pattern list and song timeline remain visible, with tracks and audio recording controls alongside them.

**Review question:** Does the large step editor make it feel easy to start playing? Is the timeline still visible enough to show where each pattern belongs? Does audio recording feel like a first-class activity?

## B. Track-first

The song timeline is the stable home. A selected clip opens a contextual step editor while the track list, input/monitor controls, effects, and destination remain visible.

**Review question:** Is it clear that each sound belongs to a track and mixer channel? Can the user move between recording, arranging, and editing without losing context? Does the timeline feel too dominant when the user only wants to experiment?

## C. Idea-grid hybrid

A scene launcher provides alternate combinations of clips; the arrangement timeline stays visible, and a clear capture action turns launches into a linear song.

**Review question:** Does the grid help audition song sections before arranging them? Is “Capture to Timeline” understandable? Does the grid add useful freedom or another model the user must learn?

## Initial design reading

- **Pattern-first** makes rhythm creation the clearest first action, but its pattern-versus-timeline relationship needs careful explanation.
- **Track-first** makes audio recording and routing easiest to understand, while the empty-project experience needs enough playfulness to invite experimentation.
- **Idea-grid hybrid** makes section alternatives and capture visible, but introduces the most concepts and uses the most screen area.

These are design observations, not a selection. The next decision should come from trying the three interaction models against the same first-session task list. A combined design should only be considered after identifying which specific task benefits from each model; avoid placing all three full workspaces on screen at once.

## Asset provenance

Twelve candidates were generated: four for each workflow. The comparison bundle contains the selected representatives: pattern-first candidate B, track-first candidate C, and idea-grid candidate A. The remaining candidates were not included in the bundle.