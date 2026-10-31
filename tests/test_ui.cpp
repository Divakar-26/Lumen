#include "lumen/lumen.h"

#include <algorithm>
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace {

int failures = 0;
int click_count = 0;
int first_action_count = 0;
int second_action_count = 0;
int observed_draw_vertices = 0;
bool paint_order_probe_enabled = false;
int paint_order_base_last_vertex = -1;
int paint_order_portal_first_vertex = -1;
int portal_action_count = 0;
int modal_action_count = 0;
int background_action_count = 0;
int invalid_portal_accept_count = 0;
constexpr std::uint64_t kPortalTestLayer = 0x4C554D454E7A001ull;
constexpr std::uint64_t kModalTestLayer = 0x4C554D454E7A002ull;
constexpr std::uint64_t kPinnedToastTestLayer = 0x4C554D454E7A003ull;

struct EventRecord {
    Lumen::UIEventType type = Lumen::UIEventType::PointerMove;
    Lumen::UIEventPhase phase = Lumen::UIEventPhase::Target;
    std::uint64_t current_target = 0u;
    std::uint64_t target = 0u;
};

struct EventProbe {
    EventRecord events[512]{};
    std::size_t count = 0u;
    std::uint64_t parent_id = 0u;
    std::uint64_t button_id = 0u;
    bool capture_on_down = false;
    bool capture_succeeded = false;
    bool captured_move_received = false;
    bool pointer_up_received = false;
    bool prevent_click = false;
    bool stop_down_at_parent_capture = false;
};

EventProbe event_probe{};

void Check(bool condition, const char* expression, int line) {
    if (!condition) {
        std::fprintf(stderr, "FAIL line %d: %s\n", line, expression);
        ++failures;
    }
}
#define CHECK(expression) Check((expression), #expression, __LINE__)

struct HostFrame {
    HostFrame() {
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(800.0f, 600.0f);
        io.DeltaTime = 1.0f / 60.0f;
        if (!io.Fonts->IsBuilt()) {
            unsigned char* pixels = nullptr;
            int width = 0;
            int height = 0;
            io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        }
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(800.0f, 600.0f), ImGuiCond_Always);
        ImGui::Begin("Lumen UI tests", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize);
    }
    ~HostFrame() {
        ImGui::End();
        ImGui::Render();
    }
};

Lumen::Context* MakeContext() {
    if (ImGui::GetCurrentContext() == nullptr) ImGui::CreateContext();
    return Lumen::CreateContext();
}

Lumen::Context* MakeContext(const Lumen::Clock* clock) {
    if (ImGui::GetCurrentContext() == nullptr) ImGui::CreateContext();
    Lumen::Config config{};
    config.clock = clock;
    return Lumen::CreateContext(config);
}

bool RunFrame(Lumen::Context* context, const Lumen::Viewport& viewport,
              void (*body)(Lumen::Context*), bool* end_result = nullptr) {
    HostFrame host;
    if (!Lumen::BeginFrame(context, viewport, 1.0f / 60.0f)) return false;
    if (body != nullptr) body(context);
    const bool ended = Lumen::EndFrame(context);
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    observed_draw_vertices = draw_list != nullptr ? draw_list->VtxBuffer.Size : 0;
    if (paint_order_probe_enabled && draw_list != nullptr) {
        constexpr ImU32 base_color = IM_COL32(33, 51, 71, 255);
        constexpr ImU32 portal_color = IM_COL32(211, 51, 73, 255);
        paint_order_base_last_vertex = -1;
        paint_order_portal_first_vertex = -1;
        for (int i = 0; i < draw_list->VtxBuffer.Size; ++i) {
            const ImU32 color = draw_list->VtxBuffer[i].col;
            if (color == base_color) paint_order_base_last_vertex = i;
            if (color == portal_color && paint_order_portal_first_vertex < 0) paint_order_portal_first_vertex = i;
        }
    }
    if (end_result != nullptr) *end_result = ended;
    return ended;
}

void ClickHandler(Lumen::Context*, std::uint64_t, void*) { ++click_count; }
void FirstActionHandler(Lumen::Context*, std::uint64_t, void*) { ++first_action_count; }
void SecondActionHandler(Lumen::Context*, std::uint64_t, void*) { ++second_action_count; }

void BuildSmallTree(Lumen::Context* context) {
    Lumen::NodeStyle panel{};
    panel.layout = Lumen::LayoutMode::Column;
    panel.width = 240.0f;
    panel.padding = {12.0f, 12.0f, 12.0f, 12.0f};
    panel.gap = 8.0f;
    panel.background = {0.1f, 0.12f, 0.17f, 1.0f};
    panel.has_background = true;
    panel.radius = 10.0f;
    CHECK(Lumen::BeginNode(context, "panel", panel));
    Lumen::NodeStyle title{};
    title.font_size = 21.0f;
    CHECK(Lumen::Label(context, "title", "UI tree", title));
    Lumen::NodeStyle wrapped{};
    wrapped.font_size = 12.0f;
    CHECK(Lumen::Label(context, "wrapped-copy", "Long labels wrap to the width provided by their parent container.", wrapped));
    CHECK(Lumen::Button(context, "action", "Activate", ClickHandler));
    Lumen::EndNode(context);
}

void BuildClickTarget(Lumen::Context* context) {
    CHECK(Lumen::Button(context, "click-target", "Click me", ClickHandler));
}

void BuildDuplicateTree(Lumen::Context* context) {
    CHECK(Lumen::Label(context, "same-key", "first"));
    CHECK(!Lumen::Label(context, "same-key", "duplicate"));
}

void TestViewportValidation() {
    ImGui::CreateContext();
    Lumen::Context* context = MakeContext();
    CHECK(context != nullptr);
    if (context == nullptr) { ImGui::DestroyContext(); return; }

    Lumen::Viewport invalid{Lumen::Rect{0.0f, 0.0f, 100.0f, 100.0f}, {},
                            {std::numeric_limits<float>::quiet_NaN(), 1.0f}};
    const Lumen::DebugStats before = Lumen::GetDebugStats(context);
    CHECK(!RunFrame(context, invalid, nullptr));
    const Lumen::DebugStats after = Lumen::GetDebugStats(context);
    CHECK(!after.in_frame);
    CHECK(after.frames_started == before.frames_started);

    Lumen::Viewport valid{Lumen::Rect{0.0f, 0.0f, 100.0f, 80.0f},
                          {20.0f, 1000.0f, 1000.0f, 30.0f}, {1.0f, 1.0f}};
    CHECK(RunFrame(context, valid, nullptr)); // Oversized safe area is normalized, not allowed to invert the root.
    const Lumen::FrameInfo* frame = Lumen::GetFrameInfo(context);
    CHECK(frame != nullptr);
    if (frame != nullptr) {
        CHECK(frame->viewport.safe_area.left == 30.0f);
        CHECK(frame->viewport.safe_area.right == 70.0f);
        CHECK(frame->viewport.safe_area.top == 20.0f);
        CHECK(frame->viewport.safe_area.bottom == 60.0f);
    }
    Lumen::DestroyContext(context);
    ImGui::DestroyContext();
}

void TestTreeBuildAndPaint() {
    ImGui::CreateContext();
    Lumen::Context* context = MakeContext();
    CHECK(context != nullptr);
    if (context == nullptr) { ImGui::DestroyContext(); return; }
    Lumen::SetTheme(context, Lumen::ThemeKind::Dark);
    const Lumen::Viewport viewport{Lumen::Rect{100.0f, 100.0f, 320.0f, 220.0f}, {}, {1.0f, 1.0f}};
    CHECK(RunFrame(context, viewport, BuildSmallTree));
    const Lumen::DebugStats stats = Lumen::GetDebugStats(context);
    CHECK(stats.nodes_built == 4u);
    CHECK(stats.nodes_painted == 4u);
    CHECK(stats.duplicate_node_ids == 0u);
    CHECK(stats.arena_bytes_used > 0u);
    CHECK(!stats.in_frame);
    CHECK(observed_draw_vertices > 0);
    const Lumen::Theme theme = Lumen::GetTheme(context);
    CHECK(theme.accent.a > 0.0f);
    Lumen::SetTheme(context, Lumen::ThemeKind::Light);
    CHECK(Lumen::IsThemeTransitioning(context));
    CHECK(std::fabs(Lumen::GetTheme(context).canvas.r - theme.canvas.r) < 1.0e-4f);
    CHECK(Lumen::NeedsFrame(context));
    Lumen::DestroyContext(context);
    ImGui::DestroyContext();
}

void TestThemeCrossfadeAndReducedMotion() {
    ImGui::CreateContext();
    Lumen::ManualClock clock(10.0);
    Lumen::Context* context = MakeContext(&clock);
    CHECK(context != nullptr);
    if (context == nullptr) { ImGui::DestroyContext(); return; }

    const Lumen::Viewport viewport{Lumen::Rect{0.0f, 0.0f, 640.0f, 480.0f}, {}, {1.0f, 1.0f}};
    CHECK(RunFrame(context, viewport, nullptr)); // Initialize the clock's high-water mark.
    const Lumen::Theme dark = Lumen::GetTheme(context);

    Lumen::SetTheme(context, Lumen::ThemeKind::Light);
    CHECK(Lumen::IsThemeTransitioning(context));
    CHECK(Lumen::NeedsFrame(context));
    const Lumen::Theme start = Lumen::GetTheme(context);
    CHECK(std::fabs(start.canvas.r - dark.canvas.r) < 1.0e-4f);

    clock.Set(10.09);
    CHECK(RunFrame(context, viewport, nullptr));
    const Lumen::Theme middle = Lumen::GetTheme(context);
    CHECK(middle.canvas.r > dark.canvas.r);
    CHECK(middle.canvas.r < 0.95f);
    CHECK(Lumen::IsThemeTransitioning(context));
    CHECK(Lumen::NeedsFrame(context));

    // An interrupted transition must begin at the currently displayed token values.
    Lumen::SetTheme(context, Lumen::ThemeKind::HighContrast);
    const Lumen::Theme interrupted = Lumen::GetTheme(context);
    CHECK(std::fabs(interrupted.canvas.r - middle.canvas.r) < 1.0e-4f);
    CHECK(Lumen::IsThemeTransitioning(context));

    // Enabling reduced motion must complete the active appearance change and request repaint.
    Lumen::SetReduceMotion(context, true);
    CHECK(!Lumen::IsThemeTransitioning(context));
    CHECK(Lumen::NeedsFrame(context));
    const Lumen::Theme high_contrast = Lumen::GetTheme(context);
    CHECK(high_contrast.canvas.r < 0.1f);

    // Reduced motion applies subsequent appearance changes immediately.
    Lumen::SetTheme(context, Lumen::ThemeKind::Light);
    CHECK(!Lumen::IsThemeTransitioning(context));
    CHECK(Lumen::GetTheme(context).canvas.r > 0.9f);

    Lumen::DestroyContext(context);
    ImGui::DestroyContext();
}

std::uint64_t interaction_panel_id = 0u;
std::uint64_t interaction_child_id = 0u;

void BuildInteractionTree(Lumen::Context* context) {
    const Lumen::FrameInfo* info = Lumen::GetFrameInfo(context);
    const std::uint64_t root_id = info != nullptr ? info->root_id : 0u;
    interaction_panel_id = Lumen::MakeId(root_id, "interaction-panel");
    interaction_child_id = Lumen::MakeId(interaction_panel_id, "interaction-child");

    Lumen::NodeStyle panel{};
    panel.width = 240.0f;
    panel.height = 150.0f;
    panel.layout = Lumen::LayoutMode::Column;
    panel.padding = {8.0f, 8.0f, 8.0f, 8.0f};
    panel.gap = 0.0f;
    panel.background = {0.12f, 0.14f, 0.18f, 1.0f};
    panel.has_background = true;
    panel.hover_background = {0.22f, 0.27f, 0.36f, 1.0f};
    panel.has_hover_background = true;
    panel.pressed_background = {0.32f, 0.38f, 0.50f, 1.0f};
    panel.has_pressed_background = true;
    panel.focusable = true;
    panel.animate_layout = false;
    CHECK(Lumen::BeginNode(context, "interaction-panel", panel));

    Lumen::NodeStyle child{};
    child.height = 36.0f;
    child.animate_layout = false;
    CHECK(Lumen::Button(context, "interaction-child", "Child", nullptr, nullptr, child));
    Lumen::EndNode(context);
}

std::uint64_t shadow_only_panel_id = 0u;

void BuildShadowOnlyInteractionTree(Lumen::Context* context) {
    static const Lumen::ShadowStyle hover_shadow = [] {
        Lumen::ShadowStyle value{};
        value.blur = 0.0f;
        value.hover_blur = 16.0f;
        value.hover_offset_y = 4.0f;
        value.has_hover = true;
        return value;
    }();
    const Lumen::FrameInfo* frame = Lumen::GetFrameInfo(context);
    shadow_only_panel_id = Lumen::MakeId(frame != nullptr ? frame->root_id : 0u, "shadow-only-panel");
    Lumen::NodeStyle panel{};
    panel.width = 180.0f;
    panel.height = 100.0f;
    panel.shadow = &hover_shadow;
    panel.animate_layout = false;
    CHECK(Lumen::BeginNode(context, "shadow-only-panel", panel));
    Lumen::EndNode(context);
}

void TestHoverShadowMakesContainerPointerTarget() {
    ImGui::CreateContext();
    Lumen::Context* context = MakeContext();
    CHECK(context != nullptr);
    if (context == nullptr) { ImGui::DestroyContext(); return; }
    const Lumen::Viewport viewport{Lumen::Rect{100.0f, 100.0f, 420.0f, 260.0f}, {}, {1.0f, 1.0f}};
    CHECK(RunFrame(context, viewport, BuildShadowOnlyInteractionTree));
    Lumen::NodeInteractionState state = Lumen::GetNodeState(context, shadow_only_panel_id);
    CHECK(state.found && !state.hovered);
    const int resting_vertex_count = observed_draw_vertices;

    Lumen::Rect panel_rect{};
    CHECK(Lumen::GetNodeRect(context, shadow_only_panel_id, &panel_rect));
    ImGui::GetIO().AddMousePosEvent((panel_rect.min.x + panel_rect.max.x) * 0.5f,
                                    (panel_rect.min.y + panel_rect.max.y) * 0.5f);
    CHECK(RunFrame(context, viewport, BuildShadowOnlyInteractionTree));
    state = Lumen::GetNodeState(context, shadow_only_panel_id);
    CHECK(state.found && state.hovered);
    CHECK(observed_draw_vertices > resting_vertex_count);

    Lumen::DestroyContext(context);
    ImGui::DestroyContext();
}

