#include "gallery_app.h"

#include "lumen/kit/button.h"
#include "lumen/kit/controls.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

namespace Kit = Lumen::Kit;

constexpr std::uint64_t kPopoverLayer = 0x4C554D454E000002ull;
constexpr std::uint64_t kDialogLayer = 0x4C554D454E000003ull;
constexpr int kChartBars = 9;

// Overlays float above the flat page. BeginNode copies a ShadowStyle into the frame arena, so a file-scope constant is fine.
const Lumen::ShadowStyle kOverlayShadow = [] {
    Lumen::ShadowStyle shadow{};
    shadow.blur = 24.0f;
    shadow.offset_y = 8.0f;
    return shadow;
}();

Lumen::NodeStyle Text(float size, Lumen::Color color) {
    Lumen::NodeStyle style{};
    style.font_size = size;
    style.text_color = color;
    style.has_text_color = true;
    return style;
}

Lumen::NodeStyle Row(float gap, Lumen::Align align = Lumen::Align::Center) {
    Lumen::NodeStyle style{};
    style.layout = Lumen::LayoutMode::Row;
    style.gap = gap;
    style.align_items = align;
    style.flex_wrap = true;
    return style;
}

Lumen::NodeStyle Column(float gap) {
    Lumen::NodeStyle style{};
    style.layout = Lumen::LayoutMode::Column;
    style.gap = gap;
    return style;
}

// A card with a heading and a caption; pair with Lumen::EndNode.
std::uint64_t BeginSection(Lumen::Context* context, std::uint64_t parent_id, const char* key, const char* title,
                           const char* caption) {
    const Lumen::Theme theme = Lumen::GetTheme(context);
    Lumen::NodeStyle card = Kit::CardStyle(theme);
    card.gap = 12.0f;
    Lumen::BeginNode(context, key, card);
    Lumen::NodeStyle heading = Text(theme.heading_size, theme.text);
    Lumen::Label(context, "title", title, heading);
    Lumen::NodeStyle note = Text(theme.caption_size, theme.muted_text);
    note.margin.bottom = 2.0f;
    Lumen::Label(context, "caption", caption, note);
    return Lumen::MakeId(parent_id, key);
}

void Spacer(Lumen::Context* context, const char* key) {
    Lumen::NodeStyle spacer{};
    spacer.flex_grow = 1.0f;
    spacer.height = 1.0f;
    Lumen::BeginNode(context, key, spacer);
    Lumen::EndNode(context);
}

void Chip(Lumen::Context* context, const char* key, const char* text) {
    const Lumen::Theme theme = Lumen::GetTheme(context);
    Lumen::NodeStyle chip = Text(theme.caption_size, theme.text);
    chip.padding = {6.0f, 12.0f, 6.0f, 12.0f};
    chip.radius = 999.0f;
    chip.background = theme.surface;
    chip.has_background = true;
    Lumen::Label(context, key, text, chip);
}

// ---- Handlers ----------------------------------------------------------------------------

GalleryState* AsState(void* data) { return static_cast<GalleryState*>(data); }

void OnIncrement(Lumen::Context*, std::uint64_t, void* data) { ++AsState(data)->click_count; }
void OnReset(Lumen::Context*, std::uint64_t, void* data) { AsState(data)->click_count = 0; }
void OnShuffle(Lumen::Context*, std::uint64_t, void* data) { ++AsState(data)->chart_seed; }

void OnSuccessToast(Lumen::Context*, std::uint64_t, void* data) {
    auto* state = AsState(data);
    ++state->toasts_shown;
    Kit::ShowToast(state->toasts, Kit::ToastStatus::Success, "Changes saved", "Your profile is up to date.");
}

void OnUndoToast(Lumen::Context*, std::uint64_t, void* data) {
    auto* state = AsState(data);
    ++state->toasts_shown;
    Kit::ShowToast(state->toasts, Kit::ToastStatus::Info, "Message archived", "Find it in the Archive folder.", "UNDO", 5.0);
}

void OnErrorToast(Lumen::Context*, std::uint64_t, void* data) {
    auto* state = AsState(data);
    ++state->toasts_shown;
    Kit::ShowToast(state->toasts, Kit::ToastStatus::Error, "Upload failed", "Check your connection and try again.");
}

