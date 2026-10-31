#include "lumen/kit/button.h"
#include "lumen/kit/controls.h"
#include "lumen/kit/ink.h"
#include "lumen/kit/overlays.h"
#include "lumen/kit/text_field.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

int failures = 0;

void Check(bool condition, const char* expression, int line) {
    if (!condition) {
        std::fprintf(stderr, "FAIL line %d: %s\n", line, expression);
        ++failures;
    }
}
#define CHECK(expression) Check((expression), #expression, __LINE__)

constexpr Lumen::Color kInk{0.2f, 0.9f, 0.4f, 0.5f};

bool Near(float a, float b, float tolerance) { return std::abs(a - b) <= tolerance; }

bool SameColor(Lumen::Color a, Lumen::Color b, float tolerance = 0.002f) {
    return Near(a.r, b.r, tolerance) && Near(a.g, b.g, tolerance) && Near(a.b, b.b, tolerance) && Near(a.a, b.a, tolerance);
}

Lumen::Color Hex(std::uint32_t rgb) {
    return {static_cast<float>((rgb >> 16) & 0xFFu) / 255.0f, static_cast<float>((rgb >> 8) & 0xFFu) / 255.0f,
            static_cast<float>(rgb & 0xFFu) / 255.0f, 1.0f};
}

ImU32 ToRgb(Lumen::Color color) {
    const auto byte = [](float value) { return static_cast<ImU32>(Lumen::Clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f); };
    return IM_COL32(byte(color.r), byte(color.g), byte(color.b), 0);
}

struct InkScene {
    bool nested = false;
    bool disabled = false;
    bool prevent_default = false;
    int pointer_downs = 0;
    int clicks[4]{};
};

InkScene scene{};

// Vertices of one RGB colour within an alpha range, collected after each frame.
struct ColorProbe {
    ImU32 rgb = 0u;
    unsigned min_alpha = 1u;
    unsigned max_alpha = 255u;
};

struct ColorSample {
    int vertices = 0;
    Lumen::Rect bounds{{1.0e9f, 1.0e9f}, {-1.0e9f, -1.0e9f}};
    unsigned max_alpha = 0u;
};

ColorProbe probe{};

void CountingHandler(Lumen::Context*, Lumen::UIEvent& event, void* user_data) {
    auto* target = static_cast<InkScene*>(user_data);
    if (event.type != Lumen::UIEventType::PointerDown || event.phase != Lumen::UIEventPhase::Target) return;
    ++target->pointer_downs;
    if (target->prevent_default) event.PreventDefault();
}

void BuildInkScene(Lumen::Context* context) {
    Lumen::NodeStyle outer{};
    outer.width = 300.0f;
    outer.height = 160.0f;
    outer.radius = 12.0f;
    outer.animate_layout = false;
    outer.disabled = scene.disabled;
    outer.event_handler = CountingHandler;
    outer.event_user_data = &scene;
    CHECK(Lumen::Kit::BeginInkSurface(context, "outer", outer));
    if (scene.nested) {
        Lumen::NodeStyle inner{};
        inner.width = 100.0f;
        inner.height = 60.0f;
        inner.radius = 6.0f;
        inner.animate_layout = false;
        CHECK(Lumen::Kit::BeginInkSurface(context, "inner", inner));
        Lumen::EndNode(context);
    }
    Lumen::EndNode(context);
}

void CountClick(Lumen::Context*, std::uint64_t, void* user_data) { ++*static_cast<int*>(user_data); }

constexpr const char* kButtonKeys[4] = {"filled", "tonal", "outlined", "text"};

void BuildButtonScene(Lumen::Context* context) {
    Lumen::NodeStyle row{};
    row.layout = Lumen::LayoutMode::Row;
    row.gap = 10.0f;
    row.align_items = Lumen::Align::Start;
    CHECK(Lumen::BeginNode(context, "row", row));
    for (int i = 0; i < 4; ++i) {
        const auto variant = static_cast<Lumen::Kit::ButtonVariant>(i);
        Lumen::NodeStyle style = Lumen::Kit::ButtonStyle(context, variant);
        style.animate_layout = false;
        CHECK(Lumen::Kit::Button(context, kButtonKeys[i], "Action", variant, CountClick, &scene.clicks[i], style));
    }
    Lumen::EndNode(context);
}

