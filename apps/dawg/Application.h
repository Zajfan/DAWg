#pragma once

#include "MainComponent.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <string>

namespace dawg::commands { class ProjectController; }
namespace dawg::engine { class AudioEngine; }
namespace dawg::media {
class AudioMediaService;
class AudioRecordService;
class DiskStreamPool;
}
namespace dawg::platform::juce {
class JuceAudioDeviceBridge;
class JuceWavAssetReaderFactory;
class JuceWavAssetWriterFactory;
namespace window { class WindowManager; }
}

namespace dawg::app {
class ApplicationState;

class Application final : public juce::JUCEApplication, private juce::Timer
{
public:
    Application();
    ~Application() override;

    const juce::String getApplicationName() override;
    const juce::String getApplicationVersion() override;
    bool moreThanOneInstanceAllowed() override;
    void initialise(const juce::String&) override;
    void shutdown() override;
    void systemRequestedQuit() override;

private:
    void dispatchProjectCommand(project::ProjectCommand);
    void promptImportWav();
    void importWavFile(const juce::File& file);
    void playTransport();
    void stopTransport();
    void returnTransportToStart();
    void toggleTransportRecord();
    void startRecordingSession();
    void stopRecordingSession();
    void undo();
    void redo();
    void restartAudio();
    void syncMediaStatus();
    void refreshMainComponent();
    void timerCallback() override;
    [[nodiscard]] ui::AudioStatus audioStatus() const;

    // Reverse destruction order keeps the UI, media worker, bridge, and adapter
    // inside the lifetimes of the controller, engine, and reader factory they use.
    std::unique_ptr<platform::juce::JuceWavAssetReaderFactory> m_assetReaderFactory;
    std::unique_ptr<platform::juce::JuceWavAssetWriterFactory> m_assetWriterFactory;
    std::unique_ptr<engine::AudioEngine> m_engine;
    std::unique_ptr<commands::ProjectController> m_controller;
    std::unique_ptr<ApplicationState> m_state;
    std::unique_ptr<platform::juce::JuceAudioDeviceBridge> m_audioBridge;
    std::unique_ptr<media::AudioMediaService> m_mediaService;
    std::unique_ptr<media::DiskStreamPool> m_diskStreamPool;
    std::unique_ptr<media::AudioRecordService> m_recordService;
    std::unique_ptr<platform::juce::window::WindowManager> m_windowManager;
    juce::Component::SafePointer<ui::MainComponent> m_mainComponent;
    std::unique_ptr<juce::FileChooser> m_fileChooser;
    std::string m_audioError;
    project::TrackId m_recordingTrackId {0};
    std::uint64_t m_recordStartTimelineSample {0};
    std::size_t m_takeCounter {0};
};

} // namespace dawg::app
