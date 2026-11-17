#pragma once

#include <dawg/engine/ProcessorNode.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace dawg::engine::dsp {

// Offline and Real-Time Multi-Mic Acoustic Drum Bleed Cancellation & Spill Suppression Engine
// Employs classical Short-Time Fourier Transform (STFT) spectral subtraction with spectral floor control,
// cross-talk transfer coupling estimation, and lookahead phase-coherent sidechain transient gating.
class MultiMicDrumDebleedEngine final
{
public:
    static constexpr std::size_t FftSize = 512;
    static constexpr std::size_t HopSize = FftSize / 4; // 75% overlap (128 samples)
    static constexpr std::size_t NumBins = FftSize / 2 + 1; // 257 bins
    static constexpr std::size_t MaxLookaheadSamples = 256;

    struct Config
    {
        float couplingFactor {0.7f};           // Cross-talk coupling factor alpha (0.0 to 2.0)
        float spectralFloorDb {-30.0f};        // Minimum spectral floor beta in dB (-60 to -12 dB)
        float oversubtraction {1.2f};          // Spectral oversubtraction factor (1.0 to 3.0)
        float duckingDepthDb {-24.0f};         // Sidechain transient gate ducking depth in dB (-60 to 0 dB)
        float lookaheadMs {2.5f};              // Lookahead for transient ducking in ms (0.5 to 5.0 ms)
        float gateHoldMs {15.0f};              // Gate hold time in ms
        float gateReleaseMs {40.0f};           // Gate release time in ms
        float bleedSensitivity {0.5f};         // Bleed track transient detection threshold (0.05 to 1.0)
        float targetSensitivity {0.6f};        // Primary target track transient detection threshold
    };

    MultiMicDrumDebleedEngine() noexcept;
    ~MultiMicDrumDebleedEngine() = default;

    void setConfig(const Config& config) noexcept;
    [[nodiscard]] const Config& config() const noexcept { return m_config; }

    // Real-Time Block Processing Preparation & Execution
    void prepare(double sampleRate, std::size_t maxBlockSize) noexcept;
    void reset() noexcept;

    // Process a block of audio (Zero Heap Allocation)
    // targetIn: microphone being cleaned (e.g. Kick or Tom)
    // bleedRefIn: microphone capturing bleed source (e.g. Snare or Cymbals)
    // targetOut: cleaned drum microphone signal
    void processBlock(
        const float* targetIn,
        const float* bleedRefIn,
        float* targetOut,
        std::size_t numSamples) noexcept;

    // Classical Offline Stem Processing
    // Processes full audio stems with zero phase distortion and optimal overlap-add
    static void processOffline(
        const float* targetTrack,
        const float* bleedRefTrack,
        float* outputTrack,
        std::size_t numSamples,
        double sampleRate,
        const Config& config);

    // Telemetry
    [[nodiscard]] float currentAttenuationDb() const noexcept { return m_currentAttenuationDb; }
    [[nodiscard]] std::size_t latencySamples() const noexcept { return m_lookaheadSamples + HopSize; }

private:
    static void fftRadix2(
        std::array<float, FftSize>& real,
        std::array<float, FftSize>& imag,
        bool inverse) noexcept;

    void processStftFrame() noexcept;

    Config m_config;
    double m_sampleRate {48000.0};
    std::size_t m_lookaheadSamples {120};

    // STFT Analysis / Synthesis Windows and Buffers
    std::array<float, FftSize> m_window {};
    std::array<float, FftSize> m_inputTarget {};
    std::array<float, FftSize> m_inputBleed {};
    std::size_t m_inputPos {0};

    std::array<float, FftSize * 2> m_outputOlaBuffer {};
    std::size_t m_outputReadPos {0};

    // Spectral Bins
    std::array<float, NumBins> m_targetMag {};
    std::array<float, NumBins> m_targetPhase {};
    std::array<float, NumBins> m_bleedMag {};
    std::array<float, NumBins> m_bleedPhase {};
    std::array<float, NumBins> m_cleanedMag {};

    // Transient Detection & Lookahead Sidechain Ducking
    std::array<float, MaxLookaheadSamples + 1024> m_lookaheadRingTarget {};
    std::size_t m_lookaheadRingPos {0};

    float m_bleedEnergyPrev {0.0f};
    float m_targetEnergyPrev {0.0f};
    float m_duckingGain {1.0f};
    float m_holdCounter {0.0f};
    float m_currentAttenuationDb {0.0f};
};

} // namespace dawg::engine::dsp