ColorSample RunFrame(Lumen::Context* context, Lumen::ManualClock& clock, double step, void (*build)(Lumen::Context*)) {
    clock.Advance(step);
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(600.0f, 300.0f);
    io.DeltaTime = static_cast<float>(step > 0.0 ? step : 1.0 / 60.0);
    if (!io.Fonts->IsBuilt()) {
        unsigned char* pixels = nullptr;
        int width = 0;
        int height = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    }
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(600.0f, 300.0f), ImGuiCond_Always);
    ImGui::Begin("Lumen kit tests", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize);
    const Lumen::Viewport viewport{Lumen::Rect{0.0f, 0.0f, 600.0f, 300.0f}, {}, {1.0f, 1.0f}};
    ColorSample sample{};
    CHECK(Lumen::BeginFrame(context, viewport, io.DeltaTime));
    build(context);
    CHECK(Lumen::EndFrame(context));
    const ImDrawList* draw_list = ImGui::GetWindowDrawList();
    for (int i = 0; i < draw_list->VtxBuffer.Size; ++i) {
        const ImDrawVert& vertex = draw_list->VtxBuffer[i];
        const unsigned alpha = (vertex.col >> IM_COL32_A_SHIFT) & 0xFFu;
        if ((vertex.col & ~IM_COL32_A_MASK) != probe.rgb || alpha < probe.min_alpha || alpha > probe.max_alpha) continue;
        ++sample.vertices;
        sample.max_alpha = std::max(sample.max_alpha, alpha);
        sample.bounds.min.x = std::min(sample.bounds.min.x, vertex.pos.x);
        sample.bounds.min.y = std::min(sample.bounds.min.y, vertex.pos.y);
        sample.bounds.max.x = std::max(sample.bounds.max.x, vertex.pos.x);
        sample.bounds.max.y = std::max(sample.bounds.max.y, vertex.pos.y);
    }
    ImGui::End();
    ImGui::Render();
    return sample;
}

Lumen::Rect NodeRect(Lumen::Context* context, const char* key, const char* parent_key = nullptr) {
    Lumen::Rect rect{};
    std::uint64_t parent_id = Lumen::GetFrameInfo(context)->root_id;
    if (parent_key != nullptr) parent_id = Lumen::MakeId(parent_id, parent_key);
    CHECK(Lumen::GetNodeRect(context, Lumen::MakeId(parent_id, key), &rect));
    return rect;
}

bool Inside(const Lumen::Rect& inner, const Lumen::Rect& outer, float tolerance) {
    return inner.min.x >= outer.min.x - tolerance && inner.min.y >= outer.min.y - tolerance &&
           inner.max.x <= outer.max.x + tolerance && inner.max.y <= outer.max.y + tolerance;
}

struct Fixture {
    Lumen::ManualClock clock{10.0};
    Lumen::Context* context = nullptr;
    void (*build)(Lumen::Context*) = nullptr;

    Fixture(void (*builder)(Lumen::Context*), bool material) : build(builder) {
        ImGui::CreateContext();
        Lumen::Config config{};
        config.clock = &clock;
        context = Lumen::CreateContext(config);
        CHECK(context != nullptr);
        if (material) {
            Lumen::SetTheme(context, Lumen::ThemeKind::Material);
        } else {
            Lumen::Theme theme = Lumen::GetTheme(context);
            theme.ink = kInk;
            theme.ink_transition.duration_seconds = 0.3;
            theme.ink_transition.delay_seconds = 0.0;
            theme.ink_transition.curve = Lumen::Curve::Linear();
            Lumen::SetTheme(context, theme);
            probe = ColorProbe{ToRgb(kInk), 1u, 255u};
        }
        Lumen::InjectMousePosition(context, {590.0f, 290.0f});
        Frame(0.0);
    }
    ~Fixture() {
        Lumen::DestroyContext(context);
        ImGui::DestroyContext();
    }

    ColorSample Frame(double step) { return RunFrame(context, clock, step, build); }

    ColorSample Press(Lumen::Vec2 position) {
        Lumen::InjectMousePosition(context, position);
        Frame(1.0 / 60.0);
        Lumen::InjectMouseButton(context, ImGuiMouseButton_Left, true);
        return Frame(1.0 / 60.0);
    }

    void Release() {
        Lumen::InjectMouseButton(context, ImGuiMouseButton_Left, false);
        Frame(1.0 / 60.0);
    }

    void Click(Lumen::Vec2 position) {
        Press(position);
        Release();
    }

    void Key(ImGuiKey key) {
        Lumen::InjectKey(context, key, true);
        Frame(1.0 / 60.0);
        Lumen::InjectKey(context, key, false);
        Frame(1.0 / 60.0);
    }

    void Advance(double seconds) {
        for (double t = 0.0; t < seconds; t += 1.0 / 60.0) Frame(1.0 / 60.0);
    }
};

