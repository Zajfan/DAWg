#pragma once

#include <dawg/engine/AudioBufferView.h>
#include <dawg/engine/dsp/SslChannelStripNode.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace dawg::engine {

enum class AuxSendMode : std::uint8_t
{
    postFader,
    preFader,
    preMute
};

struct AuxSendConfig
{
    bool enabled {false};
    std::uint8_t targetAuxIndex {0}; // 0 - 7
    AuxSendMode mode {AuxSendMode::postFader};
    float sendGainDb {0.0f}; // -inf to +6 dB
    float sendPan {0.0f};    // -1 (L) to +1 (R)
};

struct VcaGroup
{
    float faderGainDb {0.0f}; // -100 to +12 dB
    bool muted {false};
    bool solo {false};
};

struct ChannelRoutingConfig
{
    std::uint8_t vcaGroup {0}; // 0 = none, 1-8 = VCA Group 1-8
    std::uint8_t targetSubBus {0}; // 0 = Master direct, 1-8 = Sub-bus 1-8
    float faderGainDb {0.0f};
    float pan {0.0f};
    dsp::PanLaw panLaw {dsp::PanLaw::equalPower3dB};
    bool muted {false};
    bool solo {false};
    bool phaseInvertL {false};
    bool phaseInvertR {false};

    static constexpr std::size_t maxSendsPerChannel = 8;
    std::array<AuxSendConfig, maxSendsPerChannel> sends {};
};

// Professional Studio Mixing Console Routing Matrix
// Supports 64 channel strips, 8 Sub-Mix Buses, 8 Aux Sends/Returns,
// 8 VCA Groups, sidechain key cross-routing, and master console summing.
class ConsoleRoutingMatrix final
{
public:
    static constexpr std::size_t maxChannels = 64;
    static constexpr std::size_t maxSubBuses = 8;
    static constexpr std::size_t maxAuxBuses = 8;
    static constexpr std::size_t maxVcaGroups = 8;

    ConsoleRoutingMatrix() noexcept;
    ~ConsoleRoutingMatrix() = default;

    void reset() noexcept;

    // Channel Routing Configuration
    void setChannelConfig(std::size_t channelIndex, const ChannelRoutingConfig& config) noexcept;
    [[nodiscard]] const ChannelRoutingConfig& getChannelConfig(std::size_t channelIndex) const noexcept;
    [[nodiscard]] ChannelRoutingConfig& getChannelConfig(std::size_t channelIndex) noexcept;

    // VCA Groups (1 - 8)
    void setVcaFader(std::size_t vcaIndex1Based, float gainDb) noexcept;
    [[nodiscard]] float getVcaFader(std::size_t vcaIndex1Based) const noexcept;
    void setVcaMute(std::size_t vcaIndex1Based, bool mute) noexcept;
    [[nodiscard]] bool getVcaMute(std::size_t vcaIndex1Based) const noexcept;

    // Sub-Mix Buses (1 - 8)
    void setSubBusGain(std::size_t subBusIndex1Based, float gainDb) noexcept;
    [[nodiscard]] float getSubBusGain(std::size_t subBusIndex1Based) const noexcept;
    void setSubBusPan(std::size_t subBusIndex1Based, float pan) noexcept;
    [[nodiscard]] float getSubBusPan(std::size_t subBusIndex1Based) const noexcept;
    void setSubBusMute(std::size_t subBusIndex1Based, bool mute) noexcept;
    [[nodiscard]] bool getSubBusMute(std::size_t subBusIndex1Based) const noexcept;

    // Aux Return Buses (1 - 8)
    void setAuxReturnGain(std::size_t auxIndex0Based, float gainDb) noexcept;
    [[nodiscard]] float getAuxReturnGain(std::size_t auxIndex0Based) const noexcept;
    void setAuxReturnMute(std::size_t auxIndex0Based, bool mute) noexcept;
    [[nodiscard]] bool getAuxReturnMute(std::size_t auxIndex0Based) const noexcept;

    // Sidechain Key Routing & Listen Mode
    void setSidechainSource(std::size_t destChannel, std::size_t srcChannel) noexcept;
    [[nodiscard]] int getSidechainSource(std::size_t destChannel) const noexcept; // -1 if disabled
    void setSidechainListen(std::size_t destChannel, bool listen) noexcept;
    [[nodiscard]] bool getSidechainListen(std::size_t destChannel) const noexcept;
    void setSidechainFilter(std::size_t destChannel, float hpfHz, float lpfHz) noexcept;
    [[nodiscard]] float getSidechainHpf(std::size_t destChannel) const noexcept;
    [[nodiscard]] float getSidechainLpf(std::size_t destChannel) const noexcept;

    // Compute effective channel gain including VCA modulation
    [[nodiscard]] float getEffectiveChannelGainLinear(std::size_t channelIndex) const noexcept;
    [[nodiscard]] bool isChannelAudible(std::size_t channelIndex) const noexcept;

    // Master Console Summing Execution
    // channelBuffers: array of stereo channel buffers (post-insert/EQ)
    // subBusBuffers: array of 8 stereo buffers for sub-mix buses
    // auxBuffers: array of 8 stereo buffers for aux sends
    // masterOut: stereo output buffer receiving master sum
    void processRouting(
        AudioBufferView* channelBuffers,
        std::size_t activeChannelCount,
        AudioBufferView* subBusBuffers,
        AudioBufferView* auxBuffers,
        AudioBufferView& masterOut,
        std::size_t frameCount) noexcept;

private:
    std::array<ChannelRoutingConfig, maxChannels> m_channels {};
    std::array<VcaGroup, maxVcaGroups> m_vcas {};
    std::array<float, maxSubBuses> m_subBusGainDb {};
    std::array<float, maxSubBuses> m_subBusPan {};
    std::array<bool, maxSubBuses> m_subBusMute {};
    std::array<float, maxAuxBuses> m_auxReturnGainDb {};
    std::array<bool, maxAuxBuses> m_auxReturnMute {};
    std::array<int, maxChannels> m_sidechainSources {};
    std::array<bool, maxChannels> m_sidechainListen {};
    std::array<float, maxChannels> m_sidechainHpf {};
    std::array<float, maxChannels> m_sidechainLpf {};
};

} // namespace dawg::engine
