#pragma once

#include <dawg/engine/AudioBufferView.h>
#include <dawg/engine/ProcessorNode.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace dawg::engine::dsp {

enum class GridModuleType : std::uint8_t
{
    oscillator,
    noise,
    svfFilter,
    mathSaturation,
    lfo,
    envelope,
    delay,
    audioOut,
    unitDelayFeedback, // z^-1 feedback loop operator with DC-blocking and saturation
    wavefolder,        // Buchla-style wavefolding & diode clipper
    xyPad,             // 2D macro performance modulator
    sampleAndHold      // Stepped random / quantized voltage generator
};

enum class PortSignalType : std::uint8_t
{
    audio,
    cv
};

struct GridPort
{
    std::string name;
    PortSignalType type {PortSignalType::audio};
    float defaultValue {0.0f};
    float currentValue {0.0f};
};

struct GridCable
{
    std::uint32_t id {0};
    std::uint32_t srcModuleId {0};
    std::uint8_t srcPort {0};
    std::uint32_t dstModuleId {0};
    std::uint8_t dstPort {0};
    std::uint32_t colorRgba {0x33FFB0FF}; // Vibrant cyan-teal default
};

struct GridMacroBinding
{
    std::size_t macroIndex {0}; // 0..7
    std::uint32_t moduleId {0};
    std::size_t paramIndex {0};
    float minVal {0.0f};
    float maxVal {1.0f};
    std::string label;
};

struct GridModule
{
    std::uint32_t id {0};
    GridModuleType type {GridModuleType::oscillator};
    std::string name;
    float posX {100.0f};
    float posY {100.0f};

    std::vector<GridPort> inputs;
    std::vector<GridPort> outputs;
    std::array<float, 4> params {0.5f, 0.5f, 0.5f, 0.5f};

    // Internal DSP state
    float phase {0.0f};
    float filterS1 {0.0f};
    float filterS2 {0.0f};
    float envStage {0.0f};
    float envValue {0.0f};
    std::vector<float> delayBuffer;
    std::size_t delayWritePos {0};

    // Single-sample feedback state
    float feedbackSample {0.0f};
    float dcBlockerPrevX {0.0f};
    float dcBlockerPrevY {0.0f};

    // Sample & Hold state
    float shHeldValue {0.0f};
    float shLastTrigger {0.0f};
    std::uint32_t rngState {123456789};
};

// Visual Modular DSP Wire Grid (Bitwig "The Grid" / Max/MSP style)
// High-performance real-time modular engine with dynamic patch cabling,
// single-sample feedback loops, wavefolders, macro surface mapping,
// live probe oscilloscopes, and studio presets.
class DspWireGrid final : public ProcessorNode
{
public:
    static constexpr std::size_t numMacros = 8;
    static constexpr std::size_t probeBufferSize = 256;

    DspWireGrid() noexcept;
    ~DspWireGrid() override = default;

    [[nodiscard]] bool prepare(
        double sampleRate,
        std::size_t maxBlockSize,
        std::uint16_t channelCount) override;

    void reset() noexcept override;
    [[nodiscard]] std::uint32_t latencySamples() const noexcept override { return 0; }
    void process(ProcessContext& context) noexcept override;

    // Macro Controls (0.0 to 1.0)
    void setMacro(std::size_t index, float value) noexcept;
    [[nodiscard]] float macro(std::size_t index) const noexcept;

    // Custom Surface Macro Mapping
    void bindMacro(
        std::size_t macroIdx,
        std::uint32_t moduleId,
        std::size_t paramIdx,
        float minVal = 0.0f,
        float maxVal = 1.0f,
        const std::string& label = "");
    void unbindMacro(std::size_t macroIdx, std::uint32_t moduleId, std::size_t paramIdx) noexcept;
    [[nodiscard]] const std::vector<GridMacroBinding>& macroBindings() const noexcept { return m_macroBindings; }

    // Module & Cable Management
    std::uint32_t addModule(GridModuleType type, float x, float y, const std::string& name = "");
    bool removeModule(std::uint32_t moduleId);
    void setModuleParam(std::uint32_t moduleId, std::size_t paramIdx, float value);

    std::uint32_t connectCable(
        std::uint32_t srcModuleId, std::uint8_t srcPort,
        std::uint32_t dstModuleId, std::uint8_t dstPort,
        std::uint32_t colorRgba = 0x33FFB0FF);
    bool disconnectCable(std::uint32_t cableId);

    [[nodiscard]] const std::vector<GridModule>& modules() const noexcept { return m_modules; }
    [[nodiscard]] const std::vector<GridCable>& cables() const noexcept { return m_cables; }

    // Probe Oscilloscope readout (ring buffer of latest probed signal samples)
    void getProbeData(float* destination, std::size_t count) const noexcept;
    void setActiveProbeCable(std::uint32_t cableId) noexcept { m_probedCableId = cableId; }
    [[nodiscard]] std::uint32_t activeProbeCable() const noexcept { return m_probedCableId; }

    // Presets
    void loadPresetAcidBass();
    void loadPresetAmbientDrone();
    void loadPresetWestCoastWavefolder();
    void loadPresetPercussionSynth();
    void loadPresetFeedbackResonator();

private:
    double m_sampleRate {48000.0};
    std::size_t m_maxBlockSize {512};
    std::uint32_t m_nextModuleId {1};
    std::uint32_t m_nextCableId {1};
    std::uint32_t m_probedCableId {0};

    std::array<float, numMacros> m_macros {};
    std::vector<GridMacroBinding> m_macroBindings;
    std::vector<GridModule> m_modules;
    std::vector<GridCable> m_cables;

    // Pre-allocated per-module audio output buffers to guarantee zero RT allocation
    std::vector<std::vector<float>> m_moduleScratchBuffers;

    // Real-time lock-free probe ring buffer
    std::array<float, probeBufferSize> m_probeRingBuffer {};
    std::size_t m_probeWritePos {0};

    void reallocateScratchBuffers();
};

} // namespace dawg::engine::dsp
