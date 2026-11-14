#include "MainComponent.h"

#include <type_traits>

namespace {
using dawg::ui::AudioStatus;
using dawg::ui::EditorViewModel;
using dawg::ui::MainComponent;
using dawg::ui::MainEditorView;

// The shell consumes values and callbacks, so it cannot create audio services.
static_assert(!std::is_default_constructible_v<MainComponent>);
static_assert(std::is_constructible_v<MainComponent,
                                      EditorViewModel,
                                      MainEditorView::CommandHandler,
                                      MainEditorView::TransportCallbacks,
                                      AudioStatus,
                                      MainComponent::RestartAudio>);

[[maybe_unused]] void verifyNoOpCallbackConstruction()
{
    MainEditorView::TransportCallbacks callbacks {
        .importWav = [] {},
        .play = [] {},
        .stop = [] {},
        .returnToStart = [] {}
    };
    MainComponent component(
        EditorViewModel {},
        [](dawg::project::ProjectCommand) {},
        callbacks,
        AudioStatus {},
        [] {});
    (void)component;
}
} // namespace
