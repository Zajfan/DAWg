#include "dawg/engine/dsp/ComplexOscillatorWavefolderNode.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace dawg::engine::dsp {

ComplexOscillatorWavefolderNode::ComplexOscillatorWavefolderNode() noexcept
{
    reset();
}

bool ComplexOscillatorWavefolderNode::prepare(
    double sampleRate,
    [[maybe_unused]] std::size_t maxBlockSize,
    [[maybe_unused]] std::uint16_t channelCount)
{
    m_sampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    reset();
    return true;
}

void ComplexOscillatorWavefolderNode::reset() noexcept
{
    m_carrierPhase = 0.0f;
    m_modulatorPhase = 0.0f;
    m_vactrolState = m_lpgManualOpen;
    m_vactrolTarget = 0.0f;
    m_lpgFilterStateL = 0.0f;
    m_lpgFilterStateR = 0.0f;
}

void ComplexOscillatorWavefolderNode::strikeLpg(float velocity) noexcept
{
    m_vactrolTarget = std::clamp(velocity, 0.0f, 1.0f);
}

float ComplexOscillatorWavefolderNode::generateModulatorSample() noexcept
{
    float sample = 0.0f;
    switch (m_modWaveform) {
    case ModulatorWaveform::Sine:
        sample = std::sin(2.0f * std::numbers::pi_v<float> * m_modulatorPhase);
        break;
    case ModulatorWaveform::Triangle:
        sample = 4.0f * std::abs(m_modulatorPhase - 0.5f) - 1.0f;
        break;
    case ModulatorWaveform::Sawtooth:
        sample = 2.0f * m_modulatorPhase - 1.0f;
        break;
    case ModulatorWaveform::Square:
        sample = m_modulatorPhase < 0.5f ? 1.0f : -1.0f;
        break;
    }

    const float phaseInc = m_modulatorFreq / static_cast<float>(m_sampleRate);
    m_modulatorPhase += phaseInc;
    if (m_modulatorPhase >= 1.0f) {
        m_modulatorPhase -= std::floor(m_modulatorPhase);
    }
    return sample;
}

float ComplexOscillatorWavefolderNode::processWavefolder(float input) const noexcept
{
    if (m_foldAmount < 1e-4f) {
        return input;
    }

    const float drive = 1.0f + m_foldAmount * 4.0f;
    float s = (input + m_foldSymmetry * 0.25f) * drive;

    // Cascaded smooth diode folding stages
    for (uint8_t stage = 0; stage < m_foldStages; ++stage) {
        s = std::sin(s * 0.5f * std::numbers::pi_v<float>);
    }

    return (1.0f - m_foldAmount) * input + m_foldAmount * s;
}

float ComplexOscillatorWavefolderNode::processVactrolLpg(float input, float& filterState) noexcept
{
    const float target = std::max(m_vactrolTarget, m_lpgManualOpen);
    const float sr = static_cast<float>(m_sampleRate);

    // Vactrol asymmetric dynamics: 2ms fast opto-strike, slow natural decay
    if (m_vactrolState < target) {
        const float alphaAtt = std::exp(-1.0f / (0.002f * sr));
        m_vactrolState = alphaAtt * m_vactrolState + (1.0f - alphaAtt) * target;
    } else {
        const float decaySec = std::max(0.01f, m_lpgDecayMs * 0.001f);
        const float alphaDec = std::exp(-1.0f / (decaySec * sr));
        m_vactrolState = alphaDec * m_vactrolState + (1.0f - alphaDec) * target;
    }

    // Decay the target impulse back to zero
    m_vactrolTarget *= 0.9995f;

    // Non-linear perceptual vactrol conductivity
    const float vactrolConductance = std::pow(std::clamp(m_vactrolState, 0.0f, 1.0f), 1.6f);

    // Low-pass filter frequency mapping: 30Hz to 18kHz
    const float cutoffHz = 30.0f + 18000.0f * vactrolConductance;
    const float omega = 2.0f * std::numbers::pi_v<float> * std::min(cutoffHz, sr * 0.45f) / sr;
    const float filterCoeff = 1.0f - std::exp(-omega);

    filterState += filterCoeff * (input - filterState);

    switch (m_lpgMode) {
    case LpgMode::LowPass:
        return filterState;
    case LpgMode::VCA:
        return input * vactrolConductance;
    case LpgMode::Combo:
    default:
        return filterState * vactrolConductance;
    }
}

void ComplexOscillatorWavefolderNode::process(ProcessContext& context) noexcept
{
    auto& audio = context.audio;
    if (audio.channelCount == 0 || audio.frameCount == 0 || audio.channels[0] == nullptr) {
        return;
    }

    float* left = audio.channels[0];
    float* right = audio.channelCount > 1 && audio.channels[1] != nullptr ? audio.channels[1] : nullptr;

    const float sr = static_cast<float>(m_sampleRate);

    for (std::size_t i = 0; i < audio.frameCount; ++i) {
        const float modSample = generateModulatorSample();

        // Through-Zero Linear Frequency Modulation
        const float instantCarrierFreq = m_carrierFreq + (m_fmIndex * m_carrierFreq) * modSample;
        const float phaseInc = instantCarrierFreq / sr;

        m_carrierPhase += phaseInc;
        // Natural through-zero wrapping
        if (m_carrierPhase >= 1.0f) {
            m_carrierPhase -= std::floor(m_carrierPhase);
        } else if (m_carrierPhase < 0.0f) {
            m_carrierPhase += 1.0f - std::floor(m_carrierPhase);
        }

        const float carrierSample = std::sin(2.0f * std::numbers::pi_v<float> * m_carrierPhase);

        // Multi-stage diode wavefolder
        const float foldedSample = processWavefolder(carrierSample);

        // Buchla 292 Vactrol LPG
        const float outL = processVactrolLpg(foldedSample, m_lpgFilterStateL);
        const float outR = right != nullptr ? processVactrolLpg(foldedSample, m_lpgFilterStateR) : outL;

        left[i] = outL;
        if (right != nullptr) {
            right[i] = outR;
        }
    }
}

} // namespace dawg::engine::dsp
