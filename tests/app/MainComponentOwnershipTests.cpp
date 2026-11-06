#include "../../Source/App/MainComponent.h"

#include <type_traits>

namespace {
using dawg::app::MainComponent;
using dawg::platform::juce::JuceAudioDeviceBridge;

// A UI component requires an application-owned bridge and startup status.
static_assert(!std::is_default_constructible_v<MainComponent>);
static_assert(std::is_constructible_v<MainComponent,
                                    JuceAudioDeviceBridge&, const juce::Result&>);
static_assert(!std::is_constructible_v<MainComponent,
                                     JuceAudioDeviceBridge&&, const juce::Result&>);
} // namespace