Lumen::Vec2 Center(const Lumen::Rect& rect) { return {0.5f * (rect.min.x + rect.max.x), 0.5f * (rect.min.y + rect.max.y)}; }

bool NodeRectById(Lumen::Context* context, std::uint64_t id, Lumen::Rect* rect) { return Lumen::GetNodeRect(context, id, rect); }

void TestInkSpreadAndFade() {
    scene = {};
    Fixture fixture(BuildInkScene, false);
    Lumen::Context* context = fixture.context;
    const Lumen::Rect surface = NodeRect(context, "outer");
    const Lumen::Vec2 press{surface.min.x + 60.0f, surface.min.y + 40.0f};

    // The splash starts as a 4 x 4 rect at the press point at the token's peak alpha.
    ColorSample sample = fixture.Press(press);
    CHECK(sample.vertices > 0);
    CHECK(Inside(sample.bounds, Lumen::Rect{press.x - 2.0f, press.y - 2.0f, 4.0f, 4.0f}, 1.5f));
    CHECK(Near(static_cast<float>(sample.max_alpha), kInk.a * 255.0f, 2.0f));
    CHECK(Lumen::NeedsFrame(context));

    // Halfway through a linear spread it covers half the distance to each edge at half alpha.
    sample = fixture.Frame(0.15);
    CHECK(sample.vertices > 0);
    CHECK(Near(sample.bounds.min.x, Lumen::Lerp(press.x - 2.0f, surface.min.x, 0.5f), 2.0f));
    CHECK(Near(sample.bounds.max.x, Lumen::Lerp(press.x + 2.0f, surface.max.x, 0.5f), 2.0f));
    CHECK(Near(sample.bounds.min.y, Lumen::Lerp(press.y - 2.0f, surface.min.y, 0.5f), 2.0f));
    CHECK(Near(sample.bounds.max.y, Lumen::Lerp(press.y + 2.0f, surface.max.y, 0.5f), 2.0f));
    CHECK(Near(static_cast<float>(sample.max_alpha), kInk.a * 0.5f * 255.0f, 3.0f));

    // Near the end it is almost the full surface, and once the duration passes it is gone even while held.
    sample = fixture.Frame(0.12);
    CHECK(sample.vertices > 0);
    CHECK(Inside(sample.bounds, surface, 1.5f));
    CHECK(sample.bounds.Width() > surface.Width() * 0.85f);
    CHECK(fixture.Frame(0.05).vertices == 0);
    CHECK(Lumen::GetNodeState(context, Lumen::MakeId(Lumen::GetFrameInfo(context)->root_id, "outer")).pressed);
    fixture.Release();
    for (int i = 0; i < 30 && Lumen::NeedsFrame(context); ++i) fixture.Frame(1.0 / 60.0);
    CHECK(!Lumen::NeedsFrame(context));
    CHECK(scene.pointer_downs == 1);
}

void TestInkSuppressedAndNested() {
    // PreventDefault from the node's own handler suppresses the splash but the handler still runs.
    scene = {};
    scene.prevent_default = true;
    {
        Fixture fixture(BuildInkScene, false);
        const Lumen::Rect surface = NodeRect(fixture.context, "outer");
        CHECK(fixture.Press({surface.min.x + 20.0f, surface.min.y + 20.0f}).vertices == 0);
        CHECK(fixture.Frame(0.1).vertices == 0);
        CHECK(scene.pointer_downs == 1);
    }

    // Disabled surfaces and reduced motion show no ink.
    scene = {};
    scene.disabled = true;
    {
        Fixture fixture(BuildInkScene, false);
        const Lumen::Rect surface = NodeRect(fixture.context, "outer");
        CHECK(fixture.Press({surface.min.x + 20.0f, surface.min.y + 20.0f}).vertices == 0);
        CHECK(fixture.Frame(0.1).vertices == 0);
    }
    scene = {};
    {
        Fixture fixture(BuildInkScene, false);
        Lumen::SetReduceMotion(fixture.context, true);
        const Lumen::Rect surface = NodeRect(fixture.context, "outer");
        CHECK(fixture.Press({surface.min.x + 20.0f, surface.min.y + 20.0f}).vertices == 0);
        CHECK(fixture.Frame(0.1).vertices == 0);
    }

    // A press on a nested surface splashes only that surface, and its ink stays inside it.
    scene = {};
    scene.nested = true;
    {
        Fixture fixture(BuildInkScene, false);
        const Lumen::Rect inner = NodeRect(fixture.context, "inner", "outer");
        fixture.Press({inner.min.x + 30.0f, inner.min.y + 30.0f});
        const ColorSample sample = fixture.Frame(0.25);
        CHECK(sample.vertices > 0);
        CHECK(Inside(sample.bounds, inner, 1.5f));
        CHECK(sample.bounds.Width() > inner.Width() * 0.75f);
    }
    scene = {};
}

