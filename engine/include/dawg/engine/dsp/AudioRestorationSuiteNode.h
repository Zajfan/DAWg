#pragma once

#include <dawg/engine/AudioBufferView.h>
#include <dawg/engine/ProcessorNode.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace dawg::engine::dsp {

enum class RestorationListenMode : std::uint8_t
{
    processed = 0,
    auditionDelta, // Audition only removed noise/clicks/distortion
    bypass
};

enum class MainsHumFrequency : std::uint8_t
{
    hum50Hz = 0,
    hum60Hz
};

struct NotchFilter
{
    float b0 {1.0f}, b1 {0.0f}, b2 {0.0f};
    float a1 {0.0f}, a2 {0.0f};
    float z1L {0.0f}, z2L {0.0f};
    float z1R {0.0f}, z2R {0.0f};

    void reset() noexcept
    {
        z1L = z2L = z1R = z2R = 0.0f;
    }

    [[nodiscard]] inline float processL(float in) noexcept
    {
        const float out = b0 * in + z1L;
        z1L = b1 * in - a1 * out + z2L;
        z2L = b2 * in - a2 * out;
        return out;
    }

    [[nodiscard]] inline float processR(float in) noexcept
    {
        const float out = b0 * in + z1R;
        z1R = b1 * in - a1 * out + z2R;
        z2R = b2 * in - a2 * out;
        return out;
    }
};

class AudioRestorationSuiteNode final : public ProcessorNode
{
public:
    static constexpr std::size_t maxHarmonics = 16;
    static constexpr std::size_t lookaheadFrames = 64;

    AudioRestorationSuiteNode() noexcept;
    ~AudioRestorationSuiteNode() override = default;

    [[nodiscard]] bool prepare(
        double sampleRate,
        std::size_t maxBlockSize,
        std::uint16_t channelCount) override;

    void process(ProcessContext& context) noexcept override;
    void reset() noexcept override;

    [[nodiscard]] std::string_view name() const noexcept { return "Professional Audio Restoration Suite"; }

    // Listen Mode
    void setListenMode(RestorationListenMode mode) noexcept { m_listenMode = mode; }
    [[nodiscard]] RestorationListenMode listenMode() const noexcept { return m_listenMode; }

    // Polynomial De-Clipper
    void setDeClipEnabled(bool enabled) noexcept { m_deClipEnabled = enabled; }
    [[nodiscard]] bool isDeClipEnabled() const noexcept { return m_deClipEnabled; }
    void setClipThresholdDb(float threshDb) noexcept;
    [[nodiscard]] float clipThresholdDb() const noexcept { return m_clipThresholdDb; }
    [[nodiscard]] std::size_t clippedSamplesCount() const noexcept { return m_clippedSamplesCount.load(std::memory_order_relaxed); }

    // Autoregressive De-Clicker
    void setDeClickEnabled(bool enabled) noexcept { m_deClickEnabled = enabled; }
    [[nodiscard]] bool isDeClickEnabled() const noexcept { return m_deClickEnabled; }
    void setClickSensitivity(float sens) noexcept { m_clickSensitivity = std::clamp(sens, 0.1f, 1.0f); }
    [[nodiscard]] float clickSensitivity() const noexcept { return m_clickSensitivity; }
    [[nodiscard]] std::size_t repairedClicksCount() const noexcept { return m_repairedClicksCount.load(std::memory_order_relaxed); }

    // Phase-Locked Hum & Buzz Eliminator
    void setDeHumEnabled(bool enabled) noexcept { m_deHumEnabled = enabled; }
    [[nodiscard]] bool isDeHumEnabled() const noexcept { return m_deHumEnabled; }
    void setMainsFrequency(MainsHumFrequency freq) noexcept;
    [[nodiscard]] MainsHumFrequency mainsFrequency() const noexcept { return m_mainsFrequency; }
    void setHumHarmonicsCount(std::size_t count) noexcept;
    [[nodiscard]] std::size_t humHarmonicsCount() const noexcept { return m_humHarmonicsCount; }
    void setNotchQ(float q) noexcept;
    [[nodiscard]] float notchQ() const noexcept { return m_notchQ; }

private:
    void updateHumFilters() noexcept;
    float repairClipSample(float in, float prev, float next, float thresh) noexcept;

    double m_sampleRate {48000.0};
    std::uint16_t m_channelCount {2};

    RestorationListenMode m_listenMode {RestorationListenMode::processed};

    // De-Clipper
    bool m_deClipEnabled {true};
    float m_clipThresholdDb {-0.5f};
    float m_clipThresholdLinear {0.944f};
    std::atomic<std::size_t> m_clippedSamplesCount {0};

    // De-Clicker
    bool m_deClickEnabled {true};
    float m_clickSensitivity {0.7f};
    std::atomic<std::size_t> m_repairedClicksCount {0};

    // AR(4) prediction history per channel
    std::array<float, 4> m_arHistoryL {};
    std::array<float, 4> m_arHistoryR {};
    std::array<float, 4> m_arCoeffs {1.85f, -1.25f, 0.45f, -0.05f}; // Smooth predictor
    float m_predErrorVarianceL {0.001f};
    float m_predErrorVarianceR {0.001f};

    // De-Hum
    bool m_deHumEnabled {true};
    MainsHumFrequency m_mainsFrequency {MainsHumFrequency::hum50Hz};
    std::size_t m_humHarmonicsCount {8};
    float m_notchQ {30.0f}; // Sharp surgical Q
    std::array<NotchFilter, maxHarmonics> m_humNotches;

    // Lookahead / smoothing buffer
    std::array<float, lookaheadFrames> m_lookBufferL {};
    std::array<float, lookaheadFrames> m_lookBufferR {};
    std::size_t m_lookWriteIdx {0};
};

} // namespace dawg::engine::dsp
