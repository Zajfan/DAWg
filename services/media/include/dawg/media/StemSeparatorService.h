#pragma once

#include <dawg/project/ProjectTypes.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace dawg::media {

struct SeparatedStems
{
    std::vector<float> drumsL;
    std::vector<float> drumsR;
    std::vector<float> bassL;
    std::vector<float> bassR;
    std::vector<float> vocalsL;
    std::vector<float> vocalsR;
    std::vector<float> otherL;
    std::vector<float> otherR;
};

struct StemSeparationConfig
{
    float percussiveSensitivity {1.0f}; // HPSS transient vs harmonic sensitivity
    float bassCutoffHz {180.0f};        // Low-end crossover for bass vs vocals
    float vocalLowHz {200.0f};          // Vocal fundamental lower bound
    float vocalHighHz {4500.0f};        // Vocal formant upper bound
    float centerAzimuthWidth {0.35f};   // Phantom center width (1.0 = wide, 0.2 = strict center)
    bool perfectSumReconstruction {true}; // Guarantees Drums + Bass + Vocals + Other == Mix
};

// Pure DSP Harmonic-Percussive & Azimuth Spectral Stem Splitter (Algorithmic Demixer - RX / Cedar style)
// 100% deterministic, transparent algorithmic unmixing without black-box neural networks or generative AI.
// Combines directional HPSS median filtering, stereo azimuth unmixing, and zero-phase soft Wiener masking.
class StemSeparatorService final
{
public:
    StemSeparatorService() = default;
    ~StemSeparatorService() = default;

    // 4-Stem Pure DSP Spectral Decomposition (Drums, Bass, Vocals, Other)
    [[nodiscard]] static SeparatedStems separate4Stems(
        const float* left,
        const float* right,
        std::size_t numSamples,
        double sampleRate);

    [[nodiscard]] static SeparatedStems separate4StemsConfigurable(
        const float* left,
        const float* right,
        std::size_t numSamples,
        double sampleRate,
        const StemSeparationConfig& config);

    // Dedicated Pure DSP Vocal Extraction & Instrumental Karaoke Removal
    [[nodiscard]] static std::pair<std::vector<float>, std::vector<float>> extractVocals(
        const float* left,
        const float* right,
        std::size_t numSamples,
        double sampleRate,
        float centerFocus = 0.8f);

    [[nodiscard]] static std::pair<std::vector<float>, std::vector<float>> removeVocals(
        const float* left,
        const float* right,
        std::size_t numSamples,
        double sampleRate,
        float centerFocus = 0.8f);
};

struct ExtractedGrooveNote
{
    std::uint8_t pitch {36};
    std::uint8_t velocity {100};
    std::uint64_t startSample {0};
    std::uint64_t lengthSamples {2400};
};

class GrooveExtractor final
{
public:
    GrooveExtractor() = default;
    ~GrooveExtractor() = default;

    [[nodiscard]] static std::vector<ExtractedGrooveNote> extractGroove(
        const float* audio,
        std::size_t numSamples,
        double sampleRate,
        double bpm);
};

} // namespace dawg::media
