#pragma once

#include <dawg/project/ProjectSnapshot.h>

#include <string>

namespace dawg::project {

enum class TemplateKind : std::uint8_t
{
    modernTrap,
    synthwaveCyberpunk,
    multiMicAcousticRock,
    masteringSession
};

class ProjectTemplateFactory final
{
public:
    [[nodiscard]] static ProjectSnapshot createTemplate(TemplateKind kind);

    // 1. Modern Trap / Beatmaker: 140 BPM, 808 Sub with tape saturation,
    // Kick with sidechain routing to 808 ducking bus, Trap Hi-Hats, Snare/Clap, Melody synth.
    [[nodiscard]] static ProjectSnapshot createModernTrapTemplate();

    // 2. Synthwave / Cyberpunk: 110 BPM, Analog Bass with ladder filter,
    // Retro Synth Lead with stereo delay, Arpeggiator, Gated Reverb Snare, Plate Reverb aux return.
    [[nodiscard]] static ProjectSnapshot createSynthwaveCyberpunkTemplate();

    // 3. Multi-Mic Acoustic Rock: 124 BPM, Kick In/Out, Snare Top/Bottom (phase-aligned),
    // Overheads L/R, Bass DI/Amp, Rhythm Guitars L/R, Vocal Comping Track with take lanes.
    [[nodiscard]] static ProjectSnapshot createMultiMicAcousticRockTemplate();

    // 4. Mastering Session: Reference Track, Mix Bus (A/B Scene comparison),
    // Parametric EQ, Multi-band Compressor, Loudness Radar Analyzer, Brickwall Limiter.
    [[nodiscard]] static ProjectSnapshot createMasteringSessionTemplate();
};

} // namespace dawg::project
