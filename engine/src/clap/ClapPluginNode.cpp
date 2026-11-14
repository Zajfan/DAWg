#include <dawg/engine/clap/ClapPluginNode.h>

#include <algorithm>
#include <cstring>

namespace dawg::engine {

ClapPluginNode::ClapPluginNode(const clap_plugin* plugin, const bool sandboxed) noexcept
    : m_plugin(plugin)
    , m_sandboxed(sandboxed)
{
    m_inEvents.ctx = this;
    m_inEvents.size = [](const clap_input_events* list) noexcept -> std::uint32_t {
        if (list == nullptr || list->ctx == nullptr)
            return 0;
        const auto* self = static_cast<const ClapPluginNode*>(list->ctx);
        return static_cast<std::uint32_t>(self->m_clapEventCount);
    };
    m_inEvents.get = [](const clap_input_events* list, const std::uint32_t index) noexcept -> const clap_event_header* {
        if (list == nullptr || list->ctx == nullptr)
            return nullptr;
        const auto* self = static_cast<const ClapPluginNode*>(list->ctx);
        if (index >= self->m_clapEventCount)
            return nullptr;
        return &self->m_clapMidiEvents[index].header;
    };
}

ClapPluginNode::~ClapPluginNode()
{
    if (m_plugin != nullptr)
    {
        if (m_plugin->deactivate != nullptr)
            m_plugin->deactivate(m_plugin);
        if (m_plugin->destroy != nullptr)
            m_plugin->destroy(m_plugin);
        m_plugin = nullptr;
    }
}

bool ClapPluginNode::prepare(const double sampleRate, const std::size_t maxBlockSize,
                            const std::uint16_t channelCount)
{
    if (sampleRate <= 0.0 || maxBlockSize == 0 || channelCount == 0)
        return false;

    m_sampleRate = sampleRate;
    m_maxBlockSize = maxBlockSize;
    m_channelCount = channelCount;
    m_crashed.store(false, std::memory_order_relaxed);

    if (m_plugin == nullptr)
        return true;

    if (m_plugin->init != nullptr && ! m_plugin->init(m_plugin))
        return false;

    if (m_plugin->activate != nullptr)
    {
        if (! m_plugin->activate(m_plugin, sampleRate, 1, static_cast<std::uint32_t>(maxBlockSize)))
            return false;
    }

    if (m_plugin->get_extension != nullptr)
    {
        const auto* latencyExt = static_cast<const clap_plugin_latency*>(
            m_plugin->get_extension(m_plugin, CLAP_EXT_LATENCY));
        if (latencyExt != nullptr && latencyExt->get != nullptr)
            m_latency = latencyExt->get(m_plugin);
        else
            m_latency = 0;
    }

    return true;
}

void ClapPluginNode::reset() noexcept
{
    if (m_plugin != nullptr && m_plugin->reset != nullptr && ! m_crashed.load(std::memory_order_relaxed))
        m_plugin->reset(m_plugin);
}

std::uint32_t ClapPluginNode::latencySamples() const noexcept
{
    return m_latency;
}

void ClapPluginNode::setupClapEvents(const MidiBufferView& midi) noexcept
{
    m_clapEventCount = 0;
    if (midi.events == nullptr || midi.eventCount == 0)
        return;

    const auto count = std::min(midi.eventCount, maxEvents);
    for (std::size_t i = 0; i < count; ++i)
    {
        const auto& ev = midi.events[i];
        auto& cev = m_clapMidiEvents[m_clapEventCount];

        cev.header.size = sizeof(clap_event_midi);
        cev.header.time = static_cast<std::uint32_t>(ev.frameOffset);
        cev.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        cev.header.type = CLAP_EVENT_MIDI;
        cev.header.flags = 0;
        cev.port_index = 0;

        for (std::size_t b = 0; b < 3; ++b)
            cev.data[b] = (b < ev.size) ? static_cast<std::uint8_t>(ev.data[b]) : 0;

        ++m_clapEventCount;
    }
}

void ClapPluginNode::process(ProcessContext& context) noexcept
{
    if (context.audio.channels == nullptr || context.audio.frameCount == 0 || context.audio.channelCount == 0)
        return;

    if (m_crashed.load(std::memory_order_relaxed) || m_plugin == nullptr || m_plugin->process == nullptr)
        return;

    setupClapEvents(context.midi);

    // Audio setup
    m_inChannelPtrs[0] = (context.audio.channelCount >= 1) ? context.audio.channels[0] : nullptr;
    m_inChannelPtrs[1] = (context.audio.channelCount >= 2) ? context.audio.channels[1] : nullptr;
    m_outChannelPtrs[0] = m_inChannelPtrs[0];
    m_outChannelPtrs[1] = m_inChannelPtrs[1];

    m_audioIn.data32 = m_inChannelPtrs.data();
    m_audioIn.data64 = nullptr;
    m_audioIn.channel_count = std::min<std::uint32_t>(2, context.audio.channelCount);
    m_audioIn.latency = 0;
    m_audioIn.constant_mask = 0;

    m_audioOut.data32 = m_outChannelPtrs.data();
    m_audioOut.data64 = nullptr;
    m_audioOut.channel_count = std::min<std::uint32_t>(2, context.audio.channelCount);
    m_audioOut.latency = 0;
    m_audioOut.constant_mask = 0;

    clap_process proc {
        .steady_time = -1,
        .frames_count = static_cast<std::uint32_t>(context.audio.frameCount),
        .transport = nullptr,
        .audio_inputs = &m_audioIn,
        .audio_outputs = &m_audioOut,
        .audio_inputs_count = 1,
        .audio_outputs_count = 1,
        .in_events = &m_inEvents,
        .out_events = nullptr
    };

    const auto status = m_plugin->process(m_plugin, &proc);
    if (status == CLAP_PROCESS_ERROR)
    {
        m_crashed.store(true, std::memory_order_relaxed);
    }
}

} // namespace dawg::engine
