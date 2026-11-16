#include <dawg/engine/dsp/SpringReverbMagneticDrumEchoNode.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace dawg::engine::dsp {

SpringReverbMagneticDrumEchoNode::SpringReverbMagneticDrumEchoNode() noexcept
{
    setPreampDriveDb(m_preampDriveDb);
    setSpringDecay(m_springDecaySec);
    setSpringDispersion(m_springDispersion);
    setSpringTone(m_springTone);

    // Default Binson Echorec head spacing and panning
    m_heads[0] = {true, -0.4f, 0.85f}; // Head 1: Left-center, 1/4 delay
    m_heads[1] = {true,  0.4f, 0.80f}; // Head 2: Right-center, 2/4 delay
    m_heads[2] = {false, -0.7f, 0.75f}; // Head 3: Left-wide, 3/4 delay (default bypass)
    m_heads[3] = {true,  0.0f, 0.90f}; // Head 4: Center feedback master, 4/4 delay
}

bool SpringReverbMagneticDrumEchoNode::prepare(
    const double sampleRate,
    const std::size_t maxBlockSize,
    const std::uint16_t channelCount)
{
    if (sampleRate <= 0.0 || maxBlockSize == 0 || channelCount == 0)
    {
        return false;
    }

    m_sampleRate = sampleRate;

    // Allocate drum echo buffers (~4 seconds max delay)
    const auto drumSize = std::max<std::size_t>(maxDelaySamples, static_cast<std::size_t>(sampleRate * 4.0));
    m_drumBufferL.assign(drumSize, 0.0f);
    m_drumBufferR.assign(drumSize, 0.0f);

    // Dual transmission springs: Spring A ~31ms, Spring B ~39ms (prime ratios prevent resonance pileup)
    m_springDelayLenL = std::max<std::size_t>(64, static_cast<std::size_t>(sampleRate * 0.0315));
    m_springDelayLenR = std::max<std::size_t>(64, static_cast<std::size_t>(sampleRate * 0.0392));
    m_springDelayBufL.assign(m_springDelayLenL, 0.0f);
    m_springDelayBufR.assign(m_springDelayLenR, 0.0f);

    setSpringDecay(m_springDecaySec);
    setSpringDispersion(m_springDispersion);
    setSpringTone(m_springTone);

    reset();
    return true;
}

void SpringReverbMagneticDrumEchoNode::reset() noexcept
{
    std::fill(m_drumBufferL.begin(), m_drumBufferL.end(), 0.0f);
    std::fill(m_drumBufferR.begin(), m_drumBufferR.end(), 0.0f);
    m_drumWritePos = 0;

    std::fill(m_springDelayBufL.begin(), m_springDelayBufL.end(), 0.0f);
    std::fill(m_springDelayBufR.begin(), m_springDelayBufR.end(), 0.0f);
    m_springPosL = 0;
    m_springPosR = 0;
    m_springFilterStateL = 0.0f;
    m_springFilterStateR = 0.0f;

    for (std::size_t s = 0; s < dispersionStages; ++s)
    {
        m_dispersionSpringL[s].reset();
        m_dispersionSpringR[s].reset();
    }

    m_dcBlockerIn = 0.0f;
    m_dcBlockerOut = 0.0f;
    m_rumblePhase = 0.0f;
    m_wowPhase = 0.0f;
    m_flutterPhase = 0.0f;
    m_feedbackFilterStateL = 0.0f;
    m_feedbackFilterStateR = 0.0f;
}

void SpringReverbMagneticDrumEchoNode::setPreampDriveDb(const float driveDb) noexcept
{
    m_preampDriveDb = std::clamp(driveDb, 0.0f, 24.0f);
    m_preampLinearGain = std::pow(10.0f, m_preampDriveDb / 20.0f);
}

void SpringReverbMagneticDrumEchoNode::setSpringDecay(const float decaySec) noexcept
{
    m_springDecaySec = std::clamp(decaySec, 0.5f, 6.0f);
    // T60 decay formula: g = 10^(-3 * (T_spring / T60))
    const float avgSpringDelaySec = static_cast<float>(m_springDelayLenL) / static_cast<float>(m_sampleRate);
    m_springFeedbackGain = std::clamp(std::pow(10.0f, -3.0f * (avgSpringDelaySec / m_springDecaySec)), 0.1f, 0.94f);
}

