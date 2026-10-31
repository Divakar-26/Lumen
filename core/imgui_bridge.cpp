#include "context_internal.h"

#include <imgui_internal.h>

namespace Lumen::detail {
namespace {

class CurrentContextScope {
public:
    explicit CurrentContextScope(ImGuiContext* context) noexcept : previous_(ImGui::GetCurrentContext()) {
        if (context != nullptr && context != previous_) {
            ImGui::SetCurrentContext(context);
        }
    }

    ~CurrentContextScope() {
        if (ImGui::GetCurrentContext() != previous_) {
            ImGui::SetCurrentContext(previous_);
        }
    }

private:
    ImGuiContext* previous_;
};

} // namespace

ImGuiContext* BridgeCurrentImGuiContext() noexcept {
    return ImGui::GetCurrentContext();
}

std::uint64_t BridgeCurrentRootId(ImGuiContext* context) noexcept {
    if (context == nullptr) {
        return 0;
    }
    CurrentContextScope scope(context);
    if (GImGui->CurrentWindow == nullptr || GImGui->CurrentWindow->IDStack.empty()) {
        return 0;
    }
    return static_cast<std::uint64_t>(GImGui->CurrentWindow->IDStack.back());
}

void BridgeInjectMousePosition(ImGuiContext* context, Vec2 position) noexcept {
    if (context == nullptr) return;
    CurrentContextScope scope(context);
    ImGui::GetIO().AddMousePosEvent(position.x, position.y);
}

void BridgeInjectMouseButton(ImGuiContext* context, int button, bool down) noexcept {
    if (context == nullptr || button < 0 || button >= 5) return;
    CurrentContextScope scope(context);
    ImGui::GetIO().AddMouseButtonEvent(button, down);
}

void BridgeInjectMouseWheel(ImGuiContext* context, Vec2 wheel) noexcept {
    if (context == nullptr) return;
    CurrentContextScope scope(context);
    ImGui::GetIO().AddMouseWheelEvent(wheel.x, wheel.y);
}

void BridgeInjectKey(ImGuiContext* context, ImGuiKey key, bool down) noexcept {
    if (context == nullptr || key == ImGuiKey_None) return;
    CurrentContextScope scope(context);
    ImGui::GetIO().AddKeyEvent(key, down);
}

void BridgeInjectText(ImGuiContext* context, const char* utf8_text) noexcept {
    if (context == nullptr || utf8_text == nullptr || utf8_text[0] == '\0') return;
    CurrentContextScope scope(context);
    ImGui::GetIO().AddInputCharactersUTF8(utf8_text);
}

void BridgeInjectFocus(ImGuiContext* context, bool focused) noexcept {
    if (context == nullptr) return;
    CurrentContextScope scope(context);
    ImGui::GetIO().AddFocusEvent(focused);
}

} // namespace Lumen::detail