void TestMaterialTheme() {
    ImGui::CreateContext();
    Lumen::Context* context = Lumen::CreateContext();
    CHECK(context != nullptr);
    if (context == nullptr) { ImGui::DestroyContext(); return; }

    // Every built-in theme populates the Material roles.
    const Lumen::ThemeKind kinds[4] = {Lumen::ThemeKind::Dark, Lumen::ThemeKind::Light,
                                       Lumen::ThemeKind::HighContrast, Lumen::ThemeKind::Material};
    for (const Lumen::ThemeKind kind : kinds) {
        Lumen::SetTheme(context, kind);
        const Lumen::Theme theme = Lumen::GetTheme(context);
        const Lumen::Color roles[8] = {theme.surface_container_high, theme.outline_variant, theme.primary_container,
                                       theme.secondary_container, theme.on_secondary_container, theme.inverse_surface,
                                       theme.inverse_on_surface, theme.inverse_primary};
        for (const Lumen::Color& role : roles) CHECK(role.a == 1.0f);
        CHECK(theme.ink.a > 0.0f && theme.ink_transition.duration_seconds > 0.0);
    }

    Lumen::SetTheme(context, Lumen::ThemeKind::Material);
    const Lumen::Theme material = Lumen::GetTheme(context);
    CHECK(SameColor(material.canvas, Hex(0xFEF7FF)));
    CHECK(SameColor(material.surface, Hex(0xF3EDF7)));
    CHECK(SameColor(material.surface_alt, Hex(0xE6E0E9)));
    CHECK(SameColor(material.accent, Hex(0x6750A4)));
    CHECK(SameColor(material.secondary_container, Hex(0xE8DEF8)));
    CHECK(SameColor(material.inverse_surface, Hex(0x322F35)));
    CHECK(SameColor(material.accent_hover, Lumen::Kit::StateLayer(material.accent, material.accent_text, 0.08f), 0.004f));
    CHECK(Near(material.scrim.a, 0.36f, 0.001f));
    CHECK(Near(material.ink.a, 0.26f, 0.001f));
    CHECK(Near(static_cast<float>(material.ink_transition.duration_seconds), 0.55f, 0.001f));
    CHECK(Near(static_cast<float>(material.color_transition.duration_seconds), 0.15f, 0.001f));
    CHECK(material.button_min_height == 40.0f && material.button_padding.left == 24.0f);

    Lumen::SetTheme(context, Lumen::ThemeKind::HighContrast);
    CHECK(Lumen::GetTheme(context).shadow.a == 0.0f);
    Lumen::DestroyContext(context);
    ImGui::DestroyContext();
}

// Presets captured inside a frame; the shadow pointers refer to frame data and are compared before the next frame.
Lumen::NodeStyle captured_styles[4]{};
Lumen::ShadowStyle captured_shadows[4]{};
bool captured_has_shadow[4]{};

void CaptureButtonStyles(Lumen::Context* context) {
    for (int i = 0; i < 4; ++i) {
        captured_styles[i] = Lumen::Kit::ButtonStyle(context, static_cast<Lumen::Kit::ButtonVariant>(i));
        captured_has_shadow[i] = captured_styles[i].shadow != nullptr;
        if (captured_has_shadow[i]) captured_shadows[i] = *captured_styles[i].shadow;
        captured_styles[i].shadow = nullptr;
    }
}

void TestButtonStyles() {
    Fixture fixture(CaptureButtonStyles, true);
    const Lumen::Theme theme = Lumen::GetTheme(fixture.context);
    const Lumen::NodeStyle& filled = captured_styles[0];
    const Lumen::NodeStyle& tonal = captured_styles[1];
    const Lumen::NodeStyle& outlined = captured_styles[2];
    const Lumen::NodeStyle& text = captured_styles[3];

    CHECK(filled.height == 40.0f && filled.radius == 20.0f);
    CHECK(SameColor(filled.background, theme.accent) && SameColor(filled.text_color, theme.accent_text));
    CHECK(SameColor(filled.hover_background, Lumen::Kit::StateLayer(theme.accent, theme.accent_text, 0.08f)));
    CHECK(captured_has_shadow[0] && captured_shadows[0].has_hover && captured_shadows[0].blur == 0.0f);
    CHECK(captured_shadows[0].hover_blur == 12.0f && captured_shadows[0].hover_offset_y == 4.0f);
    CHECK(SameColor(tonal.background, theme.secondary_container) && SameColor(tonal.text_color, theme.on_secondary_container));
    CHECK(captured_has_shadow[1]);
    CHECK(outlined.background.a == 0.0f && outlined.border_width == 1.0f && SameColor(outlined.border_color, theme.border));
    CHECK(Near(outlined.hover_background.a, 0.08f, 0.001f) && Near(outlined.pressed_background.a, 0.06f, 0.001f));
    CHECK(!captured_has_shadow[2] && !captured_has_shadow[3] && SameColor(outlined.text_color, theme.accent));
    CHECK(text.border_width == 0.0f && text.padding.left == 14.0f && text.padding.right == 14.0f);
    CHECK(outlined.padding.left == 24.0f);
}