void OnWarningToast(Lumen::Context*, std::uint64_t, void* data) {
    auto* state = AsState(data);
    ++state->toasts_shown;
    Kit::ShowToast(state->toasts, Kit::ToastStatus::Warning, "Storage almost full", "92% of 15 GB used.");
}

void OnOpenDialog(Lumen::Context* context, std::uint64_t, void*) { Kit::OpenAlertDialog(context, kDialogLayer); }

void OnTogglePopover(Lumen::Context* context, std::uint64_t, void*) {
    if (Lumen::IsLayerOpen(context, kPopoverLayer) && !Lumen::IsLayerClosing(context, kPopoverLayer)) {
        Lumen::CloseLayer(context, kPopoverLayer);
    } else {
        Lumen::OpenLayer(context, kPopoverLayer, Lumen::LayerKind::Popover,
                         static_cast<std::uint8_t>(Lumen::DismissOnOutsideClick | Lumen::DismissOnEscape));
    }
}

void OnClosePopover(Lumen::Context* context, std::uint64_t, void*) { Lumen::CloseLayer(context, kPopoverLayer); }

void OnDragTileEvent(Lumen::Context* context, Lumen::UIEvent& event, void* data) {
    auto* state = AsState(data);
    if (event.type == Lumen::UIEventType::PointerDown) {
        state->dragging = Lumen::CapturePointer(context, event.current_target_id, event.pointer_id);
    } else if (event.type == Lumen::UIEventType::PointerMove &&
               Lumen::HasPointerCapture(context, event.current_target_id, event.pointer_id)) {
        state->drag_x = std::clamp(state->drag_x + event.delta.x, 4.0f, 250.0f);
        state->drag_y = std::clamp(state->drag_y + event.delta.y, 4.0f, 84.0f);
        ++state->drag_updates;
    } else if (event.type == Lumen::UIEventType::PointerUp &&
               Lumen::HasPointerCapture(context, event.current_target_id, event.pointer_id)) {
        Lumen::ReleasePointer(context, event.current_target_id, event.pointer_id);
        state->dragging = false;
    }
    Lumen::RequestRedraw(context);
}

// ---- Custom paint: an animated bar chart drawn through Canvas -----------------------------------

float ChartValue(int seed, int index) {
    const float x = std::sin(static_cast<float>(seed) * 12.9898f + static_cast<float>(index) * 78.233f) * 43758.5453f;
    return 0.18f + 0.78f * (x - std::floor(x));
}

void PaintChart(Lumen::Context* context, Lumen::Canvas& canvas, void* data) {
    const auto* state = static_cast<const GalleryState*>(data);
    const Lumen::Theme theme = Lumen::GetTheme(context);
    const Lumen::Rect bounds = canvas.Bounds();
    const float label_height = 18.0f;
    const Lumen::Rect plot{{bounds.min.x, bounds.min.y + 6.0f}, {bounds.max.x, bounds.max.y - label_height}};
    for (int line = 0; line <= 3; ++line) {
        const float y = Lumen::Lerp(plot.max.y, plot.min.y, static_cast<float>(line) / 3.0f);
        canvas.Line({plot.min.x, y}, {plot.max.x, y}, theme.outline_variant, 1.0f);
    }
    const float slot = plot.Width() / static_cast<float>(kChartBars);
    const float bar_width = slot * 0.56f;
    for (int i = 0; i < kChartBars; ++i) {
        const float value = Lumen::Animate(context, Lumen::MakeId(canvas.NodeId(), static_cast<std::uint64_t>(i)),
                                           ChartValue(state->chart_seed, i), theme.reflow_transition);
        const float x = plot.min.x + slot * (static_cast<float>(i) + 0.5f) - 0.5f * bar_width;
        const float top = Lumen::Lerp(plot.max.y, plot.min.y, std::clamp(value, 0.0f, 1.05f));
        const Lumen::Color color = i % 3 == 1 ? theme.accent : theme.primary_container;
        canvas.FillRect(Lumen::Rect{{x, top}, {x + bar_width, plot.max.y}}, color, std::min(8.0f, 0.5f * bar_width));
        char label[8];
        std::snprintf(label, sizeof(label), "%c", static_cast<char>('A' + i));
        canvas.Text({x + 0.5f * bar_width - 4.0f, plot.max.y + 3.0f}, label, theme.muted_text, theme.caption_size);
    }
}

