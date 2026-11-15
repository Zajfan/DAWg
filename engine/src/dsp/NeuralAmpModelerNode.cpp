#include <dawg/engine/dsp/NeuralAmpModelerNode.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace dawg::engine::dsp {

void NeuralAmpModelerNode::Conv1DBlock::reset() noexcept
{
    writePos = 0;
    for (auto& ch : historyL)
    {
        ch.fill(0.0f);
    }
    for (auto& ch : historyR)
    {
        ch.fill(0.0f);
    }
}

NeuralAmpModelerNode::NeuralAmpModelerNode() noexcept
{
    loadProfile(NamAmpProfile::vintageBritishPlexi);
    updateGains();
}

bool NeuralAmpModelerNode::prepare(
    const double sampleRate,
    [[maybe_unused]] const std::size_t maxBlockSize,
    [[maybe_unused]] const std::uint16_t channelCount)
{
    m_sampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    reset();
    return true;
}

void NeuralAmpModelerNode::reset() noexcept
{
    for (auto& layer : m_layers)
    {
        layer.reset();
    }
    m_sagVoltage = 1.0f;
    m_gridBiasL = 0.0f;
    m_gridBiasR = 0.0f;
    m_toneLpL = 0.0f;
    m_toneLpR = 0.0f;
    m_toneHpL = 0.0f;
    m_toneHpR = 0.0f;
    m_tonePresL = 0.0f;
    m_tonePresR = 0.0f;
}

void NeuralAmpModelerNode::updateGains() noexcept
{
    m_inputLinear = std::pow(10.0f, m_inputGainDb * 0.05f);
    m_masterLinear = std::pow(10.0f, m_masterVolumeDb * 0.05f);
}

void NeuralAmpModelerNode::loadProfile(const NamAmpProfile profile) noexcept
{
    m_profile = profile;
    setupProfileWeights();
}

void NeuralAmpModelerNode::setupProfileWeights() noexcept
{
    // Dilations for WaveNet layers: 1, 2, 4, 8
    std::size_t d = 1;
    for (std::size_t l = 0; l < numLayers; ++l)
    {
        m_layers[l].dilation = d;
        d *= 2;
    }

    // Initialize input projection weights
    for (std::size_t c = 0; c < hiddenChannels; ++c)
    {
        m_inputProjWeights[c] = (c % 2 == 0) ? 0.35f : -0.25f;
    }

    // Configure profile-specific neural weights and frequency response
    float weightScale = 0.35f;
    float biasShift = 0.0f;

    switch (m_profile)
    {
    case NamAmpProfile::highGainMetal:
        weightScale = 0.65f;
        biasShift = -0.15f;
        m_inputGainDb = 24.0f;
        m_sagAmount = 0.45f;    // Tight, fast transient attack for metal chugging
        m_dynamicBias = 0.70f;  // Aggressive even-order harmonic distortion
        break;

    case NamAmpProfile::vintageBritishPlexi:
        weightScale = 0.45f;
        biasShift = -0.05f;
        m_inputGainDb = 14.0f;
        m_sagAmount = 0.65f;    // Classic British tube rectifier sag & bloom
        m_dynamicBias = 0.50f;
        break;

    case NamAmpProfile::classicTweedClean:
        weightScale = 0.22f;
        biasShift = 0.05f;
        m_inputGainDb = 4.0f;
        m_sagAmount = 0.80f;    // Deep tube power sag compression
        m_dynamicBias = 0.35f;
        break;

    case NamAmpProfile::legendaryOverdrive:
        weightScale = 0.38f;
        biasShift = 0.0f;
        m_inputGainDb = 10.0f;
        m_sagAmount = 0.30f;    // Pedal diode clipping with solid state stiffness
        m_dynamicBias = 0.20f;
        break;
    }

    updateGains();

    // Populate residual block matrix weights with profile-specific tuning
    for (std::size_t l = 0; l < numLayers; ++l)
    {
        auto& layer = m_layers[l];
        for (std::size_t i = 0; i < hiddenChannels; ++i)
        {
            layer.bias[i] = biasShift * (static_cast<float>(i) / static_cast<float>(hiddenChannels));
            for (std::size_t j = 0; j < hiddenChannels; ++j)
            {
                const float phase = static_cast<float>((i + j * 3 + l * 2) % 7);
                const float coeff = std::sin(phase * 0.897f) * weightScale;
                layer.kernelWeight[i * hiddenChannels + j] = coeff;
            }
        }
    }

    // Populate output head weights
    for (std::size_t c = 0; c < hiddenChannels; ++c)
    {
        m_outputHeadWeights[c] = (c % 2 == 0) ? 0.3f : -0.2f;
    }
}

