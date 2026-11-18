#include <dawg/engine/dsp/AnalogDegraderSuiteNode.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace dawg::engine::dsp {

namespace {

constexpr float pi = std::numbers::pi_v<float>;
constexpr double twoPi = 2.0 * std::numbers::pi_v<double>;

inline BiquadCoeffs makeLowShelf(float freqHz, float gainDb, double sampleRate) noexcept
{
    const float A = std::pow(10.0f, gainDb / 40.0f);
    const float w0 = 2.0f * pi * std::clamp(freqHz, 20.0f, static_cast<float>(sampleRate * 0.45)) / static_cast<float>(sampleRate);
    const float cosW = std::cos(w0);
    const float sinW = std::sin(w0);
    const float alpha = sinW * 0.5f * std::sqrt(2.0f);

    const float aP1 = A + 1.0f;
    const float aM1 = A - 1.0f;
    const float twoSqrtAAlpha = 2.0f * std::sqrt(A) * alpha;

    const float b0 = A * (aP1 - aM1 * cosW + twoSqrtAAlpha);
    const float b1 = 2.0f * A * (aM1 - aP1 * cosW);
    const float b2 = A * (aP1 - aM1 * cosW - twoSqrtAAlpha);
    const float a0 = aP1 + aM1 * cosW + twoSqrtAAlpha;
    const float a1 = -2.0f * (aM1 + aP1 * cosW);
    const float a2 = aP1 + aM1 * cosW - twoSqrtAAlpha;

    const float invA0 = 1.0f / a0;
    return BiquadCoeffs {
        .b0 = b0 * invA0,
        .b1 = b1 * invA0,
        .b2 = b2 * invA0,
        .a1 = a1 * invA0,
        .a2 = a2 * invA0
    };
}

inline BiquadCoeffs makeHighShelf(float freqHz, float gainDb, double sampleRate) noexcept
{
    const float A = std::pow(10.0f, gainDb / 40.0f);
    const float w0 = 2.0f * pi * std::clamp(freqHz, 20.0f, static_cast<float>(sampleRate * 0.45)) / static_cast<float>(sampleRate);
    const float cosW = std::cos(w0);
    const float sinW = std::sin(w0);
    const float alpha = sinW * 0.5f * std::sqrt(2.0f);

    const float aP1 = A + 1.0f;
    const float aM1 = A - 1.0f;
    const float twoSqrtAAlpha = 2.0f * std::sqrt(A) * alpha;

    const float b0 = A * (aP1 + aM1 * cosW + twoSqrtAAlpha);
    const float b1 = -2.0f * A * (aM1 + aP1 * cosW);
    const float b2 = A * (aP1 + aM1 * cosW - twoSqrtAAlpha);
    const float a0 = aP1 - aM1 * cosW + twoSqrtAAlpha;
    const float a1 = 2.0f * (aM1 - aP1 * cosW);
    const float a2 = aP1 - aM1 * cosW - twoSqrtAAlpha;

    const float invA0 = 1.0f / a0;
    return BiquadCoeffs {
        .b0 = b0 * invA0,
        .b1 = b1 * invA0,
        .b2 = b2 * invA0,
        .a1 = a1 * invA0,
        .a2 = a2 * invA0
    };
}

inline BiquadCoeffs makeBandpass(float centerHz, float q, double sampleRate) noexcept
{
    const float w0 = 2.0f * pi * std::clamp(centerHz, 20.0f, static_cast<float>(sampleRate * 0.45)) / static_cast<float>(sampleRate);
    const float cosW = std::cos(w0);
    const float sinW = std::sin(w0);
    const float alpha = sinW / (2.0f * std::max(0.1f, q));

    const float b0 = alpha;
    const float b1 = 0.0f;
    const float b2 = -alpha;
    const float a0 = 1.0f + alpha;
    const float a1 = -2.0f * cosW;
    const float a2 = 1.0f - alpha;

    const float invA0 = 1.0f / a0;
    return BiquadCoeffs {
        .b0 = b0 * invA0,
        .b1 = b1 * invA0,
        .b2 = b2 * invA0,
        .a1 = a1 * invA0,
        .a2 = a2 * invA0
    };
}

inline BiquadCoeffs makeHighpass(float cutoffHz, double sampleRate) noexcept
{
    const float w0 = 2.0f * pi * std::clamp(cutoffHz, 20.0f, static_cast<float>(sampleRate * 0.45)) / static_cast<float>(sampleRate);
    const float cosW = std::cos(w0);
    const float sinW = std::sin(w0);
    const float alpha = sinW / (2.0f * 0.7071f);

    const float b0 = (1.0f + cosW) * 0.5f;
    const float b1 = -(1.0f + cosW);
    const float b2 = (1.0f + cosW) * 0.5f;
    const float a0 = 1.0f + alpha;
    const float a1 = -2.0f * cosW;
    const float a2 = 1.0f - alpha;

    const float invA0 = 1.0f / a0;
    return BiquadCoeffs {
        .b0 = b0 * invA0,
        .b1 = b1 * invA0,
        .b2 = b2 * invA0,
        .a1 = a1 * invA0,
        .a2 = a2 * invA0
    };
}

} // namespace