void BuildFeedItem(Lumen::Context* context, std::size_t index, void*) {
    const Lumen::Theme theme = Lumen::GetTheme(context);
    static const char* const kMessages[4] = {"Layout pass settled", "Theme tokens blended", "Pointer captured",
                                             "Portal painted above base tree"};
    char text[96]{};
    std::snprintf(text, sizeof(text), "#%05llu   %s", static_cast<unsigned long long>(index), kMessages[index % 4u]);
    Lumen::NodeStyle style = Text(theme.caption_size, index % 4u == 0u ? theme.accent : theme.muted_text);
    style.padding = {6.0f, 10.0f, 6.0f, 10.0f};
    style.radius = 8.0f;
    style.hover_background = theme.surface_alt;
    style.has_hover_background = true;
    Lumen::Label(context, "row", text, style);
}

// ---- Sections -----------------------------------------------------------------------------

void BuildAppBar(Lumen::Context* context, GalleryState* state) {
    const Lumen::Theme theme = Lumen::GetTheme(context);
    Lumen::BeginNode(context, "app-bar", Row(16.0f));
    Lumen::NodeStyle brand = Column(2.0f);
    Lumen::BeginNode(context, "brand", brand);
    Lumen::Label(context, "name", "Lumen", Text(theme.title_size, theme.text));
    Lumen::Label(context, "tagline", "UI engine gallery", Text(theme.caption_size, theme.muted_text));
    Lumen::EndNode(context);
    Spacer(context, "spacer");
    static const char* const kThemes[3] = {"Material", "Dark", "Light"};
    Kit::Segmented(context, "theme", kThemes, 3, &state->theme_index, 104.0f);
    Lumen::EndNode(context);
}

void BuildHero(Lumen::Context* context, const GalleryState* state) {
    const Lumen::Theme theme = Lumen::GetTheme(context);
    Lumen::NodeStyle hero = Kit::CardStyle(theme);
    hero.background = theme.primary_container;
    hero.padding = {24.0f, 28.0f, 24.0f, 28.0f};
    hero.gap = 10.0f;
    hero.radius = theme.radius + 8.0f;
    Lumen::BeginNode(context, "hero", hero);
    Lumen::Label(context, "eyebrow", "RETAINED-PER-FRAME  ·  DETERMINISTIC  ·  HOST-OWNED", Text(theme.caption_size, theme.text));
    Lumen::Label(context, "title", "Build interfaces from primitives.", Text(theme.title_size + 6.0f, theme.text));
    Lumen::Label(context, "body",
                 "Every widget on this page is composed from Lumen's public API: nodes, layout, motion, "
                 "events, portals and custom painters, drawn through Dear ImGui.",
                 Text(theme.font_size, theme.muted_text));
    Lumen::NodeStyle chips = Row(8.0f);
    chips.margin.top = 6.0f;
    Lumen::BeginNode(context, "stats", chips);
    char text[64];
    std::snprintf(text, sizeof(text), "%zu nodes", state->stats.nodes_built);
    Chip(context, "nodes", text);
    std::snprintf(text, sizeof(text), "%.0f KB frame arena", static_cast<double>(state->stats.arena_bytes_used) / 1024.0);
    Chip(context, "arena", text);
    std::snprintf(text, sizeof(text), "%zu state entries", state->stats.state_entries);
    Chip(context, "state", text);
    std::snprintf(text, sizeof(text), "%.0f fps", static_cast<double>(state->fps));
    Chip(context, "fps", text);
    Lumen::EndNode(context);
    Lumen::EndNode(context);
}

