#pragma once

#include <dawg/engine/AudioBufferView.h>
#include <dawg/engine/ProcessorNode.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace dawg::engine::dsp {

struct MultibandBandSettings
{
    bool bypassed {false};
    bool solo {false};
    bool mute {false};
    float thresholdDb {-18.0f};  // -60 to 0 dB
    float ratio {4.0f};          // 1.0 to 20.0
    float attackMs {10.0f};      // 0.1 to 200 ms
    float releaseMs {100.0f};    // 10 to 1200 ms
    float makeupGainDb {0.0f};   // -12 to +24 dB

    friend bool operator==(const MultibandBandSettings&, const MultibandBandSettings&) = default;
};

// 4-Band Linear-Phase Linkwitz-Riley Dynamics Compressor
// Splits audio into 4 frequency bands (Low, Low-Mid, High-Mid, High)
// using 24 dB/oct Linkwitz-Riley (LR4) crossovers that sum with flat magnitude.
// Each band features independent dynamics compression, solo, mute, and makeup gain.
class MultibandCompressorNode final : public ProcessorNode
{
public:
    static constexpr std::size_t numBands = 4;

    MultibandCompressorNode() noexcept;
    ~MultibandCompressorNode() override = default;

    [[nodiscard]] bool prepare(
        double sampleRate,
        std::size_t maxBlockSize,
        std::uint16_t channelCount) override;

    void reset() noexcept override;
    [[nodiscard]] std::uint32_t latencySamples() const noexcept override { return 0; }
    void process(ProcessContext& context) noexcept override;

    // Crossover frequency controls
    void setCrossoverFrequencies(float f1Hz, float f2Hz, float f3Hz) noexcept;
    [[nodiscard]] float crossover1Hz() const noexcept { return m_crossover1Hz; }
    [[nodiscard]] float crossover2Hz() const noexcept { return m_crossover2Hz; }
    [[nodiscard]] float crossover3Hz() const noexcept { return m_crossover3Hz; }

    // Band settings
    void setBandSettings(std::size_t bandIdx, const MultibandBandSettings& settings) noexcept;
    [[nodiscard]] const MultibandBandSettings& bandSettings(std::size_t bandIdx) const noexcept;

    // Gain reduction telemetry in dB (0.0 to 30.0 dB GR) for visual metering
    [[nodiscard]] float gainReductionDb(std::size_t bandIdx) const noexcept
    {
        if (bandIdx < numBands) {
            return m_gainReductionMeters[bandIdx].load(std::memory_order_relaxed);
        }
        return 0.0f;
    }

private:
    struct BiquadCoeffs
    {
        float b0 {1.0f};
        float b1 {0.0f};
        float b2 {0.0f};
        float a1 {0.0f};
        float a2 {0.0f};
    };

    struct BiquadState
    {
        float z1 {0.0f};
        float z2 {0.0f};

        inline float process(float in, const BiquadCoeffs& c) noexcept
        {
            float out = c.b0 * in + z1;
            z1 = c.b1 * in - c.a1 * out + z2;
            z2 = c.b2 * in - c.a2 * out;
            return out;
        }

        void reset() noexcept
        {
            z1 = 0.0f;
            z2 = 0.0f;
        }
    };

    // 4th order Linkwitz-Riley filter = 2 cascaded 2nd-order Butterworth stages
    struct LR4Filter
    {
        BiquadCoeffs stageCoeffs {};
        std::array<BiquadState, 2> stages {};

        inline float process(float in) noexcept
        {
            float s1 = stages[0].process(in, stageCoeffs);
            return stages[1].process(s1, stageCoeffs);
        }

        void reset() noexcept
        {
            stages[0].reset();
            stages[1].reset();
        }
    };

    struct CrossoverPair
    {
        LR4Filter lp {};
        LR4Filter hp {};

        void updateCoeffs(float freqHz, double sampleRate) noexcept;
        void reset() noexcept
        {
            lp.reset();
            hp.reset();
        }
    };

    struct BandRuntime
    {
        MultibandBandSettings settings {};
        float attackCoeff {0.1f};
        float releaseCoeff {0.01f};
        float makeupLinear {1.0f};
        float grDb {0.0f};
    };

    void updateCrossovers() noexcept;
    void updateBallistics(std::size_t bandIdx) noexcept;

    double m_sampleRate {48000.0};
    float m_crossover1Hz {80.0f};    // Low / Low-Mid split
    float m_crossover2Hz {650.0f};   // Low-Mid / High-Mid split
    float m_crossover3Hz {4500.0f};  // High-Mid / High split

    std::array<BandRuntime, numBands> m_bands {};
    std::array<std::atomic<float>, numBands> m_gainReductionMeters {};

    // Stereo crossover network: 3 split pairs per channel
    // pair[0] = crossover 2 (splits into Low-Half and High-Half)
    // pair[1] = crossover 1 (splits Low-Half into Band 0 & Band 1)
    // pair[2] = crossover 3 (splits High-Half into Band 2 & Band 3)
    std::array<CrossoverPair, 3> m_crossoversL {};
    std::array<CrossoverPair, 3> m_crossoversR {};
};

} // namespace dawg::engine::dsp