void TestContainerInteractionState() {
    ImGui::CreateContext();
    Lumen::Context* context = MakeContext();
    CHECK(context != nullptr);
    if (context == nullptr) { ImGui::DestroyContext(); return; }
    const Lumen::Viewport viewport{Lumen::Rect{100.0f, 100.0f, 420.0f, 260.0f}, {}, {1.0f, 1.0f}};

    CHECK(RunFrame(context, viewport, BuildInteractionTree));
    Lumen::NodeInteractionState state = Lumen::GetNodeState(context, interaction_panel_id);
    CHECK(state.found);
    CHECK(!state.hovered && !state.pressed && !state.focused);

    // Hover the child. The styled parent participates in the hover path too.
    Lumen::Rect child_rect{};
    CHECK(Lumen::GetNodeRect(context, interaction_child_id, &child_rect));
    const float child_x = (child_rect.min.x + child_rect.max.x) * 0.5f;
    const float child_y = (child_rect.min.y + child_rect.max.y) * 0.5f;
    ImGui::GetIO().AddMousePosEvent(child_x, child_y);
    CHECK(RunFrame(context, viewport, BuildInteractionTree));
    state = Lumen::GetNodeState(context, interaction_panel_id);
    const Lumen::NodeInteractionState child_state = Lumen::GetNodeState(context, interaction_child_id);
    CHECK(state.hovered);
    CHECK(child_state.hovered);

    // Pressing a child also marks the parent container as pressed.
    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    CHECK(RunFrame(context, viewport, BuildInteractionTree));
    state = Lumen::GetNodeState(context, interaction_panel_id);
    CHECK(state.hovered && state.pressed && !state.focused);

    // A held press remains pressed while dragging outside, but hover is removed.
    ImGui::GetIO().AddMousePosEvent(600.0f, 450.0f);
    CHECK(RunFrame(context, viewport, BuildInteractionTree));
    state = Lumen::GetNodeState(context, interaction_panel_id);
    CHECK(!state.hovered && state.pressed);

    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    CHECK(RunFrame(context, viewport, BuildInteractionTree));
    state = Lumen::GetNodeState(context, interaction_panel_id);
    CHECK(!state.hovered && !state.pressed && !state.focused);

    // With the pointer in the panel's empty area, its own selectors still work
    // even though no child button is hit.
    ImGui::GetIO().AddMousePosEvent(300.0f, 230.0f);
    CHECK(RunFrame(context, viewport, BuildInteractionTree));
    state = Lumen::GetNodeState(context, interaction_panel_id);
    CHECK(state.hovered);
    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    CHECK(RunFrame(context, viewport, BuildInteractionTree));
    state = Lumen::GetNodeState(context, interaction_panel_id);
    CHECK(state.hovered && state.pressed && state.focused);
    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    CHECK(RunFrame(context, viewport, BuildInteractionTree));

    CHECK(!Lumen::GetNodeState(context, 0u).found);
    CHECK(!Lumen::GetNodeState(context, 0x12345678u).found);
    Lumen::DestroyContext(context);
    ImGui::DestroyContext();
}

void TestPointerClickDispatch() {
    ImGui::CreateContext();
    Lumen::Context* context = MakeContext();
    CHECK(context != nullptr);
    if (context == nullptr) { ImGui::DestroyContext(); return; }
    const Lumen::Viewport viewport{Lumen::Rect{100.0f, 100.0f, 320.0f, 200.0f}, {}, {1.0f, 1.0f}};
    click_count = 0;
    CHECK(RunFrame(context, viewport, nullptr));

    ImGui::GetIO().AddMousePosEvent(150.0f, 120.0f);
    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    CHECK(RunFrame(context, viewport, BuildClickTarget));
    CHECK(click_count == 0);

    ImGui::GetIO().AddMousePosEvent(150.0f, 120.0f);
    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    CHECK(RunFrame(context, viewport, BuildClickTarget));
    CHECK(click_count == 1);
    Lumen::DestroyContext(context);
    ImGui::DestroyContext();
}

void EventProbeHandler(Lumen::Context* context, Lumen::UIEvent& event, void* user_data) {
    auto* probe = static_cast<EventProbe*>(user_data);
    if (probe == nullptr) return;
    if (probe->count < (sizeof(probe->events) / sizeof(probe->events[0]))) {
        probe->events[probe->count++] = {event.type, event.phase, event.current_target_id, event.target_id};
    }
    if (event.type == Lumen::UIEventType::PointerDown && event.phase == Lumen::UIEventPhase::Capture &&
        event.current_target_id == probe->parent_id && probe->stop_down_at_parent_capture) {
        event.StopPropagation();
    }
    if (event.type == Lumen::UIEventType::PointerDown && event.current_target_id == probe->button_id &&
        probe->capture_on_down) {
        probe->capture_succeeded = Lumen::CapturePointer(context, event.current_target_id, event.pointer_id);
    }
    if (event.type == Lumen::UIEventType::PointerMove && event.current_target_id == probe->button_id &&
        Lumen::HasPointerCapture(context, probe->button_id, event.pointer_id)) {
        probe->captured_move_received = true;
    }
    if (event.type == Lumen::UIEventType::PointerUp && event.current_target_id == probe->button_id) {
        probe->pointer_up_received = true;
    }
    if (event.type == Lumen::UIEventType::Click && event.current_target_id == probe->button_id && probe->prevent_click) {
        event.PreventDefault();
    }
}

void BuildEventTree(Lumen::Context* context) {
    const Lumen::FrameInfo* info = Lumen::GetFrameInfo(context);
    const std::uint64_t root_id = info != nullptr ? info->root_id : 0u;
    event_probe.parent_id = Lumen::MakeId(root_id, "event-parent");
    event_probe.button_id = Lumen::MakeId(event_probe.parent_id, "event-button");

    Lumen::NodeStyle parent{};
    parent.layout = Lumen::LayoutMode::Column;
    parent.width = 260.0f;
    parent.height = 140.0f;
    parent.padding = {10.0f, 10.0f, 10.0f, 10.0f};
    parent.gap = 0.0f;
    parent.animate_layout = false;
    parent.event_handler = EventProbeHandler;
    parent.event_user_data = &event_probe;
    CHECK(Lumen::BeginNode(context, "event-parent", parent));

    Lumen::NodeStyle button{};
    button.height = 42.0f;
    button.animate_layout = false;
    button.event_handler = EventProbeHandler;
    button.event_user_data = &event_probe;
    CHECK(Lumen::Button(context, "event-button", "Event target", ClickHandler, nullptr, button));
    Lumen::EndNode(context);
}

void ClearEventProbe() {
    event_probe = EventProbe{};
}

void TestEventRoutingAndPointerCapture() {
    ImGui::CreateContext();
    Lumen::Context* context = MakeContext();
    CHECK(context != nullptr);
    if (context == nullptr) { ImGui::DestroyContext(); return; }
    const Lumen::Viewport viewport{Lumen::Rect{100.0f, 100.0f, 420.0f, 260.0f}, {}, {1.0f, 1.0f}};
    ClearEventProbe();
    event_probe.capture_on_down = true;
    click_count = 0;
    CHECK(RunFrame(context, viewport, BuildEventTree));

    ImGui::GetIO().AddMousePosEvent(150.0f, 120.0f);
    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    CHECK(RunFrame(context, viewport, BuildEventTree));
    CHECK(event_probe.capture_succeeded);
    CHECK(Lumen::HasPointerCapture(context, event_probe.button_id));

    std::size_t down_capture = static_cast<std::size_t>(-1);
    std::size_t down_target = static_cast<std::size_t>(-1);
    std::size_t down_bubble = static_cast<std::size_t>(-1);
    for (std::size_t i = 0; i < event_probe.count; ++i) {
        const EventRecord& record = event_probe.events[i];
        if (record.type != Lumen::UIEventType::PointerDown) continue;
        if (record.phase == Lumen::UIEventPhase::Capture && record.current_target == event_probe.parent_id) down_capture = i;
        if (record.phase == Lumen::UIEventPhase::Target && record.current_target == event_probe.button_id) down_target = i;
        if (record.phase == Lumen::UIEventPhase::Bubble && record.current_target == event_probe.parent_id) down_bubble = i;
    }
    CHECK(down_capture < down_target);
    CHECK(down_target < down_bubble);

    ImGui::GetIO().AddMousePosEvent(600.0f, 450.0f);
    CHECK(RunFrame(context, viewport, BuildEventTree));
    CHECK(event_probe.captured_move_received);

    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    CHECK(RunFrame(context, viewport, BuildEventTree));
    CHECK(event_probe.pointer_up_received);
    CHECK(!Lumen::HasPointerCapture(context, event_probe.button_id));
    CHECK(click_count == 0);
    Lumen::DestroyContext(context);
    ImGui::DestroyContext();
}

void TestStopPropagationAndPreventDefault() {
    ImGui::CreateContext();
    Lumen::Context* context = MakeContext();
    CHECK(context != nullptr);
    if (context == nullptr) { ImGui::DestroyContext(); return; }
    const Lumen::Viewport viewport{Lumen::Rect{100.0f, 100.0f, 420.0f, 260.0f}, {}, {1.0f, 1.0f}};
    ClearEventProbe();
    event_probe.stop_down_at_parent_capture = true;
    click_count = 0;
    CHECK(RunFrame(context, viewport, BuildEventTree));
    ImGui::GetIO().AddMousePosEvent(150.0f, 120.0f);
    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    CHECK(RunFrame(context, viewport, BuildEventTree));
    bool saw_button_down = false;
    bool saw_parent_down_bubble = false;
    for (std::size_t i = 0; i < event_probe.count; ++i) {
        const EventRecord& record = event_probe.events[i];
        if (record.type != Lumen::UIEventType::PointerDown) continue;
        saw_button_down = saw_button_down || record.current_target == event_probe.button_id;
        saw_parent_down_bubble = saw_parent_down_bubble ||
            (record.current_target == event_probe.parent_id && record.phase == Lumen::UIEventPhase::Bubble);
    }
    CHECK(!saw_button_down);
    CHECK(!saw_parent_down_bubble);
    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    CHECK(RunFrame(context, viewport, BuildEventTree));
    Lumen::DestroyContext(context);
    ImGui::DestroyContext();

    ImGui::CreateContext();
    context = MakeContext();
    CHECK(context != nullptr);
    if (context == nullptr) { ImGui::DestroyContext(); return; }
    ClearEventProbe();
    event_probe.prevent_click = true;
    click_count = 0;
    CHECK(RunFrame(context, viewport, BuildEventTree));
    ImGui::GetIO().AddMousePosEvent(150.0f, 120.0f);
    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    CHECK(RunFrame(context, viewport, BuildEventTree));
    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    CHECK(RunFrame(context, viewport, BuildEventTree));
    CHECK(click_count == 0);
    Lumen::DestroyContext(context);
    ImGui::DestroyContext();
}

std::uint64_t wrap_parent_id = 0u;
std::uint64_t wrap_item_ids[4]{};

void BuildWrappedRow(Lumen::Context* context) {
    const Lumen::FrameInfo* info = Lumen::GetFrameInfo(context);
    const std::uint64_t root_id = info != nullptr ? info->root_id : 0u;
    wrap_parent_id = Lumen::MakeId(root_id, "wrapped-row");
    const char* keys[] = {"wrap-a", "wrap-b", "wrap-c", "wrap-d"};
    for (std::size_t i = 0u; i < 4u; ++i) wrap_item_ids[i] = Lumen::MakeId(wrap_parent_id, keys[i]);

    Lumen::NodeStyle row{};
    row.layout = Lumen::LayoutMode::Row;
    row.width = 210.0f;
    row.height = 100.0f;
    row.padding = {4.0f, 4.0f, 4.0f, 4.0f};
    row.gap = 4.0f;
    row.flex_wrap = true;
    row.align_items = Lumen::Align::Start;
    row.animate_layout = false;
    CHECK(Lumen::BeginNode(context, "wrapped-row", row));
    for (std::size_t i = 0u; i < 4u; ++i) {
        Lumen::NodeStyle item{};
        item.flex_basis = i == 0u ? 110.0f : 90.0f;
        item.max_width = i == 0u ? 80.0f : 200.0f;
        item.height = 22.0f;
        item.animate_layout = false;
        CHECK(Lumen::Label(context, keys[i], "chip", item));
    }
    Lumen::EndNode(context);
}

void TestFlexBasisWrapAndGeometryQuery() {
    ImGui::CreateContext();
    Lumen::Context* context = MakeContext();
    CHECK(context != nullptr);
    if (context == nullptr) { ImGui::DestroyContext(); return; }
    const Lumen::Viewport viewport{Lumen::Rect{100.0f, 100.0f, 420.0f, 260.0f}, {}, {1.0f, 1.0f}};
    CHECK(RunFrame(context, viewport, BuildWrappedRow));
    Lumen::Rect row_rect{};
    Lumen::Rect first{};
    Lumen::Rect second{};
    Lumen::Rect third{};
    Lumen::Rect fourth{};
    CHECK(Lumen::GetNodeRect(context, wrap_parent_id, &row_rect));
    CHECK(Lumen::GetNodeRect(context, wrap_item_ids[0], &first));
    CHECK(Lumen::GetNodeRect(context, wrap_item_ids[1], &second));
    CHECK(Lumen::GetNodeRect(context, wrap_item_ids[2], &third));
    CHECK(Lumen::GetNodeRect(context, wrap_item_ids[3], &fourth));
    CHECK(std::fabs(first.Width() - 80.0f) < 0.1f);
    CHECK(std::fabs(second.Width() - 90.0f) < 0.1f);
    CHECK(std::fabs(first.min.y - second.min.y) < 0.1f);
    CHECK(third.min.y > first.min.y);
    CHECK(std::fabs(third.min.y - fourth.min.y) < 0.1f);
    CHECK(row_rect.Contains({first.min.x, first.min.y}));
    CHECK(row_rect.Contains({fourth.max.x, fourth.max.y}));
    Lumen::DestroyContext(context);
    ImGui::DestroyContext();
}

