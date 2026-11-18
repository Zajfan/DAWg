#pragma once

#include <dawg/engine/SmpteTimecode.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace dawg::engine {

/// Real-time SMPTE 12M Linear Timecode (LTC) Audio Decoder.
/// Slaves DAWg to external analog/digital LTC audio signals, broadcast decks, and master clocks.
/// Zero heap allocations in processAudio().
class LtcAudioDecoder final
{
public:
    explicit LtcAudioDecoder(
        double sampleRate = 48000.0,
        SmpteFrameRate frameRate = SmpteFrameRate::fps24) noexcept;

    ~LtcAudioDecoder() = default;

    /// Configures sample rate and expected SMPTE frame rate
    void configure(double sampleRate, SmpteFrameRate frameRate) noexcept;

    /// Ingests incoming mono audio buffer and extracts timecode frames
    void processAudio(const float* samples, std::size_t numSamples) noexcept;

    /// Resets decoder state and drops sync lock
    void reset() noexcept;

    /// Returns true if valid SMPTE 12M LTC sync words have been locked
    [[nodiscard]] bool hasLock() const noexcept { return m_hasLock; }

    /// Returns the most recently decoded timecode frame
    [[nodiscard]] SmpteTimecode latestTimecode() const noexcept { return m_latestTimecode; }

    /// Total valid timecode frames decoded since reset
    [[nodiscard]] std::uint64_t totalDecodedFrames() const noexcept { return m_totalDecodedFrames; }

    /// Returns true if LTC timecode is playing backwards
    [[nodiscard]] bool isReverse() const noexcept { return m_isReverse; }

private:
    void handleTransition(std::uint32_t transitionIntervalSamples) noexcept;
    void pushBit(bool bit) noexcept;
    void decodeCurrentFrame() noexcept;

    double m_sampleRate {48000.0};
    SmpteFrameRate m_frameRate {SmpteFrameRate::fps24};
    double m_bitDurationSamples {25.0}; // sampleRate / (fps * 80)

    float m_prevSample {0.0f};
    std::uint32_t m_samplesSinceLastTransition {0};
    bool m_hasLock {false};
    bool m_isReverse {false};
    std::uint32_t m_syncStreak {0};
    std::uint64_t m_totalDecodedFrames {0};

    // Bit accumulator: 80 bits per LTC frame
    std::array<bool, 80> m_rawBits {};
    std::size_t m_bitWritePos {0};
    bool m_inHalfBit {false};

    SmpteTimecode m_latestTimecode {};
};

} // namespace dawg::engine
