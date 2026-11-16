#include <dawg/engine/dsp/DynamicCabinetSimulatorNode.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace dawg::engine::dsp {

namespace {

void makePeakingEqualizer(
    DynamicCabinetSimulatorNode::Biquad& filter,
    double sampleRate,
    float freqHz,
    float gainDb,
    float q) noexcept
{
    const float clampedFreq = std::clamp(freqHz, 20.0f, static_cast<float>(sampleRate * 0.45));
    const float clampedQ = std::max(0.1f, q);
    const float a = std::pow(10.0f, gainDb / 40.0f);
    const float w0 = static_cast<float>(2.0 * std::numbers::pi * clampedFreq / sampleRate);
    const float cosW0 = std::cos(w0);
    const float sinW0 = std::sin(w0);
    const float alpha = sinW0 / (2.0f * clampedQ);

    const float b0 = 1.0f + alpha * a;
    const float b1 = -2.0f * cosW0;
    const float b2 = 1.0f - alpha * a;
    const float a0 = 1.0f + alpha / a;
    const float a1 = -2.0f * cosW0;
    const float a2 = 1.0f - alpha / a;

    const float invA0 = 1.0f / a0;
    filter.b0 = b0 * invA0;
    filter.b1 = b1 * invA0;
    filter.b2 = b2 * invA0;
    filter.a1 = a1 * invA0;
    filter.a2 = a2 * invA0;
}

void makeHighShelf(
    DynamicCabinetSimulatorNode::Biquad& filter,
    double sampleRate,
    float freqHz,
    float gainDb) noexcept
{
    const float clampedFreq = std::clamp(freqHz, 50.0f, static_cast<float>(sampleRate * 0.45));
    const float a = std::pow(10.0f, gainDb / 40.0f);
    const float w0 = static_cast<float>(2.0 * std::numbers::pi * clampedFreq / sampleRate);
    const float cosW0 = std::cos(w0);
    const float sinW0 = std::sin(w0);
    const float alpha = sinW0 * 0.5f * std::sqrt(2.0f);

    const float aPlus = a + 1.0f;
    const float aMinus = a - 1.0f;
    const float twoSqrtAAlpha = 2.0f * std::sqrt(a) * alpha;

    const float b0 = a * (aPlus + aMinus * cosW0 + twoSqrtAAlpha);
    const float b1 = -2.0f * a * (aMinus + aPlus * cosW0);
    const float b2 = a * (aPlus + aMinus * cosW0 - twoSqrtAAlpha);
    const float a0 = aPlus - aMinus * cosW0 + twoSqrtAAlpha;
    const float a1 = 2.0f * (aMinus - aPlus * cosW0);
    const float a2 = aPlus - aMinus * cosW0 - twoSqrtAAlpha;

    const float invA0 = 1.0f / a0;
    filter.b0 = b0 * invA0;
    filter.b1 = b1 * invA0;
    filter.b2 = b2 * invA0;
    filter.a1 = a1 * invA0;
    filter.a2 = a2 * invA0;
}

void makeLowPassFirstOrder(
    DynamicCabinetSimulatorNode::Biquad& filter,
    double sampleRate,
    float cutoffHz) noexcept
{
    const float clampedCutoff = std::clamp(cutoffHz, 20.0f, static_cast<float>(sampleRate * 0.48));
    const float dt = 1.0f / static_cast<float>(sampleRate);
    const float rc = 1.0f / (2.0f * static_cast<float>(std::numbers::pi) * clampedCutoff);
    const float alpha = dt / (rc + dt);

    filter.b0 = alpha;
    filter.b1 = 0.0f;
    filter.b2 = 0.0f;
    filter.a1 = -(1.0f - alpha);
    filter.a2 = 0.0f;
}

} // namespace

DynamicCabinetSimulatorNode::DynamicCabinetSimulatorNode() noexcept
{
    recomputeFilters();
    rebuildImpulseResponses();
}

