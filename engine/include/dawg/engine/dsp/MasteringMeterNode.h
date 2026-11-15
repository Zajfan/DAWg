#pragma once

#include <dawg/engine/AudioBufferView.h>
#include <dawg/engine/ProcessorNode.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace dawg::engine::dsp {

enum class MasteringTargetProfile : std::uint8_t
{
    spotify,      // -14 LUFS, -1.0 dBTP
    appleMusic,   // -16 LUFS, -1.0 dBTP
    youTube,      // -14 LUFS, -1.0 dBTP
    clubFestival, // -9 LUFS,  -0.1 dBTP
    ebuR128,      // -23 LUFS, -1.0 dBTP
    cdMaster      // -10 LUFS, -0.2 dBTP
};

struct MasteringTargetSpec
{
    const char* name;
    float targetLufs;
    float targetTruePeakDb;
};

[[nodiscard]] constexpr MasteringTargetSpec getMasteringTargetSpec(MasteringTargetProfile profile) noexcept
{
    switch (profile)
    {
    case MasteringTargetProfile::spotify:
        return {"Spotify", -14.0f, -1.0f};
    case MasteringTargetProfile::appleMusic:
        return {"Apple Music", -16.0f, -1.0f};
    case MasteringTargetProfile::youTube:
        return {"YouTube", -14.0f, -1.0f};
    case MasteringTargetProfile::clubFestival:
        return {"Club / Festival", -9.0f, -0.1f};
    case MasteringTargetProfile::ebuR128:
        return {"EBU R128 Broadcast", -23.0f, -1.0f};
    case MasteringTargetProfile::cdMaster:
        return {"CD Master", -10.0f, -0.2f};
    }
    return {"Spotify", -14.0f, -1.0f};
}

struct MasteringTelemetry
{
    float momentaryLufs {-100.0f};
    float shortTermLufs {-100.0f};
    float integratedLufs {-100.0f};
    float loudnessRangeLu {0.0f};
    float truePeakDb {-100.0f};
    float maxTruePeakDb {-100.0f};
    float correlation {-1.0f}; // -1.0 (out of phase) to +1.0 (mono)
    bool isTruePeakClipping {false};
};

// Commercial Mastering Suite & Loudness Radar Engine
// Fully compliant with ITU-R BS.1770-4 / EBU R128 loudness standards,
// 4x oversampled true-peak detection, stereo goniometer, and spectrum analysis.
class MasteringMeterNode final : public ProcessorNode
{
public:
    MasteringMeterNode() noexcept;
    ~MasteringMeterNode() override = default;

    [[nodiscard]] bool prepare(
        double sampleRate,
        std::size_t maxBlockSize,
        std::uint16_t channelCount) override;

    void reset() noexcept override;
    [[nodiscard]] std::uint32_t latencySamples() const noexcept override { return 0; }
    void process(ProcessContext& context) noexcept override;

    // Target Profile
    void setTargetProfile(MasteringTargetProfile profile) noexcept { m_targetProfile = profile; }
    [[nodiscard]] MasteringTargetProfile targetProfile() const noexcept { return m_targetProfile; }
    [[nodiscard]] MasteringTargetSpec currentTargetSpec() const noexcept { return getMasteringTargetSpec(m_targetProfile); }

    // Telemetry & Meters
    [[nodiscard]] MasteringTelemetry telemetry() const noexcept { return m_telemetry; }
    void resetIntegrated() noexcept;

    // Goniometer / Lissajous scope vector data (latest stereo samples)
    static constexpr std::size_t goniometerBufferSize = 256;
    struct VectorPoint { float x; float y; };
    void getGoniometerPoints(VectorPoint* destination, std::size_t count) const noexcept;

    // 512-band FFT / Spectrum Magnitude Readout
    static constexpr std::size_t spectrumBands = 64;
    void getSpectrumMagnitudes(float* destination, std::size_t count) const noexcept;

    // Loudness Radar History (history of momentary LUFS over the radar circle)
    static constexpr std::size_t radarHistorySize = 128;
    void getRadarHistory(float* destination, std::size_t count) const noexcept;

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

    void setupKWeightingFilters(double sampleRate) noexcept;

    double m_sampleRate {48000.0};
    MasteringTargetProfile m_targetProfile {MasteringTargetProfile::spotify};
    MasteringTelemetry m_telemetry {};

    // K-weighting Stage 1 (Pre-filter high shelf) and Stage 2 (RLB high pass) for L and R
    Biquad m_preFilterL;
    Biquad m_preFilterR;
    Biquad m_rlbFilterL;
    Biquad m_rlbFilterR;

    // Sliding ring buffers for Momentary (400ms) and Short-term (3s) power sum
    std::vector<float> m_momentaryBuffer;
    std::size_t m_momentaryPos {0};
    double m_momentarySum {0.0};

    std::vector<float> m_shortTermBuffer;
    std::size_t m_shortTermPos {0};
    double m_shortTermSum {0.0};

    // Integrated loudness gating accumulators
    double m_integratedEnergySum {0.0};
    std::uint64_t m_integratedSampleCount {0};

    // Goniometer ring buffer
    std::array<VectorPoint, goniometerBufferSize> m_goniometerBuffer {};
    std::size_t m_goniometerWritePos {0};

    // Radar circular history ring buffer
    std::array<float, radarHistorySize> m_radarHistory {};
    std::size_t m_radarWritePos {0};
    std::size_t m_radarSubsampleCounter {0};

    // Spectrum magnitude bins
    std::array<float, spectrumBands> m_spectrumMagnitudes {};
};

} // namespace dawg::engine::dsp