void TestButtonInteraction() {
    scene = {};
    Fixture fixture(BuildButtonScene, true);
    Lumen::Context* context = fixture.context;
    const Lumen::Theme theme = Lumen::GetTheme(context);
    const Lumen::Rect filled = NodeRect(context, "filled", "row");
    const Lumen::Rect outlined = NodeRect(context, "outlined", "row");
    CHECK(filled.Height() == 40.0f && filled.max.x <= NodeRect(context, "tonal", "row").min.x);

    // Hovering settles the filled container on its state-layer colour.
    probe = ColorProbe{ToRgb(theme.accent_hover), 255u, 255u};
    Lumen::InjectMousePosition(context, {filled.min.x + 20.0f, filled.min.y + 20.0f});
    fixture.Frame(1.0 / 60.0);
    ColorSample sample = fixture.Frame(0.3);
    CHECK(sample.vertices > 0 && Inside(sample.bounds, filled, 1.0f));

    // Pressing splashes on_primary ink inside the filled button (text glyphs are opaque, so the
    // alpha range isolates the ink), and releasing activates it once.
    probe = ColorProbe{ToRgb(theme.accent_text), 1u, 200u};
    sample = fixture.Press({filled.min.x + 20.0f, filled.min.y + 20.0f});
    CHECK(sample.vertices > 0 && Inside(sample.bounds, filled, 1.0f));
    CHECK(Near(static_cast<float>(sample.max_alpha), theme.ink.a * 255.0f, 3.0f));
    fixture.Release();
    CHECK(scene.clicks[0] == 1);

    // The outlined button's ink is tinted with the primary colour (its hover layer is far fainter).
    probe = ColorProbe{ToRgb(theme.accent), 40u, 200u};
    fixture.Frame(0.6);
    sample = fixture.Press({outlined.min.x + 30.0f, outlined.min.y + 20.0f});
    CHECK(sample.vertices > 0 && Inside(sample.bounds, outlined, 1.0f));
    fixture.Release();
    CHECK(scene.clicks[2] == 1);
    CHECK(scene.clicks[0] == 1 && scene.clicks[1] == 0 && scene.clicks[3] == 0);
    scene = {};
}

struct ControlScene {
    bool wifi = false;
    bool locked = true;
    int selected = 0;
};

ControlScene controls{};

void BuildControlScene(Lumen::Context* context) {
    Lumen::NodeStyle column{};
    column.layout = Lumen::LayoutMode::Column;
    column.gap = 12.0f;
    column.align_items = Lumen::Align::Start;
    CHECK(Lumen::BeginNode(context, "column", column));
    Lumen::NodeStyle row{};
    row.animate_layout = false;
    CHECK(Lumen::Kit::Switch(context, "wifi", "Wi-Fi", &controls.wifi, row));
    Lumen::NodeStyle locked = row;
    locked.disabled = true;
    CHECK(Lumen::Kit::Switch(context, "locked", "Locked", &controls.locked, locked));
    static const char* const kLabels[3] = {"One", "Two", "Three"};
    CHECK(Lumen::Kit::Segmented(context, "segments", kLabels, 3, &controls.selected, 90.0f));
    Lumen::EndNode(context);
}

std::uint64_t ControlId(Lumen::Context* context, const char* key, const char* child = nullptr) {
    std::uint64_t id = Lumen::MakeId(Lumen::MakeId(Lumen::GetFrameInfo(context)->root_id, "column"), key);
    return child != nullptr ? Lumen::MakeId(id, child) : id;
}

