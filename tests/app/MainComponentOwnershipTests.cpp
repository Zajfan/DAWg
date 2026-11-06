#include "MainComponent.h"

#include <type_traits>

namespace {
using dawg::ui::MainComponent;

// The shell consumes values and callbacks, so it cannot create audio services.
static_assert(!std::is_default_constructible_v<MainComponent>);
static_assert(std::is_constructible_v<MainComponent, dawg::project::ProjectSnapshot,
    dawg::ui::MainEditorView::CommandHandler, dawg::ui::AudioStatus, MainComponent::RestartAudio>);
} // namespace
