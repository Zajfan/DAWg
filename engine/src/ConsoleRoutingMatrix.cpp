#include <dawg/engine/ConsoleRoutingMatrix.h>

#include <cmath>

namespace dawg::engine {

namespace {

[[nodiscard]] inline float dbToLinear(float db) noexcept
{
    if (db <= -99.0f)
        return 0.0f;
    return std::pow(10.0f, db / 20.0f);
}

} // namespace

ConsoleRoutingMatrix::ConsoleRoutingMatrix() noexcept
{
    reset();
}

void ConsoleRoutingMatrix::reset() noexcept
{
    for (auto& ch : m_channels)
    {
        ch = ChannelRoutingConfig {};
    }
    for (auto& vca : m_vcas)
    {
        vca = VcaGroup {};
    }
    m_subBusGainDb.fill(0.0f);
    m_subBusPan.fill(0.0f);
    m_subBusMute.fill(false);
    m_auxReturnGainDb.fill(0.0f);
    m_auxReturnMute.fill(false);
    m_sidechainSources.fill(-1);
    m_sidechainListen.fill(false);
    m_sidechainHpf.fill(20.0f);
    m_sidechainLpf.fill(20000.0f);
}

void ConsoleRoutingMatrix::setChannelConfig(std::size_t channelIndex, const ChannelRoutingConfig& config) noexcept
{
    if (channelIndex < maxChannels)
    {
        m_channels[channelIndex] = config;
    }
}

const ChannelRoutingConfig& ConsoleRoutingMatrix::getChannelConfig(std::size_t channelIndex) const noexcept
{
    if (channelIndex < maxChannels)
        return m_channels[channelIndex];
    static const ChannelRoutingConfig s_defaultConfig {};
    return s_defaultConfig;
}

ChannelRoutingConfig& ConsoleRoutingMatrix::getChannelConfig(std::size_t channelIndex) noexcept
{
    if (channelIndex < maxChannels)
        return m_channels[channelIndex];
    static ChannelRoutingConfig s_dummyConfig {};
    return s_dummyConfig;
}

void ConsoleRoutingMatrix::setVcaFader(std::size_t vcaIndex1Based, float gainDb) noexcept
{
    if (vcaIndex1Based >= 1 && vcaIndex1Based <= maxVcaGroups)
    {
        m_vcas[vcaIndex1Based - 1].faderGainDb = std::clamp(gainDb, -100.0f, 12.0f);
    }
}

float ConsoleRoutingMatrix::getVcaFader(std::size_t vcaIndex1Based) const noexcept
{
    if (vcaIndex1Based >= 1 && vcaIndex1Based <= maxVcaGroups)
        return m_vcas[vcaIndex1Based - 1].faderGainDb;
    return 0.0f;
}

void ConsoleRoutingMatrix::setVcaMute(std::size_t vcaIndex1Based, bool mute) noexcept
{
    if (vcaIndex1Based >= 1 && vcaIndex1Based <= maxVcaGroups)
    {
        m_vcas[vcaIndex1Based - 1].muted = mute;
    }
}

bool ConsoleRoutingMatrix::getVcaMute(std::size_t vcaIndex1Based) const noexcept
{
    if (vcaIndex1Based >= 1 && vcaIndex1Based <= maxVcaGroups)
        return m_vcas[vcaIndex1Based - 1].muted;
    return false;
}

void ConsoleRoutingMatrix::setSubBusGain(std::size_t subBusIndex1Based, float gainDb) noexcept
{
    if (subBusIndex1Based >= 1 && subBusIndex1Based <= maxSubBuses)
    {
        m_subBusGainDb[subBusIndex1Based - 1] = std::clamp(gainDb, -100.0f, 12.0f);
    }
}

float ConsoleRoutingMatrix::getSubBusGain(std::size_t subBusIndex1Based) const noexcept
{
    if (subBusIndex1Based >= 1 && subBusIndex1Based <= maxSubBuses)
        return m_subBusGainDb[subBusIndex1Based - 1];
    return 0.0f;
}

void ConsoleRoutingMatrix::setSubBusPan(std::size_t subBusIndex1Based, float pan) noexcept
{
    if (subBusIndex1Based >= 1 && subBusIndex1Based <= maxSubBuses)
    {
        m_subBusPan[subBusIndex1Based - 1] = std::clamp(pan, -1.0f, 1.0f);
    }
}

float ConsoleRoutingMatrix::getSubBusPan(std::size_t subBusIndex1Based) const noexcept
{
    if (subBusIndex1Based >= 1 && subBusIndex1Based <= maxSubBuses)
        return m_subBusPan[subBusIndex1Based - 1];
    return 0.0f;
}

void ConsoleRoutingMatrix::setSubBusMute(std::size_t subBusIndex1Based, bool mute) noexcept
{
    if (subBusIndex1Based >= 1 && subBusIndex1Based <= maxSubBuses)
    {
        m_subBusMute[subBusIndex1Based - 1] = mute;
    }
}

bool ConsoleRoutingMatrix::getSubBusMute(std::size_t subBusIndex1Based) const noexcept
{
    if (subBusIndex1Based >= 1 && subBusIndex1Based <= maxSubBuses)
        return m_subBusMute[subBusIndex1Based - 1];
    return false;
}

void ConsoleRoutingMatrix::setAuxReturnGain(std::size_t auxIndex0Based, float gainDb) noexcept
{
    if (auxIndex0Based < maxAuxBuses)
    {
        m_auxReturnGainDb[auxIndex0Based] = std::clamp(gainDb, -100.0f, 12.0f);
    }
}

float ConsoleRoutingMatrix::getAuxReturnGain(std::size_t auxIndex0Based) const noexcept
{
    if (auxIndex0Based < maxAuxBuses)
        return m_auxReturnGainDb[auxIndex0Based];
    return 0.0f;
}

void ConsoleRoutingMatrix::setAuxReturnMute(std::size_t auxIndex0Based, bool mute) noexcept
{
    if (auxIndex0Based < maxAuxBuses)
    {
        m_auxReturnMute[auxIndex0Based] = mute;
    }
}

bool ConsoleRoutingMatrix::getAuxReturnMute(std::size_t auxIndex0Based) const noexcept
{
    if (auxIndex0Based < maxAuxBuses)
        return m_auxReturnMute[auxIndex0Based];
    return false;
}

void ConsoleRoutingMatrix::setSidechainSource(std::size_t destChannel, std::size_t srcChannel) noexcept
{
    if (destChannel < maxChannels)
    {
        m_sidechainSources[destChannel] = static_cast<int>(srcChannel);
    }
}

int ConsoleRoutingMatrix::getSidechainSource(std::size_t destChannel) const noexcept
{
    if (destChannel < maxChannels)
        return m_sidechainSources[destChannel];
    return -1;
}

void ConsoleRoutingMatrix::setSidechainListen(std::size_t destChannel, bool listen) noexcept
{
    if (destChannel < maxChannels)
    {
        m_sidechainListen[destChannel] = listen;
    }
}

bool ConsoleRoutingMatrix::getSidechainListen(std::size_t destChannel) const noexcept
{
    if (destChannel < maxChannels)
        return m_sidechainListen[destChannel];
    return false;
}

void ConsoleRoutingMatrix::setSidechainFilter(std::size_t destChannel, float hpfHz, float lpfHz) noexcept
{
    if (destChannel < maxChannels)
    {
        m_sidechainHpf[destChannel] = std::clamp(hpfHz, 20.0f, 2000.0f);
        m_sidechainLpf[destChannel] = std::clamp(lpfHz, 500.0f, 20000.0f);
    }
}

float ConsoleRoutingMatrix::getSidechainHpf(std::size_t destChannel) const noexcept
{
    if (destChannel < maxChannels)
        return m_sidechainHpf[destChannel];
    return 20.0f;
}

float ConsoleRoutingMatrix::getSidechainLpf(std::size_t destChannel) const noexcept
{
    if (destChannel < maxChannels)
        return m_sidechainLpf[destChannel];
    return 20000.0f;
}

float ConsoleRoutingMatrix::getEffectiveChannelGainLinear(std::size_t channelIndex) const noexcept
{
    if (channelIndex >= maxChannels)
        return 0.0f;

    const auto& ch = m_channels[channelIndex];
    float totalDb = ch.faderGainDb;

    if (ch.vcaGroup >= 1 && ch.vcaGroup <= maxVcaGroups)
    {
        totalDb += m_vcas[ch.vcaGroup - 1].faderGainDb;
    }

    return dbToLinear(totalDb);
}

bool ConsoleRoutingMatrix::isChannelAudible(std::size_t channelIndex) const noexcept
{
    if (channelIndex >= maxChannels)
        return false;

    const auto& ch = m_channels[channelIndex];
    if (ch.muted)
        return false;

    if (ch.vcaGroup >= 1 && ch.vcaGroup <= maxVcaGroups)
    {
        if (m_vcas[ch.vcaGroup - 1].muted)
            return false;
    }

    return true;
}

void ConsoleRoutingMatrix::processRouting(
    AudioBufferView* channelBuffers,
    std::size_t activeChannelCount,
    AudioBufferView* subBusBuffers,
    AudioBufferView* auxBuffers,
    AudioBufferView& masterOut,
    std::size_t frameCount) noexcept
{
    if (frameCount == 0)
        return;

    // Clear output and bus buffers
    if (masterOut.channelCount > 0 && masterOut.channels != nullptr)
    {
        for (std::size_t c = 0; c < masterOut.channelCount; ++c)
        {
            if (masterOut.channels[c] != nullptr)
                std::fill_n(masterOut.channels[c], frameCount, 0.0f);
        }
    }

    if (subBusBuffers != nullptr)
    {
        for (std::size_t b = 0; b < maxSubBuses; ++b)
        {
            auto& sbuf = subBusBuffers[b];
            if (sbuf.channelCount > 0 && sbuf.channels != nullptr)
            {
                for (std::size_t c = 0; c < sbuf.channelCount; ++c)
                {
                    if (sbuf.channels[c] != nullptr)
                        std::fill_n(sbuf.channels[c], frameCount, 0.0f);
                }
            }
        }
    }

    if (auxBuffers != nullptr)
    {
        for (std::size_t a = 0; a < maxAuxBuses; ++a)
        {
            auto& abuf = auxBuffers[a];
            if (abuf.channelCount > 0 && abuf.channels != nullptr)
            {
                for (std::size_t c = 0; c < abuf.channelCount; ++c)
                {
                    if (abuf.channels[c] != nullptr)
                        std::fill_n(abuf.channels[c], frameCount, 0.0f);
                }
            }
        }
    }

    const std::size_t numChannels = std::min(activeChannelCount, maxChannels);

    // Determine if any channel has solo active
    bool anySolo = false;
    for (std::size_t i = 0; i < numChannels; ++i)
    {
        if (m_channels[i].solo)
        {
            anySolo = true;
            break;
        }
    }

    // Process each channel
    for (std::size_t i = 0; i < numChannels; ++i)
    {
        const auto& ch = m_channels[i];
        auto& inBuf = channelBuffers[i];
        if (inBuf.channelCount == 0 || inBuf.channels == nullptr || inBuf.channels[0] == nullptr)
            continue;

        const float* inL = inBuf.channels[0];
        const float* inR = (inBuf.channelCount > 1 && inBuf.channels[1] != nullptr) ? inBuf.channels[1] : inL;

        const bool audible = isChannelAudible(i) && (!anySolo || ch.solo);
        const float effLinear = getEffectiveChannelGainLinear(i);
        const auto panGains = dsp::calculatePanLawGains(ch.pan, ch.panLaw);
        const float chGainL = effLinear * panGains.gainL * (ch.phaseInvertL ? -1.0f : 1.0f);
        const float chGainR = effLinear * panGains.gainR * (ch.phaseInvertR ? -1.0f : 1.0f);

        // Process Aux Sends
        if (auxBuffers != nullptr)
        {
            for (const auto& send : ch.sends)
            {
                if (!send.enabled || send.targetAuxIndex >= maxAuxBuses)
                    continue;

                // Pre-mute sends always fire; pre-fader and post-fader obey mute/audibility
                if (send.mode != AuxSendMode::preMute && !audible)
                    continue;

                const float sendLin = dbToLinear(send.sendGainDb);
                const auto sendPan = dsp::calculatePanLawGains(send.sendPan, dsp::PanLaw::equalPower3dB);

                float finalSendL = 0.0f;
                float finalSendR = 0.0f;

                if (send.mode == AuxSendMode::postFader)
                {
                    finalSendL = chGainL * sendLin * sendPan.gainL;
                    finalSendR = chGainR * sendLin * sendPan.gainR;
                }
                else
                {
                    // Pre-fader or Pre-mute: use dry track signal with send pan & gain
                    finalSendL = sendLin * sendPan.gainL;
                    finalSendR = sendLin * sendPan.gainR;
                }

                auto& auxBuf = auxBuffers[send.targetAuxIndex];
                if (auxBuf.channelCount > 0 && auxBuf.channels != nullptr && auxBuf.channels[0] != nullptr)
                {
                    float* auxL = auxBuf.channels[0];
                    float* auxR = (auxBuf.channelCount > 1 && auxBuf.channels[1] != nullptr) ? auxBuf.channels[1] : auxL;

                    for (std::size_t f = 0; f < frameCount; ++f)
                    {
                        auxL[f] += inL[f] * finalSendL;
                        if (auxBuf.channelCount > 1 && auxR != auxL)
                        {
                            auxR[f] += inR[f] * finalSendR;
                        }
                    }
                }
            }
        }

        // Route to Master or Sub-bus if audible
        if (audible)
        {
            if (ch.targetSubBus == 0)
            {
                // Direct to Master
                if (masterOut.channelCount > 0 && masterOut.channels != nullptr && masterOut.channels[0] != nullptr)
                {
                    float* mOutL = masterOut.channels[0];
                    float* mOutR = (masterOut.channelCount > 1 && masterOut.channels[1] != nullptr) ? masterOut.channels[1] : mOutL;

                    for (std::size_t f = 0; f < frameCount; ++f)
                    {
                        mOutL[f] += inL[f] * chGainL;
                        if (masterOut.channelCount > 1 && mOutR != mOutL)
                        {
                            mOutR[f] += inR[f] * chGainR;
                        }
                    }
                }
            }
            else if (subBusBuffers != nullptr && ch.targetSubBus <= maxSubBuses)
            {
                // Route to designated Sub-Bus
                auto& sbuf = subBusBuffers[ch.targetSubBus - 1];
                if (sbuf.channelCount > 0 && sbuf.channels != nullptr && sbuf.channels[0] != nullptr)
                {
                    float* sbL = sbuf.channels[0];
                    float* sbR = (sbuf.channelCount > 1 && sbuf.channels[1] != nullptr) ? sbuf.channels[1] : sbL;

                    for (std::size_t f = 0; f < frameCount; ++f)
                    {
                        sbL[f] += inL[f] * chGainL;
                        if (sbuf.channelCount > 1 && sbR != sbL)
                        {
                            sbR[f] += inR[f] * chGainR;
                        }
                    }
                }
            }
        }
    }

    // Sum Sub-Mix Buses into Master
    if (subBusBuffers != nullptr && masterOut.channelCount > 0 && masterOut.channels != nullptr && masterOut.channels[0] != nullptr)
    {
        float* mOutL = masterOut.channels[0];
        float* mOutR = (masterOut.channelCount > 1 && masterOut.channels[1] != nullptr) ? masterOut.channels[1] : mOutL;

        for (std::size_t b = 0; b < maxSubBuses; ++b)
        {
            if (m_subBusMute[b])
                continue;

            const float busGain = dbToLinear(m_subBusGainDb[b]);
            const auto busPan = dsp::calculatePanLawGains(m_subBusPan[b], dsp::PanLaw::equalPower3dB);
            const float gL = busGain * busPan.gainL;
            const float gR = busGain * busPan.gainR;

            auto& sbuf = subBusBuffers[b];
            if (sbuf.channelCount > 0 && sbuf.channels != nullptr && sbuf.channels[0] != nullptr)
            {
                const float* sbL = sbuf.channels[0];
                const float* sbR = (sbuf.channelCount > 1 && sbuf.channels[1] != nullptr) ? sbuf.channels[1] : sbL;

                for (std::size_t f = 0; f < frameCount; ++f)
                {
                    mOutL[f] += sbL[f] * gL;
                    if (masterOut.channelCount > 1 && mOutR != mOutL)
                    {
                        mOutR[f] += sbR[f] * gR;
                    }
                }
            }
        }
    }

    // Sum Aux Returns into Master
    if (auxBuffers != nullptr && masterOut.channelCount > 0 && masterOut.channels != nullptr && masterOut.channels[0] != nullptr)
    {
        float* mOutL = masterOut.channels[0];
        float* mOutR = (masterOut.channelCount > 1 && masterOut.channels[1] != nullptr) ? masterOut.channels[1] : mOutL;

        for (std::size_t a = 0; a < maxAuxBuses; ++a)
        {
            if (m_auxReturnMute[a])
                continue;

            const float retGain = dbToLinear(m_auxReturnGainDb[a]);
            auto& abuf = auxBuffers[a];
            if (abuf.channelCount > 0 && abuf.channels != nullptr && abuf.channels[0] != nullptr)
            {
                const float* aL = abuf.channels[0];
                const float* aR = (abuf.channelCount > 1 && abuf.channels[1] != nullptr) ? abuf.channels[1] : aL;

                for (std::size_t f = 0; f < frameCount; ++f)
                {
                    mOutL[f] += aL[f] * retGain;
                    if (masterOut.channelCount > 1 && mOutR != mOutL)
                    {
                        mOutR[f] += aR[f] * retGain;
                    }
                }
            }
        }
    }
}

} // namespace dawg::engine
