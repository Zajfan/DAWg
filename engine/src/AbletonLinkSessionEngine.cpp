#include <dawg/engine/AbletonLinkSessionEngine.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>

namespace dawg::engine {

namespace {

inline void writeU64(std::vector<std::uint8_t>& buf, const std::uint64_t val)
{
    for (int i = 7; i >= 0; --i)
        buf.push_back(static_cast<std::uint8_t>((val >> (i * 8)) & 0xFF));
}

inline std::uint64_t readU64(const std::uint8_t* p) noexcept
{
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i)
        v = (v << 8) | p[i];
    return v;
}

inline void writeDouble(std::vector<std::uint8_t>& buf, const double val)
{
    const auto u = std::bit_cast<std::uint64_t>(val);
    writeU64(buf, u);
}

inline double readDouble(const std::uint8_t* p) noexcept
{
    const std::uint64_t u = readU64(p);
    return std::bit_cast<double>(u);
}

} // namespace

AbletonLinkSessionEngine::AbletonLinkSessionEngine(const std::uint64_t localPeerId) noexcept
    : m_localPeerId(localPeerId)
{
}

void AbletonLinkSessionEngine::setEnabled(const bool enabled) noexcept
{
    m_enabled.store(enabled, std::memory_order_release);
}

bool AbletonLinkSessionEngine::isEnabled() const noexcept
{
    return m_enabled.load(std::memory_order_acquire);
}

void AbletonLinkSessionEngine::setTempo(const double bpm) noexcept
{
    m_tempoBpm.store(std::clamp(bpm, 20.0, 999.0), std::memory_order_release);
}

double AbletonLinkSessionEngine::tempo() const noexcept
{
    return m_tempoBpm.load(std::memory_order_acquire);
}

void AbletonLinkSessionEngine::setQuantum(const double quantum) noexcept
{
    m_quantum.store(std::clamp(quantum, 1.0, 32.0), std::memory_order_release);
}

double AbletonLinkSessionEngine::quantum() const noexcept
{
    return m_quantum.load(std::memory_order_acquire);
}

void AbletonLinkSessionEngine::setStartStopSyncEnabled(const bool enabled) noexcept
{
    m_startStopEnabled.store(enabled, std::memory_order_release);
}

bool AbletonLinkSessionEngine::isStartStopSyncEnabled() const noexcept
{
    return m_startStopEnabled.load(std::memory_order_acquire);
}

void AbletonLinkSessionEngine::requestPlay() noexcept
{
    m_isPlaying.store(true, std::memory_order_release);
}

void AbletonLinkSessionEngine::requestStop() noexcept
{
    m_isPlaying.store(false, std::memory_order_release);
}

bool AbletonLinkSessionEngine::isPlaying() const noexcept
{
    return m_isPlaying.load(std::memory_order_acquire);
}

void AbletonLinkSessionEngine::advanceAudioClock(const std::size_t numSamples, const double sampleRate) noexcept
{
    if (! m_enabled.load(std::memory_order_relaxed) || sampleRate <= 0.0)
        return;

    const double bpm = m_tempoBpm.load(std::memory_order_relaxed);
    const double beatsPerSecond = bpm / 60.0;
    const double deltaBeats = (static_cast<double>(numSamples) / sampleRate) * beatsPerSecond;

    if (m_isPlaying.load(std::memory_order_relaxed))
    {
        double currentBeat = m_beatTime.load(std::memory_order_relaxed);
        currentBeat += deltaBeats;
        m_beatTime.store(currentBeat, std::memory_order_relaxed);

        const double q = m_quantum.load(std::memory_order_relaxed);
        const double p = phaseAtTime(currentBeat, q);
        m_phase.store(p, std::memory_order_relaxed);
    }

    const auto micros = static_cast<std::uint64_t>((static_cast<double>(numSamples) / sampleRate) * 1000000.0);
    m_sessionMicros += micros;
}

LinkTimelineSnapshot AbletonLinkSessionEngine::captureSnapshot() const noexcept
{
    LinkTimelineSnapshot snap;
    snap.tempoBpm = m_tempoBpm.load(std::memory_order_acquire);
    snap.beatTime = m_beatTime.load(std::memory_order_acquire);
    snap.phase = m_phase.load(std::memory_order_acquire);
    snap.quantum = m_quantum.load(std::memory_order_acquire);
    snap.isPlaying = m_isPlaying.load(std::memory_order_acquire);
    snap.peerCount = static_cast<std::uint32_t>(m_activePeerCount);
    snap.sessionTimeMicros = m_sessionMicros;
    return snap;
}