void BuildTinyButton(Lumen::Context* context) {
    Lumen::NodeStyle tiny{};
    tiny.width = 20.0f;
    tiny.height = 20.0f;
    tiny.animate_layout = false;
    CHECK(Lumen::Button(context, "tiny-target", "x", ClickHandler, nullptr, tiny));
}

void TestMinimumHitTarget() {
    ImGui::CreateContext();
    Lumen::Context* context = MakeContext();
    CHECK(context != nullptr);
    if (context == nullptr) { ImGui::DestroyContext(); return; }
    const Lumen::Viewport viewport{Lumen::Rect{100.0f, 100.0f, 320.0f, 200.0f}, {}, {1.0f, 1.0f}};
    click_count = 0;
    CHECK(RunFrame(context, viewport, BuildTinyButton));
    Lumen::Rect rect{};
    const std::uint64_t tiny_id = Lumen::MakeId(Lumen::GetFrameInfo(context)->root_id, "tiny-target");
    CHECK(Lumen::GetNodeRect(context, tiny_id, &rect));
    CHECK(rect.Width() == 20.0f);
    ImGui::GetIO().AddMousePosEvent(rect.max.x + 5.0f, rect.min.y + 10.0f);
    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    CHECK(RunFrame(context, viewport, BuildTinyButton));
    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    CHECK(RunFrame(context, viewport, BuildTinyButton));
    CHECK(click_count == 1);
    Lumen::DestroyContext(context);
    ImGui::DestroyContext();
}

float observed_nested_parent_scroll = 0.0f;
float observed_nested_child_scroll = 0.0f;
std::uint64_t nested_parent_id = 0u;
std::uint64_t nested_child_id = 0u;
bool initialize_nested_scroll = false;

void BuildNestedScrollableTree(Lumen::Context* context) {
    const Lumen::FrameInfo* info = Lumen::GetFrameInfo(context);
    const std::uint64_t root_id = info != nullptr ? info->root_id : 0u;
    nested_parent_id = Lumen::MakeId(root_id, "nested-parent");
    nested_child_id = Lumen::MakeId(nested_parent_id, "nested-child");
    observed_nested_parent_scroll = Lumen::GetScrollOffset(context, nested_parent_id).y;
    observed_nested_child_scroll = Lumen::GetScrollOffset(context, nested_child_id).y;
    if (initialize_nested_scroll) {
        CHECK(Lumen::SetScrollOffset(context, nested_child_id, {0.0f, 100000.0f}));
        initialize_nested_scroll = false;
    }

    Lumen::NodeStyle parent{};
    parent.layout = Lumen::LayoutMode::Column;
    parent.width = 270.0f;
    parent.height = 120.0f;
    parent.padding = {6.0f, 6.0f, 6.0f, 6.0f};
    parent.gap = 4.0f;
    parent.scrollable_y = true;
    parent.clip_children = true;
    parent.align_items = Lumen::Align::Start;
    parent.animate_layout = false;
    CHECK(Lumen::BeginNode(context, "nested-parent", parent));

    Lumen::NodeStyle child{};
    child.layout = Lumen::LayoutMode::Column;
    child.width = 230.0f;
    child.height = 60.0f;
    child.gap = 0.0f;
    child.scrollable_y = true;
    child.clip_children = true;
    child.align_items = Lumen::Align::Start;
    child.animate_layout = false;
    CHECK(Lumen::BeginNode(context, "nested-child", child));
    for (std::uint64_t i = 0u; i < 10u; ++i) {
        char label[32]{};
        std::snprintf(label, sizeof(label), "Inner row %02llu", static_cast<unsigned long long>(i));
        Lumen::NodeStyle item{};
        item.height = 24.0f;
        item.width = 200.0f;
        item.animate_layout = false;
        CHECK(Lumen::Label(context, i + 1u, label, item));
    }
    Lumen::EndNode(context);
    for (std::uint64_t i = 0u; i < 8u; ++i) {
        char label[32]{};
        std::snprintf(label, sizeof(label), "Outer row %02llu", static_cast<unsigned long long>(i));
        Lumen::NodeStyle item{};
        item.height = 26.0f;
        item.width = 230.0f;
        item.animate_layout = false;
        item.flex_shrink = 0.0f;
        CHECK(Lumen::Label(context, i + 100u, label, item));
    }
    Lumen::EndNode(context);
}

void TestNestedScrollChaining() {
    ImGui::CreateContext();
    Lumen::Context* context = MakeContext();
    CHECK(context != nullptr);
    if (context == nullptr) { ImGui::DestroyContext(); return; }
    const Lumen::Viewport viewport{Lumen::Rect{100.0f, 100.0f, 420.0f, 260.0f}, {}, {1.0f, 1.0f}};
    initialize_nested_scroll = true;
    observed_nested_parent_scroll = 0.0f;
    observed_nested_child_scroll = 0.0f;
    CHECK(RunFrame(context, viewport, BuildNestedScrollableTree));
    CHECK(observed_nested_parent_scroll == 0.0f);

    ImGui::GetIO().AddMousePosEvent(130.0f, 120.0f);
    ImGui::GetIO().AddMouseWheelEvent(0.0f, -1.0f);
    CHECK(RunFrame(context, viewport, BuildNestedScrollableTree));
    CHECK(RunFrame(context, viewport, BuildNestedScrollableTree));
    CHECK(RunFrame(context, viewport, BuildNestedScrollableTree));
    CHECK(observed_nested_child_scroll > 0.0f);
    CHECK(observed_nested_parent_scroll > 0.0f);
    Lumen::DestroyContext(context);
    ImGui::DestroyContext();
}

void BuildLargeGrid(Lumen::Context* context) {
    Lumen::NodeStyle grid{};
    grid.layout = Lumen::LayoutMode::Grid;
    grid.grid_columns = 4;
    grid.width = 320.0f;
    grid.height = 220.0f;
    grid.padding = {8.0f, 8.0f, 8.0f, 8.0f};
    grid.gap = 4.0f;
    grid.scrollable_y = true;
    grid.clip_children = true;
    grid.background = {0.08f, 0.09f, 0.12f, 1.0f};
    grid.has_background = true;
    CHECK(Lumen::BeginNode(context, "large-grid", grid));
    for (std::uint64_t i = 1; i <= 2000; ++i) {
        CHECK(Lumen::Label(context, i, "grid cell"));
    }
    Lumen::EndNode(context);
}

float observed_scroll_y = 0.0f;

void BuildScrollableList(Lumen::Context* context) {
    const Lumen::FrameInfo* info = Lumen::GetFrameInfo(context);
    const std::uint64_t scroll_id = Lumen::MakeId(info != nullptr ? info->root_id : 0u, "scroll-panel");
    observed_scroll_y = Lumen::GetScrollOffset(context, scroll_id).y;
    Lumen::NodeStyle scroller{};
    scroller.layout = Lumen::LayoutMode::Column;
    scroller.width = 260.0f;
    scroller.height = 90.0f;
    scroller.padding = {8.0f, 8.0f, 8.0f, 8.0f};
    scroller.gap = 4.0f;
    scroller.scrollable_y = true;
    scroller.clip_children = true;
    scroller.background = {0.08f, 0.09f, 0.12f, 1.0f};
    scroller.has_background = true;
    CHECK(Lumen::BeginNode(context, "scroll-panel", scroller));
    for (std::uint64_t i = 1; i <= 15; ++i) {
        char label[32]{};
        std::snprintf(label, sizeof(label), "List item %02llu", static_cast<unsigned long long>(i));
        CHECK(Lumen::Label(context, i, label));
    }
    Lumen::EndNode(context);
}

void TestWheelScrolling() {
    ImGui::CreateContext();
    Lumen::Context* context = MakeContext();
    CHECK(context != nullptr);
    if (context == nullptr) { ImGui::DestroyContext(); return; }
    const Lumen::Viewport viewport{Lumen::Rect{100.0f, 100.0f, 320.0f, 220.0f}, {}, {1.0f, 1.0f}};
    observed_scroll_y = 0.0f;
    CHECK(RunFrame(context, viewport, BuildScrollableList));
    CHECK(observed_scroll_y == 0.0f);
    ImGui::GetIO().AddMousePosEvent(120.0f, 120.0f);
    ImGui::GetIO().AddMouseWheelEvent(0.0f, -1.0f);
    CHECK(RunFrame(context, viewport, BuildScrollableList));
    CHECK(RunFrame(context, viewport, BuildScrollableList));
    // Observe the wheel offset inside the next active frame; wheel routing occurs at EndFrame.
    CHECK(RunFrame(context, viewport, BuildScrollableList));
    CHECK(observed_scroll_y > 0.0f);
    Lumen::DestroyContext(context);
    ImGui::DestroyContext();
}


void BuildTwoFocusButtons(Lumen::Context* context) {
    CHECK(Lumen::Button(context, "focus-first", "First action", FirstActionHandler));
    CHECK(Lumen::Button(context, "focus-second", "Second action", SecondActionHandler));
}

void TestKeyboardFocusAndActivation() {
    ImGui::CreateContext();
    Lumen::Context* context = MakeContext();
    CHECK(context != nullptr);
    if (context == nullptr) { ImGui::DestroyContext(); return; }
    const Lumen::Viewport viewport{Lumen::Rect{100.0f, 100.0f, 320.0f, 220.0f}, {}, {1.0f, 1.0f}};
    first_action_count = 0;
    second_action_count = 0;
    CHECK(RunFrame(context, viewport, BuildTwoFocusButtons));

    ImGui::GetIO().AddKeyEvent(ImGuiKey_Tab, true);
    CHECK(RunFrame(context, viewport, BuildTwoFocusButtons));
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Tab, false);
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, true);
    CHECK(RunFrame(context, viewport, BuildTwoFocusButtons));
    CHECK(first_action_count == 1);
    CHECK(second_action_count == 0);

    ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, false);
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Tab, true);
    CHECK(RunFrame(context, viewport, BuildTwoFocusButtons));
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Tab, false);
    CHECK(RunFrame(context, viewport, BuildTwoFocusButtons));
    ImGui::GetIO().AddKeyEvent(ImGuiMod_Shift, true);
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Tab, true);
    CHECK(RunFrame(context, viewport, BuildTwoFocusButtons));
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Tab, false);
    ImGui::GetIO().AddKeyEvent(ImGuiMod_Shift, false);
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, true);
    CHECK(RunFrame(context, viewport, BuildTwoFocusButtons));
    CHECK(first_action_count == 2);
    CHECK(second_action_count == 0);

    Lumen::DestroyContext(context);
    ImGui::DestroyContext();
}

void TestTwoThousandNodeGrid() {
    ImGui::CreateContext();
    Lumen::Context* context = MakeContext();
    CHECK(context != nullptr);
    if (context == nullptr) { ImGui::DestroyContext(); return; }
    const Lumen::Viewport viewport{Lumen::Rect{100.0f, 100.0f, 420.0f, 300.0f}, {}, {1.0f, 1.0f}};
    CHECK(RunFrame(context, viewport, BuildLargeGrid));
    const Lumen::DebugStats stats = Lumen::GetDebugStats(context);
    CHECK(stats.nodes_built == 2001u);
    CHECK(stats.nodes_painted == 2001u);
    CHECK(stats.arena_failed_allocations == 0u);
    CHECK(stats.state_capacity_exhaustions == 0u);
    CHECK(stats.arena_bytes_used < 1024u * 1024u);
    CHECK(stats.arena_reserved_bytes == 1024u * 1024u);
    CHECK(stats.arena_bytes_used <= 525u * stats.nodes_built);
    const double bytes_per_node = static_cast<double>(stats.arena_bytes_used) /
                                  static_cast<double>(stats.nodes_built);
    std::printf("Arena budget: %zu bytes / %zu nodes (%.2f bytes per node)\n",
                stats.arena_bytes_used, stats.nodes_built, bytes_per_node);
    Lumen::DestroyContext(context);
    ImGui::DestroyContext();
}

struct VirtualListProbe {
    std::size_t built = 0u;
    std::size_t first = static_cast<std::size_t>(-1);
    std::size_t last = 0u;
};

VirtualListProbe virtual_probe{};
constexpr std::size_t kVirtualItemCount = 100000u;
constexpr float kVirtualItemExtent = 24.0f;
constexpr std::size_t kVirtualTargetIndex = 90000u;

void BuildVirtualItem(Lumen::Context* context, std::size_t index, void* user_data) {
    auto* probe = static_cast<VirtualListProbe*>(user_data);
    if (probe != nullptr) {
        ++probe->built;
        probe->first = std::min(probe->first, index);
        probe->last = std::max(probe->last, index);
    }
    char text[48]{};
    std::snprintf(text, sizeof(text), "Record %06llu", static_cast<unsigned long long>(index));
    CHECK(Lumen::Label(context, "record-label", text));
}

void BuildVirtualCollection(Lumen::Context* context) {
    const Lumen::FrameInfo* info = Lumen::GetFrameInfo(context);
    const std::uint64_t root_id = info != nullptr ? info->root_id : 0u;
    const std::uint64_t collection_id = Lumen::MakeId(root_id, "virtual-list");
    CHECK(Lumen::SetScrollOffset(context, collection_id,
        Lumen::Vec2{0.0f, static_cast<float>(kVirtualTargetIndex) * kVirtualItemExtent}));
    Lumen::NodeStyle style{};
    style.width = 320.0f;
    style.height = 120.0f;
    style.padding = {4.0f, 8.0f, 4.0f, 8.0f};
    style.background = {0.08f, 0.09f, 0.12f, 1.0f};
    style.has_background = true;
    style.radius = 8.0f;
    CHECK(Lumen::Collection(context, "virtual-list", kVirtualItemCount, kVirtualItemExtent,
                            BuildVirtualItem, &virtual_probe, style, 2u));
}

void TestVirtualizedCollection() {
    ImGui::CreateContext();
    Lumen::Context* context = MakeContext();
    CHECK(context != nullptr);
    if (context == nullptr) { ImGui::DestroyContext(); return; }
    const Lumen::Viewport viewport{Lumen::Rect{100.0f, 100.0f, 420.0f, 300.0f}, {}, {1.0f, 1.0f}};
    virtual_probe = {};
    CHECK(RunFrame(context, viewport, BuildVirtualCollection));
    CHECK(virtual_probe.built >= 8u);
    CHECK(virtual_probe.built <= 12u);
    CHECK(virtual_probe.first >= kVirtualTargetIndex - 2u);
    CHECK(virtual_probe.first <= kVirtualTargetIndex);
    CHECK(virtual_probe.last >= kVirtualTargetIndex);
    CHECK(virtual_probe.last < kVirtualTargetIndex + 12u);
    const Lumen::DebugStats stats = Lumen::GetDebugStats(context);
    CHECK(stats.nodes_built < 32u);
    CHECK(stats.nodes_painted < 32u);
    CHECK(stats.arena_failed_allocations == 0u);
    CHECK(stats.state_capacity_exhaustions == 0u);
    CHECK(stats.arena_bytes_used < 64u * 1024u);
    Lumen::DestroyContext(context);
    ImGui::DestroyContext();
}

