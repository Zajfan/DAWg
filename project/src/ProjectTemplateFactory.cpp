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

    // 4 bars at 140 BPM: 1 beat = 20571 samples, 1 bar = 82286 samples, 4 bars = 329143 samples
    const std::uint64_t barLen = 82286;
    const std::uint64_t stepLen = barLen / 16;

    // 1. 808 Sub Bass MIDI Sequence with Pitch Slides
    MidiClip clip808;
    clip808.id = 1;
    clip808.trackId = trk808.id;
    clip808.startSample = 0;
    clip808.lengthSamples = barLen * 4;

    const std::uint8_t bassPitches[4] = {36, 36, 41, 39};
    NoteId nId = 1;
    for (std::size_t bar = 0; bar < 4; ++bar)
    {
        const std::uint8_t p = bassPitches[bar];
        // Downbeat hit
        clip808.notes.push_back(MidiNote {
            .id = nId++, .pitch = p, .velocity = 115,
            .startSample = bar * barLen, .lengthSamples = stepLen * 5,
            .vibratoDepth = 0.0f, .vibratoRateHz = 5.0f, .vibratoDelayMs = 0.0f,
            .slideSemitones = (bar == 1 || bar == 3) ? 12.0f : 0.0f
        });
        // Syncopated hit
        clip808.notes.push_back(MidiNote {
            .id = nId++, .pitch = p, .velocity = 105,
            .startSample = bar * barLen + stepLen * 6, .lengthSamples = stepLen * 4,
            .vibratoDepth = 0.0f, .vibratoRateHz = 5.0f, .vibratoDelayMs = 0.0f,
            .slideSemitones = 0.0f
        });
        // Pickup hit
        clip808.notes.push_back(MidiNote {
            .id = nId++, .pitch = static_cast<std::uint8_t>(p + 2), .velocity = 95,
            .startSample = bar * barLen + stepLen * 11, .lengthSamples = stepLen * 3,
            .vibratoDepth = 0.0f, .vibratoRateHz = 5.0f, .vibratoDelayMs = 0.0f,
            .slideSemitones = 0.0f
        });
    }

    // 2. Dark Trap Melody Pluck Sequence
    MidiClip clipMelody;
    clipMelody.id = 2;
    clipMelody.trackId = trkMelody.id;
    clipMelody.startSample = 0;
    clipMelody.lengthSamples = barLen * 4;

    const std::uint8_t melodyPattern[16] = {
        72, 75, 74, 72, 79, 75, 74, 72, 71, 74, 72, 71, 75, 74, 72, 67
    };
    NoteId melId = 1;
    for (std::size_t bar = 0; bar < 4; ++bar)
    {
        for (std::size_t step = 0; step < 16; ++step)
        {
            clipMelody.notes.push_back(MidiNote {
                .id = melId++,
                .pitch = melodyPattern[step],
                .velocity = static_cast<std::uint8_t>(85 + (step % 4 == 0 ? 20 : 0)),
                .startSample = bar * barLen + step * stepLen,
                .lengthSamples = static_cast<std::uint64_t>(static_cast<double>(stepLen) * 0.85),
                .vibratoDepth = (step == 4 || step == 12) ? 0.4f : 0.0f,
                .vibratoRateHz = 6.0f,
                .vibratoDelayMs = 80.0f,
                .slideSemitones = 0.0f
            });
        }
    }

    s.midiClips = {std::move(clip808), std::move(clipMelody)};
    return s;
}

