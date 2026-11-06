#pragma once

#include "EditorViewModel.h"

#include <dawg/commands/ProjectController.h>
#include <dawg/engine/AudioEngine.h>

#include <utility>

namespace dawg::app {

// Message-thread composition shared by the application and headless tests.
// Service owners outlive this adapter; the UI receives only owned view values.
class ApplicationState final
{
public:
    ApplicationState(commands::ProjectController& controller, engine::AudioEngine& engine)
        : m_controller(controller), m_engine(engine) {}

    [[nodiscard]] ui::EditorViewModel editorViewModel() const
    {
        return {m_controller.snapshot()};
    }

    [[nodiscard]] bool dispatch(project::ProjectCommand command)
    {
        if (! m_controller.dispatch(std::move(command)))
            return false;
        m_renderPending = true;
        static_cast<void>(flushRenderSnapshot());
        return true;
    }

    // A contended publication coalesces to the latest edit on the next UI tick.
    [[nodiscard]] bool flushRenderSnapshot() noexcept
    {
        if (m_renderPending)
            m_renderPending = ! m_engine.publishRenderSnapshot(m_controller.latestRenderSnapshot());
        return ! m_renderPending;
    }

private:
    commands::ProjectController& m_controller;
    engine::AudioEngine& m_engine;
    bool m_renderPending {true};
};

} // namespace dawg::app