bool DynamicCabinetSimulatorNode::prepare(
    const double sampleRate,
    const std::size_t maxBlockSize,
    const std::uint16_t channelCount)
{
    if (sampleRate <= 0.0 || maxBlockSize == 0 || channelCount == 0)
    {
        return false;
    }

    m_sampleRate = sampleRate;

    // Sag ballistics: ~3 ms attack, ~80 ms release
    const float attackSec = 0.003f;
    const float releaseSec = 0.080f;
    m_sagAttackCoeff = 1.0f - std::exp(-1.0f / (attackSec * static_cast<float>(m_sampleRate)));
    m_sagReleaseCoeff = 1.0f - std::exp(-1.0f / (releaseSec * static_cast<float>(m_sampleRate)));

    recomputeFilters();
    rebuildImpulseResponses();
    reset();
    return true;
}

void DynamicCabinetSimulatorNode::reset() noexcept
{
    m_resFilterL.reset();
    m_resFilterR.reset();
    m_indFilterL.reset();
    m_indFilterR.reset();
    m_micFilterL.reset();
    m_micFilterR.reset();

    m_firHistoryL.fill(0.0f);
    m_firHistoryR.fill(0.0f);
    m_firWritePos = 0;

    m_sagEnvelope = 0.0f;
    m_currentSag = 0.0f;
    m_currentExcursion = 0.0f;
}

void DynamicCabinetSimulatorNode::setCabinetModel(const CabinetModel model) noexcept
{
    if (m_model != model)
    {
        m_model = model;
        rebuildImpulseResponses();
        recomputeFilters();
    }
}

void DynamicCabinetSimulatorNode::setMicrophoneDistance(const float distance) noexcept
{
    m_micDistance = std::clamp(distance, 0.0f, 1.0f);
    recomputeFilters();
}

void DynamicCabinetSimulatorNode::setMicrophoneAxis(const float axis) noexcept
{
    m_micAxis = std::clamp(axis, 0.0f, 1.0f);
    recomputeFilters();
}

void DynamicCabinetSimulatorNode::setImpedanceResonanceAmount(const float db) noexcept
{
    m_resonanceDb = std::clamp(db, -6.0f, 12.0f);
    recomputeFilters();
}

void DynamicCabinetSimulatorNode::setImpedanceInductiveAmount(const float db) noexcept
{
    m_inductiveDb = std::clamp(db, -6.0f, 12.0f);
    recomputeFilters();
}

void DynamicCabinetSimulatorNode::setOutputLevelDb(const float db) noexcept
{
    m_outputLevelDb = std::clamp(db, -24.0f, 12.0f);
    m_outputGainLinear = std::pow(10.0f, m_outputLevelDb / 20.0f);
}

void DynamicCabinetSimulatorNode::recomputeFilters() noexcept
{
    // 1. Reactive Low Resonance (Cabinet dependent fundamental)
    float resFreq = 82.0f;
    float resQ = 2.2f;
    float indFreq = 3400.0f;

    switch (m_model)
    {
        case CabinetModel::vintage4x12:
            resFreq = 84.0f;
            resQ = 2.4f;
            indFreq = 3200.0f;
            break;
        case CabinetModel::openBack2x12:
            resFreq = 95.0f;
            resQ = 1.8f;
            indFreq = 3800.0f;
            break;
        case CabinetModel::modern1x12:
            resFreq = 78.0f;
            resQ = 2.6f;
            indFreq = 3100.0f;
            break;
        case CabinetModel::bass4x10:
            resFreq = 55.0f;
            resQ = 2.8f;
            indFreq = 2200.0f;
            break;
    }

    makePeakingEqualizer(m_resFilterL, m_sampleRate, resFreq, m_resonanceDb, resQ);
    m_resFilterR = m_resFilterL;

    // 2. Reactive Inductive High Shelf
    makeHighShelf(m_indFilterL, m_sampleRate, indFreq, m_inductiveDb);
    m_indFilterR = m_indFilterL;

    // 3. Microphone tone filtering
    // Distance rolls off ultra-highs (from 16 kHz down to 6.5 kHz)
    // Axis cuts presence bite (from 12 kHz down to 4.2 kHz)
    const float cutoff = 14000.0f - (m_micDistance * 4000.0f) - (m_micAxis * 5500.0f);
    makeLowPassFirstOrder(m_micFilterL, m_sampleRate, std::max(2500.0f, cutoff));
    m_micFilterR = m_micFilterL;
}