void BuildButtons(Lumen::Context* context, std::uint64_t parent_id, GalleryState* state) {
    const Lumen::Theme theme = Lumen::GetTheme(context);
    BeginSection(context, parent_id, "buttons", "Buttons",
                 "Pill buttons with state layers, hover lift and an ink splash from the press point.");
    Lumen::BeginNode(context, "row", Row(10.0f));
    Kit::Button(context, "filled", "Increment", Kit::ButtonVariant::Filled, OnIncrement, state);
    Kit::Button(context, "tonal", "Reset", Kit::ButtonVariant::Tonal, OnReset, state);
    Kit::Button(context, "outlined", "Outlined", Kit::ButtonVariant::Outlined, OnIncrement, state);
    Kit::Button(context, "text", "Text", Kit::ButtonVariant::Text, OnIncrement, state);
    Lumen::EndNode(context);
    Lumen::BeginNode(context, "status", Row(10.0f));
    Lumen::NodeStyle disabled = Kit::ButtonStyle(context, Kit::ButtonVariant::Filled);
    disabled.disabled = true;
    Kit::Button(context, "disabled", "Disabled", Kit::ButtonVariant::Filled, nullptr, nullptr, disabled);
    char text[48];
    std::snprintf(text, sizeof(text), "Pressed %d time%s", state->click_count, state->click_count == 1 ? "" : "s");
    Lumen::Label(context, "count", text, Text(theme.font_size, theme.muted_text));
    Lumen::EndNode(context);
    Lumen::EndNode(context);
}

void BuildSwitches(Lumen::Context* context, std::uint64_t parent_id, GalleryState* state) {
    BeginSection(context, parent_id, "switches", "Switches",
                 "The thumb overshoots on the theme's toggle curve. Click a row or focus it and press Space.");
    Kit::Switch(context, "wifi", "Wi-Fi", &state->wifi);
    Kit::Switch(context, "notifications", "Notifications", &state->notifications);
    Kit::Switch(context, "reduce-motion", "Reduce motion (applies to the whole engine)", &state->reduce_motion);
    Lumen::NodeStyle locked{};
    locked.disabled = true;
    Kit::Switch(context, "locked", "Managed by your organisation", &state->locked, locked);
    Lumen::EndNode(context);
}

void BuildPopups(Lumen::Context* context, std::uint64_t parent_id, GalleryState* state) {
    const Lumen::Theme theme = Lumen::GetTheme(context);
    const std::uint64_t id = BeginSection(context, parent_id, "popups", "Popups",
        "Toasts stack, glide and pause while hovered. Dialogs fade in over a scrim; popovers anchor to their trigger.");
    Lumen::BeginNode(context, "toasts", Row(10.0f));
    Kit::Button(context, "success", "Show toast", Kit::ButtonVariant::Filled, OnSuccessToast, state);
    Kit::Button(context, "undo", "Toast with undo", Kit::ButtonVariant::Tonal, OnUndoToast, state);
    Kit::Button(context, "error", "Error", Kit::ButtonVariant::Outlined, OnErrorToast, state);
    Kit::Button(context, "warning", "Warning", Kit::ButtonVariant::Outlined, OnWarningToast, state);
    Lumen::EndNode(context);
    Lumen::Label(context, "position-title", "Toast position", Text(theme.caption_size, theme.muted_text));
    static const char* const kAnchors[3] = {"Bottom", "Bottom right", "Top right"};
    Kit::Segmented(context, "position", kAnchors, 3, &state->toast_anchor, 120.0f);
    Lumen::BeginNode(context, "dialogs", Row(10.0f));
    Kit::Button(context, "dialog", "Alert dialog", Kit::ButtonVariant::Tonal, OnOpenDialog, state);
    Kit::Button(context, "popover", "Anchored popover", Kit::ButtonVariant::Outlined, OnTogglePopover, state);
    Lumen::EndNode(context);
    Lumen::Label(context, "dialog-status", state->dialog_status, Text(theme.caption_size, theme.muted_text));
    Lumen::EndNode(context);

    if (Lumen::IsLayerOpen(context, kPopoverLayer)) {
        Lumen::PortalOptions options{};
        options.anchor_node_id = Lumen::MakeId(Lumen::MakeId(id, "dialogs"), "popover");
        options.size = {280.0f, 0.0f};
        options.placement = Lumen::Placement::BelowAnchor;
        options.offset = {0.0f, 8.0f};
        Lumen::NodeStyle panel = Kit::CardStyle(theme);
        panel.background = theme.surface_container_high;
        panel.shadow = &kOverlayShadow;
        panel.padding = {16.0f, 18.0f, 12.0f, 18.0f};
        if (Lumen::BeginPortal(context, kPopoverLayer, options, panel)) {
            Lumen::Label(context, "title", "Anchored popover", Text(theme.font_size, theme.text));
            Lumen::Label(context, "body", "Placed below its trigger, flipped when there is no room, and dismissed by Escape or an outside click.",
                         Text(theme.caption_size, theme.muted_text));
            Lumen::NodeStyle actions = Row(8.0f);
            actions.justify_content = Lumen::Justify::End;
            Lumen::BeginNode(context, "actions", actions);
            Kit::Button(context, "close", "Got it", Kit::ButtonVariant::Text, OnClosePopover, state);
            Lumen::EndNode(context);
            Lumen::EndPortal(context);
        }
    }
}