void TestSwitchAndSegmented() {
    controls = {};
    Fixture fixture(BuildControlScene, true);
    Lumen::Context* context = fixture.context;
    Lumen::Rect wifi{};
    CHECK(NodeRectById(context, ControlId(context, "wifi"), &wifi));
    CHECK(wifi.Height() >= 32.0f);

    // Keyboard: the switch row is the first focus stop; Space and Enter toggle it.
    fixture.Key(ImGuiKey_Tab);
    CHECK(Lumen::GetNodeState(context, ControlId(context, "wifi")).focused);
    fixture.Key(ImGuiKey_Space);
    CHECK(controls.wifi);
    fixture.Key(ImGuiKey_Enter);
    CHECK(!controls.wifi);

    // Clicking the label part of the row toggles; the disabled switch ignores clicks.
    fixture.Click({wifi.min.x + 80.0f, Center(wifi).y});
    CHECK(controls.wifi);
    Lumen::Rect locked{};
    CHECK(NodeRectById(context, ControlId(context, "locked"), &locked));
    fixture.Click(Center(locked));
    CHECK(controls.locked);

    // Segmented: clicking an option selects it.
    Lumen::Rect third{};
    CHECK(NodeRectById(context, ControlId(context, "segments", "item-2"), &third));
    CHECK(Near(third.Width(), 90.0f, 0.01f) && Near(third.Height(), 36.0f, 0.01f));
    fixture.Click(Center(third));
    CHECK(controls.selected == 2);
    controls = {};
}

constexpr std::uint64_t kDialogLayer = 0xD1A106ull;
Lumen::Kit::DialogResult dialog_results[8]{};
int dialog_result_count = 0;

void BuildDialogScene(Lumen::Context* context) {
    const Lumen::Kit::DialogResult result = Lumen::Kit::AlertDialog(context, kDialogLayer, "Discard draft?", "Body text.", "Discard");
    if (result != Lumen::Kit::DialogResult::None && dialog_result_count < 8) dialog_results[dialog_result_count++] = result;
}

void TestAlertDialog() {
    dialog_result_count = 0;
    Fixture fixture(BuildDialogScene, true);
    Lumen::Context* context = fixture.context;
    CHECK(Lumen::Kit::OpenAlertDialog(context, kDialogLayer));
    fixture.Advance(0.5);
    const std::uint64_t portal = Lumen::MakeId(Lumen::GetFrameInfo(context)->root_id, kDialogLayer);
    Lumen::Rect card{};
    CHECK(NodeRectById(context, portal, &card));
    CHECK(Near(card.Width(), 312.0f, 0.01f));
    CHECK(Near(Center(card).x, 300.0f, 0.5f) && Near(Center(card).y, 150.0f, 0.5f));
    Lumen::Rect confirm{};
    CHECK(NodeRectById(context, Lumen::MakeId(Lumen::MakeId(portal, "actions"), "confirm"), &confirm));
    CHECK(Inside(confirm, card, 0.01f));

    fixture.Click(Center(confirm));
    fixture.Advance(0.5);
    CHECK(dialog_result_count == 1 && dialog_results[0] == Lumen::Kit::DialogResult::Confirm);
    CHECK(!Lumen::IsLayerOpen(context, kDialogLayer));

    // Escape dismisses and reports Cancel exactly once.
    CHECK(Lumen::Kit::OpenAlertDialog(context, kDialogLayer));
    fixture.Advance(0.5);
    fixture.Key(ImGuiKey_Escape);
    fixture.Advance(0.5);
    CHECK(dialog_result_count == 2 && dialog_results[1] == Lumen::Kit::DialogResult::Cancel);
    CHECK(!Lumen::IsLayerOpen(context, kDialogLayer));
}

Lumen::Kit::ToastQueue toast_queue{};
std::uint64_t toast_action = 0u;

void BuildToastScene(Lumen::Context* context) {
    const std::uint64_t action = Lumen::Kit::Toasts(context, toast_queue);
    if (action != 0u) toast_action = action;
}