void BuildImageNode(Lumen::Context* context) {
    Lumen::NodeStyle style{};
    style.width = 160.0f;
    style.height = 90.0f;
    style.radius = 10.0f;
    CHECK(Lumen::Image(context, "host-image", ImTextureID{}, Lumen::Vec2{320.0f, 180.0f},
                       Lumen::ImageFit::Cover, style, "Host-owned image texture"));
}

void TestHostOwnedImageResourceSeam() {
    ImGui::CreateContext();
    Lumen::Context* context = MakeContext();
    CHECK(context != nullptr);
    if (context == nullptr) { ImGui::DestroyContext(); return; }
    const Lumen::Viewport viewport{Lumen::Rect{20.0f, 20.0f, 320.0f, 200.0f}, {}, {1.0f, 1.0f}};
    CHECK(RunFrame(context, viewport, BuildImageNode));
    Lumen::Rect image_rect{};
    CHECK(Lumen::GetNodeRect(context, Lumen::MakeId(Lumen::GetFrameInfo(context)->root_id, "host-image"), &image_rect));
    CHECK(std::abs(image_rect.Width() - 160.0f) < 0.01f);
    CHECK(std::abs(image_rect.Height() - 90.0f) < 0.01f);
    CHECK(observed_draw_vertices > 0);
    Lumen::DestroyContext(context);
    ImGui::DestroyContext();
}


void BuildPresencePortalTree(Lumen::Context* context) {
    if (!Lumen::IsLayerOpen(context, kPortalTestLayer)) return;
    Lumen::PortalOptions options{};
    options.size = {180.0f, 100.0f};
    Lumen::NodeStyle panel{};
    panel.padding = {10.0f, 10.0f, 10.0f, 10.0f};
    panel.background = Lumen::GetTheme(context).surface;
    panel.has_background = true;
    panel.radius = 8.0f;
    if (Lumen::BeginPortal(context, kPortalTestLayer, options, panel)) {
        CHECK(Lumen::Label(context, "presence-copy", "Presence lifecycle"));
        Lumen::EndPortal(context);
    }
}

void BuildTimedToastPortalTree(Lumen::Context* context) {
    if (!Lumen::IsLayerOpen(context, kPinnedToastTestLayer)) return;
    Lumen::PortalOptions options{};
    options.size = {180.0f, 100.0f};
    options.pin_to_top_right = true;
    Lumen::NodeStyle panel{};
    panel.padding = {10.0f, 10.0f, 10.0f, 10.0f};
    panel.background = Lumen::GetTheme(context).surface;
    panel.has_background = true;
    if (Lumen::BeginPortal(context, kPinnedToastTestLayer, options, panel)) {
        CHECK(Lumen::Label(context, "timed-presence-copy", "Timed toast lifecycle"));
        Lumen::EndPortal(context);
    }
}

void TestPortalPresenceLifecycle() {
    ImGui::CreateContext();
    Lumen::ManualClock clock(0.0);
    Lumen::Context* context = MakeContext(&clock);
    CHECK(context != nullptr);
    if (context == nullptr) { ImGui::DestroyContext(); return; }
    Lumen::Theme theme = Lumen::GetTheme(context);
    theme.color_transition.duration_seconds = 0.2;
    Lumen::SetTheme(context, theme);
    const Lumen::Viewport viewport{Lumen::Rect{0.0f, 0.0f, 640.0f, 480.0f}, {}, {1.0f, 1.0f}};
    CHECK(RunFrame(context, viewport, nullptr));

    CHECK(Lumen::OpenLayer(context, kPortalTestLayer, Lumen::LayerKind::Popover));
    CHECK(Lumen::NeedsFrame(context));
    CHECK(RunFrame(context, viewport, BuildPresencePortalTree));
    const std::uint64_t portal_id = Lumen::MakeId(Lumen::GetFrameInfo(context)->root_id, kPortalTestLayer);
    Lumen::Rect portal_rect{};
    CHECK(Lumen::GetNodeRect(context, portal_id, &portal_rect));

    CHECK(Lumen::CloseLayer(context, kPortalTestLayer));
    CHECK(Lumen::IsLayerOpen(context, kPortalTestLayer)); // Keep content mounted during exit.
    CHECK(RunFrame(context, viewport, BuildPresencePortalTree));
    CHECK(Lumen::GetNodeRect(context, portal_id, &portal_rect));
    CHECK(Lumen::NeedsFrame(context));

    clock.Set(0.10);
    CHECK(RunFrame(context, viewport, BuildPresencePortalTree));
    CHECK(Lumen::IsLayerOpen(context, kPortalTestLayer));
    CHECK(Lumen::GetNodeRect(context, portal_id, &portal_rect));

    clock.Set(0.25);
    CHECK(RunFrame(context, viewport, BuildPresencePortalTree));
    CHECK(!Lumen::IsLayerOpen(context, kPortalTestLayer));
    CHECK(!Lumen::GetNodeRect(context, portal_id, &portal_rect));
    CHECK(!Lumen::NeedsFrame(context));

    Lumen::DestroyContext(context);
    ImGui::DestroyContext();
}

void PortalActionHandler(Lumen::Context*, std::uint64_t, void*) { ++portal_action_count; }
void ModalActionHandler(Lumen::Context*, std::uint64_t, void*) { ++modal_action_count; }
void BackgroundActionHandler(Lumen::Context*, std::uint64_t, void*) { ++background_action_count; }

void BuildAnchoredPortalTree(Lumen::Context* context) {
    const Lumen::FrameInfo* info = Lumen::GetFrameInfo(context);
    CHECK(info != nullptr);
    Lumen::NodeStyle anchor{};
    anchor.width = 138.0f;
    anchor.height = 42.0f;
    anchor.background = {0.16f, 0.24f, 0.42f, 1.0f};
    anchor.has_background = true;
    CHECK(Lumen::Button(context, "portal-anchor", "Open anchored layer", nullptr, nullptr, anchor));
    if (Lumen::IsLayerOpen(context, kPortalTestLayer)) {
        Lumen::PortalOptions options{};
        options.anchor_node_id = Lumen::MakeId(info->root_id, "portal-anchor");
        options.size = {206.0f, 122.0f};
        options.placement = Lumen::Placement::BelowAnchor;
        options.align = Lumen::PlacementAlign::Start;
        options.offset = {0.0f, 8.0f};
        Lumen::NodeStyle popup{};
        popup.padding = {12.0f, 12.0f, 12.0f, 12.0f};
        popup.gap = 8.0f;
        popup.background = Lumen::GetTheme(context).surface;
        popup.has_background = true;
        popup.border_color = Lumen::GetTheme(context).accent;
        popup.has_border_color = true;
        popup.border_width = 1.0f;
        popup.radius = 10.0f;
        if (Lumen::BeginPortal(context, kPortalTestLayer, options, popup)) {
            CHECK(Lumen::Label(context, "portal-copy", "Rendered in a separate layer"));
            CHECK(Lumen::Button(context, "portal-action", "Layer action", PortalActionHandler));
            Lumen::EndPortal(context);
        }
    }
    if (Lumen::IsLayerOpen(context, kModalTestLayer)) {
        Lumen::PortalOptions options{};
        options.size = {290.0f, 180.0f};
        options.scrim = true;
        Lumen::NodeStyle modal{};
        modal.padding = {18.0f, 18.0f, 18.0f, 18.0f};
        modal.gap = 12.0f;
        modal.background = Lumen::GetTheme(context).surface;
        modal.has_background = true;
        modal.border_color = Lumen::GetTheme(context).border;
        modal.has_border_color = true;
        modal.border_width = 1.0f;
        modal.radius = 12.0f;
        if (Lumen::BeginPortal(context, kModalTestLayer, options, modal)) {
            CHECK(Lumen::Label(context, "portal-modal-title", "Nested modal focus scope"));
            CHECK(Lumen::Button(context, "portal-modal-action", "Confirm", ModalActionHandler));
            Lumen::EndPortal(context);
        }
    }
}

void BuildModalScopeTree(Lumen::Context* context) {
    const Lumen::FrameInfo* info = Lumen::GetFrameInfo(context);
    CHECK(info != nullptr);
    Lumen::NodeStyle background{};
    background.width = 180.0f;
    background.height = 44.0f;
    CHECK(Lumen::Button(context, "background-action", "Background action", BackgroundActionHandler, nullptr, background));
    if (Lumen::IsLayerOpen(context, kModalTestLayer)) {
        Lumen::PortalOptions options{};
        options.size = {290.0f, 180.0f};
        options.scrim = true;
        Lumen::NodeStyle modal{};
        modal.padding = {18.0f, 18.0f, 18.0f, 18.0f};
        modal.gap = 12.0f;
        modal.background = Lumen::GetTheme(context).surface;
        modal.has_background = true;
        modal.border_color = Lumen::GetTheme(context).border;
        modal.has_border_color = true;
        modal.border_width = 1.0f;
        modal.radius = 12.0f;
        if (Lumen::BeginPortal(context, kModalTestLayer, options, modal)) {
            CHECK(Lumen::Label(context, "modal-title", "Modal focus scope"));
            CHECK(Lumen::Button(context, "modal-action", "Confirm", ModalActionHandler));
            Lumen::EndPortal(context);
        }
    }
}


constexpr std::uint64_t kPaintOrderLayer = 0x4C554D454E7A004ull;

void BuildPortalPaintOrderTree(Lumen::Context* context) {
    Lumen::NodeStyle base{};
    base.width = 340.0f;
    base.height = 240.0f;
    base.has_background = true;
    base.background = {33.0f / 255.0f, 51.0f / 255.0f, 71.0f / 255.0f, 1.0f};
    CHECK(Lumen::BeginNode(context, "paint-order-base", base));
    Lumen::EndNode(context);

    if (!Lumen::IsLayerOpen(context, kPaintOrderLayer)) return;
    Lumen::PortalOptions options{};
    options.size = {180.0f, 120.0f};
    Lumen::NodeStyle portal{};
    portal.has_background = true;
    portal.background = {211.0f / 255.0f, 51.0f / 255.0f, 73.0f / 255.0f, 1.0f};
    CHECK(Lumen::BeginPortal(context, kPaintOrderLayer, options, portal));
    Lumen::EndPortal(context);
}

void TestPortalPaintOrderIsAboveBaseTree() {
    ImGui::CreateContext();
    Lumen::Context* context = MakeContext();
    CHECK(context != nullptr);
    if (context == nullptr) { ImGui::DestroyContext(); return; }
    Lumen::SetReduceMotion(context, true);
    const Lumen::Viewport viewport{Lumen::Rect{40.0f, 40.0f, 480.0f, 320.0f}, {}, {1.0f, 1.0f}};
    paint_order_probe_enabled = true;
    CHECK(RunFrame(context, viewport, BuildPortalPaintOrderTree));
    CHECK(Lumen::OpenLayer(context, kPaintOrderLayer, Lumen::LayerKind::Popover));
    CHECK(RunFrame(context, viewport, BuildPortalPaintOrderTree));
    CHECK(paint_order_base_last_vertex >= 0);
    CHECK(paint_order_portal_first_vertex >= 0);
    CHECK(paint_order_base_last_vertex < paint_order_portal_first_vertex);
    paint_order_probe_enabled = false;
    CHECK(Lumen::CloseLayer(context, kPaintOrderLayer));
    CHECK(RunFrame(context, viewport, BuildPortalPaintOrderTree));
    Lumen::DestroyContext(context);
    ImGui::DestroyContext();
}

