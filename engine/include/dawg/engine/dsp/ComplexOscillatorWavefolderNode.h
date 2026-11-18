#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <string_view>

#include "dawg/engine/ProcessorNode.h"

namespace dawg::engine::dsp {

enum class LpgMode : uint8_t
{
    LowPass = 0,
    VCA,
    Combo // Simultaneous filter & amplitude dynamics
};

enum class ModulatorWaveform : uint8_t
{
    Sine = 0,
    Triangle,
    Sawtooth,
    Square
};

class ComplexOscillatorWavefolderNode final : public ProcessorNode
{
public:
    ComplexOscillatorWavefolderNode() noexcept;
    ~ComplexOscillatorWavefolderNode() override = default;

    [[nodiscard]] std::string_view name() const noexcept { return "West-Coast Complex Oscillator & Wavefolder"; }

    [[nodiscard]] bool prepare(
        double sampleRate,
        std::size_t maxBlockSize,
        std::uint16_t channelCount) override;

    void process(ProcessContext& context) noexcept override;
    void reset() noexcept override;

    // Carrier Oscillator Controls
    void setCarrierFrequency(float freqHz) noexcept { m_carrierFreq = std::clamp(freqHz, 10.0f, 20000.0f); }
    [[nodiscard]] float carrierFrequency() const noexcept { return m_carrierFreq; }

    // Modulator Oscillator Controls
    void setModulatorFrequency(float freqHz) noexcept { m_modulatorFreq = std::clamp(freqHz, 0.1f, 10000.0f); }
    [[nodiscard]] float modulatorFrequency() const noexcept { return m_modulatorFreq; }

    void setModulatorWaveform(ModulatorWaveform wave) noexcept { m_modWaveform = wave; }
    [[nodiscard]] ModulatorWaveform modulatorWaveform() const noexcept { return m_modWaveform; }

    // Through-Zero Linear FM
    void setFmIndex(float index) noexcept { m_fmIndex = std::clamp(index, 0.0f, 10.0f); }
    [[nodiscard]] float fmIndex() const noexcept { return m_fmIndex; }

    // Multi-Stage Diode Wavefolder
    void setFoldAmount(float fold) noexcept { m_foldAmount = std::clamp(fold, 0.0f, 1.0f); }
    [[nodiscard]] float foldAmount() const noexcept { return m_foldAmount; }

    void setFoldSymmetry(float symmetry) noexcept { m_foldSymmetry = std::clamp(symmetry, -1.0f, 1.0f); }
    [[nodiscard]] float foldSymmetry() const noexcept { return m_foldSymmetry; }

    void setFoldStages(uint8_t stages) noexcept { m_foldStages = std::clamp<uint8_t>(stages, 1, 4); }
    [[nodiscard]] uint8_t foldStages() const noexcept { return m_foldStages; }

    // Buchla 292 Vactrol Low-Pass Gate (LPG)
    void setLpgMode(LpgMode mode) noexcept { m_lpgMode = mode; }
    [[nodiscard]] LpgMode lpgMode() const noexcept { return m_lpgMode; }

    void strikeLpg(float velocity = 1.0f) noexcept;
    void setLpgDecayMs(float decayMs) noexcept { m_lpgDecayMs = std::clamp(decayMs, 10.0f, 5000.0f); }
    [[nodiscard]] float lpgDecayMs() const noexcept { return m_lpgDecayMs; }

    void setLpgOpen(float openAmount) noexcept { m_lpgManualOpen = std::clamp(openAmount, 0.0f, 1.0f); }
    [[nodiscard]] float lpgOpen() const noexcept { return m_lpgManualOpen; }

private:
    double m_sampleRate {48000.0};

    // Oscillator states
    float m_carrierFreq {220.0f};
    float m_modulatorFreq {110.0f};
    ModulatorWaveform m_modWaveform {ModulatorWaveform::Sine};
    float m_fmIndex {0.0f};

    float m_carrierPhase {0.0f};
    float m_modulatorPhase {0.0f};

    // Wavefolder parameters
    float m_foldAmount {0.0f};
    float m_foldSymmetry {0.0f};
    uint8_t m_foldStages {4};

    // Vactrol LPG physical modeling
    LpgMode m_lpgMode {LpgMode::Combo};
    float m_lpgDecayMs {180.0f};
    float m_lpgManualOpen {1.0f};
    float m_vactrolState {1.0f};
    float m_vactrolTarget {1.0f};
    float m_lpgFilterStateL {0.0f};
    float m_lpgFilterStateR {0.0f};

    [[nodiscard]] float generateModulatorSample() noexcept;
    [[nodiscard]] float processWavefolder(float input) const noexcept;
    [[nodiscard]] float processVactrolLpg(float input, float& filterState) noexcept;
};

} // namespace dawg::engine::dsp
