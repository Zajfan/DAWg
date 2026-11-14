#pragma once

#include <dawg/engine/ProcessorNode.h>
#include <dawg/engine/clap/clap.h>

#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace dawg::engine {

class ClapPluginNode final : public ProcessorNode
{
public:
    explicit ClapPluginNode(const clap_plugin* plugin, bool sandboxed = false) noexcept;
    ~ClapPluginNode() override;

    ClapPluginNode(const ClapPluginNode&) = delete;
    ClapPluginNode& operator=(const ClapPluginNode&) = delete;

    [[nodiscard]] bool prepare(double sampleRate, std::size_t maxBlockSize,
                               std::uint16_t channelCount) override;
    void reset() noexcept override;
    [[nodiscard]] std::uint32_t latencySamples() const noexcept override;
    void process(ProcessContext& context) noexcept override;

    [[nodiscard]] bool isSandboxed() const noexcept { return m_sandboxed; }
    [[nodiscard]] bool isCrashed() const noexcept { return m_crashed.load(std::memory_order_relaxed); }
    void markCrashed() noexcept { m_crashed.store(true, std::memory_order_relaxed); }
    void clearCrashed() noexcept { m_crashed.store(false, std::memory_order_relaxed); }

    [[nodiscard]] const clap_plugin* plugin() const noexcept { return m_plugin; }

private:
    void setupClapEvents(const MidiBufferView& midi) noexcept;

    const clap_plugin* m_plugin {nullptr};
    bool m_sandboxed {false};
    std::atomic<bool> m_crashed {false};
    double m_sampleRate {48000.0};
    std::size_t m_maxBlockSize {512};
    std::uint16_t m_channelCount {2};
    std::uint32_t m_latency {0};

    // Preallocated event translation arrays
    static constexpr std::size_t maxEvents = 256;
    std::array<clap_event_midi, maxEvents> m_clapMidiEvents {};
    std::size_t m_clapEventCount {0};
    clap_input_events m_inEvents {};

    // Scratch buffers for planar audio
    std::array<float*, 2> m_inChannelPtrs {nullptr, nullptr};
    std::array<float*, 2> m_outChannelPtrs {nullptr, nullptr};
    clap_audio_buffer m_audioIn {};
    clap_audio_buffer m_audioOut {};
};

} // namespace dawg::engine
