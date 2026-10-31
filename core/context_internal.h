#ifndef LUMEN_CORE_CONTEXT_INTERNAL_H
#define LUMEN_CORE_CONTEXT_INTERNAL_H

#include "arena.h"
#include "state_store.h"
#include "node_internal.h"
#include "lumen/lumen.h"

struct ImGuiContext;

namespace Lumen {

struct Context {
    static constexpr std::size_t kMaxFonts = 8u;
    Config config{};
    ImGuiContext* imgui_context = nullptr;
    ImFont* fonts[kMaxFonts]{};
    std::size_t font_count = 0u;
    detail::FrameArena frame_arena{};
    detail::StateStore state_store{};
    MotionScheduler motion_scheduler{};
    LayerStack layers{};
    struct PortalRuntime {
        std::uint64_t id = 0u;
        Presence presence{};
        bool used = false;
        bool target_present = false;
        double expires_at = 0.0;
    };
    PortalRuntime portal_runtime[LayerStack::kCapacity]{};
    Node* portal_nodes[LayerStack::kCapacity]{};
    Node* portal_return_parents[LayerStack::kCapacity]{};
    std::uint64_t portal_layer_ids[LayerStack::kCapacity]{};
    PortalOptions portal_options[LayerStack::kCapacity]{};
    bool portal_scrims[LayerStack::kCapacity]{};
    bool portal_layout_valid[LayerStack::kCapacity]{};
    Node* active_portals[LayerStack::kCapacity]{};
    std::size_t portal_count = 0u;
    std::size_t active_portal_count = 0u;
    std::uint64_t modal_restore_focus_id = 0u;
    std::uint64_t modal_focus_layer_id = 0u;
    Node* building_portal_root = nullptr;
    Node root_storage{};
    Node* root_node = nullptr;
    Node* building_parent = nullptr;
    Theme theme{};
    Theme theme_from{};
    Theme theme_target{};
    Curve theme_transition_curve = Curve::Standard();
    double theme_transition_start_seconds = 0.0;
    double theme_transition_duration_seconds = 0.0;
    bool theme_transition_active = false;
    std::uint64_t pressed_node_id = 0;
    std::uint64_t captured_node_id = 0;
    std::uint64_t captured_pointer_id = 0;
    std::uint64_t dispatch_current_target_id = 0;
    std::uint64_t dispatch_pointer_id = 0;
    UIEventType dispatch_event_type = UIEventType::PointerMove;
    std::uint64_t hovered_node_id = 0;
    std::uint64_t focused_node_id = 0;
    Vec2 last_pointer_position{};
    bool pointer_position_initialized = false;
    bool dispatching_event = false;
    bool pointer_press_default_prevented = false;
    bool pointer_position_valid = false;
    bool keyboard_focus_visible = false;
    std::size_t nodes_built = 0;
    std::size_t nodes_painted = 0;
    std::uint64_t duplicate_node_ids = 0;
    RenderCommand* render_commands = nullptr;
    std::size_t render_command_count = 0;
    bool node_tree_invalid = false;
    SteadyClock default_clock{};
    const Clock* clock = nullptr;
    FrameInfo frame_info{};
    DebugStats stats{};
    double last_clock_time_seconds = 0.0;
    double motion_time_seconds = 0.0;
    float motion_time_scale = 1.0f;
    bool clock_initialized = false;
    bool reduce_motion = false;
    bool implicit_animations_active = false;
    bool in_frame = false;
    bool needs_frame = false;
};

namespace detail {
void BeginThemeTransition(Context* context, const Theme& theme) noexcept;
ImGuiContext* BridgeCurrentImGuiContext() noexcept;
std::uint64_t BridgeCurrentRootId(ImGuiContext* context) noexcept;
void BridgeInjectMousePosition(ImGuiContext* context, Vec2 position) noexcept;
void BridgeInjectMouseButton(ImGuiContext* context, int button, bool down) noexcept;
void BridgeInjectMouseWheel(ImGuiContext* context, Vec2 wheel) noexcept;
void BridgeInjectKey(ImGuiContext* context, ImGuiKey key, bool down) noexcept;
void BridgeInjectText(ImGuiContext* context, const char* utf8_text) noexcept;
void BridgeInjectFocus(ImGuiContext* context, bool focused) noexcept;
const char* BridgeGetClipboardText(ImGuiContext* context) noexcept;
void BridgeSetClipboardText(ImGuiContext* context, const char* text) noexcept;
}

} // namespace Lumen

#endif