void SpringReverbMagneticDrumEchoNode::setSpringDispersion(const float dispersion) noexcept
{
    m_springDispersion = std::clamp(dispersion, 0.0f, 1.0f);
    // Maps 0.0..1.0 to allpass pole coefficient 0.20..0.78
    const float coeff = 0.20f + m_springDispersion * 0.58f;
    for (std::size_t s = 0; s < dispersionStages; ++s)
    {
        // Stagger coefficients slightly across stages to broaden chirp dispersion
        const float stageCoeff = std::clamp(coeff + static_cast<float>(s) * 0.02f, 0.1f, 0.85f);
        m_dispersionSpringL[s].coefficient = stageCoeff;
        m_dispersionSpringR[s].coefficient = stageCoeff;
    }
}

void SpringReverbMagneticDrumEchoNode::setSpringTone(const float tone) noexcept
{
    m_springTone = std::clamp(tone, 0.0f, 1.0f);
    // Damping coefficient in feedback loop: bright tone = low damping (0.1), dark tone = heavy damping (0.65)
    m_springDampingCoeff = 0.65f - m_springTone * 0.52f;
}

void SpringReverbMagneticDrumEchoNode::setDrumSpeedMs(const float speedMs) noexcept
{
    m_drumSpeedMs = std::clamp(speedMs, 50.0f, 800.0f);
}

void SpringReverbMagneticDrumEchoNode::setHeadEnabled(const std::uint8_t headIndex, const bool enabled) noexcept
{
    if (headIndex < numHeads)
    {
        m_heads[headIndex].enabled = enabled;
    }
}

bool SpringReverbMagneticDrumEchoNode::isHeadEnabled(const std::uint8_t headIndex) const noexcept
{
    if (headIndex < numHeads)
    {
        return m_heads[headIndex].enabled;
    }
    return false;
}

void SpringReverbMagneticDrumEchoNode::setHeadPan(const std::uint8_t headIndex, const float pan) noexcept
{
    if (headIndex < numHeads)
    {
        m_heads[headIndex].pan = std::clamp(pan, -1.0f, 1.0f);
    }
}

float SpringReverbMagneticDrumEchoNode::headPan(const std::uint8_t headIndex) const noexcept
{
    if (headIndex < numHeads)
    {
        return m_heads[headIndex].pan;
    }
    return 0.0f;
}

void SpringReverbMagneticDrumEchoNode::setHeadLevel(const std::uint8_t headIndex, const float level) noexcept
{
    if (headIndex < numHeads)
    {
        m_heads[headIndex].level = std::clamp(level, 0.0f, 1.0f);
    }
}

float SpringReverbMagneticDrumEchoNode::headLevel(const std::uint8_t headIndex) const noexcept
{
    if (headIndex < numHeads)
    {
        return m_heads[headIndex].level;
    }
    return 0.0f;
}

