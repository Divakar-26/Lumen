#include <imgui.h>
#include <lumen/lumen.h>

// This function is called by a host after its ImGui backend's NewFrame calls.
void BuildLumenFrame(Lumen::Context* ui, float delta_seconds, float width, float height) {
    ImGui::Begin("Host panel");

    const Lumen::Viewport viewport{
        Lumen::Rect{0.0f, 0.0f, width, height},
        {},
        {1.0f, 1.0f}
    };

    if (Lumen::BeginFrame(ui, viewport, delta_seconds)) {
        // Lumen declarations are added here as node and paint milestones land.
        Lumen::EndFrame(ui);
    }

    ImGui::End();
}