AnalogDegraderSuiteNode::AnalogDegraderSuiteNode() noexcept
{
    m_delayBufferL.resize(maxDelayFrames, 0.0f);
    m_delayBufferR.resize(maxDelayFrames, 0.0f);
    updateFilters();
}

bool AnalogDegraderSuiteNode::prepare(double sampleRate, std::size_t /*maxBlockSize*/, std::uint16_t channelCount)
{
    m_sampleRate = (sampleRate > 0.0) ? sampleRate : 48000.0;
    m_channelCount = channelCount;
    reset();
    updateFilters();
    return true;
}

void AnalogDegraderSuiteNode::reset() noexcept
{
    std::fill(m_delayBufferL.begin(), m_delayBufferL.end(), 0.0f);
    std::fill(m_delayBufferR.begin(), m_delayBufferR.end(), 0.0f);
    m_writeIdx = 0;
    m_warpPhase = 0.0;
    m_azimuthJitterPhase = 0.0;
    m_dropoutGain = 1.0f;
    m_prngSeed = 0x12345678;

    m_headBumpStateL.reset();
    m_headBumpStateR.reset();
    m_highLossStateL.reset();
    m_highLossStateR.reset();
    m_microBandpassStateL.reset();
    m_microBandpassStateR.reset();
    m_monoBassStateSide.reset();
}

void AnalogDegraderSuiteNode::setProfile(DegraderProfile profile) noexcept
{
    m_profile = profile;
    updateFilters();
}

void AnalogDegraderSuiteNode::updateFilters() noexcept
{
    // Configure head bump & roll-off according to profile
    switch (m_profile)
    {
        case DegraderProfile::typeI_Ferric:
            m_headBumpCoeffs = makeLowShelf(70.0f, m_headBumpDb, m_sampleRate);
            m_highLossCoeffs = makeHighShelf(12500.0f, -4.0f, m_sampleRate);
            break;
        case DegraderProfile::typeII_Chrome:
            m_headBumpCoeffs = makeLowShelf(60.0f, m_headBumpDb * 0.7f, m_sampleRate);
            m_highLossCoeffs = makeHighShelf(15000.0f, -1.5f, m_sampleRate);
            break;
        case DegraderProfile::typeIV_Metal:
            m_headBumpCoeffs = makeLowShelf(50.0f, m_headBumpDb * 0.5f, m_sampleRate);
            m_highLossCoeffs = makeHighShelf(18000.0f, -0.5f, m_sampleRate);
            break;
        case DegraderProfile::microcassette:
            m_headBumpCoeffs = makeLowShelf(100.0f, -6.0f, m_sampleRate); // Bass cut
            m_highLossCoeffs = makeHighShelf(4000.0f, -18.0f, m_sampleRate); // Steep high cut
            m_microBandpassCoeffs = makeBandpass(1600.0f, 0.8f, m_sampleRate);
            break;
        case DegraderProfile::vhs_HiFi:
            m_headBumpCoeffs = makeLowShelf(80.0f, 2.0f, m_sampleRate);
            m_highLossCoeffs = makeHighShelf(11000.0f, -3.0f, m_sampleRate);
            break;
        case DegraderProfile::vinyl_33:
        case DegraderProfile::vinyl_45:
        case DegraderProfile::vinyl_78:
        {
            const float igdRollOff = 16000.0f - (m_radialPosition * 8000.0f);
            m_headBumpCoeffs = makeLowShelf(50.0f, 1.5f, m_sampleRate);
            m_highLossCoeffs = makeHighShelf(igdRollOff, -3.0f - (m_radialPosition * 6.0f), m_sampleRate);
            break;
        }
    }

    m_monoBassCoeffs = makeHighpass(150.0f, m_sampleRate);
}

