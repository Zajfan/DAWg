#pragma once

#include "MainComponent.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <string>

namespace dawg::commands { class ProjectController; }
namespace dawg::engine { class AudioEngine; }
namespace dawg::platform::juce {
class JuceAudioDeviceBridge;
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
    void restartAudio();
    void timerCallback() override;
    [[nodiscard]] ui::AudioStatus audioStatus() const;

    // Reverse destruction order keeps the UI, bridge and adapter inside the
    // lifetimes of the controller/engine they use, even on partial startup.
    std::unique_ptr<engine::AudioEngine> m_engine;
    std::unique_ptr<commands::ProjectController> m_controller;
    std::unique_ptr<ApplicationState> m_state;
    std::unique_ptr<platform::juce::JuceAudioDeviceBridge> m_audioBridge;
    std::unique_ptr<platform::juce::window::WindowManager> m_windowManager;
    juce::Component::SafePointer<ui::MainComponent> m_mainComponent;
    std::string m_audioError;
};

} // namespace dawg::app
