#pragma once

#include <dawg/engine/AudioBufferView.h>
#include <dawg/engine/ProcessorNode.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace dawg::engine::dsp {

enum class SpeakerTargetCurve : std::uint8_t
{
    flat,              // Strict 0 dB reference across entire audible band
    harmanInRoom,      // Harman listener target (+6dB bass shelf, -0.8dB/oct treble)
    bk1974,            // Bruel & Kjaer 1974 house curve (+3dB bass, -3dB treble)
    auratoneMonoCheck  // 5" Auratone 5C / Grotbox phone check (250Hz - 5kHz bandpass)
};

struct CalibrationSweepConfig
{
    float startFreqHz {20.0f};
    float endFreqHz {20000.0f};
    float durationSeconds {4.0f};
    float amplitude {0.707f};
    float taperSeconds {0.05f};
};

// Studio Monitor Calibration & Inverted Minimum-Phase Room Correction Utility
// Implements synchronized logarithmic sine-sweep chirp generation, microphone impulse response
// deconvolution, 31-band 1/3-octave smoothed magnitude inversion with safety headroom limits,
// and reference target curve compensation for studio monitors and mixing desks.
class StudioMonitorCalibrationNode final : public ProcessorNode
{
public:
    static constexpr std::size_t kNumBands = 31;
    static constexpr std::array<float, kNumBands> kIsoCenterFrequencies {
        20.0f, 25.0f, 31.5f, 40.0f, 50.0f, 63.0f, 80.0f, 100.0f,
        125.0f, 160.0f, 200.0f, 250.0f, 315.0f, 400.0f, 500.0f, 630.0f,
        800.0f, 1000.0f, 1250.0f, 1600.0f, 2000.0f, 2500.0f, 3150.0f, 4000.0f,
        5000.0f, 6300.0f, 8000.0f, 10000.0f, 12500.0f, 16000.0f, 20000.0f
    };

    StudioMonitorCalibrationNode() noexcept;
    ~StudioMonitorCalibrationNode() override = default;

    [[nodiscard]] bool prepare(
        double sampleRate,
        std::size_t maxBlockSize,
        std::uint16_t channelCount) override;

    void reset() noexcept override;
    [[nodiscard]] std::uint32_t latencySamples() const noexcept override { return 0; }
    void process(ProcessContext& context) noexcept override;

    // Target Curve Selection
    void setTargetCurve(SpeakerTargetCurve curve) noexcept;
    [[nodiscard]] SpeakerTargetCurve targetCurve() const noexcept { return m_targetCurve; }

    // Calibration Control & Inversion
    void setCalibrationEnabled(bool enabled) noexcept { m_enabled = enabled; }
    [[nodiscard]] bool isCalibrationEnabled() const noexcept { return m_enabled; }

    void setMaxBoostDb(float maxBoost) noexcept { m_maxBoostDb = std::clamp(maxBoost, 0.0f, 12.0f); updateFilters(); }
    [[nodiscard]] float maxBoostDb() const noexcept { return m_maxBoostDb; }

    void setMaxCutDb(float maxCut) noexcept { m_maxCutDb = std::clamp(maxCut, -30.0f, 0.0f); updateFilters(); }
    [[nodiscard]] float maxCutDb() const noexcept { return m_maxCutDb; }

    // Generate synchronized logarithmic sine chirp sweep
    [[nodiscard]] static std::vector<float> generateLogChirpSweep(
        double sampleRate,
        const CalibrationSweepConfig& config = {});

    // Deconvolve microphone recording with inverse sweep to obtain Room Impulse Response (RIR)
    [[nodiscard]] static std::vector<float> deconvolveImpulseResponse(
        const float* recordedMic,
        std::size_t sampleCount,
        double sampleRate,
        const CalibrationSweepConfig& config = {});

    // Calibrate from measured Room Impulse Response
    bool calibrateFromImpulseResponse(
        const float* impulseResponse,
        std::size_t sampleCount);

    // Manual / telemetry inspection of band corrections
    [[nodiscard]] float getBandCorrectionDb(std::size_t bandIdx) const noexcept;
    [[nodiscard]] float getMeasuredBandDb(std::size_t bandIdx) const noexcept;
    [[nodiscard]] float getTargetBandDb(std::size_t bandIdx) const noexcept;

private:
    struct BiquadCoeffs
    {
        float b0 {1.0f}, b1 {0.0f}, b2 {0.0f};
        float a1 {0.0f}, a2 {0.0f};
    };

    struct BiquadState
    {
        float z1 {0.0f}, z2 {0.0f};
        inline float process(float in, const BiquadCoeffs& c) noexcept
        {
            const float out = in * c.b0 + z1;
            z1 = in * c.b1 - c.a1 * out + z2;
            z2 = in * c.b2 - c.a2 * out;
            return out;
        }
        inline void reset() noexcept { z1 = 0.0f; z2 = 0.0f; }
    };

    void updateFilters() noexcept;
    static BiquadCoeffs designPeakingEQ(double sampleRate, float freqHz, float gainDb, float Q) noexcept;
    static BiquadCoeffs designBandpass(double sampleRate, float freqHz, float Q) noexcept;

    double m_sampleRate {48000.0};
    bool m_enabled {true};
    SpeakerTargetCurve m_targetCurve {SpeakerTargetCurve::flat};
    float m_maxBoostDb {9.0f};
    float m_maxCutDb {-20.0f};

    std::array<float, kNumBands> m_measuredDb {};
    std::array<float, kNumBands> m_targetDb {};
    std::array<float, kNumBands> m_correctionDb {};

    std::array<BiquadCoeffs, kNumBands> m_filterCoeffs {};
    std::array<BiquadState, kNumBands> m_filterStatesL {};
    std::array<BiquadState, kNumBands> m_filterStatesR {};

    // Auratone 5C bandpass filters
    BiquadCoeffs m_auratoneHpCoeff {};
    BiquadCoeffs m_auratoneLpCoeff {};
    BiquadState m_auratoneHpStateL {}, m_auratoneHpStateR {};
    BiquadState m_auratoneLpStateL {}, m_auratoneLpStateR {};
};

} // namespace dawg::engine::dsp