float AnalogDegraderSuiteNode::readDelayFractional(
    const std::vector<float>& buffer,
    std::size_t writeIdx,
    double delaySamples) const noexcept
{
    const double maxFramesD = static_cast<double>(maxDelayFrames);
    double readPos = static_cast<double>(writeIdx) - delaySamples;
    while (readPos < 0.0)
    {
        readPos += maxFramesD;
    }
    while (readPos >= maxFramesD)
    {
        readPos -= maxFramesD;
    }

    const auto idx0 = static_cast<std::size_t>(readPos) % maxDelayFrames;
    const auto idx1 = (idx0 + 1) % maxDelayFrames;
    const float frac = static_cast<float>(readPos - static_cast<double>(idx0));

    return buffer[idx0] * (1.0f - frac) + buffer[idx1] * frac;
}

void AnalogDegraderSuiteNode::process(ProcessContext& context) noexcept
{
    const auto blockFrames = context.audio.frameCount;
    if (blockFrames == 0 || context.audio.channels == nullptr || context.audio.channelCount == 0)
    {
        return;
    }

    float* left = context.audio.channels[0];
    float* right = (context.audio.channelCount > 1 && context.audio.channels[1] != nullptr)
        ? context.audio.channels[1]
        : left;

    const float driveLinear = std::pow(10.0f, m_driveDb / 20.0f);
    const double azimuthBaseDelaySamples = (static_cast<double>(m_azimuthMicros) * 1e-6) * m_sampleRate;

    // Turntable RPM rate
    double rpmFreq = 0.555; // 33.33 RPM
    if (m_profile == DegraderProfile::vinyl_45)
    {
        rpmFreq = 0.75;
    }
    else if (m_profile == DegraderProfile::vinyl_78)
    {
        rpmFreq = 1.30;
    }

    const double warpInc = (rpmFreq / m_sampleRate) * twoPi;
    const double jitterInc = (3.5 / m_sampleRate) * twoPi; // 3.5 Hz flutter/jitter

    const bool isVinyl = (m_profile == DegraderProfile::vinyl_33 ||
                          m_profile == DegraderProfile::vinyl_45 ||
                          m_profile == DegraderProfile::vinyl_78);

    for (std::size_t i = 0; i < blockFrames; ++i)
    {
        float inL = left[i];
        float inR = right[i];

        // 1. Azimuth & delay write
        m_delayBufferL[m_writeIdx] = inL;
        m_delayBufferR[m_writeIdx] = inR;

        m_azimuthJitterPhase = std::fmod(m_azimuthJitterPhase + jitterInc, twoPi);
        const double jitter = static_cast<double>(m_azimuthJitter) * 2.0 * std::sin(m_azimuthJitterPhase);
        const double rightDelay = std::clamp(azimuthBaseDelaySamples + jitter, 0.0, static_cast<double>(maxDelayFrames - 4));

        // Read right channel with azimuth misalignment
        float delayedR = readDelayFractional(m_delayBufferR, m_writeIdx, rightDelay);

        // Turntable warp wow
        if (isVinyl && m_warpWowDepth > 0.001f)
        {
            m_warpPhase = std::fmod(m_warpPhase + warpInc, twoPi);
            const double wowMod = (std::sin(m_warpPhase) + 0.3 * std::sin(m_warpPhase * 2.0)) * (static_cast<double>(m_warpWowDepth) * 20.0);
            inL = readDelayFractional(m_delayBufferL, m_writeIdx, std::clamp(30.0 + wowMod, 0.0, static_cast<double>(maxDelayFrames - 4)));
            delayedR = readDelayFractional(m_delayBufferR, m_writeIdx, std::clamp(30.0 + rightDelay + wowMod, 0.0, static_cast<double>(maxDelayFrames - 4)));
        }

        m_writeIdx = (m_writeIdx + 1) % maxDelayFrames;

        // 2. Dropouts & mechanical tape crinkle
        m_prngSeed = m_prngSeed * 1664525u + 1013904223u;
        const float rnd = static_cast<float>(m_prngSeed & 0xFFFF) / 65535.0f;
        if (!isVinyl && m_dropoutRate > 0.001f && rnd < (m_dropoutRate * 0.0002f))
        {
            m_dropoutGain = 0.05f; // Momentary -26 dB drop
        }
        else
        {
            m_dropoutGain += (1.0f - m_dropoutGain) * 0.002f; // Smooth recovery over ~500 samples
        }

        inL *= m_dropoutGain;
        delayedR *= m_dropoutGain;

        // 3. Drive & Tape Hysteresis Saturation
        float saturatedL = inL * driveLinear;
        float saturatedR = delayedR * driveLinear;

        if (m_hysteresis > 0.001f || m_driveDb > 0.1f)
        {
            // Non-linear magnetic core magnetization
            const float k = 1.0f + m_hysteresis * 1.5f;
            const float bias = 0.05f * m_hysteresis; // Subtle even-harmonic asymmetry
            saturatedL = std::tanh(k * saturatedL + bias) - std::tanh(bias);
            saturatedR = std::tanh(k * saturatedR + bias) - std::tanh(bias);
        }

        // 4. Equalization & Head Bump / High Loss
        float procL = m_headBumpCoeffs.process(saturatedL, m_headBumpStateL);
        float procR = m_headBumpCoeffs.process(saturatedR, m_headBumpStateR);

        procL = m_highLossCoeffs.process(procL, m_highLossStateL);
        procR = m_highLossCoeffs.process(procR, m_highLossStateR);

        if (m_profile == DegraderProfile::microcassette)
        {
            procL = m_microBandpassCoeffs.process(procL, m_microBandpassStateL);
            procR = m_microBandpassCoeffs.process(procR, m_microBandpassStateR);
        }

        // 5. Vinyl crackle & dust pops
        if (isVinyl)
        {
            m_prngSeed = m_prngSeed * 1664525u + 1013904223u;
            const float crackleRnd = static_cast<float>(m_prngSeed & 0xFFFF) / 65535.0f;
            if (crackleRnd < (m_crackleLevel * 0.04f))
            {
                const float crackleNoise = (crackleRnd * 2.0f - 1.0f) * 0.08f * m_crackleLevel;
                procL += crackleNoise;
                procR += crackleNoise * 0.8f;
            }

            // Procedural dust pop impulses
            if (crackleRnd < (m_dustPopDensity * 0.0003f))
            {
                const float popAmp = (crackleRnd > 0.5f ? 0.35f : -0.35f);
                procL += popAmp;
                procR += popAmp * 0.7f;
            }

            // Inner groove distortion (high frequency saturation on louder signals)
            if (m_radialPosition > 0.5f)
            {
                const float igdFactor = (m_radialPosition - 0.5f) * 2.0f;
                procL = procL - 0.1f * igdFactor * (procL * procL * procL);
                procR = procR - 0.1f * igdFactor * (procR * procR * procR);
            }
        }

        // 6. Mono Bass elliptical crossover summing (essential for vinyl tracking)
        if (m_monoBassEnabled && (right != left))
        {
            const float mid = 0.5f * (procL + procR);
            float side = 0.5f * (procL - procR);

            // Highpass filter side signal so bass is strictly mono
            side = m_monoBassCoeffs.process(side, m_monoBassStateSide);

            procL = mid + side;
            procR = mid - side;
        }

        left[i] = procL;
        if (right != left)
        {
            right[i] = procR;
        }
    }
}

} // namespace dawg::engine::dsp