void TestPortalPaintingAnchorAndDismissal() {
    ImGui::CreateContext();
    Lumen::Context* context = MakeContext();
    CHECK(context != nullptr);
    if (context == nullptr) { ImGui::DestroyContext(); return; }
    Lumen::SetReduceMotion(context, true);
    const Lumen::Viewport viewport{Lumen::Rect{50.0f, 40.0f, 520.0f, 360.0f}, {}, {1.0f, 1.0f}};
    portal_action_count = 0;
    CHECK(RunFrame(context, viewport, BuildAnchoredPortalTree));
    Lumen::Rect anchor_rect{};
    CHECK(Lumen::GetNodeRect(context, Lumen::MakeId(Lumen::GetFrameInfo(context)->root_id, "portal-anchor"), &anchor_rect));
    CHECK(Lumen::OpenLayer(context, kPortalTestLayer, Lumen::LayerKind::Popover,
        static_cast<std::uint8_t>(Lumen::DismissOnOutsideClick | Lumen::DismissOnEscape)));
    CHECK(RunFrame(context, viewport, BuildAnchoredPortalTree));
    const std::uint64_t portal_id = Lumen::MakeId(Lumen::GetFrameInfo(context)->root_id, kPortalTestLayer);
    Lumen::Rect portal_copy_rect{};
    CHECK(Lumen::GetNodeRect(context, Lumen::MakeId(portal_id, "portal-copy"), &portal_copy_rect));
    CHECK(portal_copy_rect.min.y >= anchor_rect.max.y + 5.0f);
    CHECK(portal_copy_rect.min.x >= viewport.bounds.min.x);
    CHECK(observed_draw_vertices > 100);

    // Non-modal portal controls participate in the same Tab order after base controls.
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Tab, true);
    CHECK(RunFrame(context, viewport, BuildAnchoredPortalTree));
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Tab, false);
    CHECK(RunFrame(context, viewport, BuildAnchoredPortalTree));
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Tab, true);
    CHECK(RunFrame(context, viewport, BuildAnchoredPortalTree));
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Tab, false);
    CHECK(RunFrame(context, viewport, BuildAnchoredPortalTree));
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, true);
    CHECK(RunFrame(context, viewport, BuildAnchoredPortalTree));
    CHECK(portal_action_count == 1);
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, false);
    CHECK(RunFrame(context, viewport, BuildAnchoredPortalTree));

    // A modal opened over a focused popover must restore focus to that portal when closed.
    modal_action_count = 0;
    CHECK(Lumen::OpenLayer(context, kModalTestLayer, Lumen::LayerKind::Modal,
        static_cast<std::uint8_t>(Lumen::DismissOnOutsideClick | Lumen::DismissOnEscape)));
    CHECK(RunFrame(context, viewport, BuildAnchoredPortalTree));
    CHECK(modal_action_count == 0);
    CHECK(Lumen::CloseLayer(context, kModalTestLayer));
    CHECK(RunFrame(context, viewport, BuildAnchoredPortalTree));
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, true);
    CHECK(RunFrame(context, viewport, BuildAnchoredPortalTree));
    CHECK(portal_action_count == 2); // Focus returns to the portal button, not the base anchor.
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, false);
    CHECK(RunFrame(context, viewport, BuildAnchoredPortalTree));

    // The actual portal child is also pointer-hittable and dispatches its normal click handler.
    Lumen::Rect portal_action_rect{};
    CHECK(Lumen::GetNodeRect(context, Lumen::MakeId(portal_id, "portal-action"), &portal_action_rect));
    ImGui::GetIO().AddMousePosEvent(portal_action_rect.Center().x, portal_action_rect.Center().y);
    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    CHECK(RunFrame(context, viewport, BuildAnchoredPortalTree));
    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    CHECK(RunFrame(context, viewport, BuildAnchoredPortalTree));
    CHECK(portal_action_count == 3);

    // Escape is a real default action handled by the engine's layer manager.
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, true);
    CHECK(RunFrame(context, viewport, BuildAnchoredPortalTree));
    CHECK(!Lumen::IsLayerOpen(context, kPortalTestLayer));
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, false);
    CHECK(RunFrame(context, viewport, BuildAnchoredPortalTree));
    CHECK(!Lumen::GetNodeRect(context, Lumen::MakeId(portal_id, "portal-copy"), &portal_copy_rect));

    // Clicking outside the portal dismisses it by policy.
    CHECK(Lumen::OpenLayer(context, kPortalTestLayer, Lumen::LayerKind::Popover,
        static_cast<std::uint8_t>(Lumen::DismissOnOutsideClick | Lumen::DismissOnEscape)));
    CHECK(RunFrame(context, viewport, BuildAnchoredPortalTree));
    ImGui::GetIO().AddMousePosEvent(viewport.bounds.max.x - 8.0f, viewport.bounds.max.y - 8.0f);
    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    CHECK(RunFrame(context, viewport, BuildAnchoredPortalTree));
    CHECK(!Lumen::IsLayerOpen(context, kPortalTestLayer));
    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    CHECK(RunFrame(context, viewport, BuildAnchoredPortalTree));

    Lumen::DestroyContext(context);
    ImGui::DestroyContext();
}

void TestModalFocusTrapScrimAndRestore() {
    ImGui::CreateContext();
    Lumen::Context* context = MakeContext();
    CHECK(context != nullptr);
    if (context == nullptr) { ImGui::DestroyContext(); return; }
    Lumen::SetReduceMotion(context, true);
    const Lumen::Viewport viewport{Lumen::Rect{20.0f, 20.0f, 520.0f, 360.0f}, {}, {1.0f, 1.0f}};
    background_action_count = 0;
    modal_action_count = 0;
    CHECK(RunFrame(context, viewport, BuildModalScopeTree));

    // Establish background focus before opening the modal.
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Tab, true);
    CHECK(RunFrame(context, viewport, BuildModalScopeTree));
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Tab, false);
    CHECK(RunFrame(context, viewport, BuildModalScopeTree));

    CHECK(Lumen::OpenLayer(context, kModalTestLayer, Lumen::LayerKind::Modal,
        static_cast<std::uint8_t>(Lumen::DismissOnOutsideClick | Lumen::DismissOnEscape)));
    CHECK(RunFrame(context, viewport, BuildModalScopeTree));
    CHECK(observed_draw_vertices > 100); // Includes the scrim and modal surface.

    ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, true);
    CHECK(RunFrame(context, viewport, BuildModalScopeTree));
    CHECK(modal_action_count == 1);
    CHECK(background_action_count == 0); // Focus traversal/activation is trapped in the modal scope.
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, false);
    CHECK(RunFrame(context, viewport, BuildModalScopeTree));

    CHECK(Lumen::CloseLayer(context, kModalTestLayer));
    CHECK(RunFrame(context, viewport, BuildModalScopeTree));
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, true);
    CHECK(RunFrame(context, viewport, BuildModalScopeTree));
    CHECK(background_action_count == 1); // The pre-modal focus target is restored.
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, false);
    CHECK(RunFrame(context, viewport, BuildModalScopeTree));

    // Outside click dismisses the modal but cannot activate the background in the same click.
    CHECK(Lumen::OpenLayer(context, kModalTestLayer, Lumen::LayerKind::Modal,
        static_cast<std::uint8_t>(Lumen::DismissOnOutsideClick | Lumen::DismissOnEscape)));
    CHECK(RunFrame(context, viewport, BuildModalScopeTree));
    const int background_before_outside_click = background_action_count;
    ImGui::GetIO().AddMousePosEvent(viewport.bounds.max.x - 12.0f, viewport.bounds.max.y - 12.0f);
    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    CHECK(RunFrame(context, viewport, BuildModalScopeTree));
    CHECK(!Lumen::IsLayerOpen(context, kModalTestLayer));
    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
    CHECK(RunFrame(context, viewport, BuildModalScopeTree));
    CHECK(background_action_count == background_before_outside_click);

    Lumen::DestroyContext(context);
    ImGui::DestroyContext();
}

void BuildPinnedToastTree(Lumen::Context* context) {
    if (!Lumen::IsLayerOpen(context, kPinnedToastTestLayer)) return;
    Lumen::PortalOptions options{};
    options.size = {180.0f, 70.0f};
    options.pin_to_top_right = true;
    options.edge_margin = 12.0f;
    Lumen::NodeStyle toast{};
    toast.layout = Lumen::LayoutMode::Row;
    toast.align_items = Lumen::Align::Center;
    toast.gap = 8.0f;
    toast.padding = {8.0f, 8.0f, 8.0f, 8.0f};
    toast.background = Lumen::GetTheme(context).surface;
    toast.has_background = true;
    toast.radius = 8.0f;
    if (Lumen::BeginPortal(context, kPinnedToastTestLayer, options, toast)) {
        CHECK(Lumen::Label(context, "pinned-toast-label", "Pinned to viewport"));
        CHECK(Lumen::Label(context, "pinned-toast-tail", "Status"));
        Lumen::EndPortal(context);
    }
}

void BuildInvalidPortalOptionsTree(Lumen::Context* context) {
    if (!Lumen::IsLayerOpen(context, kPinnedToastTestLayer)) return;
    const Lumen::FrameInfo* info = Lumen::GetFrameInfo(context);
    Lumen::PortalOptions options{};
    options.pin_to_top_right = true;
    options.edge_margin = std::numeric_limits<float>::quiet_NaN();
    if (Lumen::BeginPortal(context, kPinnedToastTestLayer, options)) {
        ++invalid_portal_accept_count;
        Lumen::EndPortal(context);
    }
    options.edge_margin = 8.0f;
    options.scrim = true; // A scrim is modal-only.
    if (Lumen::BeginPortal(context, kPinnedToastTestLayer, options)) {
        ++invalid_portal_accept_count;
        Lumen::EndPortal(context);
    }
    options.scrim = false;
    options.anchor_node_id = info != nullptr ? Lumen::MakeId(info->root_id, "missing-anchor") : 1u;
    options.pin_to_top_right = false;
    if (Lumen::BeginPortal(context, kPinnedToastTestLayer, options)) {
        ++invalid_portal_accept_count;
        Lumen::EndPortal(context);
    }
    options.pin_to_top_right = true; // Mutually exclusive positioning modes.
    if (Lumen::BeginPortal(context, kPinnedToastTestLayer, options)) {
        ++invalid_portal_accept_count;
        Lumen::EndPortal(context);
    }
}

void TestPinnedPortalTracksSafeViewport() {
    ImGui::CreateContext();
    Lumen::Context* context = MakeContext();
    CHECK(context != nullptr);
    if (context == nullptr) { ImGui::DestroyContext(); return; }
    Lumen::SetReduceMotion(context, true);
    const Lumen::Viewport first{Lumen::Rect{20.0f, 30.0f, 640.0f, 420.0f}, {10.0f, 18.0f, 14.0f, 22.0f}, {1.0f, 1.0f}};
    CHECK(Lumen::OpenLayer(context, kPinnedToastTestLayer, Lumen::LayerKind::Toast));
    CHECK(RunFrame(context, first, BuildPinnedToastTree));
    const std::uint64_t portal_id = Lumen::MakeId(Lumen::GetFrameInfo(context)->root_id, kPinnedToastTestLayer);
    Lumen::Rect toast_rect{};
    CHECK(Lumen::GetNodeRect(context, portal_id, &toast_rect));
    Lumen::Rect toast_label_rect{};
    CHECK(Lumen::GetNodeRect(context, Lumen::MakeId(portal_id, "pinned-toast-label"), &toast_label_rect));
    Lumen::Rect toast_tail_rect{};
    CHECK(Lumen::GetNodeRect(context, Lumen::MakeId(portal_id, "pinned-toast-tail"), &toast_tail_rect));
    CHECK(toast_label_rect.min.x >= toast_rect.min.x);
    CHECK(std::abs(toast_tail_rect.min.y - toast_label_rect.min.y) < 2.0f);
    CHECK(toast_tail_rect.min.x > toast_label_rect.max.x);
    CHECK(toast_rect.min.x >= first.bounds.min.x + first.safe_area.left + 10.0f);
    CHECK(toast_rect.max.x <= first.bounds.max.x - first.safe_area.right - 10.0f);
    CHECK(toast_rect.min.y >= first.bounds.min.y + first.safe_area.top + 10.0f);

    const Lumen::Viewport resized{Lumen::Rect{40.0f, 10.0f, 440.0f, 300.0f}, {4.0f, 16.0f, 8.0f, 12.0f}, {1.0f, 1.0f}};
    CHECK(RunFrame(context, resized, BuildPinnedToastTree));
    CHECK(Lumen::GetNodeRect(context, portal_id, &toast_rect));
    CHECK(toast_rect.max.x <= resized.bounds.max.x - resized.safe_area.right - 10.0f);
    CHECK(toast_rect.min.y >= resized.bounds.min.y + resized.safe_area.top + 10.0f);

    CHECK(Lumen::CloseLayer(context, kPinnedToastTestLayer));
    CHECK(RunFrame(context, resized, BuildPinnedToastTree));
    CHECK(!Lumen::GetNodeRect(context, portal_id, &toast_rect));

    // Reject malformed placement policy before any frame-arena node is allocated.
    CHECK(Lumen::OpenLayer(context, kPinnedToastTestLayer, Lumen::LayerKind::Toast));
    invalid_portal_accept_count = 0;
    CHECK(RunFrame(context, resized, BuildInvalidPortalOptionsTree));
    CHECK(invalid_portal_accept_count == 0);
    CHECK(Lumen::CloseLayer(context, kPinnedToastTestLayer));
    Lumen::DestroyContext(context);
    ImGui::DestroyContext();
}


void TestPortalTimeoutUsesExitTransition() {
    ImGui::CreateContext();
    Lumen::ManualClock clock(1.0);
    Lumen::Context* context = MakeContext(&clock);
    CHECK(context != nullptr);
    if (context == nullptr) { ImGui::DestroyContext(); return; }
    Lumen::Theme theme = Lumen::GetTheme(context);
    theme.color_transition.duration_seconds = 0.2;
    Lumen::SetTheme(context, theme);
    const Lumen::Viewport viewport{Lumen::Rect{0.0f, 0.0f, 640.0f, 480.0f}, {}, {1.0f, 1.0f}};
    CHECK(RunFrame(context, viewport, nullptr));

    CHECK(Lumen::OpenLayer(context, kPinnedToastTestLayer, Lumen::LayerKind::Toast,
        Lumen::DismissOnTimeout, 0.1));
    CHECK(RunFrame(context, viewport, BuildTimedToastPortalTree));
    const std::uint64_t portal_id = Lumen::MakeId(Lumen::GetFrameInfo(context)->root_id, kPinnedToastTestLayer);
    Lumen::Rect portal_rect{};
    CHECK(Lumen::GetNodeRect(context, portal_id, &portal_rect));

    clock.Set(1.11); // Timeout starts an exit transition rather than deleting the layer immediately.
    CHECK(RunFrame(context, viewport, BuildTimedToastPortalTree));
    CHECK(Lumen::IsLayerOpen(context, kPinnedToastTestLayer));
    CHECK(Lumen::GetNodeRect(context, portal_id, &portal_rect));
    CHECK(Lumen::NeedsFrame(context));

    clock.Set(1.22);
    CHECK(RunFrame(context, viewport, BuildTimedToastPortalTree));
    CHECK(Lumen::IsLayerOpen(context, kPinnedToastTestLayer));

    clock.Set(1.34);
    CHECK(RunFrame(context, viewport, BuildTimedToastPortalTree));
    CHECK(!Lumen::IsLayerOpen(context, kPinnedToastTestLayer));
    CHECK(!Lumen::GetNodeRect(context, portal_id, &portal_rect));

    Lumen::DestroyContext(context);
    ImGui::DestroyContext();
}

void TestDuplicateIdsRejectFrame() {
    ImGui::CreateContext();
    Lumen::Context* context = MakeContext();
    CHECK(context != nullptr);
    if (context == nullptr) { ImGui::DestroyContext(); return; }
    const Lumen::Viewport viewport{Lumen::Rect{0.0f, 0.0f, 320.0f, 200.0f}, {}, {1.0f, 1.0f}};
    bool ended = true;
    CHECK(!RunFrame(context, viewport, BuildDuplicateTree, &ended));
    CHECK(!ended);
    CHECK(Lumen::GetDebugStats(context).duplicate_node_ids == 1u);
    Lumen::DestroyContext(context);
    ImGui::DestroyContext();
}

} // namespace

bool shadow_enabled = false;
float shadow_blur_value = 16.0f;

