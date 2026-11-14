#include <dawg/engine/DspGraph.h>

#include <algorithm>
#include <queue>
#include <utility>

namespace dawg::engine
{
DspGraph::NodeId DspGraph::addNode(
    std::unique_ptr<ProcessorNode> node,
    bool isInputSource,
    bool isMasterOutput)
{
    m_compiled = false;
    const auto id = static_cast<NodeId>(m_nodes.size());
    m_nodes.push_back(NodeEntry {
        .processor = std::move(node),
        .isInputSource = isInputSource,
        .isMasterOutput = isMasterOutput,
        .outgoing = {},
        .incoming = {},
        .sidechainEdge = std::nullopt,
        .bufferStorage = {},
        .bufferChannels = {},
        .arrivalLatency = 0,
        .outputLatency = 0
    });
    return id;
}

bool DspGraph::addEdge(NodeId from, NodeId to)
{
    if (from >= m_nodes.size() || to >= m_nodes.size() || from == to)
    {
        return false;
    }

    auto& source = m_nodes[from];
    if (std::find(source.outgoing.begin(), source.outgoing.end(), to) != source.outgoing.end())
    {
        return false;
    }

    m_compiled = false;
    source.outgoing.push_back(to);
    m_nodes[to].incoming.push_back(IncomingEdge {
        .fromNode = from,
        .compensationSamples = 0,
        .compensator = DelayCompensatorNode {0},
        .scratchStorage = {},
        .scratchChannels = {}
    });
    return true;
}

bool DspGraph::addSidechainEdge(NodeId from, NodeId to)
{
    if (from >= m_nodes.size() || to >= m_nodes.size() || from == to)
    {
        return false;
    }

    auto& source = m_nodes[from];
    if (std::find(source.outgoing.begin(), source.outgoing.end(), to) != source.outgoing.end())
    {
        return false;
    }

    m_compiled = false;
    source.outgoing.push_back(to);
    m_nodes[to].sidechainEdge = IncomingEdge {
        .fromNode = from,
        .compensationSamples = 0,
        .compensator = DelayCompensatorNode {0},
        .scratchStorage = {},
        .scratchChannels = {}
    };
    return true;
}

bool DspGraph::compile(
    double sampleRate,
    std::size_t maxBlockSize,
    std::uint16_t channelCount)
{
    m_compiled = false;
    m_wavefronts.clear();
    m_totalLatencySamples = 0;

    if (sampleRate <= 0.0 || maxBlockSize == 0 || channelCount == 0 || m_nodes.empty())
    {
        return false;
    }

    const auto nodeCount = m_nodes.size();
    std::vector<std::size_t> inDegree(nodeCount, 0);
    std::vector<std::size_t> level(nodeCount, 0);

    for (std::size_t i = 0; i < nodeCount; ++i)
    {
        if (m_nodes[i].processor == nullptr)
        {
            return false;
        }
        inDegree[i] = m_nodes[i].incoming.size() + (m_nodes[i].sidechainEdge.has_value() ? 1 : 0);
    }

    std::queue<NodeId> ready;
    for (NodeId i = 0; i < static_cast<NodeId>(nodeCount); ++i)
    {
        if (inDegree[i] == 0)
        {
            ready.push(i);
        }
    }

    std::size_t visitedCount = 0;
    std::size_t maxLevel = 0;

    while (! ready.empty())
    {
        const auto u = ready.front();
        ready.pop();
        ++visitedCount;

        for (const auto v : m_nodes[u].outgoing)
        {
            level[v] = std::max(level[v], level[u] + 1);
            maxLevel = std::max(maxLevel, level[v]);
            if (--inDegree[v] == 0)
            {
                ready.push(v);
            }
        }
    }

    if (visitedCount != nodeCount)
    {
        return false;
    }

    m_wavefronts.resize(maxLevel + 1);
    for (NodeId i = 0; i < static_cast<NodeId>(nodeCount); ++i)
    {
        m_wavefronts[level[i]].push_back(i);
    }

    for (auto& node : m_nodes)
    {
        if (! node.processor->prepare(sampleRate, maxBlockSize, channelCount))
        {
            return false;
        }

        node.bufferStorage.assign(static_cast<std::size_t>(channelCount) * maxBlockSize, 0.0f);
        node.bufferChannels.resize(channelCount);
        for (std::uint16_t ch = 0; ch < channelCount; ++ch)
        {
            node.bufferChannels[ch] = node.bufferStorage.data() + static_cast<std::size_t>(ch) * maxBlockSize;
        }
        node.arrivalLatency = 0;
        node.outputLatency = 0;
    }

    // Forward pass in topological wavefront order to compute arrival/output latency and insert PDC
    std::uint32_t maxMasterLatency = 0;
    std::uint32_t maxAnyLatency = 0;

    for (const auto& wavefront : m_wavefronts)
    {
        for (const auto nodeId : wavefront)
        {
            auto& node = m_nodes[nodeId];
            std::uint32_t maxIncomingLatency = 0;
            for (const auto& edge : node.incoming)
            {
                maxIncomingLatency = std::max(maxIncomingLatency, m_nodes[edge.fromNode].outputLatency);
            }

            node.arrivalLatency = maxIncomingLatency;
            node.outputLatency = node.arrivalLatency + node.processor->latencySamples();

            for (auto& edge : node.incoming)
            {
                const auto sourceLatency = m_nodes[edge.fromNode].outputLatency;
                edge.compensationSamples = node.arrivalLatency - sourceLatency;
                edge.compensator = DelayCompensatorNode {edge.compensationSamples};

                if (edge.compensationSamples > 0)
                {
                    if (! edge.compensator.prepare(sampleRate, maxBlockSize, channelCount))
                    {
                        return false;
                    }
                    edge.scratchStorage.assign(static_cast<std::size_t>(channelCount) * maxBlockSize, 0.0f);
                    edge.scratchChannels.resize(channelCount);
                    for (std::uint16_t ch = 0; ch < channelCount; ++ch)
                    {
                        edge.scratchChannels[ch] =
                            edge.scratchStorage.data() + static_cast<std::size_t>(ch) * maxBlockSize;
                    }
                }
                else
                {
                    edge.scratchStorage.clear();
                    edge.scratchChannels.clear();
                }
            }

            if (node.sidechainEdge.has_value())
            {
                auto& scEdge = *node.sidechainEdge;
                const auto sourceLatency = m_nodes[scEdge.fromNode].outputLatency;
                scEdge.compensationSamples = (node.arrivalLatency > sourceLatency)
                    ? (node.arrivalLatency - sourceLatency)
                    : 0;
                scEdge.compensator = DelayCompensatorNode {scEdge.compensationSamples};

                if (scEdge.compensationSamples > 0)
                {
                    if (! scEdge.compensator.prepare(sampleRate, maxBlockSize, channelCount))
                    {
                        return false;
                    }
                    scEdge.scratchStorage.assign(static_cast<std::size_t>(channelCount) * maxBlockSize, 0.0f);
                    scEdge.scratchChannels.resize(channelCount);
                    for (std::uint16_t ch = 0; ch < channelCount; ++ch)
                    {
                        scEdge.scratchChannels[ch] =
                            scEdge.scratchStorage.data() + static_cast<std::size_t>(ch) * maxBlockSize;
                    }
                }
                else
                {
                    scEdge.scratchStorage.clear();
                    scEdge.scratchChannels.clear();
                }
            }

            maxAnyLatency = std::max(maxAnyLatency, node.outputLatency);
            if (node.isMasterOutput)
            {
                maxMasterLatency = std::max(maxMasterLatency, node.outputLatency);
            }
        }
    }

    m_sampleRate = sampleRate;
    m_maxBlockSize = maxBlockSize;
    m_channelCount = channelCount;
    m_totalLatencySamples = (maxMasterLatency > 0) ? maxMasterLatency : maxAnyLatency;
    m_compiled = true;
    return true;
}

void DspGraph::reset() noexcept
{
    for (auto& node : m_nodes)
    {
        if (node.processor != nullptr)
        {
            node.processor->reset();
        }
        std::fill(node.bufferStorage.begin(), node.bufferStorage.end(), 0.0f);
        for (auto& edge : node.incoming)
        {
            edge.compensator.reset();
            std::fill(edge.scratchStorage.begin(), edge.scratchStorage.end(), 0.0f);
        }
        if (node.sidechainEdge.has_value())
        {
            node.sidechainEdge->compensator.reset();
            std::fill(node.sidechainEdge->scratchStorage.begin(), node.sidechainEdge->scratchStorage.end(), 0.0f);
        }
    }
}

void DspGraph::processNode(NodeId nodeId, AudioBufferView input, std::size_t numSamples) noexcept
{
    auto& node = m_nodes[nodeId];

    for (std::uint16_t ch = 0; ch < m_channelCount; ++ch)
    {
        std::fill_n(node.bufferChannels[ch], numSamples, 0.0f);
    }

    if (node.isInputSource && input.channels != nullptr && input.frameCount > 0)
    {
        const auto copyChannels = std::min<std::size_t>(input.channelCount, m_channelCount);
        const auto copySamples = std::min<std::size_t>(input.frameCount, numSamples);
        for (std::size_t ch = 0; ch < copyChannels; ++ch)
        {
            if (input.channels[ch] != nullptr)
            {
                std::copy_n(input.channels[ch], copySamples, node.bufferChannels[ch]);
            }
        }
    }

    for (auto& edge : node.incoming)
    {
        const auto& upstream = m_nodes[edge.fromNode];
        if (edge.compensationSamples > 0)
        {
            for (std::uint16_t ch = 0; ch < m_channelCount; ++ch)
            {
                std::copy_n(upstream.bufferChannels[ch], numSamples, edge.scratchChannels[ch]);
            }

            ProcessContext edgeContext {
                .audio = AudioBufferView {edge.scratchChannels.data(), m_channelCount, numSamples},
                .midi = {},
                .sampleRate = m_sampleRate
            };
            edge.compensator.process(edgeContext);

            for (std::uint16_t ch = 0; ch < m_channelCount; ++ch)
            {
                auto* dst = node.bufferChannels[ch];
                const auto* src = edge.scratchChannels[ch];
                for (std::size_t i = 0; i < numSamples; ++i)
                {
                    dst[i] += src[i];
                }
            }
        }
        else
        {
            for (std::uint16_t ch = 0; ch < m_channelCount; ++ch)
            {
                auto* dst = node.bufferChannels[ch];
                const auto* src = upstream.bufferChannels[ch];
                for (std::size_t i = 0; i < numSamples; ++i)
                {
                    dst[i] += src[i];
                }
            }
        }
    }

    AudioBufferView sidechainView {};
    if (node.sidechainEdge.has_value())
    {
        auto& scEdge = *node.sidechainEdge;
        const auto& upstream = m_nodes[scEdge.fromNode];
        if (scEdge.compensationSamples > 0)
        {
            for (std::uint16_t ch = 0; ch < m_channelCount; ++ch)
            {
                std::copy_n(upstream.bufferChannels[ch], numSamples, scEdge.scratchChannels[ch]);
            }

            ProcessContext scContext {
                .audio = AudioBufferView {scEdge.scratchChannels.data(), m_channelCount, numSamples},
                .midi = {},
                .sampleRate = m_sampleRate,
                .sidechain = {}
            };
            scEdge.compensator.process(scContext);
            sidechainView = AudioBufferView {scEdge.scratchChannels.data(), m_channelCount, numSamples};
        }
        else
        {
            sidechainView = AudioBufferView {
                const_cast<float**>(upstream.bufferChannels.data()),
                m_channelCount,
                numSamples
            };
        }
    }

    ProcessContext nodeContext {
        .audio = AudioBufferView {node.bufferChannels.data(), m_channelCount, numSamples},
        .midi = {},
        .sampleRate = m_sampleRate,
        .sidechain = sidechainView
    };
    node.processor->process(nodeContext);
}

void DspGraph::process(
    AudioBufferView input,
    AudioBufferView output,
    WorkStealingScheduler* scheduler) noexcept
{
    if (output.channels != nullptr && output.frameCount > 0)
    {
        for (std::size_t ch = 0; ch < output.channelCount; ++ch)
        {
            if (output.channels[ch] != nullptr)
            {
                std::fill_n(output.channels[ch], output.frameCount, 0.0f);
            }
        }
    }

    if (! m_compiled || output.channels == nullptr || output.frameCount == 0)
    {
        return;
    }

    const auto numSamples = std::min<std::size_t>(output.frameCount, m_maxBlockSize);

    struct WavefrontTaskContext
    {
        DspGraph* graph {nullptr};
        const std::vector<NodeId>* wavefront {nullptr};
        AudioBufferView input {};
        std::size_t numSamples {0};
    };

    for (const auto& wavefront : m_wavefronts)
    {
        if (scheduler != nullptr && wavefront.size() > 1)
        {
            WavefrontTaskContext taskCtx {
                .graph = this,
                .wavefront = &wavefront,
                .input = input,
                .numSamples = numSamples
            };
            scheduler->executeWavefront(
                wavefront.size(),
                [](void* rawCtx, std::size_t taskIndex) noexcept {
                    auto* ctx = static_cast<WavefrontTaskContext*>(rawCtx);
                    const auto nodeId = (*ctx->wavefront)[taskIndex];
                    ctx->graph->processNode(nodeId, ctx->input, ctx->numSamples);
                },
                &taskCtx);
        }
        else
        {
            for (const auto nodeId : wavefront)
            {
                processNode(nodeId, input, numSamples);
            }
        }
    }

    const auto outChannels = std::min<std::size_t>(output.channelCount, m_channelCount);
    for (const auto& node : m_nodes)
    {
        if (! node.isMasterOutput)
        {
            continue;
        }

        for (std::size_t ch = 0; ch < outChannels; ++ch)
        {
            auto* dst = output.channels[ch];
            if (dst == nullptr)
            {
                continue;
            }
            const auto* src = node.bufferChannels[ch];
            for (std::size_t i = 0; i < numSamples; ++i)
            {
                dst[i] += src[i];
            }
        }
    }
}
} // namespace dawg::engine
