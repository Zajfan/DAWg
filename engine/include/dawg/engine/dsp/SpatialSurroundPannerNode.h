#pragma once

#include <dawg/engine/AudioBufferView.h>
#include <dawg/engine/ProcessorNode.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace dawg::engine::dsp {

enum class SurroundChannel714 : std::uint8_t
{
    left = 0,
    right = 1,
    center = 2,
    lfe = 3,
    leftSideSurround = 4,
    rightSideSurround = 5,
    leftRearSurround = 6,
    rightRearSurround = 7,
    topFrontLeft = 8,
    topFrontRight = 9,
    topRearLeft = 10,
    topRearRight = 11
};

// Immersive 7.1.4 Dolby Atmos Surround Bed & 3D Spatial Object Panner
// Pans audio across 12 speaker channels with 3D (X, Y, Z) trajectory positioning,
// elevation spread, LFE subwoofer routing, real-time 7.1.4 binaural headphone downmixing,
// and ADM BWF (Broadcast Wave Format) XML metadata generation.
class SpatialSurroundPannerNode final : public ProcessorNode
{
public:
    static constexpr std::size_t numSpeakers = 12;

    SpatialSurroundPannerNode() noexcept;
    ~SpatialSurroundPannerNode() override = default;

    [[nodiscard]] bool prepare(
        double sampleRate,
        std::size_t maxBlockSize,
        std::uint16_t channelCount) override;

    void reset() noexcept override;
    [[nodiscard]] std::uint32_t latencySamples() const noexcept override { return 0; }
    void process(ProcessContext& context) noexcept override;

    // 3D Cartesian Coordinates (-1.0 to +1.0)
    // X: -1.0 (Left) to +1.0 (Right)
    // Y: -1.0 (Rear) to +1.0 (Front)
    // Z: -1.0 (Floor) to 0.0 (Ear level) to +1.0 (Ceiling)
    void setCoordinates(float x, float y, float z) noexcept;
    [[nodiscard]] float x() const noexcept { return m_x; }
    [[nodiscard]] float y() const noexcept { return m_y; }
    [[nodiscard]] float z() const noexcept { return m_z; }

    void setSizeSpread(float spread) noexcept { m_sizeSpread = std::clamp(spread, 0.0f, 1.0f); updateGains(); }
    [[nodiscard]] float sizeSpread() const noexcept { return m_sizeSpread; }

    void setLfeSendDb(float db) noexcept { m_lfeSendDb = std::clamp(db, -60.0f, 12.0f); updateGains(); }
    [[nodiscard]] float lfeSendDb() const noexcept { return m_lfeSendDb; }

    // Headphone Binaural 7.1.4 Downmix Monitoring Mode
    // When enabled, downmixes the 12 speaker outputs to stereo using HRTF/HRIR binaural filters
    void setBinauralMonitoring(bool enabled) noexcept { m_binauralMonitoring = enabled; }
    [[nodiscard]] bool isBinauralMonitoring() const noexcept { return m_binauralMonitoring; }

    // Speaker Gains Telemetry
    [[nodiscard]] float speakerGain(SurroundChannel714 ch) const noexcept;
    [[nodiscard]] const std::array<float, numSpeakers>& speakerGains() const noexcept { return m_speakerGains; }

    // ADM BWF (Broadcast Wave Format) XML Metadata Generator (ITU-R BS.2076)
    [[nodiscard]] std::string generateAdmMetadataXml(const std::string& objectName, double durationSec) const;

private:
    struct SpeakerPos
    {
        float x {0.0f};
        float y {0.0f};
        float z {0.0f};
    };

    struct BinauralFilterState
    {
        float filterL {0.0f};
        float filterR {0.0f};
        float delayLineL[64] {};
        float delayLineR[64] {};
        std::size_t writePos {0};
    };

    void setupSpeakerPositions() noexcept;
    void updateGains() noexcept;

    double m_sampleRate {48000.0};
    float m_x {0.0f};
    float m_y {0.707f};
    float m_z {0.0f};
    float m_sizeSpread {0.0f};
    float m_lfeSendDb {-60.0f};
    bool m_binauralMonitoring {true};

    std::array<SpeakerPos, numSpeakers> m_speakerPositions {};
    std::array<float, numSpeakers> m_speakerGains {};
    float m_lfeGainLinear {0.0f};

    // Binaural downmix filter states per virtual speaker
    std::array<BinauralFilterState, numSpeakers> m_binauralStates {};
};

} // namespace dawg::engine::dsp