void TestToastLifecycle() {
    toast_queue = Lumen::Kit::ToastQueue{};
    toast_action = 0u;
    Fixture fixture(BuildToastScene, true);
    Lumen::Context* context = fixture.context;
    const std::uint64_t portal = Lumen::MakeId(Lumen::GetFrameInfo(context)->root_id, toast_queue.layer_id);
    const std::uint64_t first = Lumen::Kit::ShowToast(toast_queue, Lumen::Kit::ToastStatus::Success, "Saved", "Done.", nullptr, 1.0);
    fixture.Advance(0.7);
    CHECK(Lumen::IsLayerOpen(context, toast_queue.layer_id));
    Lumen::Rect card{};
    CHECK(NodeRectById(context, Lumen::MakeId(portal, first), &card));
    CHECK(Near(card.Width(), 344.0f, 0.01f));
    CHECK(Near(Center(card).x, 300.0f, 0.5f) && Near(card.max.y, 300.0f - 24.0f, 1.0f));

    // Hovering pauses the countdown; leaving resumes it, then the toast exits and the layer closes.
    Lumen::InjectMousePosition(context, Center(card));
    fixture.Advance(1.5);
    CHECK(toast_queue.count == 1 && toast_queue.entries[0].remaining > 0.0);
    Lumen::InjectMousePosition(context, {5.0f, 5.0f});
    fixture.Advance(1.5);
    CHECK(toast_queue.count == 0);
    fixture.Advance(0.5);
    CHECK(!Lumen::IsLayerOpen(context, toast_queue.layer_id));

    // The action button reports its toast once and dismisses it.
    const std::uint64_t undo = Lumen::Kit::ShowToast(toast_queue, Lumen::Kit::ToastStatus::Info, "Archived", nullptr, "UNDO", 5.0);
    fixture.Advance(0.7);
    Lumen::Rect action{};
    CHECK(NodeRectById(context, Lumen::MakeId(Lumen::MakeId(portal, undo), "action"), &action));
    fixture.Click(Center(action));
    fixture.Frame(1.0 / 60.0);
    CHECK(toast_action == undo);
    fixture.Advance(0.5);
    CHECK(toast_queue.count == 0);

    // A newer toast stacks above an older one at the bottom edge; beyond four visible the oldest leaves.
    const std::uint64_t older = Lumen::Kit::ShowToast(toast_queue, Lumen::Kit::ToastStatus::Info, "Older", nullptr);
    const std::uint64_t newer = Lumen::Kit::ShowToast(toast_queue, Lumen::Kit::ToastStatus::Error, "Newer", nullptr);
    fixture.Advance(1.0);
    Lumen::Rect older_rect{};
    Lumen::Rect newer_rect{};
    CHECK(NodeRectById(context, Lumen::MakeId(portal, older), &older_rect));
    CHECK(NodeRectById(context, Lumen::MakeId(portal, newer), &newer_rect));
    CHECK(newer_rect.min.y > older_rect.max.y);
    for (int i = 0; i < 3; ++i) Lumen::Kit::ShowToast(toast_queue, Lumen::Kit::ToastStatus::Warning, "More", nullptr);
    CHECK(toast_queue.entries[0].id == older && toast_queue.entries[0].close_requested);
    CHECK(!toast_queue.entries[1].close_requested);
    toast_queue = Lumen::Kit::ToastQueue{};
}

char field_text[16]{};
char locked_text[16] = "fixed";
int field_changes = 0;
int field_clicks = 0;

void CountFieldClick(Lumen::Context*, Lumen::UIEvent& event, void*) {
    if (event.type == Lumen::UIEventType::Click) ++field_clicks;
}

void BuildFieldScene(Lumen::Context* context) {
    Lumen::NodeStyle column{};
    column.layout = Lumen::LayoutMode::Column;
    column.gap = 12.0f;
    CHECK(Lumen::BeginNode(context, "column", column));
    Lumen::NodeStyle style{};
    style.width = 240.0f;
    style.animate_layout = false;
    style.event_handler = CountFieldClick;
    if (Lumen::Kit::TextField(context, "field", field_text, sizeof(field_text), "Type here", style)) ++field_changes;
    Lumen::NodeStyle locked = style;
    locked.disabled = true;
    Lumen::Kit::TextField(context, "locked", locked_text, sizeof(locked_text), nullptr, locked);
    Lumen::EndNode(context);
}

struct FieldFixture : Fixture {
    FieldFixture() : Fixture(BuildFieldScene, true) {}

    void Type(const char* text) {
        Lumen::InjectText(context, text);
        Frame(1.0 / 60.0);
    }

    void Chord(ImGuiKey modifier, ImGuiKey key) {
        Lumen::InjectKey(context, modifier, true);
        Key(key);
        Lumen::InjectKey(context, modifier, false);
        Frame(1.0 / 60.0);
    }
};