void DynamicCabinetSimulatorNode::rebuildImpulseResponses() noexcept
{
    m_activeIrClean.fill(0.0f);
    m_activeIrDriven.fill(0.0f);

    // Parametrically synthesize physical cabinet impulse responses
    // based on speaker cone mechanical response and cabinet reflections.
    float baseDecay = 0.88f;
    float reflectionDelay = 12.0f;
    float reflectionGain = -0.35f;
    float resonanceRumble = 0.15f;

    switch (m_model)
    {
        case CabinetModel::vintage4x12:
            baseDecay = 0.86f;
            reflectionDelay = 14.0f; // internal cab dimension bounce
            reflectionGain = -0.32f;
            resonanceRumble = 0.20f;
            break;
        case CabinetModel::openBack2x12:
            baseDecay = 0.82f;
            reflectionDelay = 9.0f; // shorter reflection
            reflectionGain = -0.45f; // phase cancellation from open rear
            resonanceRumble = 0.10f;
            break;
        case CabinetModel::modern1x12:
            baseDecay = 0.85f;
            reflectionDelay = 11.0f;
            reflectionGain = -0.28f;
            resonanceRumble = 0.16f;
            break;
        case CabinetModel::bass4x10:
            baseDecay = 0.91f;
            reflectionDelay = 18.0f;
            reflectionGain = -0.25f;
            resonanceRumble = 0.35f;
            break;
    }

    float normClean = 0.0f;
    float normDriven = 0.0f;

    for (std::size_t i = 0; i < irLength; ++i)
    {
        const float t = static_cast<float>(i);
        const float env = std::pow(baseDecay, t);

        // Direct blast + cabinet internal cavity reflection
        float direct = (i == 0) ? 1.0f : ((i == 1) ? 0.65f : 0.0f);
        if (i >= static_cast<std::size_t>(reflectionDelay) && i < static_cast<std::size_t>(reflectionDelay) + 3)
        {
            direct += reflectionGain * std::exp(-static_cast<float>(i - reflectionDelay) * 0.5f);
        }

        // Speaker cone damped mechanical ring
        const float coneRing = std::sin(t * 0.45f) * env * resonanceRumble;

        const float sampleClean = direct + coneRing;
        m_activeIrClean[i] = sampleClean;
        normClean += std::abs(sampleClean);

        // Driven mode: speaker excursion saturation compresses fast transients and broadens low ring
        const float sampleDriven = std::tanh(sampleClean * 1.3f) * 0.85f + coneRing * 0.3f;
        m_activeIrDriven[i] = sampleDriven;
        normDriven += std::abs(sampleDriven);
    }

    // Normalize impulse energy
    if (normClean > 1e-5f)
    {
        const float invNormClean = 1.6f / normClean;
        for (float& tap : m_activeIrClean)
        {
            tap *= invNormClean;
        }
    }

    if (normDriven > 1e-5f)
    {
        const float invNormDriven = 1.6f / normDriven;
        for (float& tap : m_activeIrDriven)
        {
            tap *= invNormDriven;
        }
    }
}

