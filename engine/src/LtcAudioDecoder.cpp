#include <dawg/engine/LtcAudioDecoder.h>

#include <algorithm>
#include <cmath>

namespace dawg::engine {

namespace {

// SMPTE 12M sync word: 16 bits = 0011 1111 1111 1101 (forward, 0x3FFD)
constexpr std::uint16_t kLtcForwardSync = 0x3FFD;
// Reverse sync word: 1011 1111 1111 1100 (0xBFFC)
constexpr std::uint16_t kLtcReverseSync = 0xBFFC;

} // namespace

LtcAudioDecoder::LtcAudioDecoder(
    const double sampleRate,
    const SmpteFrameRate frameRate) noexcept
{
    configure(sampleRate, frameRate);
}

void LtcAudioDecoder::configure(const double sampleRate, const SmpteFrameRate frameRate) noexcept
{
    m_sampleRate = sampleRate > 1000.0 ? sampleRate : 48000.0;
    m_frameRate = frameRate;
    const double fps = SmpteTimecodeEngine::getFramesPerSecond(frameRate);
    m_bitDurationSamples = m_sampleRate / (fps * 80.0);
    reset();
}

void LtcAudioDecoder::reset() noexcept
{
    m_prevSample = 0.0f;
    m_samplesSinceLastTransition = 0;
    m_hasLock = false;
    m_isReverse = false;
    m_syncStreak = 0;
    m_totalDecodedFrames = 0;
    m_rawBits.fill(false);
    m_bitWritePos = 0;
    m_inHalfBit = false;
    m_latestTimecode = {};
}

void LtcAudioDecoder::processAudio(const float* samples, const std::size_t numSamples) noexcept
{
    if (samples == nullptr || numSamples == 0) return;

    for (std::size_t i = 0; i < numSamples; ++i)
    {
        const float sample = samples[i];
        ++m_samplesSinceLastTransition;

        // Zero-crossing edge detection with hysteresis threshold
        constexpr float threshold = 0.08f;
        const bool prevHigh = m_prevSample > threshold;
        const bool curLow = sample < -threshold;
        const bool prevLow = m_prevSample < -threshold;
        const bool curHigh = sample > threshold;

        if ((prevHigh && curLow) || (prevLow && curHigh))
        {
            handleTransition(m_samplesSinceLastTransition);
            m_samplesSinceLastTransition = 0;
            m_prevSample = sample;
        }
        else if (std::abs(sample) > threshold)
        {
            m_prevSample = sample;
        }

        // Loss of sync guard if no transition after 4 full bit periods
        if (m_samplesSinceLastTransition > static_cast<std::uint32_t>(m_bitDurationSamples * 4.0))
        {
            m_hasLock = false;
            m_syncStreak = 0;
            m_inHalfBit = false;
        }
    }
}

void LtcAudioDecoder::handleTransition(const std::uint32_t transitionIntervalSamples) noexcept
{
    const double interval = static_cast<double>(transitionIntervalSamples);
    const double halfBitPeriod = m_bitDurationSamples * 0.5;

    // Tolerance thresholds: short (half bit) vs long (full bit)
    const double shortLow = halfBitPeriod * 0.65;
    const double shortHigh = halfBitPeriod * 1.35;
    const double longLow = m_bitDurationSamples * 0.70;
    const double longHigh = m_bitDurationSamples * 1.30;

    if (interval >= shortLow && interval <= shortHigh)
    {
        // Half-bit transition: part of a '1' bit
        if (m_inHalfBit)
        {
            pushBit(true);
            m_inHalfBit = false;
        }
        else
        {
            m_inHalfBit = true;
        }
    }
    else if (interval >= longLow && interval <= longHigh)
    {
        // Full-bit transition: '0' bit
        pushBit(false);
        m_inHalfBit = false;
    }
    else
    {
        // Out-of-bounds jitter
        m_inHalfBit = false;
    }
}

void LtcAudioDecoder::pushBit(const bool bit) noexcept
{
    // Shift bit into circular buffer
    for (std::size_t i = 0; i < 79; ++i)
    {
        m_rawBits[i] = m_rawBits[i + 1];
    }
    m_rawBits[79] = bit;

    // Inspect last 16 bits for sync word (bits 64..79)
    std::uint16_t syncWord = 0;
    for (std::size_t i = 64; i < 80; ++i)
    {
        syncWord = static_cast<std::uint16_t>((syncWord << 1) | (m_rawBits[i] ? 1 : 0));
    }

    if (syncWord == kLtcForwardSync)
    {
        m_isReverse = false;
        ++m_syncStreak;
        if (m_syncStreak >= 1) m_hasLock = true;
        decodeCurrentFrame();
    }
    else if (syncWord == kLtcReverseSync)
    {
        m_isReverse = true;
        ++m_syncStreak;
        if (m_syncStreak >= 1) m_hasLock = true;
        decodeCurrentFrame();
    }
}

void LtcAudioDecoder::decodeCurrentFrame() noexcept
{
    // BCD extraction helper
    auto extractBcd = [this](std::size_t startBit, std::size_t numBits) -> std::uint32_t {
        std::uint32_t val = 0;
        for (std::size_t i = 0; i < numBits; ++i)
        {
            if (m_rawBits[startBit + i])
            {
                val |= (1u << i);
            }
        }
        return val;
    };

    // SMPTE 12M Frame layout:
    // Bits 0..3: Frame units (0..9)
    // Bits 8..9: Frame tens (0..2 or 0..3)
    const std::uint32_t frameUnits = extractBcd(0, 4);
    const std::uint32_t frameTens = extractBcd(8, 2);
    const std::uint32_t frameNum = frameTens * 10 + frameUnits;

    // Bit 10: Drop frame flag
    const bool dropFrame = m_rawBits[10];

    // Bits 16..19: Seconds units (0..9)
    // Bits 24..26: Seconds tens (0..5)
    const std::uint32_t secUnits = extractBcd(16, 4);
    const std::uint32_t secTens = extractBcd(24, 3);
    const std::uint32_t secNum = secTens * 10 + secUnits;

    // Bits 32..35: Minutes units (0..9)
    // Bits 40..42: Minutes tens (0..5)
    const std::uint32_t minUnits = extractBcd(32, 4);
    const std::uint32_t minTens = extractBcd(40, 3);
    const std::uint32_t minNum = minTens * 10 + minUnits;

    // Bits 48..51: Hours units (0..9)
    // Bits 56..57: Hours tens (0..2)
    const std::uint32_t hourUnits = extractBcd(48, 4);
    const std::uint32_t hourTens = extractBcd(56, 2);
    const std::uint32_t hourNum = hourTens * 10 + hourUnits;

    if (hourNum < 24 && minNum < 60 && secNum < 60 && frameNum < 60)
    {
        m_latestTimecode.hours = hourNum;
        m_latestTimecode.minutes = minNum;
        m_latestTimecode.seconds = secNum;
        m_latestTimecode.frames = frameNum;
        m_latestTimecode.dropFrame = dropFrame;
        m_latestTimecode.frameRate = m_frameRate;
        ++m_totalDecodedFrames;
    }
}

} // namespace dawg::engine