void TestTextFieldEditing() {
    std::memset(field_text, 0, sizeof(field_text));
    field_changes = 0;
    field_clicks = 0;
    FieldFixture fixture;
    Lumen::Context* context = fixture.context;
    const std::uint64_t column = Lumen::MakeId(Lumen::GetFrameInfo(context)->root_id, "column");
    Lumen::Rect field{};
    CHECK(Lumen::GetNodeRect(context, Lumen::MakeId(column, "field"), &field));
    CHECK(Near(field.Height(), 48.0f, 0.01f));

    // Focus by click, type, and the declaration reports the change once.
    fixture.Click(Center(field));
    CHECK(Lumen::GetNodeState(context, Lumen::MakeId(column, "field")).focused);
    fixture.Type("Hello");
    CHECK(std::strcmp(field_text, "Hello") == 0);
    fixture.Frame(1.0 / 60.0);
    CHECK(field_changes == 1);
    fixture.Frame(1.0 / 60.0);
    CHECK(field_changes == 1);

    // Caret movement and insertion; Space types a space and does not activate the field as a click.
    const int clicks_before = field_clicks;
    fixture.Key(ImGuiKey_LeftArrow);
    fixture.Key(ImGuiKey_LeftArrow);
    fixture.Type("X");
    CHECK(std::strcmp(field_text, "HelXlo") == 0);
    fixture.Key(ImGuiKey_End);
    fixture.Key(ImGuiKey_Space);
    fixture.Type(" ");
    CHECK(std::strcmp(field_text, "HelXlo ") == 0);
    CHECK(field_clicks == clicks_before);

    // Word movement, selection replacement and word deletion.
    std::strcpy(field_text, "one two");
    fixture.Key(ImGuiKey_End);
    fixture.Chord(ImGuiMod_Ctrl, ImGuiKey_LeftArrow);
    fixture.Type("X");
    CHECK(std::strcmp(field_text, "one Xtwo") == 0);
    fixture.Chord(ImGuiMod_Shift, ImGuiKey_Home);
    fixture.Type("A");
    CHECK(std::strcmp(field_text, "Atwo") == 0);
    fixture.Key(ImGuiKey_End);
    fixture.Chord(ImGuiMod_Ctrl, ImGuiKey_Backspace);
    CHECK(field_text[0] == '\0');

    // UTF-8 characters are inserted and deleted whole; the buffer never overflows.
    fixture.Type("a\xC3\xA9");
    CHECK(std::strcmp(field_text, "a\xC3\xA9") == 0);
    fixture.Key(ImGuiKey_Backspace);
    CHECK(std::strcmp(field_text, "a") == 0);
    fixture.Type("bcdefghijklmnopqrstuvwxyz");
    CHECK(std::strlen(field_text) == sizeof(field_text) - 1u);

    // Select all, copy, clear, paste twice through the host clipboard.
    std::strcpy(field_text, "copy");
    fixture.Chord(ImGuiMod_Ctrl, ImGuiKey_A);
    fixture.Chord(ImGuiMod_Ctrl, ImGuiKey_C);
    fixture.Key(ImGuiKey_Delete);
    CHECK(field_text[0] == '\0');
    fixture.Chord(ImGuiMod_Ctrl, ImGuiKey_V);
    fixture.Chord(ImGuiMod_Ctrl, ImGuiKey_V);
    CHECK(std::strcmp(field_text, "copycopy") == 0);
    fixture.Chord(ImGuiMod_Ctrl, ImGuiKey_A);
    fixture.Chord(ImGuiMod_Ctrl, ImGuiKey_X);
    CHECK(field_text[0] == '\0');
    const char* clip = Lumen::GetClipboardText(context);
    CHECK(clip != nullptr && std::strcmp(clip, "copycopy") == 0);

    // Holding Backspace auto-repeats.
    std::strcpy(field_text, "abcdefghij");
    fixture.Key(ImGuiKey_End);
    Lumen::InjectKey(context, ImGuiKey_Backspace, true);
    fixture.Advance(0.6);
    Lumen::InjectKey(context, ImGuiKey_Backspace, false);
    fixture.Frame(1.0 / 60.0);
    CHECK(std::strlen(field_text) < 7u);

    // Disabled fields ignore clicks and typing.
    Lumen::Rect locked{};
    CHECK(Lumen::GetNodeRect(context, Lumen::MakeId(column, "locked"), &locked));
    fixture.Click(Center(locked));
    fixture.Type("zz");
    CHECK(std::strcmp(locked_text, "fixed") == 0);
}

} // namespace

int main() {
    TestInkSpreadAndFade();
    TestInkSuppressedAndNested();
    TestMaterialTheme();
    TestButtonStyles();
    TestButtonInteraction();
    TestSwitchAndSegmented();
    TestAlertDialog();
    TestToastLifecycle();
    TestTextFieldEditing();
    if (failures != 0) {
        std::fprintf(stderr, "%d Lumen kit test(s) failed.\n", failures);
        return EXIT_FAILURE;
    }
    std::puts("All Lumen kit tests passed.");
    return EXIT_SUCCESS;
}
