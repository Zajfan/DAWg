#pragma once

#include <dawg/engine/AudioBufferView.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dawg::engine {

// Standard AES67 / RTP 12-byte uncompressed audio payload header
struct RtpAudioHeader
{
    std::uint8_t version {2};
    std::uint8_t payloadType {96}; // Dynamic PCM 24-bit / 32-bit float
    std::uint16_t sequenceNumber {0};
    std::uint32_t timestamp {0};
    std::uint32_t ssrc {0xDA560001};
};

struct StageViewTelemetry
{
    std::string sectionName {"Verse 1"};
    std::uint32_t currentBar {1};
    std::uint32_t currentBeat {1};
    double tempoBpm {120.0};
    double songTimeSeconds {0.0};
    std::string clickAccent {"high"}; // high, mid, low
    std::string nextCue {"Chorus in 4 bars"};
    bool isPlaying {false};
};

struct LanJamSyncMessage
{
    std::string senderId;
    std::uint32_t messageSequence {0};
    bool isPlaying {false};
    double bpm {120.0};
    std::int64_t samplePosition {0};
    std::uint32_t activeSceneIndex {0};
};

struct LanJamPeer
{
    std::string peerId;
    std::string peerName;
    std::string ipAddress;
    std::uint16_t port {9002};
    double latencyMs {0.0};
    bool isMaster {false};
};

class LanMulticastAudioStreamer final
{
public:
    static constexpr std::size_t maxPacketFrames = 256;
    static constexpr std::size_t maxChannels = 2; // Stereo LAN bus
    static constexpr std::size_t headerSize = 12;
    static constexpr std::size_t bytesPerSample = 3; // 24-bit linear PCM (AES67 standard)
    static constexpr std::size_t maxPacketPayloadBytes = maxPacketFrames * maxChannels * bytesPerSample;
    static constexpr std::size_t maxPacketTotalBytes = headerSize + maxPacketPayloadBytes;

    LanMulticastAudioStreamer(std::uint32_t ssrc = 0xDA560001) noexcept;
    ~LanMulticastAudioStreamer() = default;

    // Streamer configuration
    void setStreamSsrc(std::uint32_t ssrc) noexcept { m_ssrc = ssrc; }
    [[nodiscard]] std::uint32_t streamSsrc() const noexcept { return m_ssrc; }

    void setSampleRate(double sampleRate) noexcept { m_sampleRate = sampleRate; }
    [[nodiscard]] double sampleRate() const noexcept { return m_sampleRate; }

    // Real-Time Audio Thread Packetizer (Zero Heap Allocations)
    // Encodes stereo float buffers into 24-bit linear PCM network datagram
    [[nodiscard]] std::size_t packetizeAudio(
        const float* left,
        const float* right,
        std::size_t frameCount,
        std::span<std::uint8_t> outDatagramBuffer) noexcept;

    // Real-Time Audio Receiver / Depacketizer with Jitter & Drop Concealment
    // Decodes 24-bit linear PCM network datagram into float stereo buffers
    [[nodiscard]] bool depacketizeAudio(
        std::span<const std::uint8_t> inboundDatagram,
        float* outLeft,
        float* outRight,
        std::size_t maxFrames,
        std::size_t& outDecodedFrames) noexcept;

    // Statistics
    [[nodiscard]] std::uint64_t transmittedPackets() const noexcept { return m_txPackets; }
    [[nodiscard]] std::uint64_t receivedPackets() const noexcept { return m_rxPackets; }
    [[nodiscard]] std::uint64_t droppedPackets() const noexcept { return m_droppedPackets; }

    // =========================================================================
    // Remote Web/Tablet Stage View JSON Telemetry Generator
    // =========================================================================
    [[nodiscard]] static std::string formatStageViewJson(const StageViewTelemetry& telemetry);

    // =========================================================================
    // Multi-User LAN Jam P2P Transport & Scene Synchronization
    // =========================================================================
    [[nodiscard]] static std::string serializeJamSync(const LanJamSyncMessage& msg);
    [[nodiscard]] static std::optional<LanJamSyncMessage> deserializeJamSync(std::string_view jsonStr);

private:
    std::uint32_t m_ssrc {0xDA560001};
    double m_sampleRate {48000.0};
    std::uint16_t m_txSeqNumber {0};
    std::uint32_t m_txTimestamp {0};

    std::uint16_t m_lastRxSeqNumber {0};
    bool m_firstPacketReceived {false};

    std::uint64_t m_txPackets {0};
    std::uint64_t m_rxPackets {0};
    std::uint64_t m_droppedPackets {0};
};

} // namespace dawg::engine
