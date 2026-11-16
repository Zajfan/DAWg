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

enum class AmbisonicsDecoderMode : std::uint8_t
{
    binauralHeadphones = 0, // 3D Binaural rendering with virtual loudspeaker array & HRTF
    surround51 = 1,         // 5.1 Surround decode (L, R, C, LFE, Ls, Rs)
    surround71 = 2,         // 7.1 Surround decode (L, R, C, LFE, Ls, Rs, Lb, Rb)
    atmos714 = 3,           // 7.1.4 Dolby Atmos 12-channel immersive decode
    rawBFormatHoa3 = 4      // 16-channel HOA 3rd Order B-format (AmbiX ACN/SN3D)
};

// Surround & Binaural Ambisonics B-Format Encoder/Decoder (Higher Order Ambisonics HOA 3rd Order)
// 16-channel soundfield representation (orders 0 to 3) in AmbiX ACN/SN3D format.
// Features 3D spherical harmonics encoding, real-time 3D head rotation matrix (yaw/pitch/roll),
// virtual loudspeaker binaural headphone decoding with ITD/head-shadowing,
// and multichannel 5.1 / 7.1 / 7.1.4 Dolby Atmos surround decoding.
class AmbisonicsProcessorNode final : public ProcessorNode
{
public:
    static constexpr std::size_t numHoaChannels = 16;
    static constexpr std::size_t numVirtualSpeakers = 12;

    AmbisonicsProcessorNode() noexcept;
    ~AmbisonicsProcessorNode() override = default;

    [[nodiscard]] bool prepare(
        double sampleRate,
        std::size_t maxBlockSize,
        std::uint16_t channelCount) override;

    void reset() noexcept override;
    [[nodiscard]] std::uint32_t latencySamples() const noexcept override { return 0; }
    void process(ProcessContext& context) noexcept override;

    // Decoder Mode
    void setDecoderMode(AmbisonicsDecoderMode mode) noexcept { m_decoderMode = mode; }
    [[nodiscard]] AmbisonicsDecoderMode decoderMode() const noexcept { return m_decoderMode; }

    // 3D Sound Source Spherical Coordinates
    // Azimuth: -180.0 deg (Rear Left) to 0.0 (Front) to +180.0 deg (Rear Right)
    void setSourceAzimuthDeg(float deg) noexcept;
    [[nodiscard]] float sourceAzimuthDeg() const noexcept { return m_sourceAzimuthDeg; }

    // Elevation: -90.0 deg (Floor) to 0.0 (Horizon) to +90.0 deg (Zenith)
    void setSourceElevationDeg(float deg) noexcept;
    [[nodiscard]] float sourceElevationDeg() const noexcept { return m_sourceElevationDeg; }

    // Source Angular Spread (0.0 = point source to 1.0 = diffuse ambient soundfield)
    void setSourceSpread(float spread) noexcept;
    [[nodiscard]] float sourceSpread() const noexcept { return m_sourceSpread; }

    // 3D Head Orientation / Rotation Tracking (degrees)
    void setHeadRotation(float yawDeg, float pitchDeg, float rollDeg) noexcept;
    [[nodiscard]] float headYawDeg() const noexcept { return m_headYawDeg; }
    [[nodiscard]] float headPitchDeg() const noexcept { return m_headPitchDeg; }
    [[nodiscard]] float headRollDeg() const noexcept { return m_headRollDeg; }

    // Master Output Level (-24.0 dB to +12.0 dB)
    void setOutputLevelDb(float db) noexcept;
    [[nodiscard]] float outputLevelDb() const noexcept { return m_outputLevelDb; }

    // HOA 16-Channel Energy Telemetry (for 3D soundfield visualization)
    void getHoaChannelLevels(float* dest, std::size_t count) const noexcept;

    // Direct multi-channel surround decoding into an external multi-channel buffer
    void decodeMultichannel(
        const float* const* hoaInputs,
        float* const* speakerOutputs,
        std::size_t channelCount,
        std::size_t frameCount) noexcept;

    // Compute spherical harmonic weights for given azimuth and elevation (AmbiX SN3D)
    static void computeHarmonics(float azimuthRad, float elevationRad, float* weightsOut) noexcept;

private:
    struct VirtualSpeaker
    {
        float azimuthRad {0.0f};
        float elevationRad {0.0f};
        std::array<float, numHoaChannels> decodeWeights {};
        // Binaural filter state
        float itdSamples {0.0f};
        float shadowFilterL {0.0f};
        float shadowFilterR {0.0f};
        float delayBufferL[64] {};
        float delayBufferR[64] {};
        std::size_t delayWritePos {0};
    };

    void initializeVirtualSpeakers() noexcept;
    void updateEncodingWeights() noexcept;
    void updateRotationMatrix() noexcept;

    double m_sampleRate {48000.0};
    AmbisonicsDecoderMode m_decoderMode {AmbisonicsDecoderMode::binauralHeadphones};

    float m_sourceAzimuthDeg {0.0f};
    float m_sourceElevationDeg {0.0f};
    float m_sourceSpread {0.0f};

    float m_headYawDeg {0.0f};
    float m_headPitchDeg {0.0f};
    float m_headRollDeg {0.0f};

    float m_outputLevelDb {0.0f};
    float m_outputGainLinear {1.0f};

    // Encoding spherical harmonics weights for order 0..3 (16 channels)
    std::array<float, numHoaChannels> m_encodingWeights {};

    // 16x16 Full HOA 3D Rotation Matrix
    std::array<std::array<float, numHoaChannels>, numHoaChannels> m_rotMatrix {};

    // 16 internal HOA audio scratch buffers
    std::vector<std::vector<float>> m_hoaBuffers;

    // Virtual Loudspeakers for Binaural & Surround decoding
    std::array<VirtualSpeaker, numVirtualSpeakers> m_speakers {};

    // Telemetry
    std::array<float, numHoaChannels> m_hoaLevels {};
};

} // namespace dawg::engine::dsp
