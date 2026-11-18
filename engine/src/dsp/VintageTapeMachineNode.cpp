#include <dawg/engine/dsp/VintageTapeMachineNode.h>

#include <algorithm>
#include <cmath>

namespace dawg::engine::dsp {

namespace {
constexpr double kTwoPi = 6.283185307179586;
constexpr float kBaseDelaySec = 0.0015f;   // Fixed transport delay (reported as latency)
constexpr float kMaxWowSec = 0.0010f;      // ~0.5% peak speed deviation at 0.8 Hz
constexpr float kMaxFlutterSec = 0.00005f; // ~0.3% peak speed deviation at 9 Hz
constexpr float kReferenceToneAmp = 0.12589f; // -18 dBFS = 0 VU alignment level
} // namespace

void TapeShelfFilter::design(const double sampleRate, const float gain, const float tau1, const float tau2) noexcept
{
    if (std::abs(tau1 - tau2) < 1e-12f)
    {
        b0 = gain;
        b1 = 0.0f;
        a1 = 0.0f;
        return;
    }
    const auto K = static_cast<float>(2.0 * sampleRate);
    const float denom = 1.0f + K * tau2;
    b0 = gain * (1.0f + K * tau1) / denom;
    b1 = gain * (1.0f - K * tau1) / denom;
    a1 = (1.0f - K * tau2) / denom;
}

float VintageTapeMachineNode::hysteresis(const float h, float& playState, const float width,
                                         const float loopWeight, const float asymmetry) noexcept
{
    // Play operator: state follows input only once it moves more than `width` away.
    playState = std::clamp(playState, h - width, h + width);
    const float lambda = std::clamp(loopWeight, 0.0f, 0.95f);
    float u = (1.0f - lambda) * h + lambda * playState;
    u += asymmetry * 0.3f * u * u; // even-order component
    return std::tanh(u);
}

bool VintageTapeMachineNode::prepare(const double sampleRate, const std::size_t /*maxBlockSize*/,
                                     const std::uint16_t /*channelCount*/)
{
    if (sampleRate <= 0.0)
        return false;
    m_sampleRate = sampleRate;
    m_dcCoeff = 1.0f - static_cast<float>(kTwoPi * 10.0 / sampleRate);
    updateFilters();
    reset();
    return true;
}

void VintageTapeMachineNode::reset() noexcept
{
    for (auto& ch : m_channels)
    {
        ch.preLf.reset();
        ch.preHf.reset();
        ch.deHf.reset();
        ch.deLf.reset();
        ch.headBump.reset();
        ch.gapLossState = 0.0f;
        ch.playState = 0.0f;
        ch.dcX1 = 0.0f;
        ch.dcY1 = 0.0f;
        ch.delay.fill(0.0f);
    }
    m_wowPhase = 0.0;
    m_flutterPhase = 0.0;
    m_tonePhase = 0.0;
    m_wowDrift = 0.0f;
    m_writePos = 0;
}

std::uint32_t VintageTapeMachineNode::latencySamples() const noexcept
{
    return static_cast<std::uint32_t>(std::lround(kBaseDelaySec * m_sampleRate));
}

void VintageTapeMachineNode::setInputDriveDb(const float db) noexcept
{
    m_driveGain = std::pow(10.0f, std::clamp(db, 0.0f, 24.0f) / 20.0f);
}

void VintageTapeMachineNode::setBias(const float bias) noexcept { m_bias = std::clamp(bias, 0.0f, 1.0f); }
void VintageTapeMachineNode::setAsymmetry(const float amount) noexcept { m_asymmetry = std::clamp(amount, 0.0f, 1.0f); }

void VintageTapeMachineNode::setHeadBumpDb(const float db) noexcept
{
    m_headBumpDb = std::clamp(db, 0.0f, 6.0f);
    updateFilters();
}

void VintageTapeMachineNode::setWow(const float depth, const float rateHz) noexcept
{
    m_wowDepth = std::clamp(depth, 0.0f, 1.0f);
    m_wowRateHz = std::clamp(rateHz, 0.5f, 2.0f);
}

void VintageTapeMachineNode::setFlutter(const float depth, const float rateHz) noexcept
{
    m_flutterDepth = std::clamp(depth, 0.0f, 1.0f);
    m_flutterRateHz = std::clamp(rateHz, 6.0f, 15.0f);
}

void VintageTapeMachineNode::setHissDb(const float db) noexcept
{
    const float clamped = std::clamp(db, -120.0f, -40.0f);
    m_hissGain = clamped <= -120.0f ? 0.0f : std::pow(10.0f, clamped / 20.0f);
}

void VintageTapeMachineNode::setOutputTrimDb(const float db) noexcept
{
    m_outputTrim = std::pow(10.0f, std::clamp(db, -24.0f, 24.0f) / 20.0f);
}

float VintageTapeMachineNode::headBumpFrequencyHz() const noexcept
{
    switch (m_speed)
    {
    case TapeMachineSpeed::ips7_5: return 35.0f;
    case TapeMachineSpeed::ips15: return 50.0f;
    case TapeMachineSpeed::ips30: return 80.0f;
    }
    return 50.0f;
}

float VintageTapeMachineNode::highFrequencyTimeConstantUs() const noexcept
{
    if (m_speed == TapeMachineSpeed::ips30)
        return 17.5f;
    if (m_eq == TapeEqStandard::nab)
        return 50.0f;
    return m_speed == TapeMachineSpeed::ips7_5 ? 70.0f : 35.0f;
}

float VintageTapeMachineNode::lowFrequencyTimeConstantUs() const noexcept
{
    return (m_eq == TapeEqStandard::nab && m_speed != TapeMachineSpeed::ips30) ? 3180.0f : 0.0f;
}

void VintageTapeMachineNode::updateFilters() noexcept
{
    const float tauH = highFrequencyTimeConstantUs() * 1e-6f;
    const float tauL = lowFrequencyTimeConstantUs() * 1e-6f;
    const auto fs = static_cast<float>(m_sampleRate);

    float gapLossHz = 18000.0f;
    if (m_speed == TapeMachineSpeed::ips7_5) gapLossHz = 12000.0f;
    if (m_speed == TapeMachineSpeed::ips30) gapLossHz = 22000.0f;
    gapLossHz = std::min(gapLossHz, 0.45f * fs);
    m_gapLossCoeff = 1.0f - std::exp(-static_cast<float>(kTwoPi) * gapLossHz / fs);

    // RBJ peaking EQ for the head bump
    const float f0 = headBumpFrequencyHz();
    const float A = std::pow(10.0f, m_headBumpDb / 40.0f);
    const float w0 = static_cast<float>(kTwoPi) * f0 / fs;
    const float alpha = std::sin(w0) / (2.0f * 1.0f);
    const float a0 = 1.0f + alpha / A;

    for (auto& ch : m_channels)
    {
        ch.preHf.design(m_sampleRate, 1.0f, tauH, tauH * 0.1f);
        ch.deHf.design(m_sampleRate, 1.0f, tauH * 0.1f, tauH);
        if (tauL > 0.0f)
        {
            ch.preLf.design(m_sampleRate, 0.1f, tauL, tauL * 0.1f);
            ch.deLf.design(m_sampleRate, 10.0f, tauL * 0.1f, tauL);
        }
        else
        {
            ch.preLf.design(m_sampleRate, 1.0f, 0.0f, 0.0f);
            ch.deLf.design(m_sampleRate, 1.0f, 0.0f, 0.0f);
        }

        ch.headBump.b0 = (1.0f + alpha * A) / a0;
        ch.headBump.b1 = (-2.0f * std::cos(w0)) / a0;
        ch.headBump.b2 = (1.0f - alpha * A) / a0;
        ch.headBump.a1 = (-2.0f * std::cos(w0)) / a0;
        ch.headBump.a2 = (1.0f - alpha / A) / a0;
    }
}

float VintageTapeMachineNode::nextNoise() noexcept
{
    m_rng ^= m_rng << 13;
    m_rng ^= m_rng >> 7;
    m_rng ^= m_rng << 17;
    return static_cast<float>(static_cast<double>(m_rng >> 11) * (1.0 / 9007199254740992.0)) * 2.0f - 1.0f;
}

void VintageTapeMachineNode::process(ProcessContext& context) noexcept
{
    auto& audio = context.audio;
    if (audio.channels == nullptr || audio.channelCount == 0 || audio.frameCount == 0)
        return;

    const std::size_t numChannels = std::min(audio.channelCount, maxChannels);
    const auto fs = static_cast<float>(m_sampleRate);
    const float width = 0.05f + 0.30f * (1.0f - m_bias);
    const float loopWeight = 0.15f + 0.35f * (1.0f - m_bias);
    const float lowLevelCompensation = 1.0f / ((1.0f - loopWeight) * m_driveGain);
    const float baseDelay = kBaseDelaySec * fs;
    constexpr std::size_t mask = delayBufferSize - 1;

    double toneHz = 0.0;
    if (m_tone == TapeReferenceTone::hz100) toneHz = 100.0;
    if (m_tone == TapeReferenceTone::hz1k) toneHz = 1000.0;
    if (m_tone == TapeReferenceTone::hz10k) toneHz = 10000.0;

    for (std::size_t i = 0; i < audio.frameCount; ++i)
    {
        // Capstan speed modulation (shared by all channels, as on a real transport)
        m_wowDrift += (nextNoise() * 0.5f - m_wowDrift) * 0.0005f;
        const double wowRate = m_wowRateHz * (1.0 + 0.2 * m_wowDrift);
        m_wowPhase += kTwoPi * wowRate / m_sampleRate;
        m_flutterPhase += kTwoPi * m_flutterRateHz / m_sampleRate;
        if (m_wowPhase > kTwoPi) m_wowPhase -= kTwoPi;
        if (m_flutterPhase > kTwoPi) m_flutterPhase -= kTwoPi;
        const float modDelay = fs * (m_wowDepth * kMaxWowSec * static_cast<float>(std::sin(m_wowPhase))
                                     + m_flutterDepth * kMaxFlutterSec * static_cast<float>(std::sin(m_flutterPhase)));
        const float readDelay = baseDelay + modDelay;
        const auto whole = static_cast<std::size_t>(readDelay);
        const float frac = readDelay - static_cast<float>(whole);

        float tone = 0.0f;
        if (toneHz > 0.0)
        {
            m_tonePhase += kTwoPi * toneHz / m_sampleRate;
            if (m_tonePhase > kTwoPi) m_tonePhase -= kTwoPi;
            tone = kReferenceToneAmp * static_cast<float>(std::sin(m_tonePhase));
        }

        for (std::size_t c = 0; c < numChannels; ++c)
        {
            float* buf = audio.channels[c];
            if (buf == nullptr)
                continue;
            auto& ch = m_channels[c];

            const float in = toneHz > 0.0 ? tone : buf[i];
            float x = in * m_driveGain;
            x = ch.preHf.process(ch.preLf.process(x));
            x = hysteresis(x, ch.playState, width, loopWeight, m_asymmetry);
            x += m_hissGain * nextNoise();
            x = ch.deLf.process(ch.deHf.process(x));
            ch.gapLossState += (x - ch.gapLossState) * m_gapLossCoeff;
            x = ch.headBump.process(ch.gapLossState);

            ch.delay[m_writePos] = x;
            const float a = ch.delay[(m_writePos + delayBufferSize - whole) & mask];
            const float b = ch.delay[(m_writePos + delayBufferSize - whole - 1) & mask];
            x = a + (b - a) * frac;

            const float dc = x - ch.dcX1 + m_dcCoeff * ch.dcY1;
            ch.dcX1 = x;
            ch.dcY1 = dc;

            buf[i] = dc * lowLevelCompensation * m_outputTrim;
        }
        m_writePos = (m_writePos + 1) & mask;
    }
}

} // namespace dawg::engine::dsp
