#pragma once

#include <dawg/engine/HardwareControlSurfaceEngine.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace dawg::engine {

struct MidiDeviceInfo
{
    std::string identifier;
    std::string name;
    bool isInput {true};
};

struct SurfaceConfig
{
    ControllerProfile profile {ControllerProfile::mackieControlUniversal};
    std::string inputPortId;
    std::string outputPortId;
    ParameterPickupMode pickupMode {ParameterPickupMode::pickup};
    bool enabled {true};
};

struct ChannelStripFeedback
{
    float volume {0.8f};
    float pan {0.0f};
    bool mute {false};
    bool solo {false};
    bool arm {false};
    std::string trackName {"Track"};
};

// Hardware Control Surface Manager & Real-Time MIDI I/O Bridge
// Handles USB/MIDI port enumeration, profile auto-detection,
// and background thread state synchronization (faders, scribble LCDs, RGB rings).
class HardwareControlSurfaceManager final
{
public:
    using MidiSendCallback = std::function<void(const std::uint8_t* data, std::size_t size)>;
    using FaderMovedCallback = std::function<void(std::size_t channelIdx, float value)>;
    using VpotTurnedCallback = std::function<void(std::size_t channelIdx, float deltaTicks)>;
    using TransportCallback = std::function<void(McuTransportButton button, bool pressed)>;
    using PadCallback = std::function<void(std::size_t row, std::size_t col, float velocity, bool pressed)>;

    HardwareControlSurfaceManager() noexcept;
    ~HardwareControlSurfaceManager();

    HardwareControlSurfaceManager(const HardwareControlSurfaceManager&) = delete;
    HardwareControlSurfaceManager& operator=(const HardwareControlSurfaceManager&) = delete;

    // Device Auto-Detection
    [[nodiscard]] static ControllerProfile autoDetectProfile(std::string_view deviceName) noexcept;

    // Configuration
    void setConfig(const SurfaceConfig& config) noexcept;
    [[nodiscard]] SurfaceConfig config() const noexcept;

    // Callbacks for outbound MIDI & inbound hardware actions
    void setMidiSendCallback(MidiSendCallback cb) noexcept { m_midiSendCb = std::move(cb); }
    void setFaderMovedCallback(FaderMovedCallback cb) noexcept { m_faderCb = std::move(cb); }
    void setVpotTurnedCallback(VpotTurnedCallback cb) noexcept { m_vpotCb = std::move(cb); }
    void setTransportCallback(TransportCallback cb) noexcept { m_transportCb = std::move(cb); }
    void setPadCallback(PadCallback cb) noexcept { m_padCb = std::move(cb); }

    // DAW State Updates for Hardware Feedback (Thread-safe)
    void updateChannelStrip(std::size_t channelIdx, const ChannelStripFeedback& state) noexcept;
    void updatePadMatrix(std::size_t row, std::size_t col, GridPadState state) noexcept;
    void syncSessionMatrix(const SessionMatrixEngine& sessionMatrix, double currentBeat = 0.0) noexcept;

    // Background Sync Loop Control
    void startSyncLoop(std::chrono::milliseconds interval = std::chrono::milliseconds(30));
    void stopSyncLoop() noexcept;
    [[nodiscard]] bool isSyncing() const noexcept { return m_syncRunning.load(std::memory_order_relaxed); }

    // Inbound MIDI Processing
    void handleIncomingMidi(const std::uint8_t* data, std::size_t size) noexcept;

    // Synchronize one cycle manually (useful for tests and single-thread updates)
    void syncCycle() noexcept;

    [[nodiscard]] HardwareControlSurfaceEngine& engine() noexcept { return m_engine; }
    [[nodiscard]] const HardwareControlSurfaceEngine& engine() const noexcept { return m_engine; }

private:
    void syncWorkerLoop(std::chrono::milliseconds interval) noexcept;

    HardwareControlSurfaceEngine m_engine;
    SurfaceConfig m_config;

    mutable std::mutex m_stateMutex;
    std::array<ChannelStripFeedback, 9> m_channels {}; // 8 tracks + 1 master
    std::array<float, 9> m_lastSentFaderValues {};
    std::array<float, 8> m_lastSentPanValues {};
    std::array<std::string, 8> m_lastSentTrackNames {};

    MidiSendCallback m_midiSendCb;
    FaderMovedCallback m_faderCb;
    VpotTurnedCallback m_vpotCb;
    TransportCallback m_transportCb;
    PadCallback m_padCb;

    std::atomic<bool> m_syncRunning {false};
    std::thread m_syncThread;
};

} // namespace dawg::engine