void BuildTypography(Lumen::Context* context, std::uint64_t parent_id) {
    const Lumen::Theme theme = Lumen::GetTheme(context);
    BeginSection(context, parent_id, "typography", "Typography",
                 "Theme type tokens snap to the host's baked font ladder, so text stays crisp at every size.");
    Lumen::Label(context, "sample-title", "Title · the quick brown fox", Text(theme.title_size, theme.text));
    Lumen::Label(context, "sample-heading", "Heading · jumps over the lazy dog", Text(theme.heading_size, theme.text));
    Lumen::Label(context, "sample-body", "Body · 16 px for everything people read at length.", Text(theme.font_size, theme.text));
    Lumen::Label(context, "sample-caption", "Caption · secondary text and metadata.", Text(theme.caption_size, theme.muted_text));
    Lumen::EndNode(context);
}

void BuildChart(Lumen::Context* context, std::uint64_t parent_id, GalleryState* state) {
    BeginSection(context, parent_id, "chart", "Custom paint",
                 "A Canvas painter draws this chart; each bar animates to its new value on the reflow curve.");
    const Lumen::CustomPaint paint{PaintChart, state};
    Lumen::NodeStyle chart{};
    chart.height = 170.0f;
    chart.custom_paint = &paint;
    Lumen::BeginNode(context, "plot", chart);
    Lumen::EndNode(context);
    Lumen::NodeStyle shuffle = Kit::ButtonStyle(context, Kit::ButtonVariant::Tonal);
    shuffle.align_self = Lumen::Align::Start;
    Kit::Button(context, "shuffle", "Shuffle data", Kit::ButtonVariant::Tonal, OnShuffle, state, shuffle);
    Lumen::EndNode(context);
}

void BuildDragLab(Lumen::Context* context, std::uint64_t parent_id, GalleryState* state) {
    const Lumen::Theme theme = Lumen::GetTheme(context);
    BeginSection(context, parent_id, "drag", "Pointer capture",
                 "Drag the tile and keep dragging outside the area; the captured tile still receives moves.");
    Lumen::NodeStyle area{};
    area.layout = Lumen::LayoutMode::Absolute;
    area.height = 140.0f;
    area.radius = 16.0f;
    area.background = theme.surface_alt;
    area.has_background = true;
    area.clip_children = true;
    Lumen::BeginNode(context, "area", area);
    Lumen::NodeStyle tile = Kit::ButtonStyle(context, Kit::ButtonVariant::Filled);
    tile.x = state->drag_x;
    tile.y = state->drag_y;
    tile.width = 96.0f;
    tile.height = 48.0f;
    tile.radius = 14.0f;
    tile.animate_layout = false;
    tile.event_handler = OnDragTileEvent;
    tile.event_user_data = state;
    Lumen::Button(context, "tile", state->dragging ? "Dragging" : "Drag me", nullptr, nullptr, tile);
    Lumen::EndNode(context);
    char text[64];
    std::snprintf(text, sizeof(text), "%d captured move events", state->drag_updates);
    Lumen::Label(context, "status", text, Text(theme.caption_size, theme.muted_text));
    Lumen::EndNode(context);
}

void BuildFeed(Lumen::Context* context, std::uint64_t parent_id) {
    const Lumen::Theme theme = Lumen::GetTheme(context);
    BeginSection(context, parent_id, "feed", "Virtualized list",
                 "5,000 logical rows; only the visible window is built each frame.");
    Lumen::NodeStyle list{};
    list.height = 220.0f;
    list.radius = 14.0f;
    list.padding = {6.0f, 6.0f, 6.0f, 6.0f};
    list.background = theme.canvas;
    list.has_background = true;
    Lumen::Collection(context, "rows", 5000u, 30.0f, BuildFeedItem, nullptr, list);
    Lumen::EndNode(context);
}

