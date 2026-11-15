#pragma once

#include <dawg/engine/AudioBufferView.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace dawg::engine::dsp {

enum class GenreProfile : std::uint8_t
{
    pop,
    electronic,
    hipHop,
    modernRock,
    orchestral
};

struct SpectralBandInfo
{
    float centerFreqHz {1000.0f};
    float targetDb {0.0f};
    float minToleranceDb {-3.5f};
    float maxToleranceDb {3.5f};
    float measuredDb {-60.0f};
    bool inTolerance {true};
};

struct SpectralCollision
{
    std::size_t bandIndex {0};
    float centerFreqHz {100.0f};
    float trackADb {-60.0f};
    float trackBDb {-60.0f};
    float collisionSeverity {0.0f}; // 0.0 (none) to 1.0 (severe masking)
};

struct UnmaskingRecommendation
{
    float centerFreqHz {100.0f};
    float qFactor {2.0f};
    float recommendedGainCutDb {-3.0f};
    std::string description;
};

// Intelligent Spectral Balance & Auto-Mix Assistant (Tonal Balance / Gullfoss style)
// Analyzes master mix spectral distribution against commercial genre target curves,
// detects inter-track masking collisions across 32 bands, and calculates smart dynamic unmasking filters.
class SpectralBalanceAssistant
{
public:
    static constexpr std::size_t bandCount = 32;

    SpectralBalanceAssistant() noexcept;
    ~SpectralBalanceAssistant() = default;

    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    // Genre Target Configuration
    void setGenreProfile(GenreProfile profile) noexcept;
    [[nodiscard]] GenreProfile genreProfile() const noexcept { return m_genre; }
    [[nodiscard]] std::string_view genreName() const noexcept;

    // Real-Time Master Analysis
    void analyzeAudioBlock(const AudioBufferView& buffer) noexcept;
    [[nodiscard]] const std::array<SpectralBandInfo, bandCount>& bandAnalysis() const noexcept { return m_bands; }
    [[nodiscard]] float mixBalanceScore() const noexcept; // 0.0 to 100.0%

    // Multi-Track Collision Radar (Track A vs Track B, e.g. Kick vs 808/Bass or Vocal vs Synth)
    [[nodiscard]] std::vector<SpectralCollision> detectCollisions(
        const std::array<float, bandCount>& spectrumTrackA,
        const std::array<float, bandCount>& spectrumTrackB,
        float thresholdDb = -36.0f) const;

    // Smart Dynamic Unmasking Filter Calculator
    [[nodiscard]] std::vector<UnmaskingRecommendation> calculateSmartUnmasking(
        const std::vector<SpectralCollision>& collisions) const;

    // Helper to get ISO center frequencies
    static float bandCenterFreq(std::size_t index) noexcept;

private:
    void initBands() noexcept;
    void applyGenreTargetCurves() noexcept;

    double m_sampleRate {48000.0};
    GenreProfile m_genre {GenreProfile::pop};
    std::array<SpectralBandInfo, bandCount> m_bands {};
    std::array<float, bandCount> m_smoothedEnergies {};
};

} // namespace dawg::engine::dsp
