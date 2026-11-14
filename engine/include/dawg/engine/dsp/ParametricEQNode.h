#pragma once

#include <dawg/engine/ProcessorNode.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

namespace dawg::engine::dsp {

enum class FilterBandType : std::uint8_t
{
    highPass,
    lowShelf,
    bell,
    highShelf,
    lowPass
};

struct BandSettings
{
    FilterBandType type {FilterBandType::bell};
    double frequencyHz {1000.0};
    double q {0.707};
    double gainDb {0.0};
    bool enabled {true};

    friend bool operator==(const BandSettings&, const BandSettings&) = default;
};

struct ParametricEQSettings
{
    bool bypassed {false};
    std::array<BandSettings, 4> bands {{
        {FilterBandType::highPass, 80.0, 0.707, 0.0, true},
        {FilterBandType::bell, 300.0, 1.0, 0.0, true},
        {FilterBandType::bell, 2500.0, 1.0, 0.0, true},
        {FilterBandType::highShelf, 8000.0, 0.707, 0.0, true}
    }};

    friend bool operator==(const ParametricEQSettings&, const ParametricEQSettings&) = default;
};

class ParametricEQNode final : public ProcessorNode
{
public:
    explicit ParametricEQNode(ParametricEQSettings settings = {}) noexcept;

    void setSettings(const ParametricEQSettings& settings) noexcept;
    [[nodiscard]] const ParametricEQSettings& settings() const noexcept { return m_settings; }

    [[nodiscard]] bool prepare(
        double sampleRate,
        std::size_t maxBlockSize,
        std::uint16_t channelCount) override;

    void reset() noexcept override;

    void process(ProcessContext& context) noexcept override;

private:
    struct BiquadCoeffs
    {
        double b0 {1.0};
        double b1 {0.0};
        double b2 {0.0};
        double a1 {0.0};
        double a2 {0.0};
    };

    struct FilterState
    {
        double z1 {0.0};
        double z2 {0.0};
    };

    static BiquadCoeffs computeCoeffs(
        FilterBandType type,
        double frequencyHz,
        double q,
        double gainDb,
        double sampleRate) noexcept;

    void updateAllCoeffs() noexcept;

    ParametricEQSettings m_settings;
    double m_sampleRate {48000.0};
    std::uint16_t m_channelCount {0};
    std::array<BiquadCoeffs, 4> m_coeffs;
    // States: channel -> 4 bands
    std::vector<std::array<FilterState, 4>> m_states;
};

} // namespace dawg::engine::dsp
