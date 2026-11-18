#include <dawg/engine/OscSurfaceProtocolEngine.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <sstream>

namespace dawg::engine {

namespace {

inline std::uint32_t readBigEndianU32(const std::uint8_t* p) noexcept
{
    return (static_cast<std::uint32_t>(p[0]) << 24) |
           (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8)  |
           (static_cast<std::uint32_t>(p[3]));
}

inline float readBigEndianFloat(const std::uint8_t* p) noexcept
{
    const std::uint32_t u = readBigEndianU32(p);
    return std::bit_cast<float>(u);
}

inline std::int32_t readBigEndianI32(const std::uint8_t* p) noexcept
{
    const std::uint32_t u = readBigEndianU32(p);
    return static_cast<std::int32_t>(u);
}

std::size_t getPaddedStringLength(const std::uint8_t* data, std::size_t maxLen) noexcept
{
    std::size_t len = 0;
    while (len < maxLen && data[len] != '\0')
    {
        ++len;
    }
    if (len >= maxLen)
        return 0; // Not null-terminated

    // String null terminator + pad to 4 bytes
    const std::size_t padded = (len + 4) & ~0x03;
    return (padded <= maxLen) ? padded : 0;
}

void routeAddress(OscParsedMessage& msg)
{
    std::string_view addr = msg.address;
    if (addr.rfind("/dawg", 0) == 0)
    {
        addr = addr.substr(5);
    }

    if (addr.rfind("/track/", 0) == 0)
    {
        // Format: /track/{idx}/{param}
        const auto slash2 = addr.find('/', 7);
        if (slash2 != std::string_view::npos)
        {
            const auto idxStr = addr.substr(7, slash2 - 7);
            try {
                const auto trk = static_cast<std::size_t>(std::stoul(std::string(idxStr)));
                msg.trackIndex = (trk > 0) ? trk - 1 : 0;
            } catch (...) {
                msg.trackIndex = 0;
            }

            const auto param = addr.substr(slash2 + 1);
            if (param == "volume" || param == "vol")
                msg.type = OscMessageType::trackVolume;
            else if (param == "pan")
                msg.type = OscMessageType::trackPan;
            else if (param == "mute")
                msg.type = OscMessageType::trackMute;
            else if (param == "solo")
                msg.type = OscMessageType::trackSolo;
            else if (param == "arm" || param == "rec")
                msg.type = OscMessageType::trackArm;
            else if (param.rfind("macro/", 0) == 0)
            {
                msg.type = OscMessageType::trackMacro;
                try {
                    const auto mIdx = static_cast<std::size_t>(std::stoul(std::string(param.substr(6))));
                    msg.subIndex = (mIdx > 0) ? mIdx - 1 : 0;
                } catch (...) {
                    msg.subIndex = 0;
                }
            }
        }
    }
    else if (addr == "/master/volume" || addr == "/master/vol")
    {
        msg.type = OscMessageType::masterVolume;
    }
    else if (addr == "/transport/play")
    {
        msg.type = OscMessageType::transportPlay;
    }
    else if (addr == "/transport/stop")
    {
        msg.type = OscMessageType::transportStop;
    }
    else if (addr == "/transport/record" || addr == "/transport/rec")
    {
        msg.type = OscMessageType::transportRecord;
    }
    else if (addr == "/transport/tempo" || addr == "/transport/bpm")
    {
        msg.type = OscMessageType::transportTempo;
    }
    else if (addr == "/transport/timecode" || addr == "/transport/smpte")
    {
        msg.type = OscMessageType::transportTimecode;
    }
    else if (addr.rfind("/clip/", 0) == 0)
    {
        // Format: /clip/{row}/{col}/launch
        msg.type = OscMessageType::clipLaunch;
        std::stringstream ss(std::string(addr.substr(6)));
        std::string segment;
        if (std::getline(ss, segment, '/'))
            msg.trackIndex = static_cast<std::size_t>(std::stoul(segment));
        if (std::getline(ss, segment, '/'))
            msg.subIndex = static_cast<std::size_t>(std::stoul(segment));
    }
    else if (addr.rfind("/scene/", 0) == 0)
    {
        msg.type = OscMessageType::sceneLaunch;
        std::stringstream ss(std::string(addr.substr(7)));
        std::string segment;
        if (std::getline(ss, segment, '/'))
            msg.subIndex = static_cast<std::size_t>(std::stoul(segment));
    }
    else
    {
        msg.type = OscMessageType::customParameter;
    }
}

} // namespace

void OscSurfaceProtocolEngine::appendPaddedString(std::vector<std::uint8_t>& out, const std::string_view str)
{
    out.insert(out.end(), str.begin(), str.end());
    out.push_back('\0');
    while (out.size() % 4 != 0)
    {
        out.push_back('\0');
    }
}

void OscSurfaceProtocolEngine::appendBigEndianInt32(std::vector<std::uint8_t>& out, const std::int32_t val)
{
    const auto u = static_cast<std::uint32_t>(val);
    out.push_back(static_cast<std::uint8_t>((u >> 24) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((u >> 16) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((u >> 8)  & 0xFF));
    out.push_back(static_cast<std::uint8_t>(u & 0xFF));
}

void OscSurfaceProtocolEngine::appendBigEndianFloat(std::vector<std::uint8_t>& out, const float val)
{
    const std::uint32_t u = std::bit_cast<std::uint32_t>(val);
    out.push_back(static_cast<std::uint8_t>((u >> 24) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((u >> 16) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((u >> 8)  & 0xFF));
    out.push_back(static_cast<std::uint8_t>(u & 0xFF));
}

bool OscSurfaceProtocolEngine::parseMessage(const std::span<const std::uint8_t> buffer, OscParsedMessage& outMessage)
{
    if (buffer.size() < 4)
        return false;

    // 1. Parse address
    const std::size_t addrPadLen = getPaddedStringLength(buffer.data(), buffer.size());
    if (addrPadLen == 0 || buffer[0] != '/')
        return false;

    outMessage.address = std::string(reinterpret_cast<const char*>(buffer.data()));
    std::size_t offset = addrPadLen;

    if (offset >= buffer.size())
    {
        routeAddress(outMessage);
        return true;
    }

    // 2. Parse type tag string
    if (buffer[offset] != ',')
    {
        routeAddress(outMessage);
        return true;
    }

    const std::size_t tagPadLen = getPaddedStringLength(buffer.data() + offset, buffer.size() - offset);
    if (tagPadLen == 0)
        return false;

    const std::string tagStr(reinterpret_cast<const char*>(buffer.data() + offset));
    offset += tagPadLen;

    // 3. Parse arguments according to tags
    for (std::size_t i = 1; i < tagStr.size(); ++i)
    {
        const char tag = tagStr[i];
        if (tag == 'f')
        {
            if (offset + 4 > buffer.size()) return false;
            outMessage.floatValue = readBigEndianFloat(buffer.data() + offset);
            outMessage.intValue = static_cast<int>(outMessage.floatValue);
            offset += 4;
        }
        else if (tag == 'i')
        {
            if (offset + 4 > buffer.size()) return false;
            outMessage.intValue = readBigEndianI32(buffer.data() + offset);
            outMessage.floatValue = static_cast<float>(outMessage.intValue);
            offset += 4;
        }
        else if (tag == 's')
        {
            if (offset >= buffer.size()) return false;
            const std::size_t strPad = getPaddedStringLength(buffer.data() + offset, buffer.size() - offset);
            if (strPad == 0) return false;
            outMessage.stringValue = std::string(reinterpret_cast<const char*>(buffer.data() + offset));
            offset += strPad;
        }
    }

    routeAddress(outMessage);
    return true;
}

std::vector<OscParsedMessage> OscSurfaceProtocolEngine::parsePacket(const std::span<const std::uint8_t> buffer)
{
    std::vector<OscParsedMessage> result;
    if (buffer.size() < 4)
        return result;

    // Check if it's an OSC Bundle
    if (buffer.size() >= 16 && std::memcmp(buffer.data(), "#bundle\0", 8) == 0)
    {
        std::size_t offset = 16; // Skip "#bundle\0" (8 bytes) + Timetag (8 bytes)
        while (offset + 4 <= buffer.size())
        {
            const std::size_t elemSize = readBigEndianU32(buffer.data() + offset);
            offset += 4;
            if (offset + elemSize > buffer.size())
                break;

            const auto elemSpan = buffer.subspan(offset, elemSize);
            const auto subMessages = parsePacket(elemSpan);
            result.insert(result.end(), subMessages.begin(), subMessages.end());
            offset += elemSize;
        }
        return result;
    }

    // Normal OSC Message
    OscParsedMessage msg;
    if (parseMessage(buffer, msg))
    {
        result.push_back(std::move(msg));
    }
    return result;
}

std::vector<std::uint8_t> OscSurfaceProtocolEngine::encodeFloat(const std::string_view address, const float value)
{
    std::vector<std::uint8_t> packet;
    packet.reserve(64);
    appendPaddedString(packet, address);
    appendPaddedString(packet, ",f");
    appendBigEndianFloat(packet, value);
    return packet;
}

std::vector<std::uint8_t> OscSurfaceProtocolEngine::encodeInt(const std::string_view address, const int value)
{
    std::vector<std::uint8_t> packet;
    packet.reserve(64);
    appendPaddedString(packet, address);
    appendPaddedString(packet, ",i");
    appendBigEndianInt32(packet, value);
    return packet;
}

std::vector<std::uint8_t> OscSurfaceProtocolEngine::encodeString(const std::string_view address, const std::string_view str)
{
    std::vector<std::uint8_t> packet;
    packet.reserve(64 + str.size());
    appendPaddedString(packet, address);
    appendPaddedString(packet, ",s");
    appendPaddedString(packet, str);
    return packet;
}

std::vector<std::uint8_t> OscSurfaceProtocolEngine::encodeBundle(
    const std::span<const std::vector<std::uint8_t>> messages,
    const std::uint64_t ntpTimestamp)
{
    std::vector<std::uint8_t> bundle;
    bundle.reserve(256);
    appendPaddedString(bundle, "#bundle");
    // 64-bit big endian NTP timestamp
    for (int i = 7; i >= 0; --i)
    {
        bundle.push_back(static_cast<std::uint8_t>((ntpTimestamp >> (i * 8)) & 0xFF));
    }

    for (const auto& msg : messages)
    {
        appendBigEndianInt32(bundle, static_cast<std::int32_t>(msg.size()));
        bundle.insert(bundle.end(), msg.begin(), msg.end());
    }
    return bundle;
}

std::vector<std::uint8_t> OscSurfaceProtocolEngine::buildTrackVolume(const std::size_t trackIdx, const float volume0to1)
{
    const std::string addr = "/track/" + std::to_string(trackIdx + 1) + "/volume";
    return encodeFloat(addr, volume0to1);
}

std::vector<std::uint8_t> OscSurfaceProtocolEngine::buildTrackPan(const std::size_t trackIdx, const float panMinus1to1)
{
    const std::string addr = "/track/" + std::to_string(trackIdx + 1) + "/pan";
    return encodeFloat(addr, panMinus1to1);
}

std::vector<std::uint8_t> OscSurfaceProtocolEngine::buildTrackMute(const std::size_t trackIdx, const bool muted)
{
    const std::string addr = "/track/" + std::to_string(trackIdx + 1) + "/mute";
    return encodeInt(addr, muted ? 1 : 0);
}

std::vector<std::uint8_t> OscSurfaceProtocolEngine::buildTrackSolo(const std::size_t trackIdx, const bool soloed)
{
    const std::string addr = "/track/" + std::to_string(trackIdx + 1) + "/solo";
    return encodeInt(addr, soloed ? 1 : 0);
}

std::vector<std::uint8_t> OscSurfaceProtocolEngine::buildTrackArm(const std::size_t trackIdx, const bool armed)
{
    const std::string addr = "/track/" + std::to_string(trackIdx + 1) + "/arm";
    return encodeInt(addr, armed ? 1 : 0);
}

std::vector<std::uint8_t> OscSurfaceProtocolEngine::buildTrackMacro(const std::size_t trackIdx, const std::size_t macroIdx, const float val0to1)
{
    const std::string addr = "/dawg/track/" + std::to_string(trackIdx + 1) + "/macro/" + std::to_string(macroIdx + 1);
    return encodeFloat(addr, std::clamp(val0to1, 0.0f, 1.0f));
}

std::vector<std::uint8_t> OscSurfaceProtocolEngine::buildTransportPlay(const bool playing)
{
    return encodeInt("/transport/play", playing ? 1 : 0);
}

std::vector<std::uint8_t> OscSurfaceProtocolEngine::buildTransportTempo(const float bpm)
{
    return encodeFloat("/transport/tempo", bpm);
}

std::vector<std::uint8_t> OscSurfaceProtocolEngine::buildClipState(const std::size_t row, const std::size_t col, const int state)
{
    const std::string addr = "/clip/" + std::to_string(row) + "/" + std::to_string(col) + "/state";
    return encodeInt(addr, state);
}

std::pair<std::string, std::string> OscSurfaceProtocolEngine::formatMcuScribbleLines(
    const std::array<TrackScribbleData, 8>& strips,
    const bool showMetersOnLine2)
{
    std::string line1;
    std::string line2;
    line1.reserve(56);
    line2.reserve(56);

    for (std::size_t i = 0; i < 8; ++i)
    {
        const auto& strip = strips[i];

        // Format 7 chars for Line 1 (Track Name)
        std::string name7 = strip.trackName;
        if (name7.size() > 6)
            name7 = name7.substr(0, 6);
        while (name7.size() < 7)
            name7.push_back(' ');
        line1 += name7;

        // Format 7 chars for Line 2 (dB value or ASCII VU meter)
        if (showMetersOnLine2)
        {
            // ASCII VU meter: 6 blocks + 1 space e.g. [||||  ]
            const auto level = std::clamp(strip.peakMeter0to1, 0.0f, 1.0f);
            const auto bars = static_cast<std::size_t>(std::round(level * 6.0f));
            std::string meter;
            for (std::size_t b = 0; b < 6; ++b)
            {
                meter.push_back((b < bars) ? '|' : '.');
            }
            meter.push_back(' ');
            line2 += meter;
        }
        else
        {
            // Value readout e.g. "-12.4dB" or "L32%" or "0.0dB"
            std::string val7 = strip.paramValue;
            if (val7.size() > 6)
                val7 = val7.substr(0, 6);
            while (val7.size() < 7)
                val7.push_back(' ');
            line2 += val7;
        }
    }

    return {line1, line2};
}

std::vector<std::uint8_t> OscSurfaceProtocolEngine::generateMcuDisplaySysEx(
    const std::uint8_t lineIndex,
    const std::string_view text56)
{
    // MCU Display SysEx Format:
    // F0 00 00 66 14 12 <offset> <data...> F7
    // Offset: 0x00 for line 1, 0x38 (56) for line 2
    std::vector<std::uint8_t> sysex;
    sysex.reserve(70);

    sysex.push_back(0xF0); // SysEx start
    sysex.push_back(0x00); // Manufacturer ID
    sysex.push_back(0x00);
    sysex.push_back(0x66);
    sysex.push_back(0x14); // Model: Mackie Control
    sysex.push_back(0x12); // Command: LCD write
    sysex.push_back((lineIndex == 0) ? 0x00 : 0x38); // Cursor offset

    for (std::size_t i = 0; i < 56; ++i)
    {
        const char c = (i < text56.size()) ? text56[i] : ' ';
        sysex.push_back(static_cast<std::uint8_t>(c & 0x7F));
    }

    sysex.push_back(0xF7); // SysEx end
    return sysex;
}

} // namespace dawg::engine