void BuildDialog(Lumen::Context* context, GalleryState* state) {
    const Kit::DialogResult result = Kit::AlertDialog(context, kDialogLayer, "Discard draft?",
        "This removes the draft from all your devices. You can't undo this action.", "Discard");
    if (result == Kit::DialogResult::Confirm) {
        state->dialog_status = "Dialog result: Discard";
        Kit::ShowToast(state->toasts, Kit::ToastStatus::Success, "Draft discarded", nullptr);
    } else if (result == Kit::DialogResult::Cancel) {
        state->dialog_status = "Dialog result: Cancel";
    }
}

} // namespace

Lumen::ThemeKind GalleryThemeKind(int theme_index) {
    return theme_index == 1 ? Lumen::ThemeKind::Dark : theme_index == 2 ? Lumen::ThemeKind::Light : Lumen::ThemeKind::Material;
}

void BuildLumenGallery(Lumen::Context* context, GalleryState* state) {
    if (context == nullptr || state == nullptr) return;
    if (state->applied_theme != state->theme_index) {
        state->applied_theme = state->theme_index;
        Lumen::SetTheme(context, GalleryThemeKind(state->theme_index));
    }
    if (state->applied_reduce_motion != state->reduce_motion) {
        state->applied_reduce_motion = state->reduce_motion;
        Lumen::SetReduceMotion(context, state->reduce_motion);
    }
    const Lumen::FrameInfo* frame = Lumen::GetFrameInfo(context);
    if (frame == nullptr) return;
    if (frame->delta_seconds > 0.0f) state->fps = Lumen::Lerp(state->fps, 1.0f / frame->delta_seconds, 0.05f);
    if (!state->initial_overlay_opened && state->initial_overlay != 0) {
        state->initial_overlay_opened = true;
        if (state->initial_overlay == 1) OnTogglePopover(context, 0u, state);
        else if (state->initial_overlay == 2) Kit::OpenAlertDialog(context, kDialogLayer);
        else {
            OnSuccessToast(context, 0u, state);
            OnUndoToast(context, 0u, state);
        }
    }
    state->toasts.anchor = static_cast<Kit::ToastAnchor>(std::clamp(state->toast_anchor, 0, 2));

    const Lumen::Rect bounds = frame->viewport.bounds;
    Lumen::NodeStyle page = Column(16.0f);
    page.width = bounds.Width();
    page.height = bounds.Height();
    page.padding = {20.0f, 28.0f, 28.0f, 28.0f};
    page.scrollable_y = true;
    page.clip_children = true;
    Lumen::BeginNode(context, "page", page);
    const std::uint64_t page_id = Lumen::MakeId(frame->root_id, "page");
    BuildAppBar(context, state);
    BuildHero(context, state);

    const bool wide = bounds.Width() >= 960.0f;
    Lumen::NodeStyle columns = wide ? Row(16.0f, Lumen::Align::Start) : Column(16.0f);
    columns.flex_wrap = false;
    Lumen::BeginNode(context, "columns", columns);
    const std::uint64_t columns_id = Lumen::MakeId(page_id, "columns");
    Lumen::NodeStyle column = Column(16.0f);
    column.flex_grow = 1.0f;
    column.flex_basis = 0.0f;
    Lumen::BeginNode(context, "left", column);
    const std::uint64_t left_id = Lumen::MakeId(columns_id, "left");
    BuildButtons(context, left_id, state);
    BuildSwitches(context, left_id, state);
    BuildChart(context, left_id, state);
    BuildFeed(context, left_id);
    Lumen::EndNode(context);
    Lumen::BeginNode(context, "right", column);
    const std::uint64_t right_id = Lumen::MakeId(columns_id, "right");
    BuildPopups(context, right_id, state);
    BuildTypography(context, right_id);
    BuildDragLab(context, right_id, state);
    Lumen::EndNode(context);
    Lumen::EndNode(context);
    Lumen::EndNode(context);

    BuildDialog(context, state);
    if (Kit::Toasts(context, state->toasts) != 0u) {
        Kit::ShowToast(state->toasts, Kit::ToastStatus::Success, "Message restored", "It's back in your inbox.");
    }
    // Sampled once the tree is declared and shown on the next frame.
    state->stats = Lumen::GetDebugStats(context);
}
