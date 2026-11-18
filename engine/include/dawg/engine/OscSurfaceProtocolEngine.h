#pragma once

#include <dawg/engine/HardwareControlSurfaceEngine.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace dawg::engine {

enum class OscMessageType : std::uint8_t
{
    unknown,
    trackVolume,
    trackPan,
    trackMute,
    trackSolo,
    trackArm,
    masterVolume,
    transportPlay,
    transportStop,
    transportRecord,
    transportTempo,
    clipLaunch,
    sceneLaunch,
    customParameter
};

struct OscParsedMessage
{
    OscMessageType type {OscMessageType::unknown};
    std::string address;
    std::size_t trackIndex {0};
    std::size_t subIndex {0}; // e.g. scene or clip index
    float floatValue {0.0f};
    int intValue {0};
    std::string stringValue;
};

struct TrackScribbleData
{
    std::string trackName {"Track"};
    float volumeDb {0.0f};
    float pan {-0.0f};
    float peakMeter0to1 {0.0f};
    std::string paramName {"Vol"};
    std::string paramValue {"0.0dB"};
};

class OscSurfaceProtocolEngine final
{
public:
    OscSurfaceProtocolEngine() = default;
    ~OscSurfaceProtocolEngine() = default;

    // =========================================================================
    // OSC 1.0 Binary Packet Parsing
    // =========================================================================

    /// Parses a raw binary OSC packet (Message or Bundle)
    [[nodiscard]] static std::vector<OscParsedMessage> parsePacket(std::span<const std::uint8_t> buffer);

    /// Parses a single OSC message from a byte span
    [[nodiscard]] static bool parseMessage(std::span<const std::uint8_t> buffer, OscParsedMessage& outMessage);

    // =========================================================================
    // OSC 1.0 Binary Packet Encoding (DAW -> Tablet / TouchOSC)
    // =========================================================================

    /// Encodes an OSC message with a single float argument
    [[nodiscard]] static std::vector<std::uint8_t> encodeFloat(std::string_view address, float value);

    /// Encodes an OSC message with a single integer argument
    [[nodiscard]] static std::vector<std::uint8_t> encodeInt(std::string_view address, int value);

    /// Encodes an OSC message with a string argument
    [[nodiscard]] static std::vector<std::uint8_t> encodeString(std::string_view address, std::string_view str);

    /// Encodes a bundle containing multiple encoded OSC messages
    [[nodiscard]] static std::vector<std::uint8_t> encodeBundle(
        std::span<const std::vector<std::uint8_t>> messages,
        std::uint64_t ntpTimestamp = 1ULL); // 1 = immediate execution

    // Helper builders for common DAW telemetry
    [[nodiscard]] static std::vector<std::uint8_t> buildTrackVolume(std::size_t trackIdx, float volume0to1);
    [[nodiscard]] static std::vector<std::uint8_t> buildTrackPan(std::size_t trackIdx, float panMinus1to1);
    [[nodiscard]] static std::vector<std::uint8_t> buildTrackMute(std::size_t trackIdx, bool muted);
    [[nodiscard]] static std::vector<std::uint8_t> buildTrackSolo(std::size_t trackIdx, bool soloed);
    [[nodiscard]] static std::vector<std::uint8_t> buildTrackArm(std::size_t trackIdx, bool armed);
    [[nodiscard]] static std::vector<std::uint8_t> buildTransportPlay(bool playing);
    [[nodiscard]] static std::vector<std::uint8_t> buildTransportTempo(float bpm);
    [[nodiscard]] static std::vector<std::uint8_t> buildClipState(std::size_t row, std::size_t col, int state);

    // =========================================================================
    // Mackie Control Universal (MCU) Two-Line Scribble Strip Formatter
    // =========================================================================

    /// Formats a 2-line x 56-character LCD display for 8 channel strips
    /// Line 1 (top): Track names / parameter labels (7 characters per channel)
    /// Line 2 (bottom): Value readouts, Pan or horizontal ASCII dB metering bars
    [[nodiscard]] static std::pair<std::string, std::string> formatMcuScribbleLines(
        const std::array<TrackScribbleData, 8>& strips,
        bool showMetersOnLine2 = false);

    /// Generates the complete Mackie SysEx messages for Line 1 and Line 2
    [[nodiscard]] static std::vector<std::uint8_t> generateMcuDisplaySysEx(
        std::uint8_t lineIndex, // 0 = Line 1 (offset 0x00), 1 = Line 2 (offset 0x38)
        std::string_view text56);

private:
    static void appendPaddedString(std::vector<std::uint8_t>& out, std::string_view str);
    static void appendBigEndianInt32(std::vector<std::uint8_t>& out, std::int32_t val);
    static void appendBigEndianFloat(std::vector<std::uint8_t>& out, float val);
};

} // namespace dawg::engine
