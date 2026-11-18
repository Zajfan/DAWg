#pragma once

#include <dawg/engine/ProcessorNode.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace dawg::engine::dsp {

enum class DspOpcode : std::uint8_t
{
    noop = 0,
    pushConst,
    pushIn0,
    pushIn1,
    pushParam,
    pushMem,
    popMem,
    add,
    sub,
    mul,
    div,
    neg,
    sinOp,
    cosOp,
    tanhOp,
    clampOp,
    absOp,
    storeOut0,
    storeOut1
};

struct DspInstruction
{
    DspOpcode opcode {DspOpcode::noop};
    float operand {0.0f};
    std::uint8_t index {0}; // param index, mem register index, etc.
};

struct ScriptParameterInfo
{
    std::string name;
    float defaultValue {0.5f};
    float minValue {0.0f};
    float maxValue {1.0f};
};

struct ScriptCompilationResult
{
    bool success {false};
    std::string errorMessage;
    std::vector<ScriptParameterInfo> parameters;
    std::size_t instructionCount {0};
};

class LiveDspScriptSandboxNode final : public ProcessorNode
{
public:
    static constexpr std::size_t maxInstructions = 512;
    static constexpr std::size_t maxParameters = 16;
    static constexpr std::size_t maxMemoryRegisters = 32;
    static constexpr std::size_t maxDelaySamples = 48000; // 1 second mono/stereo delay
    static constexpr std::size_t stackSize = 64;

    LiveDspScriptSandboxNode() noexcept;
    ~LiveDspScriptSandboxNode() override = default;

    [[nodiscard]] bool prepare(
        double sampleRate,
        std::size_t maxBlockSize,
        std::uint16_t channelCount) override;

    void process(ProcessContext& context) noexcept override;
    void reset() noexcept override;

    [[nodiscard]] std::string_view name() const noexcept { return "Live DSP Script Sandbox"; }

    // =========================================================================
    // Lock-Free Hot Reloading Script Compilation (Call from control/GUI thread)
    // =========================================================================
    [[nodiscard]] ScriptCompilationResult compileScript(std::string_view scriptSource);
    [[nodiscard]] std::string_view lastScriptSource() const noexcept { return m_lastScriptSource; }

    // =========================================================================
    // Dynamic Parameter Reflection & UI Controls
    // =========================================================================
    [[nodiscard]] std::size_t parameterCount() const noexcept;
    [[nodiscard]] ScriptParameterInfo parameterInfo(std::size_t index) const;
    void setParameterValue(std::size_t index, float value) noexcept;
    [[nodiscard]] float parameterValue(std::size_t index) const noexcept;

private:
    struct CompiledProgram
    {
        std::array<DspInstruction, maxInstructions> instructions {};
        std::size_t instructionCount {0};
        std::array<ScriptParameterInfo, maxParameters> params {};
        std::size_t paramCount {0};
    };

    double m_sampleRate {48000.0};
    std::string m_lastScriptSource;

    // Ping-pong double buffers for lock-free audio thread hot-swapping
    std::array<CompiledProgram, 2> m_programs {};
    std::atomic<int> m_activeProgramIndex {0};

    // Real-time parameter values (smoothly updated)
    std::array<std::atomic<float>, maxParameters> m_paramValues {};
    std::array<float, maxParameters> m_smoothedParamValues {};

    // Internal VM state registers (per voice/instance)
    std::array<float, maxMemoryRegisters> m_memory {};

    // Internal delay line for time-based DSP scripts
    std::array<float, maxDelaySamples> m_delayBuffer {};
    std::size_t m_delayWritePos {0};
};

} // namespace dawg::engine::dsp
