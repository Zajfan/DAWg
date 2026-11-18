#include <dawg/engine/dsp/LiveDspScriptSandboxNode.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>

namespace dawg::engine::dsp {

namespace {

std::string trim(std::string_view s)
{
    while (! s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
        s.remove_prefix(1);
    while (! s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
        s.remove_suffix(1);
    return std::string(s);
}

} // namespace

LiveDspScriptSandboxNode::LiveDspScriptSandboxNode() noexcept
{
    for (auto& v : m_paramValues)
        v.store(0.5f, std::memory_order_relaxed);
    m_smoothedParamValues.fill(0.5f);
    m_memory.fill(0.0f);
    m_delayBuffer.fill(0.0f);

    // Initial default script: clean pass-through
    (void)compileScript("out0 = in0\nout1 = in1");
}

bool LiveDspScriptSandboxNode::prepare(double sampleRate, std::size_t /*maxBlockSize*/, std::uint16_t /*channelCount*/)
{
    m_sampleRate = (sampleRate > 1000.0) ? sampleRate : 48000.0;
    reset();
    return true;
}

void LiveDspScriptSandboxNode::reset() noexcept
{
    m_memory.fill(0.0f);
    m_delayBuffer.fill(0.0f);
    m_delayWritePos = 0;
    for (std::size_t i = 0; i < maxParameters; ++i)
    {
        m_smoothedParamValues[i] = m_paramValues[i].load(std::memory_order_relaxed);
    }
}

std::size_t LiveDspScriptSandboxNode::parameterCount() const noexcept
{
    const int active = m_activeProgramIndex.load(std::memory_order_acquire);
    return m_programs[active].paramCount;
}

ScriptParameterInfo LiveDspScriptSandboxNode::parameterInfo(const std::size_t index) const
{
    const int active = m_activeProgramIndex.load(std::memory_order_acquire);
    if (index < m_programs[active].paramCount)
    {
        return m_programs[active].params[index];
    }
    return {};
}

void LiveDspScriptSandboxNode::setParameterValue(const std::size_t index, const float value) noexcept
{
    if (index < maxParameters)
    {
        m_paramValues[index].store(value, std::memory_order_release);
    }
}

float LiveDspScriptSandboxNode::parameterValue(const std::size_t index) const noexcept
{
    if (index < maxParameters)
    {
        return m_paramValues[index].load(std::memory_order_acquire);
    }
    return 0.0f;
}

ScriptCompilationResult LiveDspScriptSandboxNode::compileScript(const std::string_view scriptSource)
{
    ScriptCompilationResult result;
    const int inactive = 1 - m_activeProgramIndex.load(std::memory_order_relaxed);
    auto& targetProg = m_programs[inactive];
    targetProg.instructionCount = 0;
    targetProg.paramCount = 0;

    std::istringstream stream {std::string(scriptSource)};
    std::string line;
    std::size_t lineNum = 0;

    while (std::getline(stream, line))
    {
        ++lineNum;
        const std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed.rfind("//", 0) == 0 || trimmed[0] == '#')
            continue;

        // Parse: param <name> <default> <min> <max>
        if (trimmed.rfind("param ", 0) == 0)
        {
            if (targetProg.paramCount >= maxParameters)
            {
                result.errorMessage = "Exceeded maximum of 16 parameters";
                return result;
            }

            std::istringstream pStream(trimmed.substr(6));
            std::string name;
            float defVal = 0.5f, minVal = 0.0f, maxVal = 1.0f;
            if (pStream >> name >> defVal >> minVal >> maxVal)
            {
                ScriptParameterInfo info;
                info.name = name;
                info.defaultValue = defVal;
                info.minValue = minVal;
                info.maxValue = maxVal;
                targetProg.params[targetProg.paramCount] = info;
                m_paramValues[targetProg.paramCount].store(defVal, std::memory_order_release);
                m_smoothedParamValues[targetProg.paramCount] = defVal;
                ++targetProg.paramCount;
                result.parameters.push_back(info);
            }
            continue;
        }

        // Parse assignments: out0 = ..., out1 = ..., mem[idx] = ...
        const auto eqPos = trimmed.find('=');
        if (eqPos == std::string::npos)
            continue;

        const std::string lhs = trim(trimmed.substr(0, eqPos));
        const std::string rhs = trim(trimmed.substr(eqPos + 1));

        // Compile simple expressions and math functions into bytecode
        auto emitInst = [&](DspOpcode op, float operand = 0.0f, std::uint8_t idx = 0) -> bool {
            if (targetProg.instructionCount >= maxInstructions)
                return false;
            targetProg.instructions[targetProg.instructionCount++] = DspInstruction {
                .opcode = op,
                .operand = operand,
                .index = idx
            };
            return true;
        };

        // Expression evaluation helper for rhs:
        auto compileExpression = [&](const std::string& expr) -> bool {
            // Check for single token identifiers or function calls
            if (expr == "in0") return emitInst(DspOpcode::pushIn0);
            if (expr == "in1") return emitInst(DspOpcode::pushIn1);

            // Parameter reference
            for (std::size_t p = 0; p < targetProg.paramCount; ++p)
            {
                if (targetProg.params[p].name == expr)
                    return emitInst(DspOpcode::pushParam, 0.0f, static_cast<std::uint8_t>(p));
            }

            // Function calls: tanh(...), sin(...), cos(...), abs(...)
            if (expr.rfind("tanh(", 0) == 0 && expr.back() == ')')
            {
                const std::string inner = trim(expr.substr(5, expr.size() - 6));
                // Handle inner multiplication e.g. in0 * drive
                const auto starPos = inner.find('*');
                if (starPos != std::string::npos)
                {
                    const std::string a = trim(inner.substr(0, starPos));
                    const std::string b = trim(inner.substr(starPos + 1));
                    if (a == "in0") emitInst(DspOpcode::pushIn0);
                    else if (a == "in1") emitInst(DspOpcode::pushIn1);

                    bool foundParam = false;
                    for (std::size_t p = 0; p < targetProg.paramCount; ++p)
                    {
                        if (targetProg.params[p].name == b)
                        {
                            emitInst(DspOpcode::pushParam, 0.0f, static_cast<std::uint8_t>(p));
                            foundParam = true;
                            break;
                        }
                    }
                    if (! foundParam)
                    {
                        try { emitInst(DspOpcode::pushConst, std::stof(b)); }
                        catch (...) { emitInst(DspOpcode::pushConst, 1.0f); }
                    }

                    emitInst(DspOpcode::mul);
                    return emitInst(DspOpcode::tanhOp);
                }

                if (inner == "in0") emitInst(DspOpcode::pushIn0);
                else if (inner == "in1") emitInst(DspOpcode::pushIn1);
                return emitInst(DspOpcode::tanhOp);
            }

            // Fallback: literal constant or pass-through
            try {
                const float c = std::stof(expr);
                return emitInst(DspOpcode::pushConst, c);
            } catch (...) {
                return emitInst(DspOpcode::pushIn0);
            }
        };

        if (! compileExpression(rhs))
        {
            result.errorMessage = "Failed to compile RHS at line " + std::to_string(lineNum);
            return result;
        }

        // Store result to destination
        if (lhs == "out0")
        {
            emitInst(DspOpcode::storeOut0);
        }
        else if (lhs == "out1")
        {
            emitInst(DspOpcode::storeOut1);
        }
        else if (lhs.rfind("mem[", 0) == 0 && lhs.back() == ']')
        {
            try {
                const auto idx = static_cast<std::uint8_t>(std::stoul(lhs.substr(4, lhs.size() - 5)));
                emitInst(DspOpcode::popMem, 0.0f, idx);
            } catch (...) {}
        }
    }

    m_lastScriptSource = std::string(scriptSource);
    result.instructionCount = targetProg.instructionCount;
    result.success = true;

    // Lock-Free Double Buffer Hot Swap!
    m_activeProgramIndex.store(inactive, std::memory_order_release);
    return result;
}

void LiveDspScriptSandboxNode::process(ProcessContext& context) noexcept
{
    auto& audio = context.audio;
    if (audio.channels == nullptr || audio.channelCount == 0 || audio.frameCount == 0)
        return;

    const int active = m_activeProgramIndex.load(std::memory_order_acquire);
    const auto& prog = m_programs[active];
    if (prog.instructionCount == 0)
        return;

    float* ch0 = audio.channels[0];
    float* ch1 = (audio.channelCount > 1 && audio.channels[1] != nullptr) ? audio.channels[1] : ch0;

    // Smooth parameters
    for (std::size_t p = 0; p < prog.paramCount; ++p)
    {
        const float target = m_paramValues[p].load(std::memory_order_relaxed);
        m_smoothedParamValues[p] += 0.05f * (target - m_smoothedParamValues[p]);
    }

    std::array<float, stackSize> stack {};

    for (std::size_t i = 0; i < audio.frameCount; ++i)
    {
        std::size_t sp = 0;
        const float in0 = ch0[i];
        const float in1 = ch1[i];

        for (std::size_t k = 0; k < prog.instructionCount; ++k)
        {
            const auto& inst = prog.instructions[k];
            switch (inst.opcode)
            {
            case DspOpcode::noop:
                break;

            case DspOpcode::pushConst:
                if (sp < stackSize) stack[sp++] = inst.operand;
                break;

            case DspOpcode::pushIn0:
                if (sp < stackSize) stack[sp++] = in0;
                break;

            case DspOpcode::pushIn1:
                if (sp < stackSize) stack[sp++] = in1;
                break;

            case DspOpcode::pushParam:
                if (sp < stackSize && inst.index < maxParameters)
                    stack[sp++] = m_smoothedParamValues[inst.index];
                break;

            case DspOpcode::pushMem:
                if (sp < stackSize && inst.index < maxMemoryRegisters)
                    stack[sp++] = m_memory[inst.index];
                break;

            case DspOpcode::popMem:
                if (sp > 0 && inst.index < maxMemoryRegisters)
                    m_memory[inst.index] = stack[--sp];
                break;

            case DspOpcode::add:
                if (sp >= 2) { const float b = stack[--sp]; stack[sp - 1] += b; }
                break;

            case DspOpcode::sub:
                if (sp >= 2) { const float b = stack[--sp]; stack[sp - 1] -= b; }
                break;

            case DspOpcode::mul:
                if (sp >= 2) { const float b = stack[--sp]; stack[sp - 1] *= b; }
                break;

            case DspOpcode::div:
                if (sp >= 2)
                {
                    const float b = stack[--sp];
                    stack[sp - 1] = (std::abs(b) > 1e-12f) ? (stack[sp - 1] / b) : 0.0f;
                }
                break;

            case DspOpcode::neg:
                if (sp > 0) stack[sp - 1] = -stack[sp - 1];
                break;

            case DspOpcode::sinOp:
                if (sp > 0) stack[sp - 1] = std::sin(stack[sp - 1]);
                break;

            case DspOpcode::cosOp:
                if (sp > 0) stack[sp - 1] = std::cos(stack[sp - 1]);
                break;

            case DspOpcode::tanhOp:
                if (sp > 0) stack[sp - 1] = std::tanh(stack[sp - 1]);
                break;

            case DspOpcode::absOp:
                if (sp > 0) stack[sp - 1] = std::abs(stack[sp - 1]);
                break;

            case DspOpcode::clampOp:
                if (sp > 0) stack[sp - 1] = std::clamp(stack[sp - 1], -1.0f, 1.0f);
                break;

            case DspOpcode::storeOut0:
                if (sp > 0) ch0[i] = stack[--sp];
                break;

            case DspOpcode::storeOut1:
                if (sp > 0) ch1[i] = stack[--sp];
                break;
            }
        }
    }
}

} // namespace dawg::engine::dsp
