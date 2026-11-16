#pragma once

#include <dawg/engine/AudioBufferView.h>
#include <dawg/engine/dsp/MasteringMeterNode.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace dawg::engine::dsp {

struct StemNormalizationResult
{
    std::string stemName;
    float originalIntegratedLufs {-100.0f};
    float originalTruePeakDb {-100.0f};
    float loudnessRangeLu {0.0f};
    float appliedGainDb {0.0f};
    float finalIntegratedLufs {-100.0f};
    float finalTruePeakDb {-100.0f};
    bool limiterEngaged {false};
};

// Automated Target Loudness Stem Exporter & Mastering Normalization Engine
// Implements ITU-R BS.1770-4 / EBU R128 two-stage gated loudness,
// EBU Tech 3342 Loudness Range (LRA), 4x polyphase oversampled true-peak detection,
// automated target gain normalization, and true-peak brickwall limiting.
class MasteringStemExportEngine
{
public:
    static constexpr std::size_t polyphaseFilterTaps = 48;
    static constexpr std::size_t oversampleFactor = 4;

    MasteringStemExportEngine() noexcept;
    ~MasteringStemExportEngine() = default;

    // Configuration
    void setTargetProfile(MasteringTargetProfile profile) noexcept;
    [[nodiscard]] MasteringTargetProfile targetProfile() const noexcept { return m_profile; }

    void setCustomTarget(float targetLufs, float ceilingTruePeakDb) noexcept;
    [[nodiscard]] float targetLufs() const noexcept { return m_targetLufs; }
    [[nodiscard]] float ceilingTruePeakDb() const noexcept { return m_ceilingTruePeakDb; }

    // Analysis: Measure full ITU-R BS.1770-4 loudness on an audio buffer
    [[nodiscard]] MasteringTelemetry analyzeBuffer(
        const AudioBufferView& buffer,
        double sampleRate) const;

    // Single-buffer / Master track normalization:
    // Measures integrated loudness, applies normalization gain offset (target - integrated),
    // and runs through lookahead true-peak brickwall limiter to ensure ceiling is respected.
    [[nodiscard]] StemNormalizationResult normalizeBuffer(
        AudioBufferView& buffer,
        double sampleRate,
        const std::string& name = "Master");

    // Batch Multi-Stem Export & Normalization:
    // Normalizes each stem in-place and returns per-stem compliance reports.
    // If 'preserveRelativeBalance' is true, all stems receive the uniform gain offset
    // computed from the summed mix bus to keep drum/bass/vocal balances intact.
    [[nodiscard]] std::vector<StemNormalizationResult> normalizeStems(
        std::vector<AudioBufferView>& stems,
        const std::vector<std::string>& stemNames,
        double sampleRate,
        bool preserveRelativeBalance = false);

    // Compute True-Peak sample value via 4x polyphase FIR interpolation
    [[nodiscard]] static float computeInterSampleTruePeak(
        const float* samples,
        std::size_t count) noexcept;

private:
    struct Biquad
    {
        float b0 {1.0f}, b1 {0.0f}, b2 {0.0f};
        float a1 {0.0f}, a2 {0.0f};
        float z1 {0.0f}, z2 {0.0f};

        inline float process(float in) noexcept
        {
            const float out = in * b0 + z1;
            z1 = in * b1 - a1 * out + z2;
            z2 = in * b2 - a2 * out;
            return out;
        }

        inline void reset() noexcept
        {
            z1 = 0.0f;
            z2 = 0.0f;
        }
    };

    static void setupKFilters(double sampleRate, Biquad& preFilter, Biquad& rlbFilter) noexcept;

    // Apply lookahead True-Peak brickwall limiter
    static bool applyTruePeakLimiter(
        AudioBufferView& buffer,
        double sampleRate,
        float ceilingLinear) noexcept;

    MasteringTargetProfile m_profile {MasteringTargetProfile::spotify};
    float m_targetLufs {-14.0f};
    float m_ceilingTruePeakDb {-1.0f};
};

} // namespace dawg::engine::dsp
