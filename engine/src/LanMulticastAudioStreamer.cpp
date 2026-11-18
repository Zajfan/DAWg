#include <dawg/engine/LanMulticastAudioStreamer.h>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <optional>
#include <sstream>

namespace dawg::engine {

namespace {

inline std::int32_t floatToPcm24(const float s) noexcept
{
    const float clamped = std::clamp(s, -1.0f, 1.0f);
    return static_cast<std::int32_t>(clamped * 8388607.0f); // 2^23 - 1
}

inline float pcm24ToFloat(const std::int32_t val) noexcept
{
    return static_cast<float>(val) / 8388607.0f;
}

} // namespace

LanMulticastAudioStreamer::LanMulticastAudioStreamer(const std::uint32_t ssrc) noexcept
    : m_ssrc(ssrc)
{
}

std::size_t LanMulticastAudioStreamer::packetizeAudio(
    const float* left,
    const float* right,
    const std::size_t frameCount,
    const std::span<std::uint8_t> outDatagramBuffer) noexcept
{
    const std::size_t frames = std::min(frameCount, maxPacketFrames);
    const std::size_t payloadBytes = frames * maxChannels * bytesPerSample;
    const std::size_t totalBytes = headerSize + payloadBytes;

    if (outDatagramBuffer.size() < totalBytes || left == nullptr || right == nullptr)
        return 0;

    std::uint8_t* p = outDatagramBuffer.data();

    // 1. RTP Header (12 bytes, big-endian)
    p[0] = 0x80; // V=2, P=0, X=0, CC=0
    p[1] = 96;   // Payload type: dynamic uncompressed 24-bit PCM
    p[2] = static_cast<std::uint8_t>((m_txSeqNumber >> 8) & 0xFF);
    p[3] = static_cast<std::uint8_t>(m_txSeqNumber & 0xFF);
    p[4] = static_cast<std::uint8_t>((m_txTimestamp >> 24) & 0xFF);
    p[5] = static_cast<std::uint8_t>((m_txTimestamp >> 16) & 0xFF);
    p[6] = static_cast<std::uint8_t>((m_txTimestamp >> 8)  & 0xFF);
    p[7] = static_cast<std::uint8_t>(m_txTimestamp & 0xFF);
    p[8] = static_cast<std::uint8_t>((m_ssrc >> 24) & 0xFF);
    p[9] = static_cast<std::uint8_t>((m_ssrc >> 16) & 0xFF);
    p[10] = static_cast<std::uint8_t>((m_ssrc >> 8)  & 0xFF);
    p[11] = static_cast<std::uint8_t>(m_ssrc & 0xFF);

    p += headerSize;

    // 2. Interleaved 24-bit big-endian PCM payload: [L2, L1, L0, R2, R1, R0, ...]
    for (std::size_t i = 0; i < frames; ++i)
    {
        const std::int32_t sL = floatToPcm24(left[i]);
        *p++ = static_cast<std::uint8_t>((sL >> 16) & 0xFF);
        *p++ = static_cast<std::uint8_t>((sL >> 8)  & 0xFF);
        *p++ = static_cast<std::uint8_t>(sL & 0xFF);

        const std::int32_t sR = floatToPcm24(right[i]);
        *p++ = static_cast<std::uint8_t>((sR >> 16) & 0xFF);
        *p++ = static_cast<std::uint8_t>((sR >> 8)  & 0xFF);
        *p++ = static_cast<std::uint8_t>(sR & 0xFF);
    }

    ++m_txSeqNumber;
    m_txTimestamp += static_cast<std::uint32_t>(frames);
    ++m_txPackets;

    return totalBytes;
}

bool LanMulticastAudioStreamer::depacketizeAudio(
    const std::span<const std::uint8_t> inboundDatagram,
    float* outLeft,
    float* outRight,
    const std::size_t maxFrames,
    std::size_t& outDecodedFrames) noexcept
{
    outDecodedFrames = 0;
    if (inboundDatagram.size() < headerSize || outLeft == nullptr || outRight == nullptr)
        return false;

    const std::uint8_t* p = inboundDatagram.data();
    const std::uint8_t v = (p[0] >> 6) & 0x03;
    if (v != 2)
        return false;

    const auto seq = static_cast<std::uint16_t>((static_cast<std::uint16_t>(p[2]) << 8) | p[3]);

    if (m_firstPacketReceived)
    {
        const auto expectedSeq = static_cast<std::uint16_t>(m_lastRxSeqNumber + 1);
        if (seq != expectedSeq)
        {
            const auto diff = static_cast<std::uint16_t>(seq - expectedSeq);
            if (diff < 1000)
                m_droppedPackets += diff;
        }
    }
    else
    {
        m_firstPacketReceived = true;
    }

    m_lastRxSeqNumber = seq;
    ++m_rxPackets;

    const std::size_t payloadBytes = inboundDatagram.size() - headerSize;
    const std::size_t frameBytes = maxChannels * bytesPerSample; // 6 bytes per stereo frame
    const std::size_t frames = std::min(payloadBytes / frameBytes, maxFrames);

    p += headerSize;

    for (std::size_t i = 0; i < frames; ++i)
    {
        // Decode Left 24-bit big-endian sign-extended
        std::int32_t valL = (static_cast<std::int32_t>(p[0]) << 16) |
                            (static_cast<std::int32_t>(p[1]) << 8)  |
                            static_cast<std::int32_t>(p[2]);
        if (valL & 0x800000) valL |= ~0x00FFFFFF; // sign extend
        outLeft[i] = pcm24ToFloat(valL);
        p += 3;

        // Decode Right 24-bit big-endian sign-extended
        std::int32_t valR = (static_cast<std::int32_t>(p[0]) << 16) |
                            (static_cast<std::int32_t>(p[1]) << 8)  |
                            static_cast<std::int32_t>(p[2]);
        if (valR & 0x800000) valR |= ~0x00FFFFFF;
        outRight[i] = pcm24ToFloat(valR);
        p += 3;
    }

    outDecodedFrames = frames;
    return true;
}

std::string LanMulticastAudioStreamer::formatStageViewJson(const StageViewTelemetry& telemetry)
{
    std::ostringstream ss;
    ss << "{\n"
       << "  \"section\": \"" << telemetry.sectionName << "\",\n"
       << "  \"bar\": " << telemetry.currentBar << ",\n"
       << "  \"beat\": " << telemetry.currentBeat << ",\n"
       << "  \"bpm\": " << std::fixed << std::setprecision(1) << telemetry.tempoBpm << ",\n"
       << "  \"songTimeSeconds\": " << std::fixed << std::setprecision(2) << telemetry.songTimeSeconds << ",\n"
       << "  \"clickAccent\": \"" << telemetry.clickAccent << "\",\n"
       << "  \"nextCue\": \"" << telemetry.nextCue << "\",\n"
       << "  \"isPlaying\": " << (telemetry.isPlaying ? "true" : "false") << "\n"
       << "}";
    return ss.str();
}

std::string LanMulticastAudioStreamer::serializeJamSync(const LanJamSyncMessage& msg)
{
    std::ostringstream ss;
    ss << "{\"sender\":\"" << msg.senderId << "\","
       << "\"seq\":" << msg.messageSequence << ","
       << "\"playing\":" << (msg.isPlaying ? "true" : "false") << ","
       << "\"bpm\":" << std::fixed << std::setprecision(2) << msg.bpm << ","
       << "\"samplePos\":" << msg.samplePosition << ","
       << "\"scene\":" << msg.activeSceneIndex << "}";
    return ss.str();
}

std::optional<LanJamSyncMessage> LanMulticastAudioStreamer::deserializeJamSync(std::string_view jsonStr)
{
    auto extractString = [](std::string_view s, std::string_view key) -> std::string {
        const std::string needle = "\"" + std::string(key) + "\":\"";
        const auto pos = s.find(needle);
        if (pos == std::string_view::npos) return {};
        const auto start = pos + needle.size();
        const auto end = s.find('\"', start);
        if (end == std::string_view::npos) return {};
        return std::string(s.substr(start, end - start));
    };

    auto extractNumber = [](std::string_view s, std::string_view key) -> double {
        const std::string needle = "\"" + std::string(key) + "\":";
        const auto pos = s.find(needle);
        if (pos == std::string_view::npos) return 0.0;
        const auto start = pos + needle.size();
        char* endPtr = nullptr;
        return std::strtod(s.data() + start, &endPtr);
    };

    LanJamSyncMessage msg;
    msg.senderId = extractString(jsonStr, "sender");
    if (msg.senderId.empty()) return std::nullopt;

    msg.messageSequence = static_cast<std::uint32_t>(extractNumber(jsonStr, "seq"));
    msg.isPlaying = (jsonStr.find("\"playing\":true") != std::string_view::npos);
    msg.bpm = extractNumber(jsonStr, "bpm");
    msg.samplePosition = static_cast<std::int64_t>(extractNumber(jsonStr, "samplePos"));
    msg.activeSceneIndex = static_cast<std::uint32_t>(extractNumber(jsonStr, "scene"));

    return msg;
}

} // namespace dawg::engine
