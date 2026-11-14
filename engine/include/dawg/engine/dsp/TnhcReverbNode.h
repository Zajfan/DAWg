#pragma once

#include <dawg/engine/ProcessorNode.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace dawg::engine {

class TnhcReverbNode final : public ProcessorNode
{
public:
    static constexpr std::size_t numFdnLines = 8;
    static constexpr std::size_t numDiffusers = 4;

    TnhcReverbNode() noexcept;
    ~TnhcReverbNode() override = default;

    [[nodiscard]] bool prepare(double sampleRate, std::size_t maxBlockSize,
                               std::uint16_t channelCount) override;
    void reset() noexcept override;
    [[nodiscard]] std::uint32_t latencySamples() const noexcept override { return 0; }
    void process(ProcessContext& context) noexcept override;

    // Parameters
    void setRoomSize(float size) noexcept;
    [[nodiscard]] float roomSize() const noexcept { return m_roomSize; }

    void setDecayTime(float seconds) noexcept;
    [[nodiscard]] float decayTime() const noexcept { return m_decayTime; }

    void setDamping(float damp) noexcept;
    [[nodiscard]] float damping() const noexcept { return m_damping; }

    void setStereoWidth(float width) noexcept;
    [[nodiscard]] float stereoWidth() const noexcept { return m_stereoWidth; }

    void setWetDry(float wet) noexcept;
    [[nodiscard]] float wetDry() const noexcept { return m_wetDry; }

private:
    struct Diffuser
    {
        std::vector<float> buffer;
        std::size_t writeIdx {0};
        std::size_t length {0};
        float coefficient {0.6f};

        void prepare(std::size_t len)
        {
            length = std::max<std::size_t>(1, len);
            buffer.assign(length, 0.0f);
            writeIdx = 0;
        }

        void reset() noexcept
        {
            std::fill(buffer.begin(), buffer.end(), 0.0f);
            writeIdx = 0;
        }

        [[nodiscard]] float process(float in) noexcept
        {
            if (buffer.empty())
                return in;
            const float bufOut = buffer[writeIdx];
            const float v = in - coefficient * bufOut;
            buffer[writeIdx] = v;
            writeIdx = (writeIdx + 1) % length;
            return bufOut + coefficient * v;
        }
    };

    struct FdnDelayLine
    {
        std::vector<float> buffer;
        std::size_t writeIdx {0};
        std::size_t length {0};
        float feedbackGain {0.75f};
        float dampFilterState {0.0f};

        void prepare(std::size_t len)
        {
            length = std::max<std::size_t>(1, len);
            buffer.assign(length, 0.0f);
            writeIdx = 0;
            dampFilterState = 0.0f;
        }

        void reset() noexcept
        {
            std::fill(buffer.begin(), buffer.end(), 0.0f);
            writeIdx = 0;
            dampFilterState = 0.0f;
        }

        [[nodiscard]] float read() const noexcept
        {
            if (buffer.empty())
                return 0.0f;
            return buffer[writeIdx];
        }

        void write(float val, float dampCoeff) noexcept
        {
            if (buffer.empty())
                return;
            // 1-pole lowpass damping in feedback loop
            dampFilterState = (1.0f - dampCoeff) * val + dampCoeff * dampFilterState;
            buffer[writeIdx] = dampFilterState * feedbackGain;
            writeIdx = (writeIdx + 1) % length;
        }
    };

    void updateFeedbackGains() noexcept;

    double m_sampleRate {48000.0};
    std::uint16_t m_channelCount {2};

    float m_roomSize {0.7f};
    float m_decayTime {2.5f};
    float m_damping {0.35f};
    float m_stereoWidth {1.0f};
    float m_wetDry {0.35f};

    std::array<Diffuser, numDiffusers> m_diffusers;
    std::array<FdnDelayLine, numFdnLines> m_fdnLines;
};

} // namespace dawg::engine