void DynamicCabinetSimulatorNode::process(ProcessContext& context) noexcept
{
    if (context.audio.channelCount == 0 || context.audio.frameCount == 0)
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

    float maxBlockSag = 0.0f;
    float maxBlockExcursion = 0.0f;

    for (std::size_t i = 0; i < frameCount; ++i)
    {
        const float dryL = ch0[i];
        const float dryR = (ch1 != nullptr) ? ch1[i] : dryL;

        // 1. Reactive Impedance Modeling (Power amp & speaker interaction)
        float impL = m_resFilterL.process(dryL);
        float impR = m_resFilterR.process(dryR);
        impL = m_indFilterL.process(impL);
        impR = m_indFilterR.process(impR);

        // 2. Power Amp Tube Sag & Transformer Saturation
        const float powerInstant = std::max(std::abs(impL), std::abs(impR));
        const float sagCoeff = (powerInstant > m_sagEnvelope) ? m_sagAttackCoeff : m_sagReleaseCoeff;
        m_sagEnvelope += sagCoeff * (powerInstant - m_sagEnvelope);

        // Tube Sag dynamic compression: drops instantaneous voltage rail
        const float sagAmount = m_tubeSag * (m_sagEnvelope / (0.7f + m_sagEnvelope));
        maxBlockSag = std::max(maxBlockSag, sagAmount);
        const float headroomScalar = 1.0f / (1.0f + 1.2f * sagAmount);

        // Soft non-linear saturation: asymmetric tube warmth + transformer core saturation
        auto saturate = [this, headroomScalar](float x) noexcept -> float
        {
            const float scaled = x * headroomScalar;
            // Add subtle even harmonics (transformer asymmetry)
            const float asymmetric = scaled + 0.12f * (scaled * scaled) * (scaled >= 0.0f ? 1.0f : -1.0f);
            // Non-linear soft clipping
            const float breakup = m_speakerBreakup * 1.5f;
            return asymmetric / (1.0f + std::abs(asymmetric) * breakup);
        };

        const float satL = saturate(impL);
        const float satR = saturate(impR);

        // 3. Dynamic Multi-Impulse Convolution with Speaker Excursion Morphing
        m_firHistoryL[m_firWritePos] = satL;
        m_firHistoryR[m_firWritePos] = satR;

        // Calculate instantaneous speaker excursion for morphing
        const float excursion = std::clamp(std::abs(satL + satR) * 0.7f, 0.0f, 1.0f);
        maxBlockExcursion = std::max(maxBlockExcursion, excursion);
        const float drivenBlend = excursion * m_speakerBreakup;
        const float cleanBlend = 1.0f - drivenBlend;

        float convL = 0.0f;
        float convR = 0.0f;

        for (std::size_t tap = 0; tap < irLength; ++tap)
        {
            const std::size_t readIdx = (m_firWritePos + irLength - tap) % irLength;
            const float irCoeff = cleanBlend * m_activeIrClean[tap] + drivenBlend * m_activeIrDriven[tap];
            convL += m_firHistoryL[readIdx] * irCoeff;
            convR += m_firHistoryR[readIdx] * irCoeff;
        }

        m_firWritePos = (m_firWritePos + 1) % irLength;

        // 4. Microphone Positioning Filter (Distance & Axis)
        float micL = m_micFilterL.process(convL);
        float micR = m_micFilterR.process(convR);

        // Apply proximity effect low boost based on distance
        const float proximityBoost = (1.0f - m_micDistance) * 0.25f;
        micL += satL * proximityBoost;
        micR += satR * proximityBoost;

        // 5. Output Gain and Wet/Dry Mix
        const float wetL = micL * m_outputGainLinear;
        const float wetR = micR * m_outputGainLinear;

        ch0[i] = (1.0f - m_mix) * dryL + m_mix * wetL;
        if (ch1 != nullptr)
        {
            ch1[i] = (1.0f - m_mix) * dryR + m_mix * wetR;
        }
    }

    m_currentSag = maxBlockSag;
    m_currentExcursion = maxBlockExcursion;
}

} // namespace dawg::engine::dsp
