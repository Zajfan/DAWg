#pragma once

#include <dawg/engine/AudioBufferView.h>
#include <dawg/engine/ProcessorNode.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace dawg::engine::dsp {

enum class AcousticModelMode : std::uint8_t
{
    waveguideString, // Karplus-Strong Extended (pluck, strike, bow)
    membraneDrum,    // 2D Circular/Rectangular Membrane (Bessel modal modes)
    airColumnBore    // Cylindrical/Conical Acoustic Waveguide (reed & lip-buzz)
};

enum class StringExciterType : std::uint8_t
{
    pluck,
    strikeFeltHammer,
    bowFriction
};

class AcousticPhysicalModelEngine final : public ProcessorNode
{
public:
    static constexpr std::size_t maxDelaySamples = 4096; // Down to ~12 Hz at 48kHz
    static constexpr std::size_t numMembraneModes = 6;

    AcousticPhysicalModelEngine() noexcept;
    ~AcousticPhysicalModelEngine() override = default;

    [[nodiscard]] bool prepare(
        double sampleRate,
        std::size_t maxBlockSize,
        std::uint16_t channelCount) override;

    void process(ProcessContext& context) noexcept override;
    void reset() noexcept override;

    [[nodiscard]] std::string_view name() const noexcept { return "Physical Modeling Acoustic Resonator Suite"; }

    // Model selection
    void setModelMode(AcousticModelMode mode) noexcept;
    [[nodiscard]] AcousticModelMode modelMode() const noexcept;

    // Pitch & Tuning
    void setFrequencyHz(float freqHz) noexcept;
    [[nodiscard]] float frequencyHz() const noexcept;

    // Trigger Note (MIDI Note On simulation)
    void triggerExcitation(float velocity0to1) noexcept;

    // =========================================================================
    // Waveguide String Parameters
    // =========================================================================
    void setStringExciter(StringExciterType exciter) noexcept;
    [[nodiscard]] StringExciterType stringExciter() const noexcept;

    void setStringDamping(float damping0to1) noexcept; // Loop brightness/decay
    [[nodiscard]] float stringDamping() const noexcept;

    void setStringStiffness(float stiffness0to1) noexcept; // Allpass dispersion
    [[nodiscard]] float stringStiffness() const noexcept;

    void setBowPressure(float pressure0to1) noexcept;
    [[nodiscard]] float bowPressure() const noexcept;

    // =========================================================================
    // 2D Membrane Resonator Parameters
    // =========================================================================
    void setMembraneTension(float tension0to1) noexcept;
    [[nodiscard]] float membraneTension() const noexcept;

    void setMembraneDecay(float decayMs) noexcept;
    [[nodiscard]] float membraneDecay() const noexcept;

    // =========================================================================
    // Acoustic Air Column / Bore Parameters
    // =========================================================================
    void setReedStiffness(float stiffness0to1) noexcept;
    [[nodiscard]] float reedStiffness() const noexcept;

    void setBreathPressure(float pressure0to1) noexcept;
    [[nodiscard]] float breathPressure() const noexcept;

    struct ModalBiquad
    {
        float b0 {0.0f}, b1 {0.0f}, b2 {0.0f};
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

private:
    void updateWaveguideDelay() noexcept;
    void updateMembraneModes() noexcept;

    double m_sampleRate {48000.0};

    std::atomic<AcousticModelMode> m_modelMode {AcousticModelMode::waveguideString};
    std::atomic<float> m_freqHz {220.0f};
    std::atomic<StringExciterType> m_stringExciter {StringExciterType::pluck};
    std::atomic<float> m_stringDamping {0.5f};
    std::atomic<float> m_stringStiffness {0.1f};
    std::atomic<float> m_bowPressure {0.4f};
    std::atomic<float> m_membraneTension {0.5f};
    std::atomic<float> m_membraneDecayMs {250.0f};
    std::atomic<float> m_reedStiffness {0.6f};
    std::atomic<float> m_breathPressure {0.5f};

    // Waveguide Delay Line
    std::array<float, maxDelaySamples> m_delayBuffer {};
    std::size_t m_writeIdx {0};
    double m_delayLengthSamples {218.0};

    // Filter states
    float m_lowpassState {0.0f};
    float m_allpassState {0.0f};

    // Membrane Modal Resonator Bank (6 Bessel Modes)
    std::array<ModalBiquad, numMembraneModes> m_membraneFilters {};

    // Excitation Generator State
    std::uint32_t m_exciterCountdown {0};
    float m_exciterVelocity {0.0f};
    std::uint32_t m_rng {0x54321u};
};

} // namespace dawg::engine::dsp
