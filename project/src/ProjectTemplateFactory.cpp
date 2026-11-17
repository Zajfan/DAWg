#include <dawg/project/ProjectTemplateFactory.h>

namespace dawg::project {

ProjectSnapshot ProjectTemplateFactory::createTemplate(const TemplateKind kind)
{
    switch (kind)
    {
    case TemplateKind::modernTrap:
        return createModernTrapTemplate();
    case TemplateKind::synthwaveCyberpunk:
        return createSynthwaveCyberpunkTemplate();
    case TemplateKind::multiMicAcousticRock:
        return createMultiMicAcousticRockTemplate();
    case TemplateKind::masteringSession:
        return createMasteringSessionTemplate();
    }
    return createModernTrapTemplate();
}

ProjectSnapshot ProjectTemplateFactory::createModernTrapTemplate()
{
    ProjectSnapshot s;
    s.formatVersion = 3;
    s.name = "Modern Trap Production";
    s.tempo = TempoSetting {.bpm = 140.0, .timeSigNumerator = 4, .timeSigDenominator = 4, .metronomeEnabled = false};
    s.loop = LoopSetting {.enabled = true, .startSample = 0, .endSample = 48000 * 4 * 4}; // 4 bars at 140 bpm approx
    s.punch = PunchSetting {.enabled = false, .punchInSample = 0, .punchOutSample = 48000 * 8, .prerollBars = 1, .countInBars = 1};

    // 1. 808 Sub Bass (with Tape saturation & Ducking Compressor)
    Track trk808 {
        .id = 1,
        .name = "808 Sub Bass",
        .muted = false,
        .solo = false,
        .gainLinear = 0.95f,
        .pan = 0.0f,
        .phaseInvert = false,
        .recordArmed = false,
        .kind = TrackKind::instrument,
        .inserts = {
            DspNodeDescriptor {.slotIndex = 0, .type = DspNodeType::parametricEq, .bypassed = false, .name = "808 Tone EQ", .pluginId = "", .parameters = {40.0f, 3.0f, 250.0f, -4.0f}, .stateData = ""},
            DspNodeDescriptor {.slotIndex = 1, .type = DspNodeType::dynamicsCompressor, .bypassed = false, .name = "Sidechain Ducker", .pluginId = "", .parameters = {4.0f, -14.0f, 10.0f, 60.0f}, .stateData = ""}
        },
        .sends = {},
        .outputBusTrackId = 0,
        .takes = {},
        .takeLanesExpanded = false,
        .parentFolderId = 0,
        .isFolder = false,
        .folderCollapsed = false,
        .compGroupId = 0,
        .isFrozen = false,
        .freezeAudioPath = {},
        .frozenInserts = {}
    };

    // 2. Punchy Kick (sends key to duck 808)
    Track trkKick {
        .id = 2,
        .name = "Punch Kick",
        .muted = false,
        .solo = false,
        .gainLinear = 1.0f,
        .pan = 0.0f,
        .phaseInvert = false,
        .recordArmed = false,
        .kind = TrackKind::audio,
        .inserts = {
            DspNodeDescriptor {.slotIndex = 0, .type = DspNodeType::parametricEq, .bypassed = false, .name = "Kick Thump EQ", .pluginId = "", .parameters = {60.0f, 2.5f, 350.0f, -3.0f}, .stateData = ""}
        },
        .sends = {
            TrackSend {.targetTrackId = 1, .levelLinear = 1.0f, .preFader = false}
        },
        .outputBusTrackId = 0,
        .takes = {},
        .takeLanesExpanded = false,
        .parentFolderId = 0,
        .isFolder = false,
        .folderCollapsed = false,
        .compGroupId = 0,
        .isFrozen = false,
        .freezeAudioPath = {},
        .frozenInserts = {}
    };

    // 3. Trap Claps / Snare
    Track trkSnare {
        .id = 3,
        .name = "Clap / Snare",
        .muted = false,
        .solo = false,
        .gainLinear = 0.90f,
        .pan = 0.0f,
        .phaseInvert = false,
        .recordArmed = false,
        .kind = TrackKind::audio,
        .inserts = {
            DspNodeDescriptor {.slotIndex = 0, .type = DspNodeType::tnhcReverb, .bypassed = false, .name = "Snare Plate", .pluginId = "", .parameters = {0.25f, 0.4f}, .stateData = ""}
        },
        .sends = {},
        .outputBusTrackId = 0,
        .takes = {},
        .takeLanesExpanded = false,
        .parentFolderId = 0,
        .isFolder = false,
        .folderCollapsed = false,
        .compGroupId = 0,
        .isFrozen = false,
        .freezeAudioPath = {},
        .frozenInserts = {}
    };

    // 4. Hi-Hat Rolls (Step Sequencer / Sampler)
    Track trkHats {
        .id = 4,
        .name = "Hi-Hat Rolls",
        .muted = false,
        .solo = false,
        .gainLinear = 0.85f,
        .pan = -0.15f,
        .phaseInvert = false,
        .recordArmed = false,
        .kind = TrackKind::instrument,
        .inserts = {
            DspNodeDescriptor {.slotIndex = 0, .type = DspNodeType::drumPadSampler, .bypassed = false, .name = "Roll Sampler", .pluginId = "", .parameters = {}, .stateData = ""}
        },
        .sends = {},
        .outputBusTrackId = 0,
        .takes = {},
        .takeLanesExpanded = false,
        .parentFolderId = 0,
        .isFolder = false,
        .folderCollapsed = false,
        .compGroupId = 0,
        .isFrozen = false,
        .freezeAudioPath = {},
        .frozenInserts = {}
    };

    // 5. Dark Synth Melody
    Track trkMelody {
        .id = 5,
        .name = "Dark Trap Synth",
        .muted = false,
        .solo = false,
        .gainLinear = 0.80f,
        .pan = 0.10f,
        .phaseInvert = false,
        .recordArmed = true,
        .kind = TrackKind::instrument,
        .inserts = {
            DspNodeDescriptor {.slotIndex = 0, .type = DspNodeType::tnhcSynth, .bypassed = false, .name = "Lead Poly Synth", .pluginId = "", .parameters = {}, .stateData = ""},
            DspNodeDescriptor {.slotIndex = 1, .type = DspNodeType::tnhcDelay, .bypassed = false, .name = "Ping-Pong Echo", .pluginId = "", .parameters = {0.35f, 0.4f}, .stateData = ""}
        },
        .sends = {},
        .outputBusTrackId = 0,
        .takes = {},
        .takeLanesExpanded = false,
        .parentFolderId = 0,
        .isFolder = false,
        .folderCollapsed = false,
        .compGroupId = 0,
        .isFrozen = false,
        .freezeAudioPath = {},
        .frozenInserts = {}
    };

    s.tracks = {trk808, trkKick, trkSnare, trkHats, trkMelody};
    return s;
}

ProjectSnapshot ProjectTemplateFactory::createSynthwaveCyberpunkTemplate()
{
    ProjectSnapshot s;
    s.formatVersion = 3;
    s.name = "Cyberpunk Synthwave";
    s.tempo = TempoSetting {.bpm = 110.0, .timeSigNumerator = 4, .timeSigDenominator = 4, .metronomeEnabled = false};
    s.loop = LoopSetting {.enabled = true, .startSample = 0, .endSample = 48000 * 8};
    s.punch = PunchSetting {.enabled = false, .punchInSample = 0, .punchOutSample = 48000 * 16, .prerollBars = 1, .countInBars = 1};

    // 1. Retro Saw Bass
    Track trkBass {
        .id = 1,
        .name = "Analog Saw Bass",
        .muted = false,
        .solo = false,
        .gainLinear = 0.90f,
        .pan = 0.0f,
        .phaseInvert = false,
        .recordArmed = false,
        .kind = TrackKind::instrument,
        .inserts = {
            DspNodeDescriptor {.slotIndex = 0, .type = DspNodeType::tnhcSynth, .bypassed = false, .name = "Dual Saw Synth", .pluginId = "", .parameters = {}, .stateData = ""},
            DspNodeDescriptor {.slotIndex = 1, .type = DspNodeType::dynamicsCompressor, .bypassed = false, .name = "Punchy Comp", .pluginId = "", .parameters = {3.5f, -12.0f}, .stateData = ""}
        },
        .sends = {},
        .outputBusTrackId = 0,
        .takes = {},
        .takeLanesExpanded = false,
        .parentFolderId = 0,
        .isFolder = false,
        .folderCollapsed = false,
        .compGroupId = 0,
        .isFrozen = false,
        .freezeAudioPath = {},
        .frozenInserts = {}
    };

    // 2. Gated 80s Snare
    Track trkSnare {
        .id = 2,
        .name = "Gated Reverb Snare",
        .muted = false,
        .solo = false,
        .gainLinear = 0.92f,
        .pan = 0.0f,
        .phaseInvert = false,
        .recordArmed = false,
        .kind = TrackKind::audio,
        .inserts = {
            DspNodeDescriptor {.slotIndex = 0, .type = DspNodeType::tnhcReverb, .bypassed = false, .name = "Chamber Reverb", .pluginId = "", .parameters = {0.6f, 0.5f}, .stateData = ""},
            DspNodeDescriptor {.slotIndex = 1, .type = DspNodeType::dynamicsCompressor, .bypassed = false, .name = "Non-Linear Gate", .pluginId = "", .parameters = {8.0f, -18.0f}, .stateData = ""}
        },
        .sends = {},
        .outputBusTrackId = 0,
        .takes = {},
        .takeLanesExpanded = false,
        .parentFolderId = 0,
        .isFolder = false,
        .folderCollapsed = false,
        .compGroupId = 0,
        .isFrozen = false,
        .freezeAudioPath = {},
        .frozenInserts = {}
    };

    // 3. Arpeggiator Synth
    Track trkArp {
        .id = 3,
        .name = "Neon Arpeggio",
        .muted = false,
        .solo = false,
        .gainLinear = 0.80f,
        .pan = -0.25f,
        .phaseInvert = false,
        .recordArmed = false,
        .kind = TrackKind::instrument,
        .inserts = {
            DspNodeDescriptor {.slotIndex = 0, .type = DspNodeType::tnhcSynth, .bypassed = false, .name = "Pluck Synth", .pluginId = "", .parameters = {}, .stateData = ""},
            DspNodeDescriptor {.slotIndex = 1, .type = DspNodeType::tnhcDelay, .bypassed = false, .name = "Stereo Dotted 8th", .pluginId = "", .parameters = {0.35f, 0.5f}, .stateData = ""}
        },
        .sends = {},
        .outputBusTrackId = 0,
        .takes = {},
        .takeLanesExpanded = false,
        .parentFolderId = 0,
        .isFolder = false,
        .folderCollapsed = false,
        .compGroupId = 0,
        .isFrozen = false,
        .freezeAudioPath = {},
        .frozenInserts = {}
    };

    // 4. Synthwave Lead
    Track trkLead {
        .id = 4,
        .name = "Outrun Lead",
        .muted = false,
        .solo = false,
        .gainLinear = 0.85f,
        .pan = 0.15f,
        .phaseInvert = false,
        .recordArmed = true,
        .kind = TrackKind::instrument,
        .inserts = {
            DspNodeDescriptor {.slotIndex = 0, .type = DspNodeType::tnhcSynth, .bypassed = false, .name = "Detuned Lead", .pluginId = "", .parameters = {}, .stateData = ""},
            DspNodeDescriptor {.slotIndex = 1, .type = DspNodeType::tnhcReverb, .bypassed = false, .name = "Lush Plate", .pluginId = "", .parameters = {0.4f, 0.6f}, .stateData = ""}
        },
        .sends = {},
        .outputBusTrackId = 0,
        .takes = {},
        .takeLanesExpanded = false,
        .parentFolderId = 0,
        .isFolder = false,
        .folderCollapsed = false,
        .compGroupId = 0,
        .isFrozen = false,
        .freezeAudioPath = {},
        .frozenInserts = {}
    };

    s.tracks = {trkBass, trkSnare, trkArp, trkLead};
    return s;
}

ProjectSnapshot ProjectTemplateFactory::createMultiMicAcousticRockTemplate()
{
    ProjectSnapshot s;
    s.formatVersion = 3;
    s.name = "Multi-Mic Acoustic Rock Session";
    s.tempo = TempoSetting {.bpm = 124.0, .timeSigNumerator = 4, .timeSigDenominator = 4, .metronomeEnabled = false};
    s.loop = LoopSetting {.enabled = false, .startSample = 0, .endSample = 0};
    s.punch = PunchSetting {.enabled = true, .punchInSample = 48000 * 8, .punchOutSample = 48000 * 24, .prerollBars = 2, .countInBars = 1};

    // Drum Kit with phase-locked comping group 1
    Track trkKickIn {
        .id = 1,
        .name = "Kick In (Boundary)",
        .muted = false,
        .solo = false,
        .gainLinear = 0.95f,
        .pan = 0.0f,
        .phaseInvert = false,
        .recordArmed = false,
        .kind = TrackKind::audio,
        .inserts = {
            DspNodeDescriptor {.slotIndex = 0, .type = DspNodeType::parametricEq, .bypassed = false, .name = "Beater Click EQ", .pluginId = "", .parameters = {3500.0f, 4.0f}, .stateData = ""}
        },
        .sends = {},
        .outputBusTrackId = 0,
        .takes = {},
        .takeLanesExpanded = false,
        .parentFolderId = 0,
        .isFolder = false,
        .folderCollapsed = false,
        .compGroupId = 1,
        .isFrozen = false,
        .freezeAudioPath = {},
        .frozenInserts = {}
    };

    Track trkKickOut {
        .id = 2,
        .name = "Kick Out (Sub)",
        .muted = false,
        .solo = false,
        .gainLinear = 0.90f,
        .pan = 0.0f,
        .phaseInvert = false,
        .recordArmed = false,
        .kind = TrackKind::audio,
        .inserts = {
            DspNodeDescriptor {.slotIndex = 0, .type = DspNodeType::parametricEq, .bypassed = false, .name = "Sub Weight EQ", .pluginId = "", .parameters = {50.0f, 3.0f}, .stateData = ""}
        },
        .sends = {},
        .outputBusTrackId = 0,
        .takes = {},
        .takeLanesExpanded = false,
        .parentFolderId = 0,
        .isFolder = false,
        .folderCollapsed = false,
        .compGroupId = 1,
        .isFrozen = false,
        .freezeAudioPath = {},
        .frozenInserts = {}
    };

    Track trkSnareTop {
        .id = 3,
        .name = "Snare Top",
        .muted = false,
        .solo = false,
        .gainLinear = 1.0f,
        .pan = 0.0f,
        .phaseInvert = false,
        .recordArmed = false,
        .kind = TrackKind::audio,
        .inserts = {
            DspNodeDescriptor {.slotIndex = 0, .type = DspNodeType::dynamicsCompressor, .bypassed = false, .name = "Snare Crack Comp", .pluginId = "", .parameters = {4.0f, -16.0f}, .stateData = ""}
        },
        .sends = {},
        .outputBusTrackId = 0,
        .takes = {},
        .takeLanesExpanded = false,
        .parentFolderId = 0,
        .isFolder = false,
        .folderCollapsed = false,
        .compGroupId = 1,
        .isFrozen = false,
        .freezeAudioPath = {},
        .frozenInserts = {}
    };

    Track trkSnareBottom {
        .id = 4,
        .name = "Snare Bottom (Phase Inverted)",
        .muted = false,
        .solo = false,
        .gainLinear = 0.70f,
        .pan = 0.0f,
        .phaseInvert = true, // Polarity flipped for 180 deg phase alignment with snare top!
        .recordArmed = false,
        .kind = TrackKind::audio,
        .inserts = {
            DspNodeDescriptor {.slotIndex = 0, .type = DspNodeType::parametricEq, .bypassed = false, .name = "Sizzle High-Pass", .pluginId = "", .parameters = {400.0f, 0.0f}, .stateData = ""}
        },
        .sends = {},
        .outputBusTrackId = 0,
        .takes = {},
        .takeLanesExpanded = false,
        .parentFolderId = 0,
        .isFolder = false,
        .folderCollapsed = false,
        .compGroupId = 1,
        .isFrozen = false,
        .freezeAudioPath = {},
        .frozenInserts = {}
    };

    Track trkOverheadL {
        .id = 5,
        .name = "Overhead L",
        .muted = false,
        .solo = false,
        .gainLinear = 0.85f,
        .pan = -0.80f,
        .phaseInvert = false,
        .recordArmed = false,
        .kind = TrackKind::audio,
        .inserts = {},
        .sends = {},
        .outputBusTrackId = 0,
        .takes = {},
        .takeLanesExpanded = false,
        .parentFolderId = 0,
        .isFolder = false,
        .folderCollapsed = false,
        .compGroupId = 1,
        .isFrozen = false,
        .freezeAudioPath = {},
        .frozenInserts = {}
    };

    Track trkOverheadR {
        .id = 6,
        .name = "Overhead R",
        .muted = false,
        .solo = false,
        .gainLinear = 0.85f,
        .pan = 0.80f,
        .phaseInvert = false,
        .recordArmed = false,
        .kind = TrackKind::audio,
        .inserts = {},
        .sends = {},
        .outputBusTrackId = 0,
        .takes = {},
        .takeLanesExpanded = false,
        .parentFolderId = 0,
        .isFolder = false,
        .folderCollapsed = false,
        .compGroupId = 1,
        .isFrozen = false,
        .freezeAudioPath = {},
        .frozenInserts = {}
    };

    Track trkLeadVocal {
        .id = 7,
        .name = "Lead Vocal (Comping Ready)",
        .muted = false,
        .solo = false,
        .gainLinear = 1.0f,
        .pan = 0.0f,
        .phaseInvert = false,
        .recordArmed = true,
        .kind = TrackKind::audio,
        .inserts = {
            DspNodeDescriptor {.slotIndex = 0, .type = DspNodeType::parametricEq, .bypassed = false, .name = "Vocal High-Pass 80Hz", .pluginId = "", .parameters = {80.0f, 0.0f}, .stateData = ""},
            DspNodeDescriptor {.slotIndex = 1, .type = DspNodeType::dynamicsCompressor, .bypassed = false, .name = "Opto Vocal Comp", .pluginId = "", .parameters = {3.0f, -14.0f}, .stateData = ""},
            DspNodeDescriptor {.slotIndex = 2, .type = DspNodeType::tnhcReverb, .bypassed = false, .name = "Plate Space", .pluginId = "", .parameters = {0.3f, 0.4f}, .stateData = ""}
        },
        .sends = {},
        .outputBusTrackId = 0,
        .takes = {},
        .takeLanesExpanded = true, // Takes expanded by default for instant vocal comping
        .parentFolderId = 0,
        .isFolder = false,
        .folderCollapsed = false,
        .compGroupId = 0,
        .isFrozen = false,
        .freezeAudioPath = {},
        .frozenInserts = {}
    };

    s.tracks = {trkKickIn, trkKickOut, trkSnareTop, trkSnareBottom, trkOverheadL, trkOverheadR, trkLeadVocal};
    return s;
}

ProjectSnapshot ProjectTemplateFactory::createMasteringSessionTemplate()
{
    ProjectSnapshot s;
    s.formatVersion = 3;
    s.name = "Mastering Suite Session";
    s.tempo = TempoSetting {.bpm = 120.0, .timeSigNumerator = 4, .timeSigDenominator = 4, .metronomeEnabled = false};
    s.loop = LoopSetting {.enabled = true, .startSample = 0, .endSample = 48000 * 16};
    s.punch = PunchSetting {.enabled = false, .punchInSample = 0, .punchOutSample = 0, .prerollBars = 0, .countInBars = 0};

    // 1. Unmastered Mix Ingest Track
    Track trkMix {
        .id = 1,
        .name = "Unmastered Mix Audio",
        .muted = false,
        .solo = false,
        .gainLinear = 1.0f,
        .pan = 0.0f,
        .phaseInvert = false,
        .recordArmed = false,
        .kind = TrackKind::audio,
        .inserts = {
            DspNodeDescriptor {.slotIndex = 0, .type = DspNodeType::parametricEq, .bypassed = false, .name = "Linear-Phase Polish EQ", .pluginId = "", .parameters = {30.0f, 0.0f, 12000.0f, 1.5f}, .stateData = ""},
            DspNodeDescriptor {.slotIndex = 1, .type = DspNodeType::dynamicsCompressor, .bypassed = false, .name = "Stereo Glue Bus Comp", .pluginId = "", .parameters = {2.0f, -8.0f, 30.0f, 100.0f}, .stateData = ""},
            DspNodeDescriptor {.slotIndex = 2, .type = DspNodeType::brickwallLimiter, .bypassed = false, .name = "Master Brickwall Limiter (-1.0 dBTP)", .pluginId = "", .parameters = {-1.0f}, .stateData = ""}
        },
        .sends = {},
        .outputBusTrackId = 0,
        .takes = {},
        .takeLanesExpanded = false,
        .parentFolderId = 0,
        .isFolder = false,
        .folderCollapsed = false,
        .compGroupId = 0,
        .isFrozen = false,
        .freezeAudioPath = {},
        .frozenInserts = {}
    };

    // 2. Commercial Reference Track (A/B Mix Check)
    Track trkRef {
        .id = 2,
        .name = "Commercial Reference Track",
        .muted = true, // Muted initially for instant un-mute A/B comparison
        .solo = false,
        .gainLinear = 0.85f, // Pre-gain matched for equal-loudness reference check
        .pan = 0.0f,
        .phaseInvert = false,
        .recordArmed = false,
        .kind = TrackKind::audio,
        .inserts = {},
        .sends = {},
        .outputBusTrackId = 0,
        .takes = {},
        .takeLanesExpanded = false,
        .parentFolderId = 0,
        .isFolder = false,
        .folderCollapsed = false,
        .compGroupId = 0,
        .isFrozen = false,
        .freezeAudioPath = {},
        .frozenInserts = {}
    };

    s.tracks = {trkMix, trkRef};
    return s;
}

} // namespace dawg::project
