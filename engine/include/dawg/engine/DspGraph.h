#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include <dawg/engine/AudioBufferView.h>
#include <dawg/engine/ProcessorNode.h>
#include <dawg/engine/WorkStealingScheduler.h>

namespace dawg::engine
{
class DspGraph
{
public:
    using NodeId = std::uint32_t;

    DspGraph() = default;

    NodeId addNode(
        std::unique_ptr<ProcessorNode> node,
        bool isInputSource = false,
        bool isMasterOutput = false);

    [[nodiscard]] bool addEdge(NodeId from, NodeId to);

    [[nodiscard]] bool compile(
        double sampleRate,
        std::size_t maxBlockSize,
        std::uint16_t channelCount);

    void reset() noexcept;

    void process(
        AudioBufferView input,
        AudioBufferView output,
        WorkStealingScheduler* scheduler = nullptr) noexcept;

    [[nodiscard]] std::uint32_t totalLatencySamples() const noexcept
    {
        return m_totalLatencySamples;
    }

    [[nodiscard]] std::size_t wavefrontCount() const noexcept
    {
        return m_wavefronts.size();
    }

private:
    struct IncomingEdge
    {
        NodeId fromNode {0};
        std::uint32_t compensationSamples {0};
        DelayCompensatorNode compensator {0};
        std::vector<float> scratchStorage;
        std::vector<float*> scratchChannels;
    };

    struct NodeEntry
    {
        std::unique_ptr<ProcessorNode> processor;
        bool isInputSource {false};
        bool isMasterOutput {false};
        std::vector<NodeId> outgoing;
        std::vector<IncomingEdge> incoming;
        std::vector<float> bufferStorage;
        std::vector<float*> bufferChannels;
        std::uint32_t arrivalLatency {0};
        std::uint32_t outputLatency {0};
    };

    void processNode(NodeId nodeId, AudioBufferView input, std::size_t numSamples) noexcept;

    std::vector<NodeEntry> m_nodes;
    std::vector<std::vector<NodeId>> m_wavefronts;
    double m_sampleRate {48000.0};
    std::size_t m_maxBlockSize {0};
    std::uint16_t m_channelCount {0};
    std::uint32_t m_totalLatencySamples {0};
    bool m_compiled {false};
};
} // namespace dawg::engine