void BuildShadowPanel(Lumen::Context* context) {
    Lumen::NodeStyle panel{};
    panel.width = 200.0f;
    panel.height = 100.0f;
    panel.background = {0.2f, 0.3f, 0.4f, 1.0f};
    panel.has_background = true;
    panel.radius = 12.0f;
    // Deliberately a stack local: BeginNode must copy it, not keep the pointer (ASan guards this).
    Lumen::ShadowStyle shadow{};
    shadow.blur = shadow_blur_value;
    shadow.offset_y = 6.0f;
    if (shadow_enabled) panel.shadow = &shadow;
    CHECK(Lumen::BeginNode(context, "shadow-panel", panel));
    Lumen::EndNode(context);
}

Lumen::ManualClock* shadow_clock = nullptr;

int SettledVertexCount(Lumen::Context* context, const Lumen::Viewport& viewport) {
    // 30 frames at 60 Hz of manual-clock time lets colour/blur tweens (and theme crossfades) settle.
    for (int frame = 0; frame < 30; ++frame) {
        if (shadow_clock != nullptr) shadow_clock->Advance(1.0 / 60.0);
        CHECK(RunFrame(context, viewport, BuildShadowPanel));
    }
    return observed_draw_vertices;
}

void TestDropShadow() {
    ImGui::CreateContext();
    Lumen::ManualClock clock(100.0);
    shadow_clock = &clock;
    Lumen::Context* context = MakeContext(&clock);
    CHECK(context != nullptr);
    if (context == nullptr) { ImGui::DestroyContext(); return; }
    const Lumen::Viewport viewport{Lumen::Rect{0.0f, 0.0f, 400.0f, 300.0f}, {}, {1.0f, 1.0f}};

    // Dark theme: a shadow adds geometry underneath the node, and removing it removes that geometry.
    Lumen::SetTheme(context, Lumen::ThemeKind::Dark);
    shadow_enabled = false;
    const int plain = SettledVertexCount(context, viewport);
    shadow_enabled = true;
    const int shadowed = SettledVertexCount(context, viewport);
    CHECK(shadowed > plain + 100);
    shadow_enabled = false;
    CHECK(SettledVertexCount(context, viewport) == plain);

    // Zero blur is the same as no shadow.
    shadow_enabled = true;
    shadow_blur_value = 0.0f;
    CHECK(SettledVertexCount(context, viewport) == plain);
    shadow_blur_value = 16.0f;

    // The High Contrast theme token is fully transparent, so themed shadows are skipped entirely.
    Lumen::SetTheme(context, Lumen::ThemeKind::HighContrast);
    shadow_enabled = false;
    const int hc_plain = SettledVertexCount(context, viewport);
    shadow_enabled = true;
    CHECK(SettledVertexCount(context, viewport) == hc_plain);

    // A custom theme shadow token is honoured.
    Lumen::Theme custom = Lumen::GetTheme(context);
    custom.shadow = {0.0f, 0.0f, 0.0f, 0.5f};
    Lumen::SetTheme(context, custom);
    CHECK(SettledVertexCount(context, viewport) > hc_plain + 100);

    shadow_enabled = false;
    shadow_clock = nullptr;
    Lumen::DestroyContext(context);
    ImGui::DestroyContext();
}

struct PainterProbe {
    int calls = 0;
    std::uint64_t node_id = 0u;
    Lumen::Rect bounds{};
    float opacity = 0.0f;
    bool node_declared = false;
    bool portal_declared = false;
    bool state_available = false;
    bool null_painter = false;
    bool hidden = false;
};

PainterProbe painter_probe{};
constexpr ImU32 kPainterFill = IM_COL32(17, 201, 93, 255);
constexpr ImU32 kPainterNodeFill = IM_COL32(40, 60, 80, 255);
constexpr ImU32 kPainterChildFill = IM_COL32(90, 30, 140, 255);

void RecordingPainter(Lumen::Context* context, Lumen::Canvas& canvas, void* user_data) {
    auto* probe = static_cast<PainterProbe*>(user_data);
    ++probe->calls;
    probe->node_id = canvas.NodeId();
    probe->bounds = canvas.Bounds();
    probe->opacity = canvas.Opacity();
    probe->node_declared = Lumen::BeginNode(context, "from-painter");
    Lumen::PortalOptions options{};
    probe->portal_declared = Lumen::BeginPortal(context, kPortalTestLayer, options);
    probe->state_available = Lumen::State<int>(context, Lumen::MakeId(canvas.NodeId(), "painter-state")) != nullptr;
    canvas.FillRect(canvas.Bounds(), {17.0f / 255.0f, 201.0f / 255.0f, 93.0f / 255.0f, 1.0f});
    canvas.FillCircle({canvas.Bounds().min.x + 10.0f, canvas.Bounds().min.y + 10.0f}, std::numeric_limits<float>::quiet_NaN(), {1.0f, 1.0f, 1.0f, 1.0f});
    CHECK(canvas.PushClip(canvas.Bounds()));
    CHECK(!canvas.PushClip({{std::numeric_limits<float>::infinity(), 0.0f}, {1.0f, 1.0f}}));
}

void BuildPaintedNode(Lumen::Context* context) {
    Lumen::CustomPaint paint{};
    paint.painter = painter_probe.null_painter ? nullptr : RecordingPainter;
    paint.user_data = &painter_probe;
    Lumen::NodeStyle style{};
    style.width = 120.0f;
    style.height = 80.0f;
    style.radius = 0.0f;
    style.opacity = painter_probe.hidden ? 0.0f : 0.5f;
    style.animate_layout = false;
    style.has_background = true;
    style.background = {40.0f / 255.0f, 60.0f / 255.0f, 80.0f / 255.0f, 1.0f};
    style.custom_paint = &paint;
    CHECK(Lumen::BeginNode(context, "painted", style));
    Lumen::NodeStyle child{};
    child.width = 20.0f;
    child.height = 20.0f;
    child.has_background = true;
    child.background = {90.0f / 255.0f, 30.0f / 255.0f, 140.0f / 255.0f, 1.0f};
    CHECK(Lumen::BeginNode(context, "child", child));
    Lumen::EndNode(context);
    Lumen::EndNode(context);
}

// Index of the first vertex whose RGB matches the colour, or -1.
int FirstVertexWithRgb(const ImDrawList* draw_list, ImU32 color) {
    constexpr ImU32 rgb = ~IM_COL32_A_MASK;
    for (int i = 0; i < draw_list->VtxBuffer.Size; ++i) {
        if ((draw_list->VtxBuffer[i].col & rgb) == (color & rgb)) return i;
    }
    return -1;
}

void TestCustomPainter() {
    ImGui::CreateContext();
    Lumen::Context* context = MakeContext();
    CHECK(context != nullptr);
    if (context == nullptr) { ImGui::DestroyContext(); return; }
    const Lumen::Viewport viewport{Lumen::Rect{0.0f, 0.0f, 400.0f, 300.0f}, {}, {1.0f, 1.0f}};
    painter_probe = {};
    {
        HostFrame host;
        CHECK(Lumen::BeginFrame(context, viewport, 1.0f / 60.0f));
        BuildPaintedNode(context);
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        const int clip_depth = draw_list->_ClipRectStack.Size;
        CHECK(Lumen::EndFrame(context));
        CHECK(draw_list->_ClipRectStack.Size == clip_depth);

        const int node_fill = FirstVertexWithRgb(draw_list, kPainterNodeFill);
        const int painted = FirstVertexWithRgb(draw_list, kPainterFill);
        const int child_fill = FirstVertexWithRgb(draw_list, kPainterChildFill);
        CHECK(node_fill >= 0 && painted > node_fill && child_fill > painted);
        if (painted >= 0) {
            const ImU32 alpha = (draw_list->VtxBuffer[painted].col >> IM_COL32_A_SHIFT) & 0xFFu;
            CHECK(alpha >= 126u && alpha <= 129u);
        }
    }
    const std::uint64_t painted_id = Lumen::MakeId(Lumen::GetFrameInfo(context)->root_id, "painted");
    Lumen::Rect painted_rect{};
    CHECK(Lumen::GetNodeRect(context, painted_id, &painted_rect));
    CHECK(painter_probe.calls == 1);
    CHECK(painter_probe.node_id == painted_id);
    CHECK(std::abs(painter_probe.bounds.min.x - painted_rect.min.x) < 0.01f);
    CHECK(std::abs(painter_probe.bounds.Width() - 120.0f) < 0.01f);
    CHECK(std::abs(painter_probe.bounds.Height() - 80.0f) < 0.01f);
    CHECK(std::abs(painter_probe.opacity - 0.5f) < 0.001f);
    CHECK(!painter_probe.node_declared);
    CHECK(!painter_probe.portal_declared);
    CHECK(painter_probe.state_available);
    CHECK(Lumen::GetDebugStats(context).nodes_built == 2u);

    // A CustomPaint without a painter and a fully transparent node are both skipped.
    painter_probe = {};
    painter_probe.null_painter = true;
    CHECK(RunFrame(context, viewport, BuildPaintedNode));
    CHECK(painter_probe.calls == 0);
    painter_probe = {};
    painter_probe.hidden = true;
    CHECK(RunFrame(context, viewport, BuildPaintedNode));
    CHECK(painter_probe.calls == 0);

    painter_probe = {};
    Lumen::DestroyContext(context);
    ImGui::DestroyContext();
}

void BuildIntrinsicRowLabel(Lumen::Context* context) {
    Lumen::NodeStyle row{};
    row.layout = Lumen::LayoutMode::Row;
    row.align_items = Lumen::Align::Center;
    row.padding = {0.0f, 0.0f, 0.0f, 40.0f};
    CHECK(Lumen::BeginNode(context, "row", row));
    Lumen::NodeStyle text{};
    text.font_size = 16.0f;
    text.has_text_color = true;
    text.text_color = {17.0f / 255.0f, 201.0f / 255.0f, 93.0f / 255.0f, 1.0f};
    CHECK(Lumen::Label(context, "label", "Reduce motion (applies to the whole engine)", text));
    Lumen::EndNode(context);
}

// A label laid out at exactly its measured width must render on one line; ImGui's word wrap breaks
// the last word when the wrap width equals the text width.
void TestIntrinsicWidthLabelDoesNotWrap() {
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    CHECK(io.Fonts->AddFontFromFileTTF(LUMEN_SOURCE_DIR "/imgui/misc/fonts/Roboto-Medium.ttf", 16.0f) != nullptr);
    Lumen::Context* context = MakeContext();
    CHECK(context != nullptr);
    if (context == nullptr) { ImGui::DestroyContext(); return; }
    const Lumen::Viewport viewport{Lumen::Rect{0.0f, 0.0f, 600.0f, 200.0f}, {}, {1.0f, 1.0f}};
    float min_y = 1.0e9f;
    float max_y = -1.0e9f;
    for (int frame = 0; frame < 2; ++frame) {
        HostFrame host;
        CHECK(Lumen::BeginFrame(context, viewport, 1.0f / 60.0f));
        BuildIntrinsicRowLabel(context);
        CHECK(Lumen::EndFrame(context));
        if (frame == 0) continue;
        const ImDrawList* draw_list = ImGui::GetWindowDrawList();
        for (int i = 0; i < draw_list->VtxBuffer.Size; ++i) {
            const ImU32 col = draw_list->VtxBuffer[i].col;
            const auto near = [](ImU32 value, int expected) { return std::abs(static_cast<int>(value & 0xFFu) - expected) <= 2; };
            if (!near(col >> IM_COL32_R_SHIFT, 17) || !near(col >> IM_COL32_G_SHIFT, 201) || !near(col >> IM_COL32_B_SHIFT, 93)) continue;
            min_y = std::min(min_y, draw_list->VtxBuffer[i].pos.y);
            max_y = std::max(max_y, draw_list->VtxBuffer[i].pos.y);
        }
    }
    CHECK(max_y > min_y);
    CHECK(max_y - min_y < 1.5f * 16.0f);
    Lumen::DestroyContext(context);
    ImGui::DestroyContext();
}

int container_clicks = 0;

void CountContainerClick(Lumen::Context*, Lumen::UIEvent& event, void*) {
    if (event.type == Lumen::UIEventType::Click && event.phase == Lumen::UIEventPhase::Target) ++container_clicks;
}

void BuildFocusableContainer(Lumen::Context* context) {
    Lumen::NodeStyle card{};
    card.width = 120.0f;
    card.height = 40.0f;
    card.focusable = true;
    card.event_handler = CountContainerClick;
    CHECK(Lumen::BeginNode(context, "focus-card", card));
    Lumen::EndNode(context);
}

void TestKeyboardActivatesFocusableContainer() {
    ImGui::CreateContext();
    Lumen::Context* context = MakeContext();
    CHECK(context != nullptr);
    if (context == nullptr) { ImGui::DestroyContext(); return; }
    const Lumen::Viewport viewport{Lumen::Rect{0.0f, 0.0f, 320.0f, 220.0f}, {}, {1.0f, 1.0f}};
    container_clicks = 0;
    CHECK(RunFrame(context, viewport, BuildFocusableContainer));
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Tab, true);
    CHECK(RunFrame(context, viewport, BuildFocusableContainer));
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Tab, false);
    CHECK(RunFrame(context, viewport, BuildFocusableContainer));
    const std::uint64_t card_id = Lumen::MakeId(Lumen::GetFrameInfo(context)->root_id, "focus-card");
    CHECK(Lumen::GetNodeState(context, card_id).focused);
    CHECK(container_clicks == 0);

    ImGui::GetIO().AddKeyEvent(ImGuiKey_Space, true);
    CHECK(RunFrame(context, viewport, BuildFocusableContainer));
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Space, false);
    CHECK(RunFrame(context, viewport, BuildFocusableContainer));
    CHECK(container_clicks == 1);
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, true);
    CHECK(RunFrame(context, viewport, BuildFocusableContainer));
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter, false);
    CHECK(RunFrame(context, viewport, BuildFocusableContainer));
    CHECK(container_clicks == 2);
    Lumen::DestroyContext(context);
    ImGui::DestroyContext();
}

constexpr std::uint64_t kPinnedPortalLayer = 0x4C554D454E7A010ull;
Lumen::PortalPin pinned_portal_pin = Lumen::PortalPin::BottomCenter;
Lumen::Vec2 pinned_portal_offset{};
bool pinned_portal_accepted = false;

