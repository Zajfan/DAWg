#pragma once

#include <dawg/engine/AudioBufferView.h>
#include <dawg/engine/ProcessorNode.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace dawg::engine::dsp {

enum class RoomMaterial : std::uint8_t
{
    concrete = 0,
    polishedWood,
    acousticFoam,
    glass,
    drywall
};

enum class RoomOutputMode : std::uint8_t
{
    binauralStereo = 0,
    bFormat4Channel
};

struct RayReflection
{
    bool active {false};
    float distanceMeters {0.0f};
    std::size_t delaySamples {0};
    float gainLow {1.0f};
    float gainHigh {1.0f};
    float bFormatW {0.7071f};
    float bFormatX {0.0f};
    float bFormatY {0.0f};
    float bFormatZ {0.0f};
    float itdDelaySamples {0.0f}; // Interaural time difference for binaural decoding
    float ildPanL {0.7071f};       // Interaural level difference Left
    float ildPanR {0.7071f};       // Interaural level difference Right
    float filterZ {0.0f};
};

class RayTracingAcousticRoomNode final : public ProcessorNode
{
public:
    static constexpr std::size_t maxRays = 24;
    static constexpr std::size_t maxDelayFrames = 48000 * 2; // 2 seconds delay line

    RayTracingAcousticRoomNode() noexcept;
    ~RayTracingAcousticRoomNode() override = default;

    [[nodiscard]] bool prepare(
        double sampleRate,
        std::size_t maxBlockSize,
        std::uint16_t channelCount) override;

    void process(ProcessContext& context) noexcept override;
    void reset() noexcept override;

    [[nodiscard]] std::string_view name() const noexcept { return "3D Ambisonics & Ray-Tracing Acoustic Room Modeler"; }

    // Room Geometry (meters)
    void setRoomDimensions(float lengthM, float widthM, float heightM) noexcept;
    [[nodiscard]] float roomLength() const noexcept { return m_roomLength; }
    [[nodiscard]] float roomWidth() const noexcept { return m_roomWidth; }
    [[nodiscard]] float roomHeight() const noexcept { return m_roomHeight; }

    // Source & Listener 3D Coordinates (meters)
    void setSourcePosition(float x, float y, float z) noexcept;
    void setListenerPosition(float x, float y, float z) noexcept;
    [[nodiscard]] float sourceX() const noexcept { return m_sourceX; }
    [[nodiscard]] float sourceY() const noexcept { return m_sourceY; }
    [[nodiscard]] float sourceZ() const noexcept { return m_sourceZ; }
    [[nodiscard]] float listenerX() const noexcept { return m_listenerX; }
    [[nodiscard]] float listenerY() const noexcept { return m_listenerY; }
    [[nodiscard]] float listenerZ() const noexcept { return m_listenerZ; }

    // Acoustic Surface Materials
    void setWallMaterial(RoomMaterial material) noexcept;
    [[nodiscard]] RoomMaterial wallMaterial() const noexcept { return m_wallMaterial; }

    // Reverb Tail & Mix
    void setDecayTimeSeconds(float t60) noexcept { m_t60Decay = std::clamp(t60, 0.1f, 15.0f); updateFdnFeedback(); }
    [[nodiscard]] float decayTimeSeconds() const noexcept { return m_t60Decay; }
    void setEarlyLateMix(float lateMixRatio) noexcept { m_lateMix = std::clamp(lateMixRatio, 0.0f, 1.0f); }
    [[nodiscard]] float earlyLateMix() const noexcept { return m_lateMix; }
    void setOutputMode(RoomOutputMode mode) noexcept { m_outputMode = mode; }
    [[nodiscard]] RoomOutputMode outputMode() const noexcept { return m_outputMode; }

private:
    void recalculateRays() noexcept;
    void updateFdnFeedback() noexcept;

    double m_sampleRate {48000.0};
    std::uint16_t m_channelCount {2};

    float m_roomLength {8.0f};  // X
    float m_roomWidth {6.0f};   // Y
    float m_roomHeight {3.2f};  // Z

    float m_sourceX {2.0f};
    float m_sourceY {2.0f};
    float m_sourceZ {1.5f};

    float m_listenerX {4.0f};
    float m_listenerY {3.0f};
    float m_listenerZ {1.6f};

    RoomMaterial m_wallMaterial {RoomMaterial::polishedWood};
    RoomOutputMode m_outputMode {RoomOutputMode::binauralStereo};

    float m_t60Decay {1.8f};
    float m_lateMix {0.35f};
    float m_fdnFeedback {0.7f};

    std::array<RayReflection, maxRays> m_rays {};
    std::size_t m_activeRayCount {0};

    // Shared input delay line
    std::vector<float> m_inputDelayLine;
    std::size_t m_delayWriteIdx {0};

    // 4-Channel FDN diffuse late reverb lines
    static constexpr std::size_t numFdnLines = 4;
    std::array<std::vector<float>, numFdnLines> m_fdnDelayBuffers;
    std::array<std::size_t, numFdnLines> m_fdnLengths {1087, 1361, 1601, 1987}; // Prime lengths
    std::array<std::size_t, numFdnLines> m_fdnWriteIndices {0, 0, 0, 0};
    std::array<float, numFdnLines> m_fdnDampingZ {0.0f, 0.0f, 0.0f, 0.0f};
};

} // namespace dawg::engine::dsp
