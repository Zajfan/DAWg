#pragma once

#include <dawg/engine/AudioBufferView.h>
#include <dawg/engine/MidiEvent.h>
#include <dawg/engine/ProcessorNode.h>

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace dawg::engine::dsp {

enum class DrumInstrumentType : std::uint8_t
{
    kick = 0,
    snare,
    tom,
    custom
};

struct DrumSampleLayer
{
    std::vector<float> sampleDataL;
    std::vector<float> sampleDataR;
    std::size_t sampleLength {0};
    float minVelocity {0.0f};
    float maxVelocity {1.0f};
};

struct ActiveDrumVoice
{
    bool playing {false};
    std::size_t sampleIndex {0};
    std::size_t playbackCursor {0};
    float gain {1.0f};
};

class MultitrackDrumReplacerNode final : public ProcessorNode
{
public:
    static constexpr std::size_t maxLayers = 4;
    static constexpr std::size_t maxSimultaneousVoices = 4;

    MultitrackDrumReplacerNode() noexcept;
    ~MultitrackDrumReplacerNode() override = default;

    [[nodiscard]] bool prepare(
        double sampleRate,
        std::size_t maxBlockSize,
        std::uint16_t channelCount) override;

    void process(ProcessContext& context) noexcept override;
    void reset() noexcept override;

    [[nodiscard]] std::string_view name() const noexcept { return "Multitrack Drum Replacer & Transient Trigger"; }

    // Instrument Mode & Detection Filter
    void setInstrumentType(DrumInstrumentType type) noexcept;
    [[nodiscard]] DrumInstrumentType instrumentType() const noexcept { return m_instrumentType; }
    void setDetectionBand(float lowCutHz, float highCutHz) noexcept;
    [[nodiscard]] float detectionLowCut() const noexcept { return m_detectionLowCut; }
    [[nodiscard]] float detectionHighCut() const noexcept { return m_detectionHighCut; }

    // Trigger Dynamics & Bleed Rejection
    void setThresholdDb(float threshDb) noexcept;
    [[nodiscard]] float thresholdDb() const noexcept { return m_thresholdDb; }
    void setRetriggerHoldoffMs(float ms) noexcept;
    [[nodiscard]] float retriggerHoldoffMs() const noexcept { return m_holdoffMs; }
    void setDryWetMix(float mixRatio) noexcept { m_dryWetMix = std::clamp(mixRatio, 0.0f, 1.0f); }
    [[nodiscard]] float dryWetMix() const noexcept { return m_dryWetMix; }

    // MIDI Trigger Settings
    void setTriggerMidiNote(uint8_t note) noexcept { m_midiNote = note; }
    [[nodiscard]] uint8_t triggerMidiNote() const noexcept { return m_midiNote; }
    void setMidiOutputEnabled(bool enabled) noexcept { m_midiOutputEnabled = enabled; }
    [[nodiscard]] bool isMidiOutputEnabled() const noexcept { return m_midiOutputEnabled; }
    [[nodiscard]] std::size_t triggerCount() const noexcept { return m_triggerCount.load(std::memory_order_relaxed); }

    // Sample Management
    void loadSampleLayer(std::size_t layerIndex, const float* left, const float* right, std::size_t frames, float minVel, float maxVel);
    void generateInternalSamples() noexcept;

    // Last Trigger Telemetry
    [[nodiscard]] float lastTriggerVelocity() const noexcept { return m_lastTriggerVelocity.load(std::memory_order_relaxed); }

private:
    void updateDetectionFilters() noexcept;
    void triggerDrumHit(float peakLevel, std::size_t frameOffset) noexcept;

    double m_sampleRate {48000.0};
    std::uint16_t m_channelCount {2};

    DrumInstrumentType m_instrumentType {DrumInstrumentType::snare};
    float m_detectionLowCut {180.0f};
    float m_detectionHighCut {3200.0f};

    float m_thresholdDb {-20.0f};
    float m_thresholdLinear {0.1f};
    float m_holdoffMs {35.0f};
    std::size_t m_holdoffSamples {1680};
    std::size_t m_holdoffCounter {0};

    float m_dryWetMix {1.0f}; // 1.0 = 100% replacement sample, 0.5 = 50/50 blend
    uint8_t m_midiNote {38};  // General MIDI Snare 1
    bool m_midiOutputEnabled {true};

    std::atomic<std::size_t> m_triggerCount {0};
    std::atomic<float> m_lastTriggerVelocity {0.0f};

    // Detection bandpass state
    float m_bpB0 {1.0f}, m_bpB1 {0.0f}, m_bpB2 {0.0f};
    float m_bpA1 {0.0f}, m_bpA2 {0.0f};
    float m_bpZ1 {0.0f}, m_bpZ2 {0.0f};

    float m_prevDetectorSample {0.0f};
    float m_detectorEnv {0.0f};

    // Pre-loaded drum sample layers
    std::array<DrumSampleLayer, maxLayers> m_layers;
    std::array<ActiveDrumVoice, maxSimultaneousVoices> m_voices;
};

} // namespace dawg::engine::dsp