void NeuralAmpModelerNode::process(ProcessContext& context) noexcept
{
    if (context.audio.channels == nullptr || context.audio.frameCount == 0 || context.audio.channelCount == 0)
    {
        return;
    }

    const auto frameCount = context.audio.frameCount;
    auto* ch0 = context.audio.channels[0];
    auto* ch1 = (context.audio.channelCount >= 2) ? context.audio.channels[1] : ch0;

    if (ch0 == nullptr)
    {
        return;
    }

    // Time constants for tube physics
    const float dt = 1.0f / static_cast<float>(m_sampleRate);
    const float biasCoeff = 1.0f - std::exp(-dt / 0.025f); // 25ms bias recovery
    const float sagAttackCoeff = 1.0f - std::exp(-dt / 0.015f); // 15ms power discharge
    const float sagReleaseCoeff = 1.0f - std::exp(-dt / 0.120f); // 120ms power recharge

    // Tone Stack crossover filter coefficients (Butterworth approximations)
    const float lpCoeff = 1.0f - std::exp(-2.0f * std::numbers::pi_v<float> * 280.0f * dt);
    const float hpCoeff = 1.0f - std::exp(-2.0f * std::numbers::pi_v<float> * 3200.0f * dt);
    const float presCoeff = 1.0f - std::exp(-2.0f * std::numbers::pi_v<float> * 5500.0f * dt);

    for (std::size_t frame = 0; frame < frameCount; ++frame)
    {
        const float inL = ch0[frame] * m_inputLinear;
        const float inR = (ch1 != nullptr) ? (ch1[frame] * m_inputLinear) : inL;

        // 1. Dynamic Tube Grid Bias Shift
        const float rectL = std::max(0.0f, inL - 0.4f) * 0.35f * m_dynamicBias;
        const float rectR = std::max(0.0f, inR - 0.4f) * 0.35f * m_dynamicBias;
        m_gridBiasL += biasCoeff * (rectL - m_gridBiasL);
        m_gridBiasR += biasCoeff * (rectR - m_gridBiasR);

        const float tubeInL = inL - m_gridBiasL;
        const float tubeInR = inR - m_gridBiasR;

        // 2. WaveNet / Conv1D Neural Forward Pass
        std::array<float, hiddenChannels> hL {};
        std::array<float, hiddenChannels> hR {};
        std::array<float, hiddenChannels> skipL {};
        std::array<float, hiddenChannels> skipR {};
        skipL.fill(0.0f);
        skipR.fill(0.0f);

        // Input projection layer
        for (std::size_t c = 0; c < hiddenChannels; ++c)
        {
            hL[c] = tubeInL * m_inputProjWeights[c];
            hR[c] = tubeInR * m_inputProjWeights[c];
        }

        // Dilated residual blocks
        for (std::size_t l = 0; l < numLayers; ++l)
        {
            auto& layer = m_layers[l];
            const auto d = layer.dilation;
            const auto bufSize = d * 2;

            // Write current hidden state to history
            for (std::size_t c = 0; c < hiddenChannels; ++c)
            {
                layer.historyL[c][layer.writePos] = hL[c];
                layer.historyR[c][layer.writePos] = hR[c];
            }

            const auto readPos = (layer.writePos + bufSize - d) % bufSize;

            std::array<float, hiddenChannels> actL {};
            std::array<float, hiddenChannels> actR {};

            for (std::size_t i = 0; i < hiddenChannels; ++i)
            {
                float sumL = layer.bias[i];
                float sumR = layer.bias[i];
                const auto rowOffset = i * hiddenChannels;

                for (std::size_t j = 0; j < hiddenChannels; ++j)
                {
                    const float w = layer.kernelWeight[rowOffset + j];
                    sumL += w * layer.historyL[j][readPos];
                    sumR += w * layer.historyR[j][readPos];
                }

                // Gated non-linear activation: tanh(x) * sigmoid(x)
                const float sigL = 1.0f / (1.0f + std::exp(-sumL));
                const float sigR = 1.0f / (1.0f + std::exp(-sumR));
                actL[i] = std::tanh(sumL) * sigL;
                actR[i] = std::tanh(sumR) * sigR;

                // Residual + skip accumulation
                hL[i] += actL[i];
                hR[i] += actR[i];
                skipL[i] += actL[i];
                skipR[i] += actR[i];
            }

            layer.writePos = (layer.writePos + 1) % bufSize;
        }

        // Output head projection
        float outL = 0.0f;
        float outR = 0.0f;
        for (std::size_t c = 0; c < hiddenChannels; ++c)
        {
            outL += skipL[c] * m_outputHeadWeights[c];
            outR += skipR[c] * m_outputHeadWeights[c];
        }

        // 3. Dynamic Power-Amp Voltage Sag Simulation
        const float peakPwr = std::max(outL * outL, outR * outR);
        const float targetSag = 1.0f - std::clamp(peakPwr * 0.4f * m_sagAmount, 0.0f, 0.75f * m_sagAmount);

        if (targetSag < m_sagVoltage)
        {
            m_sagVoltage += sagAttackCoeff * (targetSag - m_sagVoltage);
        }
        else
        {
            m_sagVoltage += sagReleaseCoeff * (targetSag - m_sagVoltage);
        }

        // Apply sag-modulated power tube non-linear saturation
        const float headRoom = m_sagVoltage;
        outL = std::tanh((outL * 1.5f) / std::max(0.1f, headRoom)) * headRoom;
        outR = std::tanh((outR * 1.5f) / std::max(0.1f, headRoom)) * headRoom;

        // 4. Tone Stack (Bass, Middle, Treble, Presence)
        // Bass lowpass branch
        m_toneLpL += lpCoeff * (outL - m_toneLpL);
        m_toneLpR += lpCoeff * (outR - m_toneLpR);

        // Treble highpass branch
        m_toneHpL += hpCoeff * (outL - m_toneHpL);
        m_toneHpR += hpCoeff * (outR - m_toneHpR);
        const float hpSigL = outL - m_toneHpL;
        const float hpSigR = outR - m_toneHpR;

        // Middle bandpass
        const float midSigL = outL - m_toneLpL - hpSigL;
        const float midSigR = outR - m_toneLpR - hpSigR;

        // Presence high-shelf
        m_tonePresL += presCoeff * (outL - m_tonePresL);
        m_tonePresR += presCoeff * (outR - m_tonePresR);
        const float presSigL = outL - m_tonePresL;
        const float presSigR = outR - m_tonePresR;

        // Sum tone stack
        const float toneL = (m_toneLpL * m_bass + midSigL * m_middle + hpSigL * m_treble + presSigL * m_presence * 0.5f) * m_masterLinear;
        const float toneR = (m_toneLpR * m_bass + midSigR * m_middle + hpSigR * m_treble + presSigR * m_presence * 0.5f) * m_masterLinear;

        ch0[frame] = toneL;
        if (context.audio.channelCount >= 2)
        {
            ch1[frame] = toneR;
        }
    }
}

} // namespace dawg::engine::dsp