void BuildPinnedContentPortal(Lumen::Context* context) {
    Lumen::NodeStyle anchor{};
    anchor.width = 40.0f;
    anchor.height = 20.0f;
    CHECK(Lumen::BeginNode(context, "anchor", anchor));
    Lumen::EndNode(context);
    if (!Lumen::IsLayerOpen(context, kPinnedPortalLayer)) return;
    Lumen::PortalOptions bad{};
    bad.pin = Lumen::PortalPin::BottomRight;
    bad.anchor_node_id = Lumen::MakeId(Lumen::GetFrameInfo(context)->root_id, "anchor");
    CHECK(!Lumen::BeginPortal(context, kPinnedPortalLayer, bad));
    Lumen::PortalOptions options{};
    options.size = {200.0f, 0.0f};
    options.pin = pinned_portal_pin;
    options.offset = pinned_portal_offset;
    options.edge_margin = 24.0f;
    Lumen::NodeStyle panel{};
    panel.padding = {10.0f, 10.0f, 10.0f, 10.0f};
    pinned_portal_accepted = Lumen::BeginPortal(context, kPinnedPortalLayer, options, panel);
    if (pinned_portal_accepted) {
        Lumen::NodeStyle block{};
        block.height = 50.0f;
        CHECK(Lumen::BeginNode(context, "block", block));
        Lumen::EndNode(context);
        Lumen::EndPortal(context);
    }
}

void TestPortalPinsOffsetAndContentHeight() {
    ImGui::CreateContext();
    Lumen::ManualClock clock(20.0);
    Lumen::Context* context = MakeContext(&clock);
    CHECK(context != nullptr);
    if (context == nullptr) { ImGui::DestroyContext(); return; }
    const Lumen::Viewport viewport{Lumen::Rect{0.0f, 0.0f, 600.0f, 400.0f}, {}, {1.0f, 1.0f}};
    CHECK(Lumen::OpenLayer(context, kPinnedPortalLayer, Lumen::LayerKind::Toast));
    const auto portal_rect = [&]() {
        Lumen::Rect rect{};
        CHECK(Lumen::GetNodeRect(context, Lumen::MakeId(Lumen::GetFrameInfo(context)->root_id, kPinnedPortalLayer), &rect));
        return rect;
    };

    // Content height: 10 + 50 + 10. BottomCenter sits edge_margin above the bottom, centred.
    pinned_portal_pin = Lumen::PortalPin::BottomCenter;
    pinned_portal_offset = {};
    clock.Advance(1.0 / 60.0);
    CHECK(RunFrame(context, viewport, BuildPinnedContentPortal));
    CHECK(pinned_portal_accepted);
    Lumen::Rect rect = portal_rect();
    CHECK(std::abs(rect.Height() - 70.0f) < 0.01f);
    CHECK(std::abs(rect.max.y - 376.0f) < 0.01f && std::abs(rect.min.x - 200.0f) < 0.01f);

    // The offset moves pinned portals without re-clamping, so content can slide in from an edge.
    pinned_portal_offset = {0.0f, 30.0f};
    CHECK(RunFrame(context, viewport, BuildPinnedContentPortal));
    rect = portal_rect();
    CHECK(std::abs(rect.max.y - 406.0f) < 0.01f);

    pinned_portal_offset = {};
    pinned_portal_pin = Lumen::PortalPin::TopLeft;
    CHECK(RunFrame(context, viewport, BuildPinnedContentPortal));
    rect = portal_rect();
    CHECK(std::abs(rect.min.x - 24.0f) < 0.01f && std::abs(rect.min.y - 24.0f) < 0.01f);
    pinned_portal_pin = Lumen::PortalPin::BottomRight;
    CHECK(RunFrame(context, viewport, BuildPinnedContentPortal));
    rect = portal_rect();
    CHECK(std::abs(rect.max.x - 576.0f) < 0.01f && std::abs(rect.max.y - 376.0f) < 0.01f);
    Lumen::DestroyContext(context);
    ImGui::DestroyContext();
}

void TestLayerVisibilityAndClosing() {
    ImGui::CreateContext();
    Lumen::ManualClock clock(30.0);
    Lumen::Context* context = MakeContext(&clock);
    CHECK(context != nullptr);
    if (context == nullptr) { ImGui::DestroyContext(); return; }
    const Lumen::Viewport viewport{Lumen::Rect{0.0f, 0.0f, 600.0f, 400.0f}, {}, {1.0f, 1.0f}};
    pinned_portal_pin = Lumen::PortalPin::Center;
    pinned_portal_offset = {};
    CHECK(Lumen::GetLayerVisibility(context, kPinnedPortalLayer) == 0.0f);
    CHECK(!Lumen::IsLayerClosing(context, kPinnedPortalLayer));
    clock.Advance(1.0 / 60.0);
    CHECK(RunFrame(context, viewport, BuildPinnedContentPortal));
    CHECK(Lumen::OpenLayer(context, kPinnedPortalLayer, Lumen::LayerKind::Popover));
    float previous = Lumen::GetLayerVisibility(context, kPinnedPortalLayer);
    for (int i = 0; i < 30; ++i) {
        clock.Advance(1.0 / 60.0);
        CHECK(RunFrame(context, viewport, BuildPinnedContentPortal));
        const float visibility = Lumen::GetLayerVisibility(context, kPinnedPortalLayer);
        CHECK(visibility >= previous);
        previous = visibility;
    }
    CHECK(std::abs(previous - 1.0f) < 0.001f);
    CHECK(!Lumen::IsLayerClosing(context, kPinnedPortalLayer));

    CHECK(Lumen::CloseLayer(context, kPinnedPortalLayer));
    CHECK(Lumen::IsLayerClosing(context, kPinnedPortalLayer));
    clock.Advance(1.0 / 60.0);
    CHECK(RunFrame(context, viewport, BuildPinnedContentPortal));
    CHECK(Lumen::GetLayerVisibility(context, kPinnedPortalLayer) < 1.0f);
    for (int i = 0; i < 40; ++i) {
        clock.Advance(1.0 / 60.0);
        CHECK(RunFrame(context, viewport, BuildPinnedContentPortal));
    }
    CHECK(!Lumen::IsLayerOpen(context, kPinnedPortalLayer));
    CHECK(!Lumen::IsLayerClosing(context, kPinnedPortalLayer));
    CHECK(Lumen::GetLayerVisibility(context, kPinnedPortalLayer) == 0.0f);
    Lumen::DestroyContext(context);
    ImGui::DestroyContext();
}

float ladder_label_size = 16.0f;

void BuildLadderLabel(Lumen::Context* context) {
    Lumen::NodeStyle text{};
    text.font_size = ladder_label_size;
    text.align_self = Lumen::Align::Start;
    text.animate_layout = false;
    CHECK(Lumen::Label(context, "ladder", "Ladder", text));
}

void TestFontLadder() {
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    ImFont* small = io.Fonts->AddFontFromFileTTF(LUMEN_SOURCE_DIR "/imgui/misc/fonts/Roboto-Medium.ttf", 14.0f);
    ImFont* large = io.Fonts->AddFontFromFileTTF(LUMEN_SOURCE_DIR "/imgui/misc/fonts/Roboto-Medium.ttf", 24.0f);
    CHECK(small != nullptr && large != nullptr);
    Lumen::Context* context = MakeContext();
    CHECK(context != nullptr);
    if (context == nullptr || small == nullptr || large == nullptr) { if (context) Lumen::DestroyContext(context); ImGui::DestroyContext(); return; }
    const Lumen::Viewport viewport{Lumen::Rect{0.0f, 0.0f, 400.0f, 200.0f}, {}, {1.0f, 1.0f}};
    const auto label_height = [&](float size) {
        ladder_label_size = size;
        CHECK(RunFrame(context, viewport, BuildLadderLabel));
        Lumen::Rect rect{};
        CHECK(Lumen::GetNodeRect(context, Lumen::MakeId(Lumen::GetFrameInfo(context)->root_id, "ladder"), &rect));
        return rect.Height();
    };

    // Without a ladder text scales the current font to any size.
    CHECK(std::abs(label_height(20.0f) - 20.0f) < 0.01f);

    ImFont* ladder[2] = {small, large};
    CHECK(Lumen::SetFontLadder(context, ladder, 2u));
    CHECK(std::abs(label_height(20.0f) - 24.0f) < 0.01f);
    CHECK(std::abs(label_height(16.0f) - 14.0f) < 0.01f);
    CHECK(std::abs(label_height(30.0f) - 24.0f) < 0.01f);

    ImFont* invalid[2] = {small, nullptr};
    CHECK(!Lumen::SetFontLadder(context, invalid, 2u));
    ImFont* too_many[9] = {small, small, small, small, small, small, small, small, small};
    CHECK(!Lumen::SetFontLadder(context, too_many, 9u));
    CHECK(!Lumen::SetFontLadder(context, nullptr, 1u));
    CHECK(std::abs(label_height(16.0f) - 14.0f) < 0.01f);
    CHECK(Lumen::SetFontLadder(context, nullptr, 0u));
    CHECK(std::abs(label_height(20.0f) - 20.0f) < 0.01f);
    Lumen::DestroyContext(context);
    ImGui::DestroyContext();
}

std::uint64_t observed_current_ids[3]{};

void PathPainter(Lumen::Context*, Lumen::Canvas& canvas, void*) {
    const Lumen::Rect b = canvas.Bounds();
    canvas.BeginPath();
    canvas.MoveTo({b.min.x + 4.0f, b.min.y + 4.0f});
    canvas.LineTo({b.max.x - 4.0f, b.min.y + 4.0f});
    canvas.CubicTo({b.max.x, b.min.y + 20.0f}, {b.min.x, b.min.y + 30.0f}, {b.max.x - 4.0f, b.max.y - 4.0f});
    canvas.Stroke({17.0f / 255.0f, 201.0f / 255.0f, 93.0f / 255.0f, 1.0f}, 2.0f);
    canvas.BeginPath();
    canvas.MoveTo({b.min.x + 10.0f, b.max.y - 10.0f});
    canvas.LineTo({b.min.x + 30.0f, b.max.y - 10.0f});
    canvas.QuadTo({b.min.x + 20.0f, b.max.y - 40.0f}, {b.min.x + 10.0f, b.max.y - 10.0f});
    canvas.Fill({90.0f / 255.0f, 30.0f / 255.0f, 140.0f / 255.0f, 1.0f});
    // Degenerate paths are ignored without touching the draw list state.
    canvas.MoveTo({b.min.x, b.min.y});
    canvas.Stroke({1.0f, 1.0f, 1.0f, 1.0f}, 2.0f);
    canvas.LineTo({std::numeric_limits<float>::quiet_NaN(), 0.0f});
    canvas.Fill({1.0f, 1.0f, 1.0f, 1.0f});
    canvas.BeginPath();
    canvas.ArcTo({b.min.x + 50.0f, b.min.y + 40.0f}, -3.0f, 0.0f, 3.0f);
    canvas.Stroke({1.0f, 1.0f, 1.0f, 1.0f}, 2.0f);
}

void BuildPathAndIdScene(Lumen::Context* context) {
    observed_current_ids[0] = Lumen::GetCurrentNodeId(context);
    const Lumen::CustomPaint paint{PathPainter, nullptr};
    Lumen::NodeStyle style{};
    style.width = 120.0f;
    style.height = 80.0f;
    style.custom_paint = &paint;
    CHECK(Lumen::BeginNode(context, "paths", style));
    observed_current_ids[1] = Lumen::GetCurrentNodeId(context);
    Lumen::EndNode(context);
    observed_current_ids[2] = Lumen::GetCurrentNodeId(context);
    CHECK(Lumen::Label(context, "measured", "Measure me"));
}

int CountRgb(const ImDrawList* draw_list, int r, int g, int b) {
    int count = 0;
    for (int i = 0; i < draw_list->VtxBuffer.Size; ++i) {
        const ImU32 col = draw_list->VtxBuffer[i].col;
        if (static_cast<int>((col >> IM_COL32_R_SHIFT) & 0xFFu) == r && static_cast<int>((col >> IM_COL32_G_SHIFT) & 0xFFu) == g &&
            static_cast<int>((col >> IM_COL32_B_SHIFT) & 0xFFu) == b && ((col >> IM_COL32_A_SHIFT) & 0xFFu) > 0u) ++count;
    }
    return count;
}

void TestCanvasPathsMeasureAndCurrentId() {
    ImGui::CreateContext();
    Lumen::Context* context = MakeContext();
    CHECK(context != nullptr);
    if (context == nullptr) { ImGui::DestroyContext(); return; }
    const Lumen::Viewport viewport{Lumen::Rect{0.0f, 0.0f, 400.0f, 300.0f}, {}, {1.0f, 1.0f}};
    CHECK(Lumen::GetCurrentNodeId(context) == 0u);
    {
        HostFrame host;
        CHECK(Lumen::BeginFrame(context, viewport, 1.0f / 60.0f));
        BuildPathAndIdScene(context);
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        const int clip_depth = draw_list->_ClipRectStack.Size;
        CHECK(Lumen::EndFrame(context));
        CHECK(draw_list->_ClipRectStack.Size == clip_depth);
        CHECK(draw_list->_Path.Size == 0);
        CHECK(CountRgb(draw_list, 17, 201, 93) > 20);
        CHECK(CountRgb(draw_list, 90, 30, 140) > 6);
        CHECK(CountRgb(draw_list, 255, 255, 255) == 0);
    }
    const std::uint64_t root = Lumen::GetFrameInfo(context)->root_id;
    CHECK(observed_current_ids[0] == root);
    CHECK(observed_current_ids[1] == Lumen::MakeId(root, "paths"));
    CHECK(observed_current_ids[2] == root);
    CHECK(Lumen::GetCurrentNodeId(context) == 0u);

    // Public measurement matches node layout and supports ranges.
    Lumen::Rect label{};
    CHECK(Lumen::GetNodeRect(context, Lumen::MakeId(root, "measured"), &label));
    const Lumen::Vec2 whole = Lumen::MeasureText(context, "Measure me", 16.0f);
    CHECK(std::abs(whole.y - label.Height()) < 0.01f);
    const char* text = "Measure me";
    const Lumen::Vec2 prefix = Lumen::MeasureText(context, text, 16.0f, 0.0f, text + 7);
    CHECK(prefix.x > 0.0f && prefix.x < whole.x);
    CHECK(Lumen::MeasureText(context, nullptr, 16.0f).x == 0.0f);
    CHECK(Lumen::MeasureText(context, text, -4.0f).x == 0.0f);

    // Host clipboard round trip through the context's ImGui handlers.
    Lumen::SetClipboardText(context, "copied");
    const char* clip = Lumen::GetClipboardText(context);
    CHECK(clip != nullptr && std::strcmp(clip, "copied") == 0);
    Lumen::DestroyContext(context);
    ImGui::DestroyContext();
}