ProjectSnapshot ProjectTemplateFactory::createSynthwaveCyberpunkTemplate()
{
    ProjectSnapshot s;
    s.formatVersion = 3;
    s.name = "Cyberpunk Synthwave";
    s.tempo = TempoSetting {.bpm = 110.0, .timeSigNumerator = 4, .timeSigDenominator = 4, .metronomeEnabled = false};
    s.loop = LoopSetting {.enabled = true, .startSample = 0, .endSample = 837816}; // 8 bars at 110 bpm
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

    // 3. Arpeggiator Synth (Access Virus TI Polar)
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
            DspNodeDescriptor {.slotIndex = 0, .type = DspNodeType::virusTiPolarSynth, .bypassed = false, .name = "Virus TI Polar (Arp)", .pluginId = "", .parameters = {}, .stateData = ""},
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

    // 4. Synthwave Lead (Access Virus TI Polar Hypersaw)
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
            DspNodeDescriptor {.slotIndex = 0, .type = DspNodeType::virusTiPolarSynth, .bypassed = false, .name = "Virus TI Polar (Hypersaw Lead)", .pluginId = "", .parameters = {}, .stateData = ""},
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

    // 8 bars at 110 BPM: 1 beat = 26182 samples, 1 bar = 104727 samples, 8 bars = 837816 samples
    const std::uint64_t barLen = 104727;
    const std::uint64_t step16 = barLen / 16; // 6545 samples

    // 1. Rolling 16th-Note Analog Bassline (Track 1)
    MidiClip clipBass;
    clipBass.id = 10;
    clipBass.trackId = trkBass.id;
    clipBass.startSample = 0;
    clipBass.lengthSamples = barLen * 8;

    const std::uint8_t bassProg[8] = {38, 38, 34, 34, 41, 41, 36, 38}; // D1, D1, Bb0, Bb0, F1, F1, C1, D1
    NoteId bId = 1;
    for (std::size_t bar = 0; bar < 8; ++bar)
    {
        const std::uint8_t root = bassProg[bar];
        for (std::size_t step = 0; step < 16; ++step)
        {
            // Alternate root and octave on 16th notes for authentic synthwave drive
            const std::uint8_t p = (step % 2 == 1) ? static_cast<std::uint8_t>(root + 12) : root;
            const std::uint8_t vel = (step % 4 == 0) ? 110 : ((step % 2 == 0) ? 95 : 85);
            clipBass.notes.push_back(MidiNote {
                .id = bId++,
                .pitch = p,
                .velocity = vel,
                .startSample = bar * barLen + step * step16,
                .lengthSamples = static_cast<std::uint64_t>(static_cast<double>(step16) * 0.80),
                .vibratoDepth = 0.0f,
                .vibratoRateHz = 5.0f,
                .vibratoDelayMs = 0.0f,
                .slideSemitones = 0.0f
            });
        }
    }

    // 2. Rapid 16th-Note Arpeggiator on Virus TI Polar (Track 3)
    MidiClip clipArp;
    clipArp.id = 11;
    clipArp.trackId = trkArp.id;
    clipArp.startSample = 0;
    clipArp.lengthSamples = barLen * 8;

    const std::uint8_t arpChords[4][4] = {
        {50, 57, 62, 65}, // Dm:  D3, A3, D4, F4
        {46, 53, 58, 62}, // Bb:  Bb2, F3, Bb3, D4
        {41, 48, 53, 57}, // F:   F2, C3, F3, A3
        {48, 55, 60, 64}  // C:   C3, G3, C4, E4
    };
    NoteId aId = 1;
    for (std::size_t bar = 0; bar < 8; ++bar)
    {
        const auto& chord = arpChords[(bar / 2) % 4];
        for (std::size_t step = 0; step < 16; ++step)
        {
            const std::uint8_t p = chord[step % 4];
            clipArp.notes.push_back(MidiNote {
                .id = aId++,
                .pitch = p,
                .velocity = static_cast<std::uint8_t>(80 + (step % 4 == 0 ? 25 : 0)),
                .startSample = bar * barLen + step * step16,
                .lengthSamples = static_cast<std::uint64_t>(static_cast<double>(step16) * 0.75),
                .vibratoDepth = 0.0f,
                .vibratoRateHz = 6.0f,
                .vibratoDelayMs = 0.0f,
                .slideSemitones = 0.0f
            });
        }
    }

    // 3. Cyberpunk Hypersaw Lead Solo on Virus TI Polar (Track 4)
    MidiClip clipLead;
    clipLead.id = 12;
    clipLead.trackId = trkLead.id;
    clipLead.startSample = 0;
    clipLead.lengthSamples = barLen * 8;

    NoteId lId = 1;
    // Bar 1: Soaring D4 sustain
    clipLead.notes.push_back(MidiNote {.id = lId++, .pitch = 62, .velocity = 115, .startSample = 0 * barLen, .lengthSamples = step16 * 14, .vibratoDepth = 0.6f, .vibratoRateHz = 5.5f, .vibratoDelayMs = 180.0f, .slideSemitones = 0.0f});
    // Bar 2: F4 -> E4 -> D4
    clipLead.notes.push_back(MidiNote {.id = lId++, .pitch = 65, .velocity = 110, .startSample = 1 * barLen, .lengthSamples = step16 * 6, .vibratoDepth = 0.4f, .vibratoRateHz = 5.5f, .vibratoDelayMs = 100.0f, .slideSemitones = 0.0f});
    clipLead.notes.push_back(MidiNote {.id = lId++, .pitch = 64, .velocity = 105, .startSample = 1 * barLen + step16 * 6, .lengthSamples = step16 * 4, .vibratoDepth = 0.0f, .vibratoRateHz = 5.5f, .vibratoDelayMs = 0.0f, .slideSemitones = 0.0f});
    clipLead.notes.push_back(MidiNote {.id = lId++, .pitch = 62, .velocity = 112, .startSample = 1 * barLen + step16 * 10, .lengthSamples = step16 * 6, .vibratoDepth = 0.5f, .vibratoRateHz = 5.5f, .vibratoDelayMs = 150.0f, .slideSemitones = 0.0f});
    // Bar 3: Bb4 climax with slide
    clipLead.notes.push_back(MidiNote {.id = lId++, .pitch = 70, .velocity = 120, .startSample = 2 * barLen, .lengthSamples = step16 * 12, .vibratoDepth = 0.7f, .vibratoRateHz = 6.0f, .vibratoDelayMs = 200.0f, .slideSemitones = 2.0f});
    clipLead.notes.push_back(MidiNote {.id = lId++, .pitch = 69, .velocity = 100, .startSample = 2 * barLen + step16 * 12, .lengthSamples = step16 * 4, .vibratoDepth = 0.0f, .vibratoRateHz = 5.5f, .vibratoDelayMs = 0.0f, .slideSemitones = 0.0f});
    // Bar 4: A4 -> G4 -> F4 -> E4
    clipLead.notes.push_back(MidiNote {.id = lId++, .pitch = 69, .velocity = 108, .startSample = 3 * barLen, .lengthSamples = step16 * 4, .vibratoDepth = 0.3f, .vibratoRateHz = 5.5f, .vibratoDelayMs = 100.0f, .slideSemitones = 0.0f});
    clipLead.notes.push_back(MidiNote {.id = lId++, .pitch = 67, .velocity = 105, .startSample = 3 * barLen + step16 * 4, .lengthSamples = step16 * 4, .vibratoDepth = 0.3f, .vibratoRateHz = 5.5f, .vibratoDelayMs = 100.0f, .slideSemitones = 0.0f});
    clipLead.notes.push_back(MidiNote {.id = lId++, .pitch = 65, .velocity = 110, .startSample = 3 * barLen + step16 * 8, .lengthSamples = step16 * 4, .vibratoDepth = 0.3f, .vibratoRateHz = 5.5f, .vibratoDelayMs = 100.0f, .slideSemitones = 0.0f});
    clipLead.notes.push_back(MidiNote {.id = lId++, .pitch = 64, .velocity = 100, .startSample = 3 * barLen + step16 * 12, .lengthSamples = step16 * 4, .vibratoDepth = 0.4f, .vibratoRateHz = 5.5f, .vibratoDelayMs = 100.0f, .slideSemitones = 0.0f});
    // Bar 5: High C5 peak
    clipLead.notes.push_back(MidiNote {.id = lId++, .pitch = 72, .velocity = 125, .startSample = 4 * barLen, .lengthSamples = step16 * 12, .vibratoDepth = 0.8f, .vibratoRateHz = 6.0f, .vibratoDelayMs = 150.0f, .slideSemitones = 0.0f});
    clipLead.notes.push_back(MidiNote {.id = lId++, .pitch = 69, .velocity = 102, .startSample = 4 * barLen + step16 * 12, .lengthSamples = step16 * 4, .vibratoDepth = 0.0f, .vibratoRateHz = 5.5f, .vibratoDelayMs = 0.0f, .slideSemitones = 0.0f});
    // Bar 6: A4 -> G4 -> E4
    clipLead.notes.push_back(MidiNote {.id = lId++, .pitch = 69, .velocity = 108, .startSample = 5 * barLen, .lengthSamples = step16 * 6, .vibratoDepth = 0.3f, .vibratoRateHz = 5.5f, .vibratoDelayMs = 100.0f, .slideSemitones = 0.0f});
    clipLead.notes.push_back(MidiNote {.id = lId++, .pitch = 67, .velocity = 105, .startSample = 5 * barLen + step16 * 6, .lengthSamples = step16 * 4, .vibratoDepth = 0.0f, .vibratoRateHz = 5.5f, .vibratoDelayMs = 0.0f, .slideSemitones = 0.0f});
    clipLead.notes.push_back(MidiNote {.id = lId++, .pitch = 64, .velocity = 112, .startSample = 5 * barLen + step16 * 10, .lengthSamples = step16 * 6, .vibratoDepth = 0.5f, .vibratoRateHz = 5.5f, .vibratoDelayMs = 120.0f, .slideSemitones = 0.0f});
    // Bar 7: F4 -> E4 -> C#4 leading tone
    clipLead.notes.push_back(MidiNote {.id = lId++, .pitch = 65, .velocity = 108, .startSample = 6 * barLen, .lengthSamples = step16 * 6, .vibratoDepth = 0.3f, .vibratoRateHz = 5.5f, .vibratoDelayMs = 100.0f, .slideSemitones = 0.0f});
    clipLead.notes.push_back(MidiNote {.id = lId++, .pitch = 64, .velocity = 105, .startSample = 6 * barLen + step16 * 6, .lengthSamples = step16 * 4, .vibratoDepth = 0.0f, .vibratoRateHz = 5.5f, .vibratoDelayMs = 0.0f, .slideSemitones = 0.0f});
    clipLead.notes.push_back(MidiNote {.id = lId++, .pitch = 61, .velocity = 110, .startSample = 6 * barLen + step16 * 10, .lengthSamples = step16 * 6, .vibratoDepth = 0.4f, .vibratoRateHz = 5.5f, .vibratoDelayMs = 100.0f, .slideSemitones = 0.0f});
    // Bar 8: Final D4 home resolution
    clipLead.notes.push_back(MidiNote {.id = lId++, .pitch = 62, .velocity = 120, .startSample = 7 * barLen, .lengthSamples = step16 * 16, .vibratoDepth = 0.7f, .vibratoRateHz = 5.5f, .vibratoDelayMs = 250.0f, .slideSemitones = 0.0f});

    s.midiClips = {std::move(clipBass), std::move(clipArp), std::move(clipLead)};
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
