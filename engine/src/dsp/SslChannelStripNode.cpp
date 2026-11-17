#include <dawg/engine/dsp/SslChannelStripNode.h>

#include <cmath>
#include <numbers>

namespace dawg::engine::dsp {

namespace {

constexpr double kPi = std::numbers::pi_v<double>;

// Helper to compute standard RBJ biquad coefficients
void computePeakingCoeffs(
    double sampleRate,
    double freqHz,
    double q,
    double gainDb,
    double& b0, double& b1, double& b2,
    double& a1, double& a2) noexcept
{
    if (std::abs(gainDb) < 0.01)
    {
        b0 = 1.0; b1 = 0.0; b2 = 0.0;
        a1 = 0.0; a2 = 0.0;
        return;
    }

    const double omega = 2.0 * kPi * std::clamp(freqHz, 10.0, sampleRate * 0.49) / sampleRate;
    const double sinOmega = std::sin(omega);
    const double cosOmega = std::cos(omega);
    const double A = std::pow(10.0, gainDb / 40.0);
    const double alpha = sinOmega / (2.0 * std::max(0.1, q));

    const double a0 = 1.0 + alpha / A;
    b0 = (1.0 + alpha * A) / a0;
    b1 = (-2.0 * cosOmega) / a0;
    b2 = (1.0 - alpha * A) / a0;
    a1 = (-2.0 * cosOmega) / a0;
    a2 = (1.0 - alpha / A) / a0;
}

void computeLowShelfCoeffs(
    double sampleRate,
    double freqHz,
    double gainDb,
    double& b0, double& b1, double& b2,
    double& a1, double& a2) noexcept
{
    if (std::abs(gainDb) < 0.01)
    {
        b0 = 1.0; b1 = 0.0; b2 = 0.0;
        a1 = 0.0; a2 = 0.0;
        return;
    }

    const double omega = 2.0 * kPi * std::clamp(freqHz, 10.0, sampleRate * 0.49) / sampleRate;
    const double sinOmega = std::sin(omega);
    const double cosOmega = std::cos(omega);
    const double A = std::pow(10.0, gainDb / 40.0);
    const double beta = std::sqrt(A + A);

    const double a0 = (A + 1.0) + (A - 1.0) * cosOmega + beta * sinOmega;
    b0 = (A * ((A + 1.0) - (A - 1.0) * cosOmega + beta * sinOmega)) / a0;
    b1 = (2.0 * A * ((A - 1.0) - (A + 1.0) * cosOmega)) / a0;
    b2 = (A * ((A + 1.0) - (A - 1.0) * cosOmega - beta * sinOmega)) / a0;
    a1 = (-2.0 * ((A - 1.0) + (A + 1.0) * cosOmega)) / a0;
    a2 = ((A + 1.0) + (A - 1.0) * cosOmega - beta * sinOmega) / a0;
}

void computeHighShelfCoeffs(
    double sampleRate,
    double freqHz,
    double gainDb,
    double& b0, double& b1, double& b2,
    double& a1, double& a2) noexcept
{
    if (std::abs(gainDb) < 0.01)
    {
        b0 = 1.0; b1 = 0.0; b2 = 0.0;
        a1 = 0.0; a2 = 0.0;
        return;
    }

    const double omega = 2.0 * kPi * std::clamp(freqHz, 10.0, sampleRate * 0.49) / sampleRate;
    const double sinOmega = std::sin(omega);
    const double cosOmega = std::cos(omega);
    const double A = std::pow(10.0, gainDb / 40.0);
    const double beta = std::sqrt(A + A);

    const double a0 = (A + 1.0) - (A - 1.0) * cosOmega + beta * sinOmega;
    b0 = (A * ((A + 1.0) + (A - 1.0) * cosOmega + beta * sinOmega)) / a0;
    b1 = (-2.0 * A * ((A - 1.0) + (A + 1.0) * cosOmega)) / a0;
    b2 = (A * ((A + 1.0) + (A - 1.0) * cosOmega - beta * sinOmega)) / a0;
    a1 = (2.0 * ((A - 1.0) - (A + 1.0) * cosOmega)) / a0;
    a2 = ((A + 1.0) - (A - 1.0) * cosOmega - beta * sinOmega) / a0;
}

void computeButterworthHighPass(
    double sampleRate,
    double freqHz,
    double q,
    double& b0, double& b1, double& b2,
    double& a1, double& a2) noexcept
{
    const double omega = 2.0 * kPi * std::clamp(freqHz, 10.0, sampleRate * 0.49) / sampleRate;
    const double sinOmega = std::sin(omega);
    const double cosOmega = std::cos(omega);
    const double alpha = sinOmega / (2.0 * std::max(0.1, q));

    const double a0 = 1.0 + alpha;
    b0 = (1.0 + cosOmega) * 0.5 / a0;
    b1 = -(1.0 + cosOmega) / a0;
    b2 = (1.0 + cosOmega) * 0.5 / a0;
    a1 = (-2.0 * cosOmega) / a0;
    a2 = (1.0 - alpha) / a0;
}

void computeButterworthLowPass(
    double sampleRate,
    double freqHz,
    double q,
    double& b0, double& b1, double& b2,
    double& a1, double& a2) noexcept
{
    const double omega = 2.0 * kPi * std::clamp(freqHz, 10.0, sampleRate * 0.49) / sampleRate;
    const double sinOmega = std::sin(omega);
    const double cosOmega = std::cos(omega);
    const double alpha = sinOmega / (2.0 * std::max(0.1, q));

    const double a0 = 1.0 + alpha;
    b0 = (1.0 - cosOmega) * 0.5 / a0;
    b1 = (1.0 - cosOmega) / a0;
    b2 = (1.0 - cosOmega) * 0.5 / a0;
    a1 = (-2.0 * cosOmega) / a0;
    a2 = (1.0 - alpha) / a0;
}

} // namespace

SslChannelStripNode::SslChannelStripNode() noexcept
{
    updateFilters();
}

bool SslChannelStripNode::prepare(
    double sampleRate,
    [[maybe_unused]] std::size_t maxBlockSize,
    [[maybe_unused]] std::uint16_t channelCount)
{
    m_sampleRate = (sampleRate > 1000.0) ? sampleRate : 44100.0;
    reset();
    updateFilters();
    return true;
}

void SslChannelStripNode::reset() noexcept
{
    for (auto& st : m_hpfState1) st.reset();
    for (auto& st : m_hpfState2) st.reset();
    for (auto& st : m_lpfState) st.reset();
    for (auto& st : m_lfState) st.reset();
    for (auto& st : m_lmfState) st.reset();
    for (auto& st : m_hmfState) st.reset();
    for (auto& st : m_hfState) st.reset();
}

void SslChannelStripNode::updateFilters() noexcept
{
    // High Pass Filter (cascaded 18 dB/oct 3rd-order Butterworth approximation)
    computeButterworthHighPass(m_sampleRate, m_hpfFreq, 1.0,
        m_hpfCoeffs1.b0, m_hpfCoeffs1.b1, m_hpfCoeffs1.b2,
        m_hpfCoeffs1.a1, m_hpfCoeffs1.a2);
    computeButterworthHighPass(m_sampleRate, m_hpfFreq, 0.5,
        m_hpfCoeffs2.b0, m_hpfCoeffs2.b1, m_hpfCoeffs2.b2,
        m_hpfCoeffs2.a1, m_hpfCoeffs2.a2);

    // Low Pass Filter (12 dB/oct 2nd-order Butterworth)
    computeButterworthLowPass(m_sampleRate, m_lpfFreq, 0.7071,
        m_lpfCoeffs.b0, m_lpfCoeffs.b1, m_lpfCoeffs.b2,
        m_lpfCoeffs.a1, m_lpfCoeffs.a2);

    // Low Band (LF)
    if (m_lfBell)
    {
        const double propQ = 0.8 * (1.0 + 0.05 * std::abs(m_lfGainDb));
        computePeakingCoeffs(m_sampleRate, m_lfFreq, propQ, m_lfGainDb,
            m_lfCoeffs.b0, m_lfCoeffs.b1, m_lfCoeffs.b2,
            m_lfCoeffs.a1, m_lfCoeffs.a2);
    }
    else
    {
        computeLowShelfCoeffs(m_sampleRate, m_lfFreq, m_lfGainDb,
            m_lfCoeffs.b0, m_lfCoeffs.b1, m_lfCoeffs.b2,
            m_lfCoeffs.a1, m_lfCoeffs.a2);
    }

    // Low-Mid Band (LMF) - Proportional Q and x3 multiplier
    const double effLmfFreq = m_lmfFreq * (m_lmfMult3x ? 3.0 : 1.0);
    const double propLmfQ = m_lmfQ * (1.0 + 0.08 * std::abs(m_lmfGainDb));
    computePeakingCoeffs(m_sampleRate, effLmfFreq, propLmfQ, m_lmfGainDb,
        m_lmfCoeffs.b0, m_lmfCoeffs.b1, m_lmfCoeffs.b2,
        m_lmfCoeffs.a1, m_lmfCoeffs.a2);

    // High-Mid Band (HMF) - Proportional Q and /3 divider
    const double effHmfFreq = m_hmfFreq * (m_hmfDiv3 ? (1.0 / 3.0) : 1.0);
    const double propHmfQ = m_hmfQ * (1.0 + 0.08 * std::abs(m_hmfGainDb));
    computePeakingCoeffs(m_sampleRate, effHmfFreq, propHmfQ, m_hmfGainDb,
        m_hmfCoeffs.b0, m_hmfCoeffs.b1, m_hmfCoeffs.b2,
        m_hmfCoeffs.a1, m_hmfCoeffs.a2);

    // High Band (HF)
    if (m_hfBell)
    {
        const double propQ = 0.8 * (1.0 + 0.05 * std::abs(m_hfGainDb));
        computePeakingCoeffs(m_sampleRate, m_hfFreq, propQ, m_hfGainDb,
            m_hfCoeffs.b0, m_hfCoeffs.b1, m_hfCoeffs.b2,
            m_hfCoeffs.a1, m_hfCoeffs.a2);
    }
    else
    {
        computeHighShelfCoeffs(m_sampleRate, m_hfFreq, m_hfGainDb,
            m_hfCoeffs.b0, m_hfCoeffs.b1, m_hfCoeffs.b2,
            m_hfCoeffs.a1, m_hfCoeffs.a2);
    }
}

void SslChannelStripNode::process(ProcessContext& context) noexcept
{
    auto& outBuffer = context.audio;
    const std::size_t numFrames = outBuffer.frameCount;
    const std::size_t numChannels = outBuffer.channelCount;

    if (numFrames == 0 || numChannels == 0 || outBuffer.channels == nullptr)
        return;

    // Fast-path: if muted, zero the buffer
    if (m_muted)
    {
        for (std::size_t ch = 0; ch < numChannels; ++ch)
        {
            float* chData = outBuffer.channels[ch];
            if (chData != nullptr)
                std::fill_n(chData, numFrames, 0.0f);
        }
        return;
    }

    const float faderLinear = (m_faderGainDb <= -99.0f) ? 0.0f : std::pow(10.0f, m_faderGainDb / 20.0f);
    const auto panGains = calculatePanLawGains(m_pan, m_panLaw);
    const float effGainL = faderLinear * panGains.gainL * (m_phaseInvertL ? -1.0f : 1.0f);
    const float effGainR = faderLinear * panGains.gainR * (m_phaseInvertR ? -1.0f : 1.0f);

    float* leftData = outBuffer.channels[0];
    float* rightData = (numChannels > 1 && outBuffer.channels[1] != nullptr) ? outBuffer.channels[1] : leftData;

    for (std::size_t i = 0; i < numFrames; ++i)
    {
        double sampleL = static_cast<double>(leftData[i]);
        double sampleR = static_cast<double>(rightData[i]);

        // Preamp saturation stage
        if (m_drive > 0.001f)
        {
            const double driveScale = 1.0 + m_drive * 3.0;
            sampleL *= driveScale;
            sampleR *= driveScale;

            // SSL console line amplifier soft-curve saturation
            sampleL = std::tanh(sampleL + 0.1 * m_drive * sampleL * sampleL);
            sampleR = std::tanh(sampleR + 0.1 * m_drive * sampleR * sampleR);
        }

        // High Pass Filter
        if (m_hpfEnabled)
        {
            sampleL = m_hpfState1[0].process(sampleL, m_hpfCoeffs1.b0, m_hpfCoeffs1.b1, m_hpfCoeffs1.b2, m_hpfCoeffs1.a1, m_hpfCoeffs1.a2);
            sampleL = m_hpfState2[0].process(sampleL, m_hpfCoeffs2.b0, m_hpfCoeffs2.b1, m_hpfCoeffs2.b2, m_hpfCoeffs2.a1, m_hpfCoeffs2.a2);

            sampleR = m_hpfState1[1].process(sampleR, m_hpfCoeffs1.b0, m_hpfCoeffs1.b1, m_hpfCoeffs1.b2, m_hpfCoeffs1.a1, m_hpfCoeffs1.a2);
            sampleR = m_hpfState2[1].process(sampleR, m_hpfCoeffs2.b0, m_hpfCoeffs2.b1, m_hpfCoeffs2.b2, m_hpfCoeffs2.a1, m_hpfCoeffs2.a2);
        }

        // Low Band (LF)
        sampleL = m_lfState[0].process(sampleL, m_lfCoeffs.b0, m_lfCoeffs.b1, m_lfCoeffs.b2, m_lfCoeffs.a1, m_lfCoeffs.a2);
        sampleR = m_lfState[1].process(sampleR, m_lfCoeffs.b0, m_lfCoeffs.b1, m_lfCoeffs.b2, m_lfCoeffs.a1, m_lfCoeffs.a2);

        // Low-Mid Band (LMF)
        sampleL = m_lmfState[0].process(sampleL, m_lmfCoeffs.b0, m_lmfCoeffs.b1, m_lmfCoeffs.b2, m_lmfCoeffs.a1, m_lmfCoeffs.a2);
        sampleR = m_lmfState[1].process(sampleR, m_lmfCoeffs.b0, m_lmfCoeffs.b1, m_lmfCoeffs.b2, m_lmfCoeffs.a1, m_lmfCoeffs.a2);

        // High-Mid Band (HMF)
        sampleL = m_hmfState[0].process(sampleL, m_hmfCoeffs.b0, m_hmfCoeffs.b1, m_hmfCoeffs.b2, m_hmfCoeffs.a1, m_hmfCoeffs.a2);
        sampleR = m_hmfState[1].process(sampleR, m_hmfCoeffs.b0, m_hmfCoeffs.b1, m_hmfCoeffs.b2, m_hmfCoeffs.a1, m_hmfCoeffs.a2);

        // High Band (HF)
        sampleL = m_hfState[0].process(sampleL, m_hfCoeffs.b0, m_hfCoeffs.b1, m_hfCoeffs.b2, m_hfCoeffs.a1, m_hfCoeffs.a2);
        sampleR = m_hfState[1].process(sampleR, m_hfCoeffs.b0, m_hfCoeffs.b1, m_hfCoeffs.b2, m_hfCoeffs.a1, m_hfCoeffs.a2);

        // Low Pass Filter
        if (m_lpfEnabled)
        {
            sampleL = m_lpfState[0].process(sampleL, m_lpfCoeffs.b0, m_lpfCoeffs.b1, m_lpfCoeffs.b2, m_lpfCoeffs.a1, m_lpfCoeffs.a2);
            sampleR = m_lpfState[1].process(sampleR, m_lpfCoeffs.b0, m_lpfCoeffs.b1, m_lpfCoeffs.b2, m_lpfCoeffs.a1, m_lpfCoeffs.a2);
        }

        // Stereo Width (Mid-Side matrix)
        if (numChannels > 1 && std::abs(m_width - 1.0f) > 0.001f)
        {
            const double mid = 0.5 * (sampleL + sampleR);
            const double side = 0.5 * (sampleL - sampleR) * m_width;
            sampleL = mid + side;
            sampleR = mid - side;
        }

        // Fader gain, pan law, and phase
        leftData[i] = static_cast<float>(sampleL) * effGainL;
        if (numChannels > 1)
        {
            rightData[i] = static_cast<float>(sampleR) * effGainR;
        }
    }
}

} // namespace dawg::engine::dsp
