#include <dawg/engine/dsp/AcousticPhysicalModelEngine.h>

#include <algorithm>
#include <cmath>

namespace dawg::engine::dsp {

namespace {

constexpr float pi = 3.14159265358979323846f;
constexpr float twoPi = 6.28318530717958647692f;

inline float nextRng(std::uint32_t& s) noexcept
{
    s = s * 1664525u + 1013904223u;
    return static_cast<float>(static_cast<std::int32_t>(s)) / 2147483648.0f;
}

// Design constant-gain bandpass peak resonator
void designResonator(float f0, float q, double sampleRate, AcousticPhysicalModelEngine::ModalBiquad& bq) noexcept
{
    const float omega = twoPi * std::clamp(f0, 20.0f, static_cast<float>(sampleRate * 0.45)) / static_cast<float>(sampleRate);
    const float alpha = std::sin(omega) / (2.0f * std::max(0.5f, q));
    const float cs = std::cos(omega);

    const float a0 = 1.0f + alpha;
    bq.b0 = alpha / a0;
    bq.b1 = 0.0f;
    bq.b2 = -alpha / a0;
    bq.a1 = (-2.0f * cs) / a0;
    bq.a2 = (1.0f - alpha) / a0;
}

} // namespace

AcousticPhysicalModelEngine::AcousticPhysicalModelEngine() noexcept
{
    m_delayBuffer.fill(0.0f);
    updateWaveguideDelay();
    updateMembraneModes();
}

bool AcousticPhysicalModelEngine::prepare(double sampleRate, std::size_t /*maxBlockSize*/, std::uint16_t /*channelCount*/)
{
    m_sampleRate = (sampleRate > 1000.0) ? sampleRate : 48000.0;
    reset();
    updateWaveguideDelay();
    updateMembraneModes();
    return true;
}

void AcousticPhysicalModelEngine::reset() noexcept
{
    m_delayBuffer.fill(0.0f);
    m_writeIdx = 0;
    m_lowpassState = 0.0f;
    m_allpassState = 0.0f;
    for (auto& f : m_membraneFilters)
        f.reset();
    m_exciterCountdown = 0;
    m_exciterVelocity = 0.0f;
}

void AcousticPhysicalModelEngine::setModelMode(const AcousticModelMode mode) noexcept
{
    m_modelMode.store(mode, std::memory_order_release);
}

AcousticModelMode AcousticPhysicalModelEngine::modelMode() const noexcept
{
    return m_modelMode.load(std::memory_order_acquire);
}

void AcousticPhysicalModelEngine::setFrequencyHz(const float freqHz) noexcept
{
    m_freqHz.store(std::clamp(freqHz, 20.0f, 5000.0f), std::memory_order_release);
    updateWaveguideDelay();
    updateMembraneModes();
}

float AcousticPhysicalModelEngine::frequencyHz() const noexcept
{
    return m_freqHz.load(std::memory_order_acquire);
}

void AcousticPhysicalModelEngine::triggerExcitation(const float velocity0to1) noexcept
{
    m_exciterVelocity = std::clamp(velocity0to1, 0.0f, 1.0f);
    // 5ms excitation window
    m_exciterCountdown = static_cast<std::uint32_t>(m_sampleRate * 0.005);
}

void AcousticPhysicalModelEngine::setStringExciter(const StringExciterType exciter) noexcept
{
    m_stringExciter.store(exciter, std::memory_order_release);
}

StringExciterType AcousticPhysicalModelEngine::stringExciter() const noexcept
{
    return m_stringExciter.load(std::memory_order_acquire);
}

void AcousticPhysicalModelEngine::setStringDamping(const float damping0to1) noexcept
{
    m_stringDamping.store(std::clamp(damping0to1, 0.0f, 1.0f), std::memory_order_release);
}

float AcousticPhysicalModelEngine::stringDamping() const noexcept
{
    return m_stringDamping.load(std::memory_order_acquire);
}

void AcousticPhysicalModelEngine::setStringStiffness(const float stiffness0to1) noexcept
{
    m_stringStiffness.store(std::clamp(stiffness0to1, 0.0f, 1.0f), std::memory_order_release);
}

float AcousticPhysicalModelEngine::stringStiffness() const noexcept
{
    return m_stringStiffness.load(std::memory_order_acquire);
}

void AcousticPhysicalModelEngine::setBowPressure(const float pressure0to1) noexcept
{
    m_bowPressure.store(std::clamp(pressure0to1, 0.0f, 1.0f), std::memory_order_release);
}

float AcousticPhysicalModelEngine::bowPressure() const noexcept
{
    return m_bowPressure.load(std::memory_order_acquire);
}

void AcousticPhysicalModelEngine::setMembraneTension(const float tension0to1) noexcept
{
    m_membraneTension.store(std::clamp(tension0to1, 0.0f, 1.0f), std::memory_order_release);
    updateMembraneModes();
}

float AcousticPhysicalModelEngine::membraneTension() const noexcept
{
    return m_membraneTension.load(std::memory_order_acquire);
}

void AcousticPhysicalModelEngine::setMembraneDecay(const float decayMs) noexcept
{
    m_membraneDecayMs.store(std::clamp(decayMs, 10.0f, 5000.0f), std::memory_order_release);
    updateMembraneModes();
}

float AcousticPhysicalModelEngine::membraneDecay() const noexcept
{
    return m_membraneDecayMs.load(std::memory_order_acquire);
}

void AcousticPhysicalModelEngine::setReedStiffness(const float stiffness0to1) noexcept
{
    m_reedStiffness.store(std::clamp(stiffness0to1, 0.0f, 1.0f), std::memory_order_release);
}

float AcousticPhysicalModelEngine::reedStiffness() const noexcept
{
    return m_reedStiffness.load(std::memory_order_acquire);
}

void AcousticPhysicalModelEngine::setBreathPressure(const float pressure0to1) noexcept
{
    m_breathPressure.store(std::clamp(pressure0to1, 0.0f, 1.0f), std::memory_order_release);
}

float AcousticPhysicalModelEngine::breathPressure() const noexcept
{
    return m_breathPressure.load(std::memory_order_acquire);
}

void AcousticPhysicalModelEngine::updateWaveguideDelay() noexcept
{
    const float f0 = m_freqHz.load(std::memory_order_relaxed);
    m_delayLengthSamples = std::clamp(m_sampleRate / std::max(20.0f, f0), 2.0, static_cast<double>(maxDelaySamples - 2));
}

void AcousticPhysicalModelEngine::updateMembraneModes() noexcept
{
    const float f0 = m_freqHz.load(std::memory_order_relaxed);
    const float tension = m_membraneTension.load(std::memory_order_relaxed);
    const float decayMs = m_membraneDecayMs.load(std::memory_order_relaxed);

    // Exact 2D Circular Membrane Bessel Ratios:
    // (0,1)=1.0, (1,1)=1.593, (2,1)=2.135, (0,2)=2.295, (3,1)=2.653, (1,2)=2.917
    constexpr std::array<float, numMembraneModes> besselRatios = { 1.0f, 1.593f, 2.135f, 2.295f, 2.653f, 2.917f };

    const float qBase = std::clamp(decayMs * 0.1f, 5.0f, 100.0f);

    for (std::size_t m = 0; m < numMembraneModes; ++m)
    {
        const float modeFreq = f0 * besselRatios[m] * (0.8f + 0.4f * tension);
        const float qMode = qBase / (1.0f + 0.2f * static_cast<float>(m)); // Higher modes damp faster
        designResonator(modeFreq, qMode, m_sampleRate, m_membraneFilters[m]);
    }
}

void AcousticPhysicalModelEngine::process(ProcessContext& context) noexcept
{
    auto& audio = context.audio;
    if (audio.channels == nullptr || audio.channelCount == 0 || audio.frameCount == 0)
        return;

    const auto mode = m_modelMode.load(std::memory_order_relaxed);
    const auto exciter = m_stringExciter.load(std::memory_order_relaxed);
    const float damping = m_stringDamping.load(std::memory_order_relaxed);
    const float stiffness = m_stringStiffness.load(std::memory_order_relaxed);
    const float bow = m_bowPressure.load(std::memory_order_relaxed);
    const float reed = m_reedStiffness.load(std::memory_order_relaxed);
    const float breath = m_breathPressure.load(std::memory_order_relaxed);

    float* ch0 = audio.channels[0];
    float* ch1 = (audio.channelCount > 1 && audio.channels[1] != nullptr) ? audio.channels[1] : ch0;

    for (std::size_t i = 0; i < audio.frameCount; ++i)
    {
        // 1. Generate excitation
        float exc = 0.0f;
        if (m_exciterCountdown > 0)
        {
            switch (exciter)
            {
            case StringExciterType::pluck:
                // Filtered white noise pulse
                exc = nextRng(m_rng) * m_exciterVelocity * 0.8f;
                break;

            case StringExciterType::strikeFeltHammer:
            {
                // Smooth half-sine hammer impulse
                const float t = 1.0f - static_cast<float>(m_exciterCountdown) / static_cast<float>(m_sampleRate * 0.005);
                exc = std::sin(pi * t) * m_exciterVelocity;
                break;
            }

            case StringExciterType::bowFriction:
                // Continuous stick-slip friction
                exc = (nextRng(m_rng) * 0.1f + 0.9f) * bow * m_exciterVelocity;
                break;
            }
            --m_exciterCountdown;
        }

        float outSample = 0.0f;

        if (mode == AcousticModelMode::waveguideString)
        {
            // Read from delay line with linear interpolation
            double readPos = static_cast<double>(m_writeIdx) - m_delayLengthSamples;
            if (readPos < 0.0) readPos += static_cast<double>(maxDelaySamples);

            const auto iPos = static_cast<std::size_t>(readPos);
            const float frac = static_cast<float>(readPos - static_cast<double>(iPos));
            const std::size_t nextPos = (iPos + 1 < maxDelaySamples) ? iPos + 1 : 0;

            const float delayed = m_delayBuffer[iPos] * (1.0f - frac) + m_delayBuffer[nextPos] * frac;

            // Lowpass loop damping filter
            const float lpCoeff = 0.05f + 0.45f * (1.0f - damping);
            m_lowpassState += lpCoeff * (delayed - m_lowpassState);

            // Allpass string dispersion filter (stiffness)
            const float apCoeff = stiffness * 0.6f;
            const float apIn = m_lowpassState;
            const float apOut = -apCoeff * apIn + m_allpassState;
            m_allpassState = apIn + apCoeff * apOut;

            // Feedback with excitation
            const float stringSample = std::tanh(apOut * 0.995f + exc);
            m_delayBuffer[m_writeIdx] = stringSample;

            m_writeIdx = (m_writeIdx + 1 < maxDelaySamples) ? m_writeIdx + 1 : 0;
            outSample = stringSample;
        }
        else if (mode == AcousticModelMode::membraneDrum)
        {
            // Pass strike impulse into parallel 2D Bessel modal resonators
            float modalSum = 0.0f;
            for (auto& res : m_membraneFilters)
            {
                modalSum += res.process(exc);
            }
            outSample = std::tanh(modalSum * 2.5f);
        }
        else if (mode == AcousticModelMode::airColumnBore)
        {
            // Acoustic Bore Waveguide with non-linear reed reflection
            double readPos = static_cast<double>(m_writeIdx) - m_delayLengthSamples;
            if (readPos < 0.0) readPos += static_cast<double>(maxDelaySamples);

            const auto iPos = static_cast<std::size_t>(readPos);
            const float delayed = m_delayBuffer[iPos];

            // Pressure differential across reed: Delta_p = p_mouth - p_bore
            const float mouthPressure = breath + exc * 0.5f;
            const float deltaP = mouthPressure - delayed;

            // Non-linear cubic reed characteristic
            const float reedFlow = reed * deltaP - 0.35f * deltaP * deltaP * deltaP;
            const float boreIn = std::clamp(-delayed + reedFlow, -1.0f, 1.0f);

            m_delayBuffer[m_writeIdx] = boreIn;
            m_writeIdx = (m_writeIdx + 1 < maxDelaySamples) ? m_writeIdx + 1 : 0;

            outSample = boreIn * 0.8f;
        }

        ch0[i] = outSample;
        ch1[i] = outSample;
    }
}

} // namespace dawg::engine::dsp
