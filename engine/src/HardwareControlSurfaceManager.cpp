#include <dawg/engine/HardwareControlSurfaceManager.h>

#include <algorithm>
#include <cctype>

namespace dawg::engine {

namespace {
[[nodiscard]] std::string toLowerString(std::string_view s)
{
    std::string res;
    res.reserve(s.size());
    for (char c : s)
    {
        res.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return res;
}
} // namespace

HardwareControlSurfaceManager::HardwareControlSurfaceManager() noexcept
{
    m_lastSentFaderValues.fill(-1.0f);
    m_lastSentPanValues.fill(-99.0f);
}

HardwareControlSurfaceManager::~HardwareControlSurfaceManager()
{
    stopSyncLoop();
}

ControllerProfile HardwareControlSurfaceManager::autoDetectProfile(std::string_view deviceName) noexcept
{
    const std::string lower = toLowerString(deviceName);

    if (lower.find("mackie") != std::string::npos ||
        lower.find("mcu") != std::string::npos ||
        lower.find("x-touch") != std::string::npos ||
        lower.find("icon") != std::string::npos ||
        lower.find("qcon") != std::string::npos ||
        lower.find("platform m") != std::string::npos ||
        lower.find("faderport") != std::string::npos)
    {
        return ControllerProfile::mackieControlUniversal;
    }

    if (lower.find("hui") != std::string::npos ||
        lower.find("baby hui") != std::string::npos)
    {
        return ControllerProfile::hui;
    }

    if (lower.find("launchpad") != std::string::npos)
    {
        return ControllerProfile::launchpadPro;
    }

    if (lower.find("push") != std::string::npos)
    {
        return ControllerProfile::push2;
    }

    if (lower.find("apc") != std::string::npos)
    {
        return ControllerProfile::apc40Mk2;
    }

    return ControllerProfile::genericRgbGrid;
}

void HardwareControlSurfaceManager::setConfig(const SurfaceConfig& config) noexcept
{
    std::lock_guard<std::mutex> lock(m_stateMutex);
    m_config = config;
    m_engine.setProfile(config.profile);
    m_engine.setPickupMode(config.pickupMode);
}

SurfaceConfig HardwareControlSurfaceManager::config() const noexcept
{
    std::lock_guard<std::mutex> lock(m_stateMutex);
    return m_config;
}

void HardwareControlSurfaceManager::updateChannelStrip(
    std::size_t channelIdx,
    const ChannelStripFeedback& state) noexcept
{
    if (channelIdx < m_channels.size())
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        m_channels[channelIdx] = state;
    }
}

void HardwareControlSurfaceManager::updatePadMatrix(
    std::size_t row,
    std::size_t col,
    GridPadState state) noexcept
{
    m_engine.setClipState(row, col, state);
}

void HardwareControlSurfaceManager::startSyncLoop(std::chrono::milliseconds interval)
{
    if (m_syncRunning.exchange(true))
        return;

    m_syncThread = std::thread(&HardwareControlSurfaceManager::syncWorkerLoop, this, interval);
}

void HardwareControlSurfaceManager::stopSyncLoop() noexcept
{
    if (!m_syncRunning.exchange(false))
        return;

    if (m_syncThread.joinable())
    {
        m_syncThread.join();
    }
}

void HardwareControlSurfaceManager::syncWorkerLoop(std::chrono::milliseconds interval) noexcept
{
    while (m_syncRunning.load(std::memory_order_relaxed))
    {
        syncCycle();
        std::this_thread::sleep_for(interval);
    }
}

void HardwareControlSurfaceManager::syncCycle() noexcept
{
    if (!m_midiSendCb)
        return;

    std::array<ChannelStripFeedback, 9> snapshotChannels;
    ControllerProfile activeProfile;
    bool enabled = false;

    {
        std::lock_guard<std::mutex> lock(m_stateMutex);
        snapshotChannels = m_channels;
        activeProfile = m_config.profile;
        enabled = m_config.enabled;
    }

    if (!enabled)
        return;

    // Mackie Control Universal Feedback Synchronization
    if (activeProfile == ControllerProfile::mackieControlUniversal)
    {
        // 1. Motorized Fader Sync (Channels 0..7 and Master 8)
        for (std::size_t ch = 0; ch < 9; ++ch)
        {
            const float targetVol = snapshotChannels[ch].volume;
            if (std::abs(targetVol - m_lastSentFaderValues[ch]) > 0.005f)
            {
                const auto msg = HardwareControlSurfaceEngine::generateMcuFaderMessage(ch, targetVol);
                m_midiSendCb(msg.data(), msg.size());
                m_lastSentFaderValues[ch] = targetVol;
            }
        }

        // 2. Rotary V-Pot LED Rings (Channels 0..7)
        for (std::size_t ch = 0; ch < 8; ++ch)
        {
            const float targetPan = snapshotChannels[ch].pan;
            if (std::abs(targetPan - m_lastSentPanValues[ch]) > 0.02f)
            {
                const float normPan = std::clamp((targetPan + 1.0f) * 0.5f, 0.0f, 1.0f);
                const auto msg = HardwareControlSurfaceEngine::generateMcuVpotLedMessage(
                    ch,
                    McuVpotLedMode::boostCut,
                    normPan);
                m_midiSendCb(msg.data(), msg.size());
                m_lastSentPanValues[ch] = targetPan;
            }
        }

        // 3. Scribble Strip LCD updates
        bool namesChanged = false;
        for (std::size_t ch = 0; ch < 8; ++ch)
        {
            if (snapshotChannels[ch].trackName != m_lastSentTrackNames[ch])
            {
                namesChanged = true;
                m_lastSentTrackNames[ch] = snapshotChannels[ch].trackName;
            }
        }

        if (namesChanged)
        {
            // Build 56-char line 0 across 8 channel strips (7 chars each)
            std::string line0;
            line0.reserve(56);
            for (std::size_t ch = 0; ch < 8; ++ch)
            {
                std::string seg = snapshotChannels[ch].trackName;
                if (seg.size() > 6) seg.resize(6);
                while (seg.size() < 7) seg.push_back(' ');
                line0 += seg;
            }

            const auto sysex = HardwareControlSurfaceEngine::generateMcuScribbleSysEx(0, line0);
            m_midiSendCb(sysex.data(), sysex.size());
        }
    }
    else if (activeProfile == ControllerProfile::launchpadPro)
    {
        // RGB Pad matrix LED updates
        for (std::size_t r = 0; r < HardwareControlSurfaceEngine::GridRows; ++r)
        {
            for (std::size_t c = 0; c < HardwareControlSurfaceEngine::GridCols; ++c)
            {
                const auto sysex = m_engine.generatePadSysEx(r, c);
                m_midiSendCb(sysex.data(), sysex.size());
            }
        }
    }
}

void HardwareControlSurfaceManager::handleIncomingMidi(
    const std::uint8_t* data,
    std::size_t size) noexcept
{
    if (data == nullptr || size < 2)
        return;

    MidiEvent ev {};
    ev.frameOffset = 0;
    ev.size = std::min(size, std::size_t {16});
    for (std::size_t i = 0; i < ev.size; ++i)
    {
        ev.data[i] = static_cast<std::byte>(data[i]);
    }

    HardwareControlSurfaceEngine::HardwareEvent hwEv;
    if (m_engine.parseMidiInput(ev, hwEv))
    {
        switch (hwEv.type)
        {
        case HardwareControlSurfaceEngine::HardwareEvent::Type::mcuFaderMove:
        case HardwareControlSurfaceEngine::HardwareEvent::Type::huiFaderMove:
            if (m_faderCb)
            {
                m_faderCb(hwEv.index, hwEv.value);
            }
            break;

        case HardwareControlSurfaceEngine::HardwareEvent::Type::mcuVpotTurn:
            if (m_vpotCb)
            {
                m_vpotCb(hwEv.index, hwEv.value);
            }
            break;

        case HardwareControlSurfaceEngine::HardwareEvent::Type::mcuTransport:
            if (m_transportCb)
            {
                m_transportCb(static_cast<McuTransportButton>(hwEv.index), hwEv.value > 0.5f);
            }
            break;

        case HardwareControlSurfaceEngine::HardwareEvent::Type::padPress:
        case HardwareControlSurfaceEngine::HardwareEvent::Type::padRelease:
            if (m_padCb)
            {
                const std::size_t row = hwEv.index / HardwareControlSurfaceEngine::GridCols;
                const std::size_t col = hwEv.index % HardwareControlSurfaceEngine::GridCols;
                m_padCb(row, col, hwEv.value, hwEv.type == HardwareControlSurfaceEngine::HardwareEvent::Type::padPress);
            }
            break;

        default:
            break;
        }
    }
}

} // namespace dawg::engine