double AbletonLinkSessionEngine::phaseAtTime(const double beatTime, const double quantum) const noexcept
{
    if (quantum <= 0.0) return 0.0;
    double p = std::fmod(beatTime, quantum);
    if (p < 0.0) p += quantum;
    return p;
}

std::vector<std::uint8_t> AbletonLinkSessionEngine::encodePacket(const LinkPacket& packet) const
{
    std::vector<std::uint8_t> buf;
    buf.reserve(48);

    // Magic: "LINK"
    buf.push_back('L');
    buf.push_back('I');
    buf.push_back('N');
    buf.push_back('K');

    buf.push_back(static_cast<std::uint8_t>(packet.type));
    buf.push_back(packet.isPlaying ? 1 : 0);
    buf.push_back(0); // Reserved
    buf.push_back(0);

    writeU64(buf, packet.senderId);
    writeDouble(buf, packet.tempoBpm);
    writeDouble(buf, packet.beatTime);
    writeU64(buf, packet.sessionMicros);

    return buf;
}

bool AbletonLinkSessionEngine::decodePacket(const std::span<const std::uint8_t> buffer, LinkPacket& outPacket)
{
    if (buffer.size() < 40)
        return false;

    if (buffer[0] != 'L' || buffer[1] != 'I' || buffer[2] != 'N' || buffer[3] != 'K')
        return false;

    outPacket.type = static_cast<PacketType>(buffer[4]);
    outPacket.isPlaying = (buffer[5] != 0);

    const std::uint8_t* p = buffer.data() + 8;
    outPacket.senderId = readU64(p); p += 8;
    outPacket.tempoBpm = readDouble(p); p += 8;
    outPacket.beatTime = readDouble(p); p += 8;
    outPacket.sessionMicros = readU64(p);

    return true;
}

void AbletonLinkSessionEngine::processInboundPacket(
    const std::span<const std::uint8_t> buffer,
    const std::string_view fromIp,
    const std::uint16_t fromPort) noexcept
{
    LinkPacket pkt;
    if (! decodePacket(buffer, pkt))
        return;

    // Ignore self packets
    if (pkt.senderId == m_localPeerId)
        return;

    const auto now = std::chrono::steady_clock::now();

    // Find or add peer
    std::size_t foundIdx = maxPeers;
    for (std::size_t i = 0; i < m_activePeerCount; ++i)
    {
        if (m_peers[i].peerId == pkt.senderId)
        {
            foundIdx = i;
            break;
        }
    }

    if (foundIdx < maxPeers)
    {
        m_peers[foundIdx].lastSeen = now;
        m_peers[foundIdx].peerBpm = pkt.tempoBpm;
        m_peers[foundIdx].port = fromPort;
    }
    else if (m_activePeerCount < maxPeers)
    {
        m_peers[m_activePeerCount].peerId = pkt.senderId;
        m_peers[m_activePeerCount].ipAddress = std::string(fromIp);
        m_peers[m_activePeerCount].port = fromPort;
        m_peers[m_activePeerCount].peerBpm = pkt.tempoBpm;
        m_peers[m_activePeerCount].lastSeen = now;
        ++m_activePeerCount;
    }

    // In Link v3, if start/stop sync is enabled and state changes:
    if (m_startStopEnabled.load(std::memory_order_relaxed))
    {
        if (pkt.type == PacketType::startStop || pkt.type == PacketType::stateUpdate)
        {
            m_isPlaying.store(pkt.isPlaying, std::memory_order_release);
        }
    }

    // Adapt tempo towards peer if changed
    if (std::abs(pkt.tempoBpm - m_tempoBpm.load(std::memory_order_relaxed)) > 0.05)
    {
        m_tempoBpm.store(pkt.tempoBpm, std::memory_order_release);
    }
}

void AbletonLinkSessionEngine::updatePeers() noexcept
{
    const auto now = std::chrono::steady_clock::now();
    std::size_t writeIdx = 0;

    for (std::size_t i = 0; i < m_activePeerCount; ++i)
    {
        const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - m_peers[i].lastSeen).count();
        if (elapsed < 3) // Peer active within last 3 seconds
        {
            if (writeIdx != i)
                m_peers[writeIdx] = m_peers[i];
            ++writeIdx;
        }
    }

    m_activePeerCount = writeIdx;
}

std::size_t AbletonLinkSessionEngine::peerCount() const noexcept
{
    return m_activePeerCount;
}

} // namespace dawg::engine
