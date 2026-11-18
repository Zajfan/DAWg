#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dawg::engine {

struct LinkPeerInfo
{
    std::uint64_t peerId {0};
    std::string ipAddress;
    std::uint16_t port {0};
    double peerBpm {120.0};
    std::chrono::steady_clock::time_point lastSeen {};
};

struct LinkTimelineSnapshot
{
    double tempoBpm {120.0};
    double beatTime {0.0};
    double phase {0.0};
    double quantum {4.0};
    bool isPlaying {false};
    std::uint32_t peerCount {0};
    std::uint64_t sessionTimeMicros {0};
};

class AbletonLinkSessionEngine final
{
public:
    static constexpr std::uint16_t defaultLinkPort = 20808; // Standard Link UDP port
    static constexpr std::size_t maxPeers = 32;

    AbletonLinkSessionEngine(std::uint64_t localPeerId = 0xDA560001ULL) noexcept;
    ~AbletonLinkSessionEngine() = default;

    // Enable / disable Link synchronization
    void setEnabled(bool enabled) noexcept;
    [[nodiscard]] bool isEnabled() const noexcept;

    // Session Configuration
    void setTempo(double bpm) noexcept;
    [[nodiscard]] double tempo() const noexcept;

    void setQuantum(double quantum) noexcept;
    [[nodiscard]] double quantum() const noexcept;

    void setStartStopSyncEnabled(bool enabled) noexcept;
    [[nodiscard]] bool isStartStopSyncEnabled() const noexcept;

    // Local Transport Integration
    void requestPlay() noexcept;
    void requestStop() noexcept;
    [[nodiscard]] bool isPlaying() const noexcept;

    // Real-Time Audio Thread Clock Sync (Zero Allocations)
    // Updates local beat and phase based on sample delta and sample rate
    void advanceAudioClock(std::size_t numSamples, double sampleRate) noexcept;

    // Returns the current snapshot of the Link session timeline
    [[nodiscard]] LinkTimelineSnapshot captureSnapshot() const noexcept;

    // Maps local sample position to Link beat and phase
    [[nodiscard]] double phaseAtTime(double beatTime, double quantum) const noexcept;

    // Link v3 Network Protocol Packet Serialization
    enum class PacketType : std::uint8_t
    {
        ping,
        pong,
        stateUpdate,
        startStop
    };

    struct LinkPacket
    {
        PacketType type {PacketType::ping};
        std::uint64_t senderId {0};
        double tempoBpm {120.0};
        double beatTime {0.0};
        std::uint64_t sessionMicros {0};
        bool isPlaying {false};
    };

    [[nodiscard]] std::vector<std::uint8_t> encodePacket(const LinkPacket& packet) const;
    [[nodiscard]] static bool decodePacket(std::span<const std::uint8_t> buffer, LinkPacket& outPacket);

    // Process inbound Link UDP datagram from peer
    void processInboundPacket(std::span<const std::uint8_t> buffer, std::string_view fromIp, std::uint16_t fromPort) noexcept;

    // Periodic housekeeping: prunes dead peers and computes consensus tempo
    void updatePeers() noexcept;
    [[nodiscard]] std::size_t peerCount() const noexcept;

private:
    std::uint64_t m_localPeerId {0};
    std::atomic<bool> m_enabled {true};
    std::atomic<bool> m_startStopEnabled {true};
    std::atomic<bool> m_isPlaying {false};

    std::atomic<double> m_tempoBpm {120.0};
    std::atomic<double> m_quantum {4.0};
    std::atomic<double> m_beatTime {0.0};
    std::atomic<double> m_phase {0.0};

    std::uint64_t m_sessionMicros {0};

    std::array<LinkPeerInfo, maxPeers> m_peers {};
    std::size_t m_activePeerCount {0};
};

} // namespace dawg::engine
