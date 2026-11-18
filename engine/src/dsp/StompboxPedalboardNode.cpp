#include <dawg/engine/dsp/StompboxPedalboardNode.h>

#include <algorithm>
#include <cmath>

namespace dawg::engine::dsp {

namespace {
constexpr double kTwoPi = 6.283185307179586;

float onePoleCoeff(const double sampleRate, const double hz) noexcept
{
    const double fc = std::min(hz, 0.45 * sampleRate);
    return static_cast<float>(1.0 - std::exp(-kTwoPi * fc / sampleRate));
}

float dbToGain(const float db) noexcept { return std::pow(10.0f, db / 20.0f); }
} // namespace

// ---------------------------------------------------------------------------
// StompboxPedalboardNode
// ---------------------------------------------------------------------------

float StompboxPedalboardNode::diodeClip(const float x) noexcept
{
    // Both branches have unity slope at 0 (smooth), but saturate at different levels.
    if (x >= 0.0f)
        return 1.0f - std::exp(-x);
    constexpr float k = 1.4f;
    return -(1.0f - std::exp(k * x)) / k;
}

bool StompboxPedalboardNode::prepare(const double sampleRate, const std::size_t /*maxBlockSize*/,
                                     const std::uint16_t /*channelCount*/)
{
    if (sampleRate <= 0.0)
        return false;
    m_sampleRate = sampleRate;
    m_coef720 = onePoleCoeff(sampleRate, 720.0);
    m_coefPost = onePoleCoeff(sampleRate, 6000.0);
    m_coefTilt = onePoleCoeff(sampleRate, 1000.0);
    m_coefBbdLp = onePoleCoeff(sampleRate, 7000.0);
    m_dcCoeff = 1.0f - static_cast<float>(kTwoPi * 10.0 / sampleRate);
    m_ldrAttack = static_cast<float>(1.0 - std::exp(-1.0 / (0.005 * sampleRate)));
    m_ldrRelease = static_cast<float>(1.0 - std::exp(-1.0 / (0.060 * sampleRate)));
    setOverdrive(m_od);
    reset();
    return true;
}

void StompboxPedalboardNode::reset() noexcept
{
    for (auto& ch : m_channels)
    {
        ch.hpLp = ch.postLp = ch.tiltLp = 0.0f;
        ch.dcX1 = ch.dcY1 = 0.0f;
        ch.bbdLp = ch.bbdFeedback = 0.0f;
        ch.bbd.fill(0.0f);
    }
    m_bbdPhase = 0.0;
    m_tremPhase = 0.0;
    m_ldrState = 1.0f;
    m_lastTremGain = 1.0f;
    m_writePos = 0;
}

void StompboxPedalboardNode::setOverdrive(const OverdrivePedalParams& params) noexcept
{
    m_od = params;
    m_od.driveDb = std::clamp(m_od.driveDb, 0.0f, 40.0f);
    m_od.tone = std::clamp(m_od.tone, -1.0f, 1.0f);
    m_od.levelDb = std::clamp(m_od.levelDb, -24.0f, 12.0f);
    m_od.cleanBlend = std::clamp(m_od.cleanBlend, 0.0f, 1.0f);
    m_driveGain = dbToGain(m_od.driveDb);
    m_levelGain = dbToGain(m_od.levelDb);
    m_tiltLowGain = dbToGain(-6.0f * m_od.tone);
    m_tiltHighGain = dbToGain(6.0f * m_od.tone);
}

void StompboxPedalboardNode::setModulation(const BbdModulationParams& params) noexcept
{
    m_bbd = params;
    m_bbd.rateHz = std::clamp(m_bbd.rateHz, 0.05f, 8.0f);
    m_bbd.depth = std::clamp(m_bbd.depth, 0.0f, 1.0f);
    m_bbd.feedback = std::clamp(m_bbd.feedback, -0.9f, 0.9f);
    m_bbd.mix = std::clamp(m_bbd.mix, 0.0f, 1.0f);
}

void StompboxPedalboardNode::setTremolo(const OpticalTremoloParams& params) noexcept
{
    m_trem = params;
    m_trem.rateHz = std::clamp(m_trem.rateHz, 0.5f, 15.0f);
    m_trem.depth = std::clamp(m_trem.depth, 0.0f, 1.0f);
}

void StompboxPedalboardNode::process(ProcessContext& context) noexcept
{
    auto& audio = context.audio;
    if (audio.channels == nullptr || audio.channelCount == 0 || audio.frameCount == 0)
        return;

    const std::size_t numChannels = std::min(audio.channelCount, maxChannels);
    const auto fs = static_cast<float>(m_sampleRate);
    const bool flanger = m_bbd.mode == BbdModulationMode::flanger;
    const float baseDelay = (flanger ? 0.0010f : 0.0070f) * fs;
    const float depthDelay = (flanger ? 0.0009f : 0.0030f) * fs * m_bbd.depth;
    const double bbdInc = kTwoPi * static_cast<double>(m_bbd.rateHz) / m_sampleRate;
    const double tremInc = static_cast<double>(m_trem.rateHz) / m_sampleRate;
    constexpr std::size_t mask = bbdBufferSize - 1;

    for (std::size_t i = 0; i < audio.frameCount; ++i)
    {
        // Shared modulation sources
        m_bbdPhase += bbdInc;
        if (m_bbdPhase > kTwoPi) m_bbdPhase -= kTwoPi;

        float tremGain = 1.0f;
        if (m_trem.enabled)
        {
            m_tremPhase += tremInc;
            if (m_tremPhase >= 1.0) m_tremPhase -= 1.0;
            float lfo = m_trem.shape == TremoloShape::sine
                ? 0.5f + 0.5f * static_cast<float>(std::sin(kTwoPi * m_tremPhase))
                : static_cast<float>(m_tremPhase < 0.5 ? 2.0 * m_tremPhase : 2.0 - 2.0 * m_tremPhase);
            const float lamp = lfo * lfo; // incandescent brightness vs drive
            m_ldrState += (lamp - m_ldrState) * (lamp > m_ldrState ? m_ldrAttack : m_ldrRelease);
            tremGain = 1.0f - m_trem.depth * (1.0f - m_ldrState);
        }
        m_lastTremGain = tremGain;

        for (std::size_t c = 0; c < numChannels; ++c)
        {
            float* buf = audio.channels[c];
            if (buf == nullptr)
                continue;
            auto& ch = m_channels[c];
            float x = buf[i];

            if (m_od.enabled)
            {
                ch.hpLp += (x - ch.hpLp) * m_coef720;
                const float hp = x - ch.hpLp;
                float clipped = diodeClip(hp * m_driveGain);
                ch.postLp += (clipped - ch.postLp) * m_coefPost;
                clipped = ch.postLp;
                float od = m_od.cleanBlend * x + clipped;
                const float dc = od - ch.dcX1 + m_dcCoeff * ch.dcY1;
                ch.dcX1 = od;
                ch.dcY1 = dc;
                od = dc;
                ch.tiltLp += (od - ch.tiltLp) * m_coefTilt;
                x = (ch.tiltLp * m_tiltLowGain + (od - ch.tiltLp) * m_tiltHighGain) * m_levelGain;
            }

            if (m_bbd.enabled)
            {
                const double phase = m_bbdPhase + (c == 1 ? 0.25 * kTwoPi : 0.0);
                const float d = std::clamp(baseDelay + depthDelay * static_cast<float>(std::sin(phase)),
                                           1.0f, static_cast<float>(bbdBufferSize - 2));
                ch.bbd[m_writePos] = x + m_bbd.feedback * std::tanh(ch.bbdFeedback);
                const auto whole = static_cast<std::size_t>(d);
                const float frac = d - static_cast<float>(whole);
                const float a = ch.bbd[(m_writePos + bbdBufferSize - whole) & mask];
                const float b = ch.bbd[(m_writePos + bbdBufferSize - whole - 1) & mask];
                ch.bbdLp += ((a + (b - a) * frac) - ch.bbdLp) * m_coefBbdLp;
                ch.bbdFeedback = ch.bbdLp;
                x = x * (1.0f - m_bbd.mix) + ch.bbdLp * m_bbd.mix;
            }

            buf[i] = x * tremGain;
        }
        m_writePos = (m_writePos + 1) & mask;
    }
}

// ---------------------------------------------------------------------------
// DualIrCabinetNode
// ---------------------------------------------------------------------------

DualIrCabinetNode::DualIrCabinetNode() noexcept
{
    rebuildCombinedIr();
}

bool DualIrCabinetNode::prepare(const double sampleRate, const std::size_t /*maxBlockSize*/,
                                const std::uint16_t /*channelCount*/)
{
    if (sampleRate <= 0.0)
        return false;
    reset();
    return true;
}

void DualIrCabinetNode::reset() noexcept
{
    for (auto& h : m_history)
        h.fill(0.0f);
    m_historyPos = 0;
}

bool DualIrCabinetNode::loadImpulseResponse(const std::size_t slot, const float* ir, const std::size_t length) noexcept
{
    if (slot > 1 || ir == nullptr || length == 0)
        return false;
    const std::size_t len = std::min(length, maxIrLength);
    std::copy_n(ir, len, m_irs[slot].begin());
    std::fill(m_irs[slot].begin() + static_cast<std::ptrdiff_t>(len), m_irs[slot].end(), 0.0f);
    m_irLengths[slot] = len;
    rebuildCombinedIr();
    return true;
}

void DualIrCabinetNode::setMicBlend(const float blend) noexcept
{
    m_blend = std::clamp(blend, 0.0f, 1.0f);
    rebuildCombinedIr();
}

void DualIrCabinetNode::setMicLevelsDb(const float micADb, const float micBDb) noexcept
{
    m_levelA = dbToGain(std::clamp(micADb, -60.0f, 12.0f));
    m_levelB = dbToGain(std::clamp(micBDb, -60.0f, 12.0f));
    rebuildCombinedIr();
}

void DualIrCabinetNode::setMicBDelaySamples(const float samples) noexcept
{
    m_micBDelay = std::clamp(samples, 0.0f, static_cast<float>(maxMicDelaySamples));
    rebuildCombinedIr();
}

void DualIrCabinetNode::setMicBPolarityInverted(const bool inverted) noexcept
{
    m_micBInverted = inverted;
    rebuildCombinedIr();
}

void DualIrCabinetNode::setOutputDb(const float db) noexcept
{
    m_outputGain = dbToGain(std::clamp(db, -60.0f, 12.0f));
}

std::size_t DualIrCabinetNode::combinedIrLength() const noexcept
{
    return m_combinedLengths[static_cast<std::size_t>(m_activeIr.load(std::memory_order_acquire))];
}

float DualIrCabinetNode::combinedTap(const std::size_t index) const noexcept
{
    const auto active = static_cast<std::size_t>(m_activeIr.load(std::memory_order_acquire));
    return index < combinedLength ? m_combined[active][index] : 0.0f;
}

void DualIrCabinetNode::rebuildCombinedIr() noexcept
{
    const auto target = static_cast<std::size_t>(1 - m_activeIr.load(std::memory_order_acquire));
    auto& comb = m_combined[target];
    comb.fill(0.0f);

    if (m_irLengths[0] == 0 && m_irLengths[1] == 0)
    {
        comb[0] = 1.0f; // pass-through until an IR is loaded
        m_combinedLengths[target] = 1;
        m_activeIr.store(static_cast<int>(target), std::memory_order_release);
        return;
    }

    constexpr float piOver2 = 1.5707963f;
    const float gA = std::cos(m_blend * piOver2) * m_levelA;
    const float gB = std::sin(m_blend * piOver2) * m_levelB * (m_micBInverted ? -1.0f : 1.0f);

    for (std::size_t k = 0; k < m_irLengths[0]; ++k)
        comb[k] += gA * m_irs[0][k];

    // Mic B shifted by a fractional delay (linear interpolation of the IR itself)
    const auto whole = static_cast<std::size_t>(m_micBDelay);
    const float frac = m_micBDelay - static_cast<float>(whole);
    for (std::size_t k = 0; k < m_irLengths[1]; ++k)
    {
        const float v = m_irs[1][k] * gB;
        comb[k + whole] += v * (1.0f - frac);
        comb[k + whole + 1] += v * frac;
    }

    const std::size_t lenB = m_irLengths[1] > 0 ? m_irLengths[1] + whole + 2 : 0;
    m_combinedLengths[target] = std::min(combinedLength, std::max(m_irLengths[0], lenB));
    m_activeIr.store(static_cast<int>(target), std::memory_order_release);
}

CabinetMicAlignment DualIrCabinetNode::estimateAlignment(const std::size_t maxLag) const noexcept
{
    CabinetMicAlignment result;
    const std::size_t lenA = m_irLengths[0];
    const std::size_t lenB = m_irLengths[1];
    if (lenA == 0 || lenB == 0)
        return result;

    double energyA = 0.0, energyB = 0.0;
    for (std::size_t i = 0; i < lenA; ++i) energyA += static_cast<double>(m_irs[0][i]) * m_irs[0][i];
    for (std::size_t i = 0; i < lenB; ++i) energyB += static_cast<double>(m_irs[1][i]) * m_irs[1][i];
    const double norm = std::sqrt(energyA * energyB);
    if (norm <= 1e-12)
        return result;

    // r(d) = sum_n A[n] * B[n + d], d >= 0 (measures delay of Mic B relative to Mic A)
    const std::size_t lags = std::min(maxLag, maxMicDelaySamples);
    std::array<double, maxMicDelaySamples + 1> r {};
    std::size_t best = 0;
    for (std::size_t d = 0; d <= lags; ++d)
    {
        double sum = 0.0;
        for (std::size_t n = 0; n < lenA && n + d < lenB; ++n)
            sum += static_cast<double>(m_irs[0][n]) * m_irs[1][n + d];
        r[d] = sum;
        if (std::abs(r[d]) > std::abs(r[best]))
            best = d;
    }

    double offset = 0.0;
    if (best > 0 && best < lags)
    {
        const double a = std::abs(r[best - 1]), b = std::abs(r[best]), c = std::abs(r[best + 1]);
        const double denom = a - 2.0 * b + c;
        if (std::abs(denom) > 1e-15)
            offset = std::clamp(0.5 * (a - c) / denom, -0.5, 0.5);
    }

    result.delaySamples = static_cast<float>(static_cast<double>(best) + offset);
    result.invertPolarity = r[best] < 0.0;
    result.correlation = static_cast<float>(std::abs(r[best]) / norm);
    return result;
}

void DualIrCabinetNode::applyAutoAlignment() noexcept
{
    const auto align = estimateAlignment();
    m_micBDelay = std::clamp(align.delaySamples, 0.0f, static_cast<float>(maxMicDelaySamples));
    m_micBInverted = align.invertPolarity;
    rebuildCombinedIr();
}

void DualIrCabinetNode::process(ProcessContext& context) noexcept
{
    auto& audio = context.audio;
    if (audio.channels == nullptr || audio.channelCount == 0 || audio.frameCount == 0)
        return;

    const auto active = static_cast<std::size_t>(m_activeIr.load(std::memory_order_acquire));
    const auto& ir = m_combined[active];
    const std::size_t len = m_combinedLengths[active];
    const std::size_t numChannels = std::min(audio.channelCount, maxChannels);

    for (std::size_t i = 0; i < audio.frameCount; ++i)
    {
        for (std::size_t c = 0; c < numChannels; ++c)
        {
            float* buf = audio.channels[c];
            if (buf == nullptr)
                continue;
            auto& hist = m_history[c];
            hist[m_historyPos] = buf[i];
            hist[m_historyPos + combinedLength] = buf[i];

            // Newest sample sits at historyPos + combinedLength; walk backwards contiguously.
            const float* newest = hist.data() + m_historyPos + combinedLength;
            float acc = 0.0f;
            for (std::size_t k = 0; k < len; ++k)
                acc += ir[k] * newest[-static_cast<std::ptrdiff_t>(k)];
            buf[i] = acc * m_outputGain;
        }
        m_historyPos = (m_historyPos + 1) % combinedLength;
    }
}

} // namespace dawg::engine::dsp