void SpringReverbMagneticDrumEchoNode::process(ProcessContext& context) noexcept
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

    const auto drumBufSize = m_drumBufferL.size();
    if (drumBufSize == 0 || m_springDelayBufL.empty())
    {
        return;
    }

    constexpr float twoPi = 2.0f * std::numbers::pi_v<float>;
    const float wowRate = 0.85f * twoPi / static_cast<float>(m_sampleRate);    // ~0.85 Hz motor wobble
    const float flutterRate = 9.2f * twoPi / static_cast<float>(m_sampleRate); // ~9.2 Hz belt flutter
    const float rumbleRate = 54.0f * twoPi / static_cast<float>(m_sampleRate); // 54 Hz mechanical hum

    // Hermite 4-point cubic interpolation for drum read head
    auto readDrumHermite = [drumBufSize](const std::vector<float>& buf, const std::size_t writePos, const float delaySamples) noexcept -> float
    {
        float readPos = static_cast<float>(writePos) - delaySamples;
        while (readPos < 0.0f) { readPos += static_cast<float>(drumBufSize); }
        while (readPos >= static_cast<float>(drumBufSize)) { readPos -= static_cast<float>(drumBufSize); }

        const auto i1 = static_cast<std::size_t>(readPos);
        const auto i0 = (i1 + drumBufSize - 1) % drumBufSize;
        const auto i2 = (i1 + 1) % drumBufSize;
        const auto i3 = (i1 + 2) % drumBufSize;

        const float frac = readPos - static_cast<float>(i1);
        const float y0 = buf[i0];
        const float y1 = buf[i1];
        const float y2 = buf[i2];
        const float y3 = buf[i3];

        const float c0 = y1;
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);

        return ((c3 * frac + c2) * frac + c1) * frac + c0;
    };

    for (std::size_t i = 0; i < frameCount; ++i)
    {
        const float dryL = ch0[i];
        const float dryR = (ch1 != nullptr) ? ch1[i] : dryL;

        // --- 1. Preamp Drive & Tube/Germanium Overdrive ---
        // Asymmetrical wave-shaping with warm 2nd harmonic excitation
        const float preDrivenL = dryL * m_preampLinearGain;
        const float preDrivenR = dryR * m_preampLinearGain;

        // Tube / Germanium non-linear saturator
        const float satL = std::tanh(preDrivenL + 0.12f * preDrivenL * std::abs(preDrivenL));
        const float satR = std::tanh(preDrivenR + 0.12f * preDrivenR * std::abs(preDrivenR));

        // DC blocker filter: y[n] = x[n] - x[n-1] + 0.995 * y[n-1]
        const float dcOutL = satL - m_dcBlockerIn + 0.995f * m_dcBlockerOut;
        m_dcBlockerIn = satL;
        m_dcBlockerOut = dcOutL;

        // Motor rumble mechanical injection
        m_rumblePhase += rumbleRate;
        if (m_rumblePhase >= twoPi) { m_rumblePhase -= twoPi; }
        const float rumbleNoise = std::sin(m_rumblePhase) * (m_motorRumble * 0.004f);

        const float drivenInL = dcOutL + rumbleNoise;
        const float drivenInR = satR + rumbleNoise;

        // --- 2. Rotating Magnetic Drum Echo Processing ---
        // Wow and flutter motor modulation
        m_wowPhase += wowRate;
        if (m_wowPhase >= twoPi) { m_wowPhase -= twoPi; }

        m_flutterPhase += flutterRate;
        if (m_flutterPhase >= twoPi) { m_flutterPhase -= twoPi; }

        const float wow = std::sin(m_wowPhase) * (0.0035f * m_wowFlutter);
        const float flutter = std::sin(m_flutterPhase) * (0.0018f * m_wowFlutter);
        const float motorSpeedMod = 1.0f + wow + flutter;

        const float baseDrumDelaySamples = m_drumSpeedMs * 0.001f * static_cast<float>(m_sampleRate) * motorSpeedMod;

        float drumWetL = 0.0f;
        float drumWetR = 0.0f;
        float feedbackSignalL = 0.0f;
        float feedbackSignalR = 0.0f;

        // Multi-head readout (4 virtual magnetic heads)
        for (std::size_t h = 0; h < numHeads; ++h)
        {
            if (!m_heads[h].enabled)
            {
                continue;
            }

            const float headDelayRatio = static_cast<float>(h + 1) * 0.25f; // 1/4, 2/4, 3/4, 4/4
            const float headDelay = std::clamp(baseDrumDelaySamples * headDelayRatio, 4.0f, static_cast<float>(drumBufSize - 8));

            const float tapSampleL = readDrumHermite(m_drumBufferL, m_drumWritePos, headDelay) * m_heads[h].level;
            const float tapSampleR = readDrumHermite(m_drumBufferR, m_drumWritePos, headDelay) * m_heads[h].level;

            // Stereo panning
            const float pan = m_heads[h].pan;
            const float gainL = 0.5f * (1.0f - pan);
            const float gainR = 0.5f * (1.0f + pan);

            drumWetL += tapSampleL * gainL;
            drumWetR += tapSampleR * gainR;

            // Head 4 (or longest active head) feeds the magnetic feedback loop
            if (h == 3 || feedbackSignalL == 0.0f)
            {
                feedbackSignalL = tapSampleL;
                feedbackSignalR = tapSampleR;
            }
        }

        // Drum Feedback Loop: Tape head high-cut filter (~3.8 kHz) + Magnetic tape saturation
        constexpr float fbFilterCoeff = 0.38f;
        m_feedbackFilterStateL = m_feedbackFilterStateL + fbFilterCoeff * (feedbackSignalL - m_feedbackFilterStateL);
        m_feedbackFilterStateR = m_feedbackFilterStateR + fbFilterCoeff * (feedbackSignalR - m_feedbackFilterStateR);

        // Soft-clip magnetic saturation: prevents runaway oscillation from creating harsh digital clipping
        const float satFbL = std::tanh(m_feedbackFilterStateL * 1.15f) * m_feedback;
        const float satFbR = std::tanh(m_feedbackFilterStateR * 1.15f) * m_feedback;

        // Determine input signal entering drum record head
        float drumInputL = drivenInL;
        float drumInputR = drivenInR;

        m_drumBufferL[m_drumWritePos] = drumInputL + satFbL;
        m_drumBufferR[m_drumWritePos] = drumInputR + satFbR;
        m_drumWritePos = (m_drumWritePos + 1) % drumBufSize;

        // --- 3. Dispersive Dual Coiled Transmission Springs ---
        // Input into spring: either driven input or output of drum echo depending on mode
        float springInL = drivenInL;
        float springInR = drivenInR;

        if (m_mode == SpringDrumMode::seriesDrumToSpring)
        {
            springInL = drumWetL * 0.85f + drivenInL * 0.15f;
            springInR = drumWetR * 0.85f + drivenInR * 0.15f;
        }

        // Allpass Dispersion Cascade: stretches high frequency wavefronts into chirp "boing"
        float dispersedL = springInL;
        float dispersedR = springInR;
        for (std::size_t s = 0; s < dispersionStages; ++s)
        {
            dispersedL = m_dispersionSpringL[s].process(dispersedL);
            dispersedR = m_dispersionSpringR[s].process(dispersedR);
        }

        // Read spring feedback delay line
        const float springDelayedL = m_springDelayBufL[m_springPosL];
        const float springDelayedR = m_springDelayBufR[m_springPosR];

        // 1-pole high frequency absorption / tone damping filter
        m_springFilterStateL = (1.0f - m_springDampingCoeff) * springDelayedL + m_springDampingCoeff * m_springFilterStateL;
        m_springFilterStateR = (1.0f - m_springDampingCoeff) * springDelayedR + m_springDampingCoeff * m_springFilterStateR;

        // Spring Rattle non-linear excursion saturation (mechanical clatter when hit hard)
        const float rattleL = m_springFilterStateL - (m_springRattle * 0.22f) * (m_springFilterStateL * m_springFilterStateL * m_springFilterStateL);
        const float rattleR = m_springFilterStateR - (m_springRattle * 0.22f) * (m_springFilterStateR * m_springFilterStateR * m_springFilterStateR);

        // Feedback write
        m_springDelayBufL[m_springPosL] = dispersedL + rattleL * m_springFeedbackGain;
        m_springDelayBufR[m_springPosR] = dispersedR + rattleR * m_springFeedbackGain;

        m_springPosL = (m_springPosL + 1) % m_springDelayLenL;
        m_springPosR = (m_springPosR + 1) % m_springDelayLenR;

        const float springWetL = dispersedL * 0.70f + springDelayedL * 1.2f;
        const float springWetR = dispersedR * 0.70f + springDelayedR * 1.2f;

        // --- 4. Mode Routing & Wet Summing ---
        float totalWetL = 0.0f;
        float totalWetR = 0.0f;

        switch (m_mode)
        {
        case SpringDrumMode::springOnly:
            totalWetL = springWetL;
            totalWetR = springWetR;
            break;
        case SpringDrumMode::drumEchoOnly:
            totalWetL = drumWetL;
            totalWetR = drumWetR;
            break;
        case SpringDrumMode::seriesDrumToSpring:
            totalWetL = springWetL * 0.7f + drumWetL * 0.4f;
            totalWetR = springWetR * 0.7f + drumWetR * 0.4f;
            break;
        case SpringDrumMode::parallel:
            totalWetL = (1.0f - m_blend) * drumWetL + m_blend * springWetL;
            totalWetR = (1.0f - m_blend) * drumWetR + m_blend * springWetR;
            break;
        }

        // --- 5. Wet / Dry Mix ---
        ch0[i] = (1.0f - m_mix) * dryL + m_mix * totalWetL;
        if (ch1 != nullptr)
        {
            ch1[i] = (1.0f - m_mix) * dryR + m_mix * totalWetR;
        }
    }
}

} // namespace dawg::engine::dsp