int wide_cell_count = 10000;

void BuildWideList(Lumen::Context* context) {
    Lumen::NodeStyle list{};
    list.height = 200.0f;
    list.scrollable_y = true;
    list.clip_children = true;
    if (!Lumen::BeginNode(context, "wide-list", list)) return;
    for (int i = 1; i <= wide_cell_count; ++i) {
        if (!Lumen::Label(context, static_cast<std::uint64_t>(i), "row")) break;
    }
    Lumen::EndNode(context);
}

// The frame arena grows past its first chunk for a large frame, keeps the chunks so the next
// identical frame reserves nothing new, and still fails cleanly at the configured cap.
void TestFrameArenaGrowsAndHonoursCap() {
    ImGui::CreateContext();
    Lumen::Context* context = MakeContext();
    CHECK(context != nullptr);
    if (context == nullptr) { ImGui::DestroyContext(); return; }
    const Lumen::Viewport viewport{Lumen::Rect{0.0f, 0.0f, 400.0f, 300.0f}, {}, {1.0f, 1.0f}};
    wide_cell_count = 10000;
    CHECK(RunFrame(context, viewport, BuildWideList));
    Lumen::DebugStats stats = Lumen::GetDebugStats(context);
    CHECK(stats.nodes_built == 10001u);
    CHECK(stats.arena_failed_allocations == 0u);
    CHECK(stats.arena_bytes_used > 1024u * 1024u);
    CHECK(stats.arena_reserved_bytes >= stats.arena_bytes_used);
    CHECK(stats.arena_reserved_bytes <= 16u * 1024u * 1024u);
    const std::size_t reserved = stats.arena_reserved_bytes;
    CHECK(RunFrame(context, viewport, BuildWideList));
    stats = Lumen::GetDebugStats(context);
    CHECK(stats.arena_reserved_bytes == reserved);
    CHECK(stats.arena_failed_allocations == 0u);
    // A small frame after a large one reuses the first chunk.
    wide_cell_count = 10;
    CHECK(RunFrame(context, viewport, BuildWideList));
    stats = Lumen::GetDebugStats(context);
    CHECK(stats.arena_bytes_used < 64u * 1024u && stats.arena_reserved_bytes == reserved);
    Lumen::DestroyContext(context);

    Lumen::Config capped{};
    capped.frame_arena_max_bytes = capped.frame_arena_bytes;
    context = Lumen::CreateContext(capped);
    CHECK(context != nullptr);
    if (context == nullptr) { ImGui::DestroyContext(); return; }
    wide_cell_count = 10000;
    bool ended = true;
    RunFrame(context, viewport, BuildWideList, &ended);
    stats = Lumen::GetDebugStats(context);
    CHECK(!ended);
    CHECK(stats.arena_failed_allocations > 0u);
    CHECK(stats.arena_reserved_bytes == capped.frame_arena_bytes);
    wide_cell_count = 10000;
    Lumen::DestroyContext(context);
    ImGui::DestroyContext();
}

void BuildSmoothList(Lumen::Context* context) {
    Lumen::NodeStyle list{};
    list.height = 200.0f;
    list.width = 300.0f;
    list.scrollable_y = true;
    list.clip_children = true;
    list.gap = 0.0f;
    list.animate_layout = false;
    CHECK(Lumen::BeginNode(context, "smooth", list));
    for (std::uint64_t i = 0; i < 40u; ++i) {
        Lumen::NodeStyle row{};
        row.height = 30.0f;
        row.animate_layout = false;
        CHECK(Lumen::BeginNode(context, i, row));
        Lumen::EndNode(context);
    }
    Lumen::EndNode(context);
}

struct ThumbSample {
    int vertices = 0;
    Lumen::Rect bounds{{1.0e9f, 1.0e9f}, {-1.0e9f, -1.0e9f}};
};

ThumbSample RunSmoothFrame(Lumen::Context* context, Lumen::ManualClock& clock, double step) {
    clock.Advance(step);
    const Lumen::Viewport viewport{Lumen::Rect{0.0f, 0.0f, 400.0f, 300.0f}, {}, {1.0f, 1.0f}};
    ThumbSample sample{};
    HostFrame host;
    CHECK(Lumen::BeginFrame(context, viewport, static_cast<float>(step)));
    BuildSmoothList(context);
    CHECK(Lumen::EndFrame(context));
    const ImDrawList* draw_list = ImGui::GetWindowDrawList();
    for (int i = 0; i < draw_list->VtxBuffer.Size; ++i) {
        const ImDrawVert& v = draw_list->VtxBuffer[i];
        if ((v.col & ~IM_COL32_A_MASK) != (IM_COL32(26, 230, 51, 0)) || ((v.col >> IM_COL32_A_SHIFT) & 0xFFu) < 8u) continue;
        ++sample.vertices;
        sample.bounds.min.x = std::min(sample.bounds.min.x, v.pos.x);
        sample.bounds.min.y = std::min(sample.bounds.min.y, v.pos.y);
        sample.bounds.max.x = std::max(sample.bounds.max.x, v.pos.x);
        sample.bounds.max.y = std::max(sample.bounds.max.y, v.pos.y);
    }
    return sample;
}

float DisplayedScroll(Lumen::Context* context) {
    const std::uint64_t list = Lumen::MakeId(Lumen::GetFrameInfo(context)->root_id, "smooth");
    Lumen::Rect list_rect{};
    Lumen::Rect first{};
    CHECK(Lumen::GetNodeRect(context, list, &list_rect));
    CHECK(Lumen::GetNodeRect(context, Lumen::MakeId(list, std::uint64_t{0}), &first));
    return list_rect.min.y - first.min.y;
}

void TestSmoothScrollingAndOverlayScrollbars() {
    ImGui::CreateContext();
    Lumen::ManualClock clock(40.0);
    Lumen::Context* context = MakeContext(&clock);
    CHECK(context != nullptr);
    if (context == nullptr) { ImGui::DestroyContext(); return; }
    Lumen::Theme theme = Lumen::GetTheme(context);
    theme.scrollbar = {26.0f / 255.0f, 230.0f / 255.0f, 51.0f / 255.0f, 0.5f};
    theme.scroll_smoothing = 18.0f;
    Lumen::SetTheme(context, theme);
    ImGui::GetIO().AddMousePosEvent(390.0f, 290.0f);
    CHECK(RunSmoothFrame(context, clock, 1.0 / 60.0).vertices == 0);

    // A wheel notch sets the logical target at once; the displayed offset eases toward it.
    ImGui::GetIO().AddMousePosEvent(150.0f, 100.0f);
    RunSmoothFrame(context, clock, 1.0 / 60.0);
    ImGui::GetIO().AddMouseWheelEvent(0.0f, -1.0f);
    RunSmoothFrame(context, clock, 1.0 / 60.0);
    const float step = theme.spacing * theme.scroll_wheel_multiplier;
    const std::uint64_t list = Lumen::MakeId(Lumen::GetFrameInfo(context)->root_id, "smooth");
    ThumbSample thumb = RunSmoothFrame(context, clock, 1.0 / 60.0);
    float displayed = DisplayedScroll(context);
    CHECK(displayed > 0.0f && displayed < step);
    CHECK(Lumen::NeedsFrame(context));
    for (int i = 0; i < 30; ++i) thumb = RunSmoothFrame(context, clock, 1.0 / 60.0);
    displayed = DisplayedScroll(context);
    CHECK(std::abs(displayed - step) < 0.01f);
    {
        HostFrame host;
        CHECK(Lumen::BeginFrame(context, {Lumen::Rect{0.0f, 0.0f, 400.0f, 300.0f}, {}, {1.0f, 1.0f}}, 1.0f / 60.0f));
        CHECK(std::abs(Lumen::GetScrollOffset(context, list).y - step) < 0.01f);
        BuildSmoothList(context);
        CHECK(Lumen::EndFrame(context));
    }

    // The overlay thumb sits at the right edge, sized by view/content and placed by the offset.
    CHECK(thumb.vertices > 0);
    CHECK(thumb.bounds.max.x <= 300.0f && thumb.bounds.min.x >= 300.0f - 3.0f - 6.0f - 1.0f);
    const float track = 200.0f - 6.0f;
    const float length = track * 200.0f / 1200.0f;
    CHECK(std::abs((thumb.bounds.max.y - thumb.bounds.min.y) - length) < 2.0f);
    CHECK(std::abs(thumb.bounds.min.y - (3.0f + (track - length) * step / 1000.0f)) < 2.0f);

    // Once the pointer leaves and the linger time passes, the thumb fades out.
    ImGui::GetIO().AddMousePosEvent(390.0f, 290.0f);
    for (int i = 0; i < 90; ++i) thumb = RunSmoothFrame(context, clock, 1.0 / 60.0);
    CHECK(thumb.vertices == 0);

    // With smoothing off the display follows the target in the same frame.
    theme.scroll_smoothing = 0.0f;
    Lumen::SetTheme(context, theme);
    for (int i = 0; i < 20; ++i) RunSmoothFrame(context, clock, 1.0 / 60.0);
    ImGui::GetIO().AddMousePosEvent(150.0f, 100.0f);
    RunSmoothFrame(context, clock, 1.0 / 60.0);
    ImGui::GetIO().AddMouseWheelEvent(0.0f, -1.0f);
    RunSmoothFrame(context, clock, 1.0 / 60.0);
    CHECK(std::abs(DisplayedScroll(context) - 2.0f * step) < 0.01f);
    Lumen::DestroyContext(context);
    ImGui::DestroyContext();
}

void GradientPainter(Lumen::Context*, Lumen::Canvas& canvas, void*) {
    canvas.FillRectGradient(canvas.Bounds(), {1.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f, 1.0f}, 0.0f, 12.0f);
    canvas.FillRectGradient(canvas.Bounds(), {1.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f, 1.0f},
                            std::numeric_limits<float>::quiet_NaN(), 12.0f);
}

void BuildGradientNode(Lumen::Context* context) {
    const Lumen::CustomPaint paint{GradientPainter, nullptr};
    Lumen::NodeStyle style{};
    style.width = 200.0f;
    style.height = 60.0f;
    style.opacity = 0.5f;
    style.custom_paint = &paint;
    CHECK(Lumen::BeginNode(context, "gradient", style));
    Lumen::EndNode(context);
}

void TestCanvasGradient() {
    ImGui::CreateContext();
    Lumen::Context* context = MakeContext();
    CHECK(context != nullptr);
    if (context == nullptr) { ImGui::DestroyContext(); return; }
    const Lumen::Viewport viewport{Lumen::Rect{0.0f, 0.0f, 400.0f, 300.0f}, {}, {1.0f, 1.0f}};
    {
    HostFrame host;
    CHECK(Lumen::BeginFrame(context, viewport, 1.0f / 60.0f));
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const int before = draw_list->VtxBuffer.Size;
    BuildGradientNode(context);
    CHECK(Lumen::EndFrame(context));
    // Leftmost opaque vertex is red, rightmost blue, all at the node's 50% opacity.
    int left = -1;
    int right = -1;
    for (int i = before; i < draw_list->VtxBuffer.Size; ++i) {
        const ImDrawVert& v = draw_list->VtxBuffer[i];
        const ImU32 alpha = (v.col >> IM_COL32_A_SHIFT) & 0xFFu;
        if (alpha < 100u || alpha > 160u) continue;
        if (left < 0 || v.pos.x < draw_list->VtxBuffer[left].pos.x) left = i;
        if (right < 0 || v.pos.x > draw_list->VtxBuffer[right].pos.x) right = i;
    }
    CHECK(left >= 0 && right >= 0);
    if (left >= 0 && right >= 0) {
        const ImU32 l = draw_list->VtxBuffer[left].col;
        const ImU32 r = draw_list->VtxBuffer[right].col;
        CHECK(((l >> IM_COL32_R_SHIFT) & 0xFFu) > 200u && ((l >> IM_COL32_B_SHIFT) & 0xFFu) < 60u);
        CHECK(((r >> IM_COL32_B_SHIFT) & 0xFFu) > 200u && ((r >> IM_COL32_R_SHIFT) & 0xFFu) < 60u);
        CHECK(std::abs(static_cast<int>((l >> IM_COL32_A_SHIFT) & 0xFFu) - 128) <= 1);
        CHECK(draw_list->VtxBuffer[left].pos.x < 10.0f && draw_list->VtxBuffer[right].pos.x > 190.0f);
    }
    }
    Lumen::DestroyContext(context);
    ImGui::DestroyContext();
}

int main() {
    TestViewportValidation();
    TestTreeBuildAndPaint();
    TestThemeCrossfadeAndReducedMotion();
    TestPointerClickDispatch();
    TestContainerInteractionState();
    TestHoverShadowMakesContainerPointerTarget();
    TestEventRoutingAndPointerCapture();
    TestStopPropagationAndPreventDefault();
    TestFlexBasisWrapAndGeometryQuery();
    TestMinimumHitTarget();
    TestWheelScrolling();
    TestNestedScrollChaining();
    TestKeyboardFocusAndActivation();
    TestTwoThousandNodeGrid();
    TestVirtualizedCollection();
    TestHostOwnedImageResourceSeam();
    TestPortalPaintOrderIsAboveBaseTree();
    TestPortalPaintingAnchorAndDismissal();
    TestModalFocusTrapScrimAndRestore();
    TestPinnedPortalTracksSafeViewport();
    TestPortalPresenceLifecycle();
    TestPortalTimeoutUsesExitTransition();
    TestDuplicateIdsRejectFrame();
    TestDropShadow();
    TestCustomPainter();
    TestIntrinsicWidthLabelDoesNotWrap();
    TestKeyboardActivatesFocusableContainer();
    TestPortalPinsOffsetAndContentHeight();
    TestLayerVisibilityAndClosing();
    TestFontLadder();
    TestCanvasPathsMeasureAndCurrentId();
    TestFrameArenaGrowsAndHonoursCap();
    TestSmoothScrollingAndOverlayScrollbars();
    TestCanvasGradient();
    if (failures != 0) {
        std::fprintf(stderr, "%d Lumen UI test(s) failed.\n", failures);
        return EXIT_FAILURE;
    }
    std::puts("All Lumen UI tests passed.");
    return EXIT_SUCCESS;
}
