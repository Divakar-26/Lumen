#include "node_internal.h"
#include "context_internal.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>

namespace Lumen {
namespace {

bool IsZeroInsets(const Insets& value) noexcept {
    return value.top == 0.0f && value.right == 0.0f && value.bottom == 0.0f && value.left == 0.0f;
}

Color Rgb(float r, float g, float b, float a = 1.0f) noexcept { return {r, g, b, a}; }

Color Hex(std::uint32_t rgb, float a = 1.0f) noexcept {
    return {static_cast<float>((rgb >> 16) & 0xFFu) / 255.0f, static_cast<float>((rgb >> 8) & 0xFFu) / 255.0f,
            static_cast<float>(rgb & 0xFFu) / 255.0f, a};
}

Color WithOpacity(Color color, float opacity) noexcept {
    color.a = Clamp(color.a * opacity, 0.0f, 1.0f);
    return color;
}

ImU32 ToImU32(Color color, float opacity) noexcept {
    color = WithOpacity(color, opacity);
    const auto byte = [](float v) -> int { return static_cast<int>(Clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
    return IM_COL32(byte(color.r), byte(color.g), byte(color.b), byte(color.a));
}

Rect InsetRect(Rect rect, const Insets& insets) noexcept {
    rect.min.x += insets.left;
    rect.min.y += insets.top;
    rect.max.x -= insets.right;
    rect.max.y -= insets.bottom;
    if (rect.max.x < rect.min.x) rect.max.x = rect.min.x;
    if (rect.max.y < rect.min.y) rect.max.y = rect.min.y;
    return rect;
}

float ClampSize(float value, float min_value, float max_value) noexcept {
    if (!std::isfinite(value)) value = 0.0f;
    if (!std::isfinite(min_value) || min_value < 0.0f) min_value = 0.0f;
    if (!std::isfinite(max_value) || max_value < min_value) max_value = min_value;
    return Clamp(value, min_value, max_value);
}

const char* CopyText(Context* context, const char* text) noexcept {
    if (text == nullptr) return nullptr;
    const std::size_t length = std::strlen(text);
    auto* copy = static_cast<char*>(FrameAlloc(context, length + 1u, alignof(char)));
    if (copy == nullptr) return nullptr;
    std::memcpy(copy, text, length + 1u);
    return copy;
}

std::uint64_t ResolveKey(Node* parent, const char* key, std::uint64_t numeric_key, bool numeric) noexcept {
    if (numeric) return MakeId(parent->id, numeric_key);
    if (key != nullptr && key[0] != '\0') return MakeId(parent->id, key);
    return MakeId(parent->id, static_cast<std::uint64_t>(parent->child_count + 1u));
}

void AppendChild(Context* context, Node* parent, Node* child) noexcept {
    for (Node* sibling = parent->first_child; sibling != nullptr; sibling = sibling->next_sibling) {
        if (sibling->id == child->id) {
            ++context->duplicate_node_ids;
            context->node_tree_invalid = true;
            return;
        }
    }
    if (parent->last_child != nullptr) parent->last_child->next_sibling = child;
    else parent->first_child = child;
    parent->last_child = child;
    ++parent->child_count;
}

struct FontChoice {
    ImFont* font = nullptr;
    float size = 0.0f;
};

// The registered rung nearest the requested size, at its baked size; otherwise the current ImGui font scaled.
FontChoice PickFont(const Context* context, float font_size) noexcept {
    const float requested = font_size > 0.0f ? font_size : ImGui::GetFontSize();
    if (context == nullptr || context->font_count == 0u) return {ImGui::GetFont(), requested};
    ImFont* best = context->fonts[0];
    for (std::size_t i = 1u; i < context->font_count; ++i) {
        if (std::abs(context->fonts[i]->FontSize - requested) < std::abs(best->FontSize - requested)) best = context->fonts[i];
    }
    return {best, best->FontSize};
}

Vec2 MeasureTextRange(const Context* context, const char* text, const char* end, float font_size, float wrap_width) noexcept {
    if (text == nullptr || text == end || text[0] == '\0') return {};
    const FontChoice choice = PickFont(context, font_size);
    if (choice.font == nullptr) return {};
    const float wrap = wrap_width > 0.0f && wrap_width < 100000.0f ? wrap_width : 0.0f;
    const ImVec2 measured = choice.font->CalcTextSizeA(choice.size, FLT_MAX, wrap, text, end);
    return {measured.x, measured.y};
}

std::uint8_t CurrentModifiers(const ImGuiIO& io) noexcept {
    unsigned bits = 0u;
    if ((io.KeyMods & ImGuiMod_Ctrl) != 0) bits |= KeyModCtrl;
    if ((io.KeyMods & ImGuiMod_Shift) != 0) bits |= KeyModShift;
    if ((io.KeyMods & ImGuiMod_Alt) != 0) bits |= KeyModAlt;
    if ((io.KeyMods & ImGuiMod_Super) != 0) bits |= KeyModSuper;
    return static_cast<std::uint8_t>(bits);
}

Align EffectiveAlign(const Node* parent, const Node* child) noexcept {
    return child->style.align_self == Align::Auto ? parent->style.align_items : child->style.align_self;
}

void ResolveInherited(Node* node, const Node* parent, const Theme& theme, std::size_t depth = 0u) noexcept {
    if (node == nullptr || depth > 512u) return;
    if (node->kind == NodeKind::Root) {
        node->resolved_text_color = theme.text;
        node->resolved_font_size = theme.font_size;
        node->resolved_opacity = 1.0f;
    } else {
        node->resolved_text_color = node->style.has_text_color ? node->style.text_color : parent->resolved_text_color;
        node->resolved_font_size = node->style.font_size > 0.0f ? node->style.font_size : parent->resolved_font_size;
        node->resolved_opacity = parent->resolved_opacity * Clamp(node->style.opacity, 0.0f, 1.0f);
    }
    for (Node* child = node->first_child; child != nullptr; child = child->next_sibling) {
        ResolveInherited(child, node, theme, depth + 1u);
    }
}

Vec2 MeasureNode(const Context* context, Node* node, float available_width, const Theme& theme, std::size_t depth = 0u) noexcept {
    if (depth > 512u) return {};
    const float width_for_measure = node->style.width >= 0.0f
        ? std::min(available_width, node->style.width) : available_width;
    const float inner_width = std::max(0.0f, width_for_measure - node->style.padding.left - node->style.padding.right);
    const Vec2 text = MeasureText(context, node->text, node->resolved_font_size, inner_width);
    float content_width = text.x;
    float content_height = text.y;
    if (node->kind == NodeKind::Image) {
        content_width = std::max(0.0f, (node->image != nullptr ? node->image->source_size.x : 0.0f));
        content_height = std::max(0.0f, (node->image != nullptr ? node->image->source_size.y : 0.0f));
    }
    const float gap = node->style.gap >= 0.0f ? node->style.gap : theme.spacing;
    const bool row = node->style.layout == LayoutMode::Row;
    const bool absolute = node->style.layout == LayoutMode::Absolute;
    const bool grid = node->style.layout == LayoutMode::Grid;

    if (grid) {
        constexpr std::size_t kMaxColumns = 16u;
        constexpr std::size_t kMaxRows = 2048u;
        float column_widths[kMaxColumns]{};
        float row_heights[kMaxRows]{};
        const std::size_t columns = std::max<std::size_t>(1u, std::min<std::size_t>(node->style.grid_columns, kMaxColumns));
        std::size_t index = 0;
        for (Node* child = node->first_child; child != nullptr; child = child->next_sibling, ++index) {
            const float cell_width = std::max(0.0f, (inner_width - gap * static_cast<float>(columns - 1u)) / static_cast<float>(columns));
            child->intrinsic = MeasureNode(context, child, cell_width, theme, depth + 1u);
            const float width = child->style.width >= 0.0f ? child->style.width : child->intrinsic.x;
            const float height = child->style.height >= 0.0f ? child->style.height : child->intrinsic.y;
            const std::size_t column = index % columns;
            const std::size_t row_index = index / columns;
            column_widths[column] = std::max(column_widths[column], width + child->style.margin.left + child->style.margin.right);
            if (row_index < kMaxRows) row_heights[row_index] = std::max(row_heights[row_index], height + child->style.margin.top + child->style.margin.bottom);
        }
        content_width = 0.0f;
        for (std::size_t col = 0; col < columns; ++col) content_width += column_widths[col];
        if (columns > 1u) content_width += gap * static_cast<float>(columns - 1u);
        const std::size_t rows = std::min<std::size_t>(kMaxRows, (node->child_count + columns - 1u) / columns);
        content_height = 0.0f;
        for (std::size_t row_index = 0; row_index < rows; ++row_index) content_height += row_heights[row_index];
        if (rows > 1u) content_height += gap * static_cast<float>(rows - 1u);
    } else {
        float main_total = 0.0f;
        float cross_max = 0.0f;
        float wrap_line_main = 0.0f;
        float wrap_line_cross = 0.0f;
        float wrap_total_cross = 0.0f;
        std::size_t wrap_line_count = 0u;
        std::size_t child_count = 0u;
        for (Node* child = node->first_child; child != nullptr; child = child->next_sibling) {
            float child_available = inner_width;
            if (row) child_available = inner_width;
            else if (absolute || node->style.layout == LayoutMode::Stack) child_available = inner_width;
            child_available = std::max(0.0f, child_available - child->style.margin.left - child->style.margin.right);
            child->intrinsic = MeasureNode(context, child, child_available, theme, depth + 1u);
            const float width = ClampSize(child->style.width >= 0.0f ? child->style.width : child->intrinsic.x,
                                          child->style.min_width, child->style.max_width);
            const float height = ClampSize(child->style.height >= 0.0f ? child->style.height : child->intrinsic.y,
                                           child->style.min_height, child->style.max_height);
            const float basis = child->style.flex_basis >= 0.0f
                ? ClampSize(child->style.flex_basis, row ? child->style.min_width : child->style.min_height,
                            row ? child->style.max_width : child->style.max_height)
                : (row ? width : height);
            if (absolute) {
                content_width = std::max(content_width, child->style.x + child->style.margin.left + width + child->style.margin.right);
                content_height = std::max(content_height, child->style.y + child->style.margin.top + height + child->style.margin.bottom);
            } else if (node->style.layout == LayoutMode::Stack) {
                content_width = std::max(content_width, width + child->style.margin.left + child->style.margin.right);
                content_height = std::max(content_height, height + child->style.margin.top + child->style.margin.bottom);
            } else if (row && node->style.flex_wrap) {
                const float item_main = basis + child->style.margin.left + child->style.margin.right;
                const float next_main = wrap_line_main + (wrap_line_count > 0u ? gap : 0.0f) + item_main;
                if (wrap_line_count > 0u && inner_width > 0.0f && next_main > inner_width) {
                    wrap_total_cross += wrap_line_cross + gap;
                    wrap_line_main = 0.0f;
                    wrap_line_cross = 0.0f;
                    wrap_line_count = 0u;
                }
                if (wrap_line_count > 0u) wrap_line_main += gap;
                wrap_line_main += item_main;
                wrap_line_cross = std::max(wrap_line_cross, height + child->style.margin.top + child->style.margin.bottom);
                ++wrap_line_count;
            } else if (row) {
                main_total += basis + child->style.margin.left + child->style.margin.right;
                cross_max = std::max(cross_max, height + child->style.margin.top + child->style.margin.bottom);
            } else {
                main_total += basis + child->style.margin.top + child->style.margin.bottom;
                cross_max = std::max(cross_max, width + child->style.margin.left + child->style.margin.right);
            }
            ++child_count;
        }
        if (child_count > 0u && node->style.layout != LayoutMode::Stack && !absolute) {
            if (row && node->style.flex_wrap) {
                content_width = std::max(content_width, wrap_line_main);
                content_height = std::max(content_height, wrap_total_cross + wrap_line_cross);
            } else {
                main_total += gap * static_cast<float>(child_count - 1u);
                if (row) {
                    content_width = std::max(content_width, main_total);
                    content_height = std::max(content_height, cross_max);
                } else {
                    content_width = std::max(content_width, cross_max);
                    content_height = std::max(content_height, main_total);
                }
            }
        }
    }

    node->content_extent = {content_width, content_height};
    Vec2 natural{content_width + node->style.padding.left + node->style.padding.right,
                 content_height + node->style.padding.top + node->style.padding.bottom};
    if (node->style.width >= 0.0f) natural.x = node->style.width;
    if (node->style.height >= 0.0f) natural.y = node->style.height;
    natural.x = ClampSize(natural.x, node->style.min_width, node->style.max_width);
    natural.y = ClampSize(natural.y, node->style.min_height, node->style.max_height);
    node->intrinsic = natural;
    return natural;
}

float DesiredMain(const Node* child, bool row) noexcept {
    const float dimension = child->style.flex_basis >= 0.0f
        ? child->style.flex_basis : (row ? child->intrinsic.x : child->intrinsic.y);
    return ClampSize(dimension, row ? child->style.min_width : child->style.min_height,
                     row ? child->style.max_width : child->style.max_height);
}

float MarginMain(const Node* child, bool row) noexcept {
    return row ? child->style.margin.left + child->style.margin.right
               : child->style.margin.top + child->style.margin.bottom;
}

Rect IntersectRect(const Rect& a, const Rect& b) noexcept {
    Rect result{{std::max(a.min.x, b.min.x), std::max(a.min.y, b.min.y)},
                {std::min(a.max.x, b.max.x), std::min(a.max.y, b.max.y)}};
    if (result.max.x < result.min.x) result.max.x = result.min.x;
    if (result.max.y < result.min.y) result.max.y = result.min.y;
    return result;
}

void LoadScrollOffsets(Context* context, Node* node) noexcept {
    if (node->style.scrollable_x || node->style.scrollable_y) {
        const std::uint64_t scroll_id = MakeId(node->id, "scroll-offset");
        ScrollMemory* memory = State<ScrollMemory>(context, scroll_id);
        node->scroll_offset = memory != nullptr ? memory->offset : Vec2{};
    } else {
        node->scroll_offset = {};
    }
    for (Node* child = node->first_child; child != nullptr; child = child->next_sibling) {
        LoadScrollOffsets(context, child);
    }
}

bool ClampScrollOffsets(Context* context, Node* node) noexcept {
    bool changed = false;
    if (node->style.scrollable_x || node->style.scrollable_y) {
        ScrollMemory* memory = State<ScrollMemory>(context, MakeId(node->id, "scroll-offset"));
        if (memory != nullptr) {
            const Rect content = InsetRect(node->target_rect, node->style.padding);
            const Vec2 old = memory->offset;
            if (node->style.scrollable_x) memory->offset.x = Clamp(memory->offset.x, 0.0f, std::max(0.0f, node->content_extent.x - content.Width()));
            else memory->offset.x = 0.0f;
            if (node->style.scrollable_y) memory->offset.y = Clamp(memory->offset.y, 0.0f, std::max(0.0f, node->content_extent.y - content.Height()));
            else memory->offset.y = 0.0f;
            node->scroll_offset = memory->offset;
            changed = changed || old.x != memory->offset.x || old.y != memory->offset.y;
        }
    }
    for (Node* child = node->first_child; child != nullptr; child = child->next_sibling) {
        changed = ClampScrollOffsets(context, child) || changed;
    }
    return changed;
}

void SetChildRect(Context* context, Node* child, Rect target_rect, const Theme& theme, std::size_t depth) noexcept;

void LayoutChildren(Context* context, Node* node, const Theme& theme, std::size_t depth) noexcept {
    if (depth > 512u || node->first_child == nullptr) return;
    Rect content = InsetRect(node->target_rect, node->style.padding);
    content.min.x -= node->style.scrollable_x ? node->scroll_offset.x : 0.0f;
    content.max.x -= node->style.scrollable_x ? node->scroll_offset.x : 0.0f;
    content.min.y -= node->style.scrollable_y ? node->scroll_offset.y : 0.0f;
    content.max.y -= node->style.scrollable_y ? node->scroll_offset.y : 0.0f;
    const float available_width = content.Width();
    const float available_height = content.Height();
    const float gap = node->style.gap >= 0.0f ? node->style.gap : theme.spacing;

    if (node->style.layout == LayoutMode::Absolute) {
        for (Node* child = node->first_child; child != nullptr; child = child->next_sibling) {
            const float width = child->style.width >= 0.0f ? child->style.width : child->intrinsic.x;
            const float height = child->style.height >= 0.0f ? child->style.height : child->intrinsic.y;
            const Vec2 pos{content.min.x + child->style.x + child->style.margin.left,
                           content.min.y + child->style.y + child->style.margin.top};
            SetChildRect(context, child, Rect{pos, {pos.x + ClampSize(width, child->style.min_width, child->style.max_width),
                                                   pos.y + ClampSize(height, child->style.min_height, child->style.max_height)}}, theme, depth + 1u);
        }
        return;
    }

    if (node->style.layout == LayoutMode::Stack) {
        for (Node* child = node->first_child; child != nullptr; child = child->next_sibling) {
            const Align ax = EffectiveAlign(node, child);
            const Align ay = EffectiveAlign(node, child);
            float width = child->style.width >= 0.0f ? child->style.width : child->intrinsic.x;
            float height = child->style.height >= 0.0f ? child->style.height : child->intrinsic.y;
            if (ax == Align::Stretch && child->style.width < 0.0f) width = available_width - child->style.margin.left - child->style.margin.right;
            if (ay == Align::Stretch && child->style.height < 0.0f) height = available_height - child->style.margin.top - child->style.margin.bottom;
            width = ClampSize(width, child->style.min_width, child->style.max_width);
            height = ClampSize(height, child->style.min_height, child->style.max_height);
            float x = content.min.x + child->style.margin.left;
            float y = content.min.y + child->style.margin.top;
            if (ax == Align::Center) x += (available_width - width - child->style.margin.left - child->style.margin.right) * 0.5f;
            else if (ax == Align::End) x += available_width - width - child->style.margin.left - child->style.margin.right;
            if (ay == Align::Center) y += (available_height - height - child->style.margin.top - child->style.margin.bottom) * 0.5f;
            else if (ay == Align::End) y += available_height - height - child->style.margin.top - child->style.margin.bottom;
            SetChildRect(context, child, Rect{x, y, width, height}, theme, depth + 1u);
        }
        return;
    }

    if (node->style.layout == LayoutMode::Grid) {
        constexpr std::size_t kMaxColumns = 16u;
        const std::size_t columns = std::max<std::size_t>(1u, std::min<std::size_t>(node->style.grid_columns, kMaxColumns));
        const float cell_width = std::max(0.0f, (available_width - gap * static_cast<float>(columns - 1u)) / static_cast<float>(columns));
        Node* child = node->first_child;
        float row_y = content.min.y;
        while (child != nullptr) {
            Node* row_children[kMaxColumns]{};
            float row_height = 0.0f;
            for (std::size_t col = 0; col < columns && child != nullptr; ++col) {
                row_children[col] = child;
                const float child_height = child->style.height >= 0.0f ? child->style.height : child->intrinsic.y;
                row_height = std::max(row_height, child_height + child->style.margin.top + child->style.margin.bottom);
                child = child->next_sibling;
            }
            for (std::size_t col = 0; col < columns; ++col) {
                Node* item = row_children[col];
                if (item == nullptr) continue;
                const Align align = EffectiveAlign(node, item);
                const float available_cell = std::max(0.0f, cell_width - item->style.margin.left - item->style.margin.right);
                float width = item->style.width >= 0.0f ? item->style.width : item->intrinsic.x;
                if (align == Align::Stretch && item->style.width < 0.0f) width = available_cell;
                width = ClampSize(width, item->style.min_width, item->style.max_width);
                float height = item->style.height >= 0.0f ? item->style.height : item->intrinsic.y;
                height = ClampSize(height, item->style.min_height, item->style.max_height);
                float x = content.min.x + static_cast<float>(col) * (cell_width + gap) + item->style.margin.left;
                if (align == Align::Center) x += (available_cell - width) * 0.5f;
                else if (align == Align::End) x += available_cell - width;
                float y = row_y + item->style.margin.top;
                if (item->style.align_self == Align::Center) y += (row_height - height - item->style.margin.top - item->style.margin.bottom) * 0.5f;
                else if (item->style.align_self == Align::End) y += row_height - height - item->style.margin.top - item->style.margin.bottom;
                SetChildRect(context, item, Rect{x, y, width, height}, theme, depth + 1u);
            }
            row_y += row_height + gap;
        }
        return;
    }

    const bool row = node->style.layout == LayoutMode::Row;
    const float main_available = row ? available_width : available_height;
    const float cross_available = row ? available_height : available_width;

    if (row && node->style.flex_wrap) {
        float line_y = content.min.y;
        Node* line_start = node->first_child;
        while (line_start != nullptr) {
            Node* line_end = nullptr;
            std::size_t line_count = 0u;
            float base_total = 0.0f;
            float grow_total = 0.0f;
            float shrink_weight = 0.0f;
            float line_cross = 0.0f;
            for (Node* child = line_start; child != nullptr; child = child->next_sibling) {
                const float base = DesiredMain(child, true);
                const float item_extent = base + MarginMain(child, true);
                const float candidate_total = base_total + (line_count > 0u ? gap : 0.0f) + item_extent;
                if (line_count > 0u && main_available > 0.0f && candidate_total > main_available) break;
                base_total = candidate_total;
                ++line_count;
                line_end = child;
                grow_total += std::max(0.0f, child->style.flex_grow);
                shrink_weight += std::max(0.0f, child->style.flex_shrink) * base;
                line_cross = std::max(line_cross, child->intrinsic.y + child->style.margin.top + child->style.margin.bottom);
            }
            if (line_end == nullptr) break;

            float free_space = main_available - base_total;
            const bool main_axis_scrolls = node->style.scrollable_x;
            const float distribute_positive = free_space > 0.0f && grow_total > 0.0f ? free_space : 0.0f;
            const float distribute_negative = !main_axis_scrolls && free_space < 0.0f && shrink_weight > 0.0f ? free_space : 0.0f;
            if (main_axis_scrolls && free_space < 0.0f) free_space = 0.0f;
            float cursor = content.min.x;
            float between_gap = gap;
            if (distribute_positive == 0.0f) {
                if (node->style.justify_content == Justify::Center) cursor += free_space * 0.5f;
                else if (node->style.justify_content == Justify::End) cursor += free_space;
                else if (node->style.justify_content == Justify::SpaceBetween && line_count > 1u)
                    between_gap += free_space / static_cast<float>(line_count - 1u);
                else if (node->style.justify_content == Justify::SpaceAround && line_count > 0u) {
                    between_gap += free_space / static_cast<float>(line_count);
                    cursor += (between_gap - gap) * 0.5f;
                }
            }

            for (Node* child = line_start; child != nullptr; child = child->next_sibling) {
                const float base = DesiredMain(child, true);
                float main_size = base;
                if (distribute_positive > 0.0f && grow_total > 0.0f)
                    main_size += distribute_positive * std::max(0.0f, child->style.flex_grow) / grow_total;
                else if (distribute_negative < 0.0f && shrink_weight > 0.0f) {
                    const float weight = std::max(0.0f, child->style.flex_shrink) * base;
                    main_size += distribute_negative * weight / shrink_weight;
                }
                main_size = ClampSize(main_size, child->style.min_width, child->style.max_width);
                const Align align = EffectiveAlign(node, child);
                float cross_size = child->intrinsic.y;
                if (align == Align::Stretch && child->style.height < 0.0f)
                    cross_size = line_cross - child->style.margin.top - child->style.margin.bottom;
                cross_size = ClampSize(cross_size, child->style.min_height, child->style.max_height);
                cursor += child->style.margin.left;
                float cross_cursor = line_y + child->style.margin.top;
                if (align == Align::Center)
                    cross_cursor += (line_cross - cross_size - child->style.margin.top - child->style.margin.bottom) * 0.5f;
                else if (align == Align::End)
                    cross_cursor += line_cross - cross_size - child->style.margin.top - child->style.margin.bottom;
                SetChildRect(context, child, Rect{cursor, cross_cursor, main_size, cross_size}, theme, depth + 1u);
                cursor += main_size + child->style.margin.right + between_gap;
                if (child == line_end) break;
            }
            line_y += line_cross + gap;
            line_start = line_end->next_sibling;
        }
        return;
    }

    float base_total = 0.0f;
    float grow_total = 0.0f;
    float shrink_weight = 0.0f;
    std::size_t count = 0;
    for (Node* child = node->first_child; child != nullptr; child = child->next_sibling) {
        base_total += DesiredMain(child, row) + MarginMain(child, row);
        grow_total += std::max(0.0f, child->style.flex_grow);
        shrink_weight += std::max(0.0f, child->style.flex_shrink) * DesiredMain(child, row);
        ++count;
    }
    if (count > 1u) base_total += gap * static_cast<float>(count - 1u);
    float free_space = main_available - base_total;
    const bool main_axis_scrolls = (row && node->style.scrollable_x) || (!row && node->style.scrollable_y);
    const float distribute_positive = free_space > 0.0f && grow_total > 0.0f ? free_space : 0.0f;
    const float distribute_negative = !main_axis_scrolls && free_space < 0.0f && shrink_weight > 0.0f ? free_space : 0.0f;
    if (main_axis_scrolls && free_space < 0.0f) free_space = 0.0f;
    float cursor = row ? content.min.x : content.min.y;
    float between_gap = gap;
    if (distribute_positive == 0.0f) {
        if (node->style.justify_content == Justify::Center) cursor += free_space * 0.5f;
        else if (node->style.justify_content == Justify::End) cursor += free_space;
        else if (node->style.justify_content == Justify::SpaceBetween && count > 1u) between_gap += free_space / static_cast<float>(count - 1u);
        else if (node->style.justify_content == Justify::SpaceAround && count > 0u) {
            between_gap += free_space / static_cast<float>(count);
            cursor += (between_gap - gap) * 0.5f;
        }
    }

    for (Node* child = node->first_child; child != nullptr; child = child->next_sibling) {
        const float base = DesiredMain(child, row);
        float main_size = base;
        if (distribute_positive > 0.0f && grow_total > 0.0f) {
            main_size += distribute_positive * std::max(0.0f, child->style.flex_grow) / grow_total;
        } else if (distribute_negative < 0.0f && shrink_weight > 0.0f) {
            const float weight = std::max(0.0f, child->style.flex_shrink) * base;
            main_size += distribute_negative * weight / shrink_weight;
        }
        main_size = ClampSize(main_size, row ? child->style.min_width : child->style.min_height,
                              row ? child->style.max_width : child->style.max_height);
        const Align align = EffectiveAlign(node, child);
        float cross_size = row ? child->intrinsic.y : child->intrinsic.x;
        const float cross_margin_start = row ? child->style.margin.top : child->style.margin.left;
        const float cross_margin_end = row ? child->style.margin.bottom : child->style.margin.right;
        const bool cross_auto = row ? child->style.height < 0.0f : child->style.width < 0.0f;
        if (align == Align::Stretch && cross_auto) cross_size = cross_available - cross_margin_start - cross_margin_end;
        cross_size = ClampSize(cross_size, row ? child->style.min_height : child->style.min_width,
                               row ? child->style.max_height : child->style.max_width);
        const float main_start_margin = row ? child->style.margin.left : child->style.margin.top;
        const float main_end_margin = row ? child->style.margin.right : child->style.margin.bottom;
        cursor += main_start_margin;
        float cross_cursor = row ? content.min.y + child->style.margin.top : content.min.x + child->style.margin.left;
        if (align == Align::Center) cross_cursor += (cross_available - cross_size - cross_margin_start - cross_margin_end) * 0.5f;
        else if (align == Align::End) cross_cursor += cross_available - cross_size - cross_margin_start - cross_margin_end;
        Rect child_rect{};
        if (row) child_rect = Rect{cursor, cross_cursor, main_size, cross_size};
        else child_rect = Rect{cross_cursor, cursor, cross_size, main_size};
        SetChildRect(context, child, child_rect, theme, depth + 1u);
        cursor += main_size + main_end_margin + between_gap;
    }
}

void SetChildRect(Context* context, Node* child, Rect target_rect, const Theme& theme, std::size_t depth) noexcept {
    child->target_rect = target_rect;
    child->rect = child->style.animate_layout
        ? Animate(context, MakeId(child->id, "layout-rect"), target_rect, theme.layout_spring)
        : target_rect;
    LayoutChildren(context, child, theme, depth);
}

bool IsPointerTarget(const Node* node) noexcept {
    if (node == nullptr || !node->style.pointer_events || node->style.disabled) return false;
    // A styled container can be its own target even without an event callback.
    // This permits hover/pressed selectors on cards and custom surfaces while
    // child controls continue to win hit testing because children are visited first.
    return node->kind == NodeKind::Button || node->style.event_handler != nullptr ||
           node->style.has_hover_background || node->style.has_pressed_background ||
           (node->style.shadow != nullptr && node->style.shadow->has_hover) ||
           node->style.focusable;
}

bool ContainsHitTarget(const Node* node, Vec2 position, const Theme& theme) noexcept {
    if (node == nullptr) return false;
    const float target = std::isfinite(theme.minimum_hit_target) ? std::max(0.0f, theme.minimum_hit_target) : 0.0f;
    const float automatic_x = std::max(0.0f, target - node->rect.Width()) * 0.5f;
    const float automatic_y = std::max(0.0f, target - node->rect.Height()) * 0.5f;
    const auto nonnegative = [](float value) noexcept { return std::isfinite(value) ? std::max(0.0f, value) : 0.0f; };
    Rect hit = node->rect;
    hit.min.x -= automatic_x + nonnegative(node->style.hit_slop.left);
    hit.max.x += automatic_x + nonnegative(node->style.hit_slop.right);
    hit.min.y -= automatic_y + nonnegative(node->style.hit_slop.top);
    hit.max.y += automatic_y + nonnegative(node->style.hit_slop.bottom);
    return hit.Contains(position);
}

Node* TopmostPointerAt(Node* node, Vec2 position, Rect clip, const Theme& theme, std::size_t depth = 0u) noexcept {
    if (node == nullptr || depth > 512u) return nullptr;
    Rect child_clip = clip;
    if (node->style.scrollable_x || node->style.scrollable_y || node->style.clip_children) {
        const Rect own_clip = (node->style.scrollable_x || node->style.scrollable_y)
            ? InsetRect(node->rect, node->style.padding) : node->rect;
        child_clip = IntersectRect(clip, own_clip);
    }

    Node* target = nullptr;
    for (Node* child = node->first_child; child != nullptr; child = child->next_sibling) {
        Node* candidate = TopmostPointerAt(child, position, child_clip, theme, depth + 1u);
        if (candidate != nullptr) target = candidate;
    }
    if (target != nullptr) return target;
    if (IsPointerTarget(node) && ContainsHitTarget(node, position, theme) && clip.Contains(position)) return node;
    return nullptr;
}

std::size_t FindPortalIndex(Context* context, Node* node) noexcept;
std::uint64_t PortalLayerId(Context* context, Node* node) noexcept;
bool PortalLayoutValid(Context* context, Node* node) noexcept;
Node* FindPortalByLayer(Context* context, std::uint64_t layer_id) noexcept;
void ResolvePortalLayouts(Context* context) noexcept;
void BuildActivePortals(Context* context) noexcept;

Node* FindScrollTarget(Node* node, Vec2 position, Rect clip, std::size_t depth, std::size_t& best_depth) noexcept;

Node* HitTestUI(Context* context, Vec2 position) noexcept {
    if (context == nullptr) return nullptr;
    const Rect viewport = context->frame_info.viewport.bounds;
    for (std::size_t i = context->active_portal_count; i > 0u; --i) {
        Node* portal = context->active_portals[i - 1u];
        if (portal == nullptr || !PortalLayoutValid(context, portal) || !context->layers.Find(PortalLayerId(context, portal))) continue;
        if (context->layers.BlocksInput(PortalLayerId(context, portal))) continue;
        Node* hit = TopmostPointerAt(portal, position, viewport, context->theme);
        if (hit != nullptr) return hit;
    }
    if (context->layers.HasModal()) return nullptr;
    return TopmostPointerAt(context->root_node, position, viewport, context->theme);
}

Node* FindScrollTargetUI(Context* context, Vec2 position) noexcept {
    if (context == nullptr) return nullptr;
    const Rect viewport = context->frame_info.viewport.bounds;
    for (std::size_t i = context->active_portal_count; i > 0u; --i) {
        Node* portal = context->active_portals[i - 1u];
        if (portal == nullptr || !PortalLayoutValid(context, portal) || !context->layers.Find(PortalLayerId(context, portal))) continue;
        if (context->layers.BlocksInput(PortalLayerId(context, portal))) continue;
        if (!portal->rect.Contains(position)) continue;
        std::size_t depth = 0u;
        if (Node* target = FindScrollTarget(portal, position, viewport, 0u, depth)) return target;
    }
    if (context->layers.HasModal()) return nullptr;
    std::size_t depth = 0u;
    return FindScrollTarget(context->root_node, position, viewport, 0u, depth);
}

void DismissOutsideLayer(Context* context, Vec2 position) noexcept {
    if (context == nullptr) return;
    const LayerEntry* top = context->layers.Top();
    if (top == nullptr || (top->dismiss_policy & DismissOnOutsideClick) == 0u) return;
    Node* portal = FindPortalByLayer(context, top->id);
    if (portal == nullptr || !PortalLayoutValid(context, portal) || portal->rect.Contains(position)) return;
    CloseLayer(context, top->id);
}

Node* FindScrollTarget(Node* node, Vec2 position, Rect clip, std::size_t depth, std::size_t& best_depth) noexcept {
    Node* target = nullptr;
    Rect child_clip = clip;
    if (node->style.scrollable_x || node->style.scrollable_y || node->style.clip_children) {
        const Rect own_clip = (node->style.scrollable_x || node->style.scrollable_y)
            ? InsetRect(node->rect, node->style.padding) : node->rect;
        child_clip = IntersectRect(clip, own_clip);
    }
    for (Node* child = node->first_child; child != nullptr; child = child->next_sibling) {
        if ((child->style.scrollable_x || child->style.scrollable_y) && child_clip.Contains(position) && child->rect.Contains(position) && depth + 1u >= best_depth) {
            target = child;
            best_depth = depth + 1u;
        }
        Node* nested = FindScrollTarget(child, position, child_clip, depth + 1u, best_depth);
        if (nested != nullptr) target = nested;
    }
    return target;
}

void ApplyWheel(Context* context, Node* target, const ImGuiIO& io, const Theme& theme) noexcept {
    if (context == nullptr || target == nullptr || (io.MouseWheel == 0.0f && io.MouseWheelH == 0.0f)) return;
    float remaining_x = -io.MouseWheelH * theme.spacing * theme.scroll_wheel_multiplier;
    float remaining_y = -io.MouseWheel * theme.spacing * theme.scroll_wheel_multiplier;
    bool changed = false;
    for (Node* current = target; current != nullptr; current = current->parent) {
        if (!current->style.scrollable_x && !current->style.scrollable_y) continue;
        ScrollMemory* memory = State<ScrollMemory>(context, MakeId(current->id, "scroll-offset"));
        if (memory == nullptr) continue;
        const Rect content = InsetRect(current->target_rect, current->style.padding);
        if (current->style.scrollable_x && std::fabs(remaining_x) > 0.001f) {
            const float old = memory->offset.x;
            const float max_offset = std::max(0.0f, current->content_extent.x - content.Width());
            memory->offset.x = Clamp(old + remaining_x, 0.0f, max_offset);
            remaining_x -= memory->offset.x - old;
            changed = changed || memory->offset.x != old;
        }
        if (current->style.scrollable_y && std::fabs(remaining_y) > 0.001f) {
            const float old = memory->offset.y;
            const float max_offset = std::max(0.0f, current->content_extent.y - content.Height());
            memory->offset.y = Clamp(old + remaining_y, 0.0f, max_offset);
            remaining_y -= memory->offset.y - old;
            changed = changed || memory->offset.y != old;
        }
        if (std::fabs(remaining_x) <= 0.001f && std::fabs(remaining_y) <= 0.001f) break;
    }
    if (!changed) return;
    Node* layout_root = context->root_node;
    for (std::size_t i = 0u; i < context->portal_count; ++i) {
        Node* portal = context->portal_nodes[i];
        for (Node* current = target; current != nullptr; current = current->parent) {
            if (current == portal) {
                layout_root = portal;
                break;
            }
        }
        if (layout_root == portal) break;
    }
    LoadScrollOffsets(context, layout_root);
    LayoutChildren(context, layout_root, context->theme, 0u);
    if (layout_root == context->root_node) {
        // A base-tree scroll can move an anchor; resolve overlays again in the same frame.
        ResolvePortalLayouts(context);
        BuildActivePortals(context);
    }
    RequestRedraw(context);
}

bool IsFocusable(const Node* node) noexcept {
    return node != nullptr && !node->style.disabled &&
           (node->kind == NodeKind::Button || node->style.focusable);
}

std::size_t CountFocusable(const Node* node, std::size_t depth = 0u) noexcept {
    if (node == nullptr || depth > 512u) return 0u;
    std::size_t count = 0u;
    for (const Node* child = node->first_child; child != nullptr; child = child->next_sibling) {
        if (IsFocusable(child)) ++count;
        count += CountFocusable(child, depth + 1u);
    }
    return count;
}

Node* FindFocusableByIndex(Node* node, std::size_t wanted, std::size_t& cursor, std::size_t depth = 0u) noexcept {
    if (node == nullptr || depth > 512u) return nullptr;
    for (Node* child = node->first_child; child != nullptr; child = child->next_sibling) {
        if (IsFocusable(child)) {
            if (cursor == wanted) return child;
            ++cursor;
        }
        if (Node* nested = FindFocusableByIndex(child, wanted, cursor, depth + 1u)) return nested;
    }
    return nullptr;
}

Node* FindFocusableById(Node* node, std::uint64_t id, std::size_t& index, std::size_t& cursor, std::size_t depth = 0u) noexcept {
    if (node == nullptr || depth > 512u) return nullptr;
    for (Node* child = node->first_child; child != nullptr; child = child->next_sibling) {
        if (IsFocusable(child)) {
            if (child->id == id) {
                index = cursor;
                return child;
            }
            ++cursor;
        }
        if (Node* nested = FindFocusableById(child, id, index, cursor, depth + 1u)) return nested;
    }
    return nullptr;
}

std::size_t FindPortalIndex(Context* context, Node* node) noexcept;
std::uint64_t PortalLayerId(Context* context, Node* node) noexcept;
bool PortalLayoutValid(Context* context, Node* node) noexcept;

Node* FocusRootForContext(Context* context) noexcept {
    if (context == nullptr) return nullptr;
    for (std::size_t i = context->active_portal_count; i > 0u; --i) {
        Node* portal = context->active_portals[i - 1u];
        if (portal == nullptr) continue;
        const LayerEntry* layer = context->layers.Find(PortalLayerId(context, portal));
        if (layer != nullptr && layer->kind == LayerKind::Modal && PortalLayoutValid(context, portal)) return portal;
    }
    return context->root_node;
}

std::size_t CountFocusableContext(Context* context) noexcept {
    if (context == nullptr || context->root_node == nullptr) return 0u;
    std::size_t count = CountFocusable(context->root_node);
    // In the absence of a modal, focus traversal is base content followed by active
    // floating-layer roots in paint order. Modal traversal is isolated by FocusRootForContext.
    for (std::size_t i = 0u; i < context->active_portal_count; ++i) {
        Node* portal = context->active_portals[i];
        if (portal == nullptr || !PortalLayoutValid(context, portal)) continue;
        const std::uint64_t layer_id = PortalLayerId(context, portal);
        if (context->layers.BlocksInput(layer_id)) continue;
        count += CountFocusable(portal);
    }
    return count;
}

Node* FindFocusableByIndexContext(Context* context, std::size_t wanted, std::size_t& cursor) noexcept {
    if (context == nullptr || context->root_node == nullptr) return nullptr;
    if (Node* found = FindFocusableByIndex(context->root_node, wanted, cursor)) return found;
    for (std::size_t i = 0u; i < context->active_portal_count; ++i) {
        Node* portal = context->active_portals[i];
        if (portal == nullptr || !PortalLayoutValid(context, portal)) continue;
        if (context->layers.BlocksInput(PortalLayerId(context, portal))) continue;
        if (Node* found = FindFocusableByIndex(portal, wanted, cursor)) return found;
    }
    return nullptr;
}

Node* FindFocusableByIdContext(Context* context, std::uint64_t id, std::size_t& index, std::size_t& cursor) noexcept {
    if (context == nullptr || context->root_node == nullptr) return nullptr;
    if (Node* found = FindFocusableById(context->root_node, id, index, cursor)) return found;
    for (std::size_t i = 0u; i < context->active_portal_count; ++i) {
        Node* portal = context->active_portals[i];
        if (portal == nullptr || !PortalLayoutValid(context, portal)) continue;
        if (context->layers.BlocksInput(PortalLayerId(context, portal))) continue;
        if (Node* found = FindFocusableById(portal, id, index, cursor)) return found;
    }
    return nullptr;
}

void SetFocusTarget(Context* context, int direction) noexcept {
    if (context == nullptr || context->root_node == nullptr) return;
    Node* scope_root = FocusRootForContext(context);
    if (scope_root == nullptr) return;
    const bool modal_scope = scope_root != context->root_node;
    const std::size_t count = modal_scope ? CountFocusable(scope_root) : CountFocusableContext(context);
    if (count == 0u) {
        context->focused_node_id = 0u;
        context->keyboard_focus_visible = true;
        return;
    }
    std::size_t current_index = 0u;
    std::size_t cursor = 0u;
    const Node* current = modal_scope
        ? FindFocusableById(scope_root, context->focused_node_id, current_index, cursor)
        : FindFocusableByIdContext(context, context->focused_node_id, current_index, cursor);
    std::size_t next_index = 0u;
    if (current == nullptr) {
        next_index = direction < 0 ? count - 1u : 0u;
    } else if (direction < 0) {
        next_index = (current_index + count - 1u) % count;
    } else {
        next_index = (current_index + 1u) % count;
    }
    cursor = 0u;
    Node* next = modal_scope ? FindFocusableByIndex(scope_root, next_index, cursor)
                            : FindFocusableByIndexContext(context, next_index, cursor);
    context->focused_node_id = next != nullptr ? next->id : 0u;
    context->keyboard_focus_visible = true;
    RequestRedraw(context);
}

Node* FindNodeById(Node* node, std::uint64_t id, std::size_t depth = 0u) noexcept {
    if (node == nullptr || id == 0u || depth > 512u) return nullptr;
    if (node->id == id) return node;
    for (Node* child = node->first_child; child != nullptr; child = child->next_sibling) {
        if (Node* found = FindNodeById(child, id, depth + 1u)) return found;
    }
    return nullptr;
}

std::size_t FindPortalIndex(Context* context, Node* node) noexcept {
    if (context == nullptr || node == nullptr) return LayerStack::kCapacity;
    for (std::size_t i = 0u; i < context->portal_count; ++i)
        if (context->portal_nodes[i] == node) return i;
    return LayerStack::kCapacity;
}

std::uint64_t PortalLayerId(Context* context, Node* node) noexcept {
    const std::size_t index = FindPortalIndex(context, node);
    return index < LayerStack::kCapacity ? context->portal_layer_ids[index] : 0u;
}

bool PortalLayoutValid(Context* context, Node* node) noexcept {
    const std::size_t index = FindPortalIndex(context, node);
    return index < LayerStack::kCapacity && context->portal_layout_valid[index];
}

Node* FindContextNodeById(Context* context, std::uint64_t id) noexcept {
    if (context == nullptr || id == 0u) return nullptr;
    if (Node* found = FindNodeById(context->root_node, id)) return found;
    for (std::size_t i = 0u; i < context->portal_count; ++i) {
        if (Node* found = FindNodeById(context->portal_nodes[i], id)) return found;
    }
    return nullptr;
}

Node* FindPortalByLayer(Context* context, std::uint64_t layer_id) noexcept {
    if (context == nullptr || layer_id == 0u) return nullptr;
    for (std::size_t i = 0u; i < context->portal_count; ++i) {
        Node* portal = context->portal_nodes[i];
        if (portal != nullptr && context->portal_layer_ids[i] == layer_id) return portal;
    }
    return nullptr;
}

bool GetVisibleAnchorRect(Node* anchor, const Rect& viewport, Rect* out_visible) noexcept {
    if (anchor == nullptr || out_visible == nullptr || anchor->rect.IsEmpty()) return false;
    Rect visible = IntersectRect(anchor->rect, viewport);
    if (visible.IsEmpty()) return false;
    for (Node* ancestor = anchor->parent; ancestor != nullptr; ancestor = ancestor->parent) {
        if (!ancestor->style.clip_children && !ancestor->style.scrollable_x && !ancestor->style.scrollable_y) continue;
        const Rect own_clip = (ancestor->style.scrollable_x || ancestor->style.scrollable_y)
            ? InsetRect(ancestor->rect, ancestor->style.padding) : ancestor->rect;
        visible = IntersectRect(visible, own_clip);
        if (visible.IsEmpty()) return false;
    }
    *out_visible = visible;
    return true;
}

void ResolvePortalLayouts(Context* context) noexcept {
    if (context == nullptr) return;
    const Viewport& viewport = context->frame_info.viewport;
    const Rect safe = InsetRect(viewport.bounds, viewport.safe_area);
    for (std::size_t i = 0u; i < context->portal_count; ++i) {
        Node* portal = context->portal_nodes[i];
        context->portal_layout_valid[i] = false;
        if (portal == nullptr || context->layers.Find(context->portal_layer_ids[i]) == nullptr || safe.IsEmpty()) continue;
        const PortalOptions& options = context->portal_options[i];
        Vec2 size{std::min(options.size.x, safe.Width()), std::min(options.size.y, safe.Height())};
        if (!IsFinite(size) || size.x <= 0.0f || size.y < 0.0f) continue;
        ResolveInherited(portal, context->root_node, context->theme);
        LoadScrollOffsets(context, portal);
        // Measure against the available width, not the requested width, so content also
        // wraps correctly when a dialog is larger than a small or split-screen viewport.
        portal->style.width = size.x;
        portal->style.height = -1.0f;
        const Vec2 measured = MeasureNode(context, portal, std::max(1.0f, size.x), context->theme);
        if (size.y <= 0.0f) size.y = std::min(measured.y, safe.Height());
        if (!(size.y > 0.0f)) continue;
        Rect resolved{};
        if (options.anchor_node_id != 0u) {
            Node* anchor = FindNodeById(context->root_node, options.anchor_node_id);
            Rect visible_anchor{};
            if (!GetVisibleAnchorRect(anchor, viewport.bounds, &visible_anchor)) continue;
            AnchorPlacementOptions placement{};
            placement.viewport = viewport.bounds;
            placement.safe_area = viewport.safe_area;
            placement.anchor = visible_anchor;
            placement.overlay_size = size;
            placement.placement = options.placement;
            placement.align = options.align;
            placement.offset = options.offset;
            placement.flip = options.flip;
            placement.clamp_to_safe_area = options.clamp_to_safe_area;
            AnchorPlacementResult result{};
            if (!ResolveAnchorPlacement(placement, &result)) continue;
            resolved = result.rect;
        } else {
            const PortalPin pin = options.pin_to_top_right ? PortalPin::TopRight : options.pin;
            const float margin = pin == PortalPin::Center ? 0.0f
                : std::min(options.edge_margin, std::min(safe.Width(), safe.Height()) * 0.5f);
            const bool left = pin == PortalPin::TopLeft || pin == PortalPin::BottomLeft;
            const bool right = pin == PortalPin::TopRight || pin == PortalPin::BottomRight;
            const bool top = pin == PortalPin::TopLeft || pin == PortalPin::TopCenter || pin == PortalPin::TopRight;
            const bool bottom = pin == PortalPin::BottomLeft || pin == PortalPin::BottomCenter || pin == PortalPin::BottomRight;
            const float x = left ? safe.min.x + margin : right ? safe.max.x - size.x - margin
                                                               : safe.min.x + (safe.Width() - size.x) * 0.5f;
            const float y = top ? safe.min.y + margin : bottom ? safe.max.y - size.y - margin
                                                               : safe.min.y + (safe.Height() - size.y) * 0.5f;
            // Clamp the origin so the full portal stays inside the safe viewport before the offset.
            const float clamped_x = Clamp(x, safe.min.x, std::max(safe.min.x, safe.max.x - size.x));
            const float clamped_y = Clamp(y, safe.min.y, std::max(safe.min.y, safe.max.y - size.y));
            resolved = Rect{clamped_x + options.offset.x, clamped_y + options.offset.y, size.x, size.y};
        }
        portal->style.width = resolved.Width();
        portal->style.height = resolved.Height();
        SetChildRect(context, portal, resolved, context->theme, 0u);
        context->portal_layout_valid[i] = !portal->rect.IsEmpty();
    }
}

void BuildActivePortals(Context* context) noexcept {
    if (context == nullptr) return;
    context->active_portal_count = 0u;
    LayerEntry ordered[LayerStack::kCapacity]{};
    const std::size_t count = context->layers.CopyPaintOrder(ordered, LayerStack::kCapacity);
    for (std::size_t i = 0u; i < count && context->active_portal_count < LayerStack::kCapacity; ++i) {
        Node* portal = FindPortalByLayer(context, ordered[i].id);
        if (portal != nullptr && PortalLayoutValid(context, portal)) {
            context->active_portals[context->active_portal_count++] = portal;
        }
    }
}

void InvokeNodeEvent(Context* context, Node* node, UIEvent& event) noexcept {
    if (context == nullptr || node == nullptr || node->style.event_handler == nullptr) return;
    context->dispatching_event = true;
    context->dispatch_current_target_id = node->id;
    context->dispatch_pointer_id = event.pointer_id;
    context->dispatch_event_type = event.type;
    node->style.event_handler(context, event, node->style.event_user_data);
    context->dispatching_event = false;
    context->dispatch_current_target_id = 0u;
    context->dispatch_pointer_id = 0u;
}

void DispatchEvent(Context* context, Node* target, UIEvent& event) noexcept {
    if (context == nullptr || target == nullptr) return;
    Node* path[513]{};
    std::size_t count = 0u;
    for (Node* current = target; current != nullptr && count < 513u; current = current->parent) {
        path[count++] = current;
    }
    if (count == 0u || (count == 513u && path[count - 1u]->parent != nullptr)) {
        event.PreventDefault();
        return;
    }
    for (std::size_t i = 0; i < count / 2u; ++i) {
        Node* swap = path[i];
        path[i] = path[count - i - 1u];
        path[count - i - 1u] = swap;
    }
    event.target_id = target->id;
    event.current_target_id = 0u;
    for (std::size_t i = 0; i + 1u < count; ++i) {
        event.phase = UIEventPhase::Capture;
        event.current_target_id = path[i]->id;
        InvokeNodeEvent(context, path[i], event);
        if (event.propagation_stopped) return;
    }
    event.phase = UIEventPhase::Target;
    event.current_target_id = target->id;
    InvokeNodeEvent(context, target, event);
    if (event.propagation_stopped) return;
    if (count > 1u) {
        for (std::size_t i = count - 1u; i > 0u; --i) {
            event.phase = UIEventPhase::Bubble;
            event.current_target_id = path[i - 1u]->id;
            InvokeNodeEvent(context, path[i - 1u], event);
            if (event.propagation_stopped) return;
        }
    }
    event.current_target_id = 0u;
}

void DispatchFocusEvent(Context* context, std::uint64_t node_id, UIEventType type) noexcept {
    Node* node = context != nullptr ? FindContextNodeById(context, node_id) : nullptr;
    if (node == nullptr) return;
    UIEvent event{};
    event.type = type;
    DispatchEvent(context, node, event);
}

bool IsPointerEvent(UIEventType type) noexcept {
    return type == UIEventType::PointerEnter || type == UIEventType::PointerLeave ||
           type == UIEventType::PointerMove || type == UIEventType::PointerDown ||
           type == UIEventType::PointerUp;
}

void InvokeClickHandler(Context* context, Node* target) noexcept {
    if (context == nullptr || target == nullptr || target->kind != NodeKind::Button ||
        target->style.disabled || target->click_handler == nullptr) return;
    target->clicked = true;
    target->click_handler(context, target->id, target->user_data);
}

bool IsNodeOnPath(const Node* node, const Node* leaf) noexcept {
    for (const Node* current = leaf; current != nullptr; current = current->parent) {
        if (current == node) return true;
    }
    return false;
}

void ApplyHoverAndClicks(Node* node, Node* hit, Node* active, std::uint64_t focused_id,
                         bool focus_visible, std::size_t depth = 0u) noexcept {
    if (node == nullptr || depth > 512u) return;
    for (Node* child = node->first_child; child != nullptr; child = child->next_sibling) {
        const bool receives_pointer_state = child->style.pointer_events && !child->style.disabled;
        // Hover/pressed selectors work on any pointer-enabled node in the target
        // ancestry, not just Button. This mirrors the event capture/bubble path.
        child->hovered = receives_pointer_state && IsNodeOnPath(child, hit);
        child->pressed = receives_pointer_state && IsNodeOnPath(child, active);
        child->focused = child->id == focused_id;
        child->focus_visible = child->focused && focus_visible;
        ApplyHoverAndClicks(child, hit, active, focused_id, focus_visible, depth + 1u);
    }
}

Color ResolveNodeFill(Context* context, Node* node, const Theme& theme, bool& has_fill) noexcept {
    Color fill{};
    has_fill = false;
    if (node->kind == NodeKind::Root) {
        fill = theme.canvas;
        has_fill = context->nodes_built > 0u;
    } else if (node->kind == NodeKind::Button) {
        if (node->style.disabled) {
            fill = node->style.has_disabled_background ? node->style.disabled_background : theme.surface_alt;
        } else if (node->pressed) {
            fill = node->style.has_pressed_background ? node->style.pressed_background : theme.accent_pressed;
        } else if (node->hovered) {
            fill = node->style.has_hover_background ? node->style.hover_background : theme.accent_hover;
        } else {
            fill = node->style.has_background ? node->style.background : theme.accent;
        }
        has_fill = true;
    } else if (node->style.disabled && node->style.has_disabled_background) {
        fill = node->style.disabled_background;
        has_fill = true;
    } else if (node->pressed && node->style.has_pressed_background) {
        fill = node->style.pressed_background;
        has_fill = true;
    } else if (node->hovered && node->style.has_hover_background) {
        fill = node->style.hover_background;
        has_fill = true;
    } else if (node->style.has_background) {
        fill = node->style.background;
        has_fill = true;
    }
    if (has_fill && (node->kind != NodeKind::Root || context->nodes_built > 0u)) {
        fill = Animate(context, MakeId(node->id, "paint-fill"), fill, theme.color_transition);
    }
    return WithOpacity(fill, node->resolved_opacity);
}


struct ResolvedShadow {
    bool active = false;
    float blur = 0.0f;
    float offset_x = 0.0f;
    float offset_y = 0.0f;
    float spread = 0.0f;
    Color color{};
};

// Resolves the (animated) drop shadow for a node. Idempotent within a frame so both the counting
// pass and the build pass see identical results. Nodes that never request a shadow create no state.
ResolvedShadow ResolveNodeShadow(Context* context, Node* node, const Theme& theme) noexcept {
    ResolvedShadow shadow{};
    const ShadowStyle* style = node->style.shadow;
    if (style == nullptr) return shadow;
    const bool pointer_lift = node->style.pointer_events && node->hovered && !node->style.disabled;
    const bool lifted = pointer_lift && style->has_hover;
    const float target_blur = std::max(0.0f, lifted ? style->hover_blur : style->blur);
    if (!(target_blur > 0.0f) && !(style->has_hover && pointer_lift)) return shadow;

    const float target_offset_y = lifted ? style->hover_offset_y : style->offset_y;
    Color target_color = style->has_color ? style->color : theme.shadow;
    if (!(target_blur > 0.0f)) target_color.a = 0.0f; // resting pointer target with only a hover shadow fades in from nothing

    const TweenSpec& spec = theme.color_transition;
    shadow.blur = Animate(context, MakeId(node->id, "paint-shadow-blur"), target_blur, spec);
    shadow.offset_y = Animate(context, MakeId(node->id, "paint-shadow-dy"), target_offset_y, spec);
    shadow.color = Animate(context, MakeId(node->id, "paint-shadow-color"), target_color, spec);
    shadow.offset_x = style->offset_x;
    shadow.spread = style->spread;
    shadow.color = WithOpacity(shadow.color, node->resolved_opacity);
    shadow.active = shadow.blur > 0.01f && shadow.color.a > 0.002f;
    return shadow;
}

std::size_t CountRenderCommands(Context* context, Node* node, std::size_t depth) noexcept {
    if (depth > 512u || node->resolved_opacity <= 0.0f) return 0u;
    bool has_fill = false;
    (void)ResolveNodeFill(context, node, context->theme, has_fill);
    std::size_t count = has_fill ? 1u : 0u;
    if (!node->rect.IsEmpty() && ResolveNodeShadow(context, node, context->theme).active) ++count;
    if (node->style.border_width > 0.0f && node->style.has_border_color) ++count;
    if (node->focused && node->focus_visible && !node->rect.IsEmpty()) ++count;
    if (node->text != nullptr && node->text[0] != '\0') ++count;
    if (node->kind == NodeKind::Image) ++count;
    if (node->style.custom_paint != nullptr && !node->rect.IsEmpty()) ++count;
    const bool clip_children = node->style.clip_children || node->style.scrollable_x || node->style.scrollable_y;
    if (clip_children && node->first_child != nullptr) count += 2u;
    for (Node* child = node->first_child; child != nullptr; child = child->next_sibling) {
        count += CountRenderCommands(context, child, depth + 1u);
    }
    return count;
}

void PushCommand(Context* context, const RenderCommand& command, std::size_t& cursor) noexcept {
    if (cursor < context->render_command_count) context->render_commands[cursor++] = command;
}

void BuildRenderCommands(Context* context, Node* node, const Theme& theme, std::size_t depth, std::size_t& cursor) noexcept {
    if (depth > 512u || node->resolved_opacity <= 0.0f) return;
    if (node->kind != NodeKind::Root) ++context->nodes_painted;
    bool has_fill = false;
    const Color fill = ResolveNodeFill(context, node, theme, has_fill);
    const float radius = node->style.radius >= 0.0f ? node->style.radius : theme.radius;
    if (!node->rect.IsEmpty()) {
        const ResolvedShadow shadow = ResolveNodeShadow(context, node, theme);
        if (shadow.active) {
            RenderCommand command{};
            command.type = RenderCommandType::Shadow;
            const float grow = shadow.spread;
            command.rect = Rect{{node->rect.min.x - grow + shadow.offset_x, node->rect.min.y - grow + shadow.offset_y},
                                {node->rect.max.x + grow + shadow.offset_x, node->rect.max.y + grow + shadow.offset_y}};
            command.color = shadow.color;
            command.radius = std::max(0.0f, radius + grow);
            command.thickness = shadow.blur;
            PushCommand(context, command, cursor);
        }
    }
    if (has_fill && !node->rect.IsEmpty()) {
        RenderCommand command{};
        command.type = RenderCommandType::FillRect;
        command.rect = node->rect;
        command.color = fill;
        command.radius = radius;
        PushCommand(context, command, cursor);
    }
    if (node->style.custom_paint != nullptr && !node->rect.IsEmpty()) {
        RenderCommand command{};
        command.type = RenderCommandType::Paint;
        command.rect = node->rect;
        command.node = node;
        PushCommand(context, command, cursor);
    }
    if (node->style.border_width > 0.0f && node->style.has_border_color && !node->rect.IsEmpty()) {
        RenderCommand command{};
        command.type = RenderCommandType::StrokeRect;
        command.rect = node->rect;
        command.color = WithOpacity(node->style.border_color, node->resolved_opacity);
        command.radius = radius;
        command.thickness = node->style.border_width;
        PushCommand(context, command, cursor);
    }
    if (node->focused && node->focus_visible && !node->rect.IsEmpty()) {
        RenderCommand focus{};
        focus.type = RenderCommandType::StrokeRect;
        const float outset = std::max(0.0f, theme.focus_ring_outset);
        focus.rect = Rect{{node->rect.min.x - outset, node->rect.min.y - outset},
                          {node->rect.max.x + outset, node->rect.max.y + outset}};
        focus.color = WithOpacity(theme.accent, node->resolved_opacity);
        focus.radius = radius + outset;
        focus.thickness = std::max(0.0f, theme.focus_ring_width);
        PushCommand(context, focus, cursor);
    }

    if (node->kind == NodeKind::Image && !node->rect.IsEmpty()) {
        RenderCommand command{};
        command.type = RenderCommandType::Image;
        command.rect = InsetRect(node->rect, node->style.padding);
        command.texture = node->image != nullptr ? node->image->texture : ImTextureID{};
        command.color = WithOpacity(Color{1.0f, 1.0f, 1.0f, 1.0f}, node->resolved_opacity);
        const float source_w = std::max(0.001f, (node->image != nullptr ? node->image->source_size.x : 0.0f));
        const float source_h = std::max(0.001f, (node->image != nullptr ? node->image->source_size.y : 0.0f));
        command.font_size = source_w;
        command.wrap_width = source_h;
        command.thickness = node->image != nullptr ? static_cast<float>(node->image->fit) : 0.0f;
        const float target_w = std::max(0.0f, command.rect.Width());
        const float target_h = std::max(0.0f, command.rect.Height());
        if (node->image != nullptr && node->image->fit == ImageFit::Contain) {
            const float scale = std::min(target_w / source_w, target_h / source_h);
            const float draw_w = source_w * scale;
            const float draw_h = source_h * scale;
            const float left = command.rect.min.x + (target_w - draw_w) * 0.5f;
            const float top = command.rect.min.y + (target_h - draw_h) * 0.5f;
            command.rect = Rect{left, top, draw_w, draw_h};
        }
        command.radius = radius;
        PushCommand(context, command, cursor);
    }

    if (node->text != nullptr && node->text[0] != '\0') {
        const bool button_text = node->kind == NodeKind::Button;
        const float available_text_width = std::max(0.0f, node->rect.Width() - node->style.padding.left - node->style.padding.right);
        float wrap_width = button_text ? 0.0f : available_text_width;
        // Text laid out at its own measured width must not wrap: ImGui breaks the last word when the
        // wrap width equals the line width.
        if (wrap_width > 0.0f && MeasureText(context, node->text, node->resolved_font_size, 0.0f).x <= wrap_width + 0.5f) {
            wrap_width = 0.0f;
        }
        const Vec2 text_size = MeasureText(context, node->text, node->resolved_font_size, wrap_width);
        float x = node->rect.min.x + node->style.padding.left;
        float y = node->rect.min.y + node->style.padding.top;
        if (button_text) {
            x = node->rect.min.x + (node->rect.Width() - text_size.x) * 0.5f;
            y = node->rect.min.y + (node->rect.Height() - text_size.y) * 0.5f;
        } else if (node->style.text_align == TextAlign::Center) {
            x = node->rect.min.x + (node->rect.Width() - text_size.x) * 0.5f;
        } else if (node->style.text_align == TextAlign::End) {
            x = node->rect.max.x - node->style.padding.right - text_size.x;
        }
        Color text_color = node->resolved_text_color;
        if (node->kind == NodeKind::Button && !node->style.has_text_color) text_color = theme.accent_text;
        if (node->style.disabled && node->kind == NodeKind::Button) text_color = theme.muted_text;
        text_color = Animate(context, MakeId(node->id, "paint-text"), text_color, theme.text_transition);
        RenderCommand command{};
        command.type = RenderCommandType::Text;
        command.rect = Rect{x, y, text_size.x, text_size.y};
        command.color = WithOpacity(text_color, node->resolved_opacity);
        command.text = node->text;
        command.font_size = node->resolved_font_size;
        command.wrap_width = wrap_width;
        PushCommand(context, command, cursor);
    }

    const bool clip_children = node->style.clip_children || node->style.scrollable_x || node->style.scrollable_y;
    if (clip_children && node->first_child != nullptr) {
        const Rect child_clip = (node->style.scrollable_x || node->style.scrollable_y)
            ? InsetRect(node->rect, node->style.padding) : node->rect;
        RenderCommand command{};
        command.type = RenderCommandType::PushClip;
        command.rect = child_clip;
        PushCommand(context, command, cursor);
    }
    for (Node* child = node->first_child; child != nullptr; child = child->next_sibling) {
        BuildRenderCommands(context, child, theme, depth + 1u, cursor);
    }
    if (clip_children && node->first_child != nullptr) {
        RenderCommand command{};
        command.type = RenderCommandType::PopClip;
        PushCommand(context, command, cursor);
    }
}

// Soft shadow from stacked rounded rects. Layer m (1 = outermost) expands the rect by
// blur * (n - m) / n and is given just enough alpha that the *composited* opacity follows a
// smoothstep ramp up to the shadow colour's alpha, which reads as a gaussian-ish falloff.
void DrawShadow(ImDrawList* draw_list, const RenderCommand& command) noexcept {
    const float blur = command.thickness;
    const float peak = Clamp(command.color.a, 0.0f, 1.0f);
    if (!(blur > 0.01f) || !(peak > 0.0f) || command.rect.IsEmpty()) return;
    const int layers = std::min(20, std::max(4, static_cast<int>(std::ceil(blur / 1.5f))));
    float previous_total = 0.0f;
    for (int m = 1; m <= layers; ++m) {
        const float t = static_cast<float>(m) / static_cast<float>(layers);
        const float total = peak * (t * t * (3.0f - 2.0f * t));
        const float layer_alpha = Clamp(1.0f - (1.0f - total) / std::max(1.0e-4f, 1.0f - previous_total), 0.0f, 1.0f);
        previous_total = total;
        if (layer_alpha <= 0.0f) continue;
        const float expand = blur * static_cast<float>(layers - m) / static_cast<float>(layers);
        draw_list->AddRectFilled(ImVec2(command.rect.min.x - expand, command.rect.min.y - expand),
                                 ImVec2(command.rect.max.x + expand, command.rect.max.y + expand),
                                 ToImU32(Color{command.color.r, command.color.g, command.color.b, layer_alpha}, 1.0f),
                                 command.radius + expand);
    }
}

void TranslateRenderList(Context* context, ImDrawList* draw_list) noexcept {
    if (context == nullptr || draw_list == nullptr) return;
    for (std::size_t index = 0; index < context->render_command_count; ++index) {
        const RenderCommand& command = context->render_commands[index];
        switch (command.type) {
            case RenderCommandType::Shadow:
                DrawShadow(draw_list, command);
                break;
            case RenderCommandType::FillRect:
                draw_list->AddRectFilled(ImVec2(command.rect.min.x, command.rect.min.y),
                                         ImVec2(command.rect.max.x, command.rect.max.y),
                                         ToImU32(command.color, 1.0f), command.radius);
                break;
            case RenderCommandType::StrokeRect:
                draw_list->AddRect(ImVec2(command.rect.min.x, command.rect.min.y),
                                   ImVec2(command.rect.max.x, command.rect.max.y),
                                   ToImU32(command.color, 1.0f), command.radius, 0, command.thickness);
                break;
            case RenderCommandType::Image:
                if (command.texture != ImTextureID{}) {
                    ImVec2 uv0(0.0f, 0.0f);
                    ImVec2 uv1(1.0f, 1.0f);
                    const float source_w = std::max(0.001f, command.font_size);
                    const float source_h = std::max(0.001f, command.wrap_width);
                    const float target_w = std::max(0.001f, command.rect.Width());
                    const float target_h = std::max(0.001f, command.rect.Height());
                    const float source_aspect = source_w / source_h;
                    const float target_aspect = target_w / target_h;
                    if (static_cast<ImageFit>(static_cast<std::uint8_t>(command.thickness)) == ImageFit::Cover) {
                        if (source_aspect > target_aspect) {
                            const float visible = target_aspect / source_aspect;
                            uv0.x = (1.0f - visible) * 0.5f;
                            uv1.x = 1.0f - uv0.x;
                        } else {
                            const float visible = source_aspect / target_aspect;
                            uv0.y = (1.0f - visible) * 0.5f;
                            uv1.y = 1.0f - uv0.y;
                        }
                    }
                    draw_list->AddImageRounded(command.texture,
                        ImVec2(command.rect.min.x, command.rect.min.y),
                        ImVec2(command.rect.max.x, command.rect.max.y), uv0, uv1,
                        ToImU32(command.color, 1.0f), command.radius);
                } else {
                    draw_list->AddRectFilled(ImVec2(command.rect.min.x, command.rect.min.y),
                        ImVec2(command.rect.max.x, command.rect.max.y),
                        IM_COL32(90, 100, 120, 100), command.radius);
                    draw_list->AddRect(ImVec2(command.rect.min.x, command.rect.min.y),
                        ImVec2(command.rect.max.x, command.rect.max.y), IM_COL32(150, 160, 180, 160), command.radius);
                }
                break;
            case RenderCommandType::Text: {
                const FontChoice font = PickFont(context, command.font_size);
                draw_list->AddText(font.font, font.size,
                                   ImVec2(command.rect.min.x, command.rect.min.y),
                                   ToImU32(command.color, 1.0f), command.text,
                                   command.text + std::strlen(command.text), command.wrap_width);
                break;
            }
            case RenderCommandType::PushClip:
                draw_list->PushClipRect(ImVec2(command.rect.min.x, command.rect.min.y),
                                        ImVec2(command.rect.max.x, command.rect.max.y), true);
                break;
            case RenderCommandType::PopClip:
                draw_list->PopClipRect();
                break;
            case RenderCommandType::Paint: {
                const CustomPaint* paint = command.node->style.custom_paint;
                Canvas canvas(draw_list, command.node->id, command.rect, command.node->resolved_opacity, context);
                paint->painter(context, canvas, paint->user_data);
                break;
            }
        }
    }
}

} // namespace

namespace detail {

Theme DefaultTheme(ThemeKind kind) noexcept {
    Theme theme{};
    if (kind == ThemeKind::Light) {
        theme.canvas = Rgb(0.955f, 0.965f, 0.985f);
        theme.surface = Rgb(1.0f, 1.0f, 1.0f);
        theme.surface_alt = Rgb(0.91f, 0.93f, 0.96f);
        theme.text = Rgb(0.10f, 0.13f, 0.19f);
        theme.muted_text = Rgb(0.37f, 0.42f, 0.50f);
        theme.border = Rgb(0.83f, 0.86f, 0.90f);
        theme.accent = Rgb(0.25f, 0.36f, 0.92f);
        theme.accent_hover = Rgb(0.20f, 0.31f, 0.84f);
        theme.accent_pressed = Rgb(0.15f, 0.25f, 0.73f);
        theme.accent_text = Rgb(1.0f, 1.0f, 1.0f);
        theme.success = Rgb(0.10f, 0.56f, 0.37f);
        theme.warning = Rgb(0.74f, 0.43f, 0.08f);
        theme.danger = Rgb(0.76f, 0.19f, 0.24f);
        theme.surface_container_high = Rgb(0.93f, 0.94f, 0.97f);
        theme.outline_variant = Rgb(0.90f, 0.92f, 0.95f);
        theme.primary_container = Rgb(0.86f, 0.89f, 1.0f);
        theme.secondary_container = Rgb(0.87f, 0.90f, 0.96f);
        theme.on_secondary_container = Rgb(0.10f, 0.13f, 0.19f);
        theme.inverse_surface = Rgb(0.17f, 0.19f, 0.24f);
        theme.inverse_on_surface = Rgb(0.95f, 0.96f, 0.98f);
        theme.inverse_primary = Rgb(0.66f, 0.73f, 1.0f);
        theme.inverse_info = Hex(0x8AB4F8);
        theme.inverse_success = Hex(0x81C995);
        theme.inverse_warning = Hex(0xFDD663);
        theme.inverse_danger = Hex(0xF28B82);
    } else if (kind == ThemeKind::HighContrast) {
        theme.canvas = Rgb(0.0f, 0.0f, 0.0f);
        theme.surface = Rgb(0.04f, 0.04f, 0.04f);
        theme.surface_alt = Rgb(0.12f, 0.12f, 0.12f);
        theme.text = Rgb(1.0f, 1.0f, 1.0f);
        theme.muted_text = Rgb(0.88f, 0.88f, 0.88f);
        theme.border = Rgb(1.0f, 1.0f, 1.0f);
        theme.accent = Rgb(1.0f, 0.86f, 0.0f);
        theme.accent_hover = Rgb(1.0f, 0.94f, 0.35f);
        theme.accent_pressed = Rgb(0.88f, 0.73f, 0.0f);
        theme.accent_text = Rgb(0.0f, 0.0f, 0.0f);
        theme.success = Rgb(0.20f, 1.0f, 0.44f);
        theme.warning = Rgb(1.0f, 0.82f, 0.15f);
        theme.danger = Rgb(1.0f, 0.26f, 0.26f);
        theme.surface_container_high = Rgb(0.08f, 0.08f, 0.08f);
        theme.outline_variant = Rgb(1.0f, 1.0f, 1.0f);
        theme.primary_container = Rgb(0.25f, 0.21f, 0.0f);
        theme.secondary_container = Rgb(0.12f, 0.12f, 0.12f);
        theme.on_secondary_container = Rgb(1.0f, 1.0f, 1.0f);
        theme.inverse_surface = Rgb(1.0f, 1.0f, 1.0f);
        theme.inverse_on_surface = Rgb(0.0f, 0.0f, 0.0f);
        theme.inverse_primary = Rgb(0.45f, 0.36f, 0.0f);
        theme.inverse_info = Rgb(0.0f, 0.25f, 0.75f);
        theme.inverse_success = Rgb(0.0f, 0.45f, 0.15f);
        theme.inverse_warning = Rgb(0.55f, 0.35f, 0.0f);
        theme.inverse_danger = Rgb(0.75f, 0.0f, 0.0f);
        theme.radius = 4.0f;
        theme.spacing = 10.0f;
        theme.font_size = 16.0f;
        theme.title_size = 26.0f;
        theme.button_min_height = 44.0f;
    } else {
        theme.canvas = Rgb(0.055f, 0.065f, 0.085f);
        theme.surface = Rgb(0.095f, 0.11f, 0.145f);
        theme.surface_alt = Rgb(0.14f, 0.16f, 0.20f);
        theme.text = Rgb(0.92f, 0.94f, 0.98f);
        theme.muted_text = Rgb(0.61f, 0.66f, 0.73f);
        theme.border = Rgb(0.23f, 0.26f, 0.32f);
        theme.accent = Rgb(0.35f, 0.43f, 0.98f);
        theme.accent_hover = Rgb(0.42f, 0.50f, 1.0f);
        theme.accent_pressed = Rgb(0.27f, 0.34f, 0.85f);
        theme.accent_text = Rgb(1.0f, 1.0f, 1.0f);
        theme.success = Rgb(0.25f, 0.78f, 0.53f);
        theme.warning = Rgb(0.98f, 0.69f, 0.24f);
        theme.danger = Rgb(0.97f, 0.36f, 0.40f);
        theme.surface_container_high = Rgb(0.17f, 0.19f, 0.24f);
        theme.outline_variant = Rgb(0.18f, 0.20f, 0.25f);
        theme.primary_container = Rgb(0.20f, 0.25f, 0.55f);
        theme.secondary_container = Rgb(0.20f, 0.23f, 0.33f);
        theme.on_secondary_container = Rgb(0.88f, 0.90f, 0.98f);
        theme.inverse_surface = Rgb(0.90f, 0.92f, 0.96f);
        theme.inverse_on_surface = Rgb(0.12f, 0.14f, 0.18f);
        theme.inverse_primary = Rgb(0.25f, 0.36f, 0.92f);
        theme.inverse_info = Rgb(0.20f, 0.42f, 0.85f);
        theme.inverse_success = Rgb(0.13f, 0.55f, 0.34f);
        theme.inverse_warning = Rgb(0.80f, 0.52f, 0.05f);
        theme.inverse_danger = Rgb(0.80f, 0.22f, 0.25f);
    }
    theme.ink = kind == ThemeKind::Light ? Rgb(0.10f, 0.13f, 0.19f, 0.14f)
              : kind == ThemeKind::HighContrast ? Rgb(1.0f, 1.0f, 1.0f, 0.32f)
              : Rgb(1.0f, 1.0f, 1.0f, 0.16f);
    theme.shadow = kind == ThemeKind::Light ? Rgb(0.10f, 0.14f, 0.30f, 0.20f)
                 : kind == ThemeKind::HighContrast ? Rgb(0.0f, 0.0f, 0.0f, 0.0f)
                 : Rgb(0.0f, 0.0f, 0.0f, 0.55f);
    theme.spacing = (kind == ThemeKind::HighContrast) ? 10.0f : 12.0f;
    theme.radius = (kind == ThemeKind::HighContrast) ? 4.0f : 12.0f;
    theme.caption_size = 13.0f;
    theme.font_size = 16.0f;
    theme.heading_size = 19.0f;
    theme.title_size = 26.0f;
    theme.button_min_height = (kind == ThemeKind::HighContrast) ? 44.0f : 40.0f;
    theme.button_padding = (kind == ThemeKind::HighContrast)
        ? Insets{10.0f, 16.0f, 10.0f, 16.0f} : Insets{8.0f, 14.0f, 8.0f, 14.0f};
    theme.focus_ring_width = (kind == ThemeKind::HighContrast) ? 3.0f : 2.0f;
    theme.focus_ring_outset = (kind == ThemeKind::HighContrast) ? 3.0f : 2.0f;
    theme.minimum_hit_target = (kind == ThemeKind::HighContrast) ? 48.0f : 44.0f;
    theme.scroll_wheel_multiplier = 3.0f;
    theme.color_transition.duration_seconds = 0.18;
    theme.color_transition.delay_seconds = 0.0;
    theme.color_transition.curve = Curve::Standard();
    theme.text_transition.duration_seconds = 0.16;
    theme.text_transition.delay_seconds = 0.0;
    theme.text_transition.curve = Curve::Standard();
    theme.ink_transition.duration_seconds = 0.45;
    theme.ink_transition.delay_seconds = 0.0;
    theme.ink_transition.curve = Curve::CubicOut();
    theme.toggle_transition.duration_seconds = 0.2;
    theme.toggle_transition.curve = Curve::Standard();
    theme.enter_transition.duration_seconds = 0.3;
    theme.enter_transition.curve = Curve::Decelerate();
    theme.exit_transition.duration_seconds = 0.18;
    theme.exit_transition.curve = Curve::Accelerate();
    theme.reflow_transition.duration_seconds = 0.3;
    theme.reflow_transition.curve = Curve::Standard();
    theme.layout_spring = (kind == ThemeKind::HighContrast) ? SpringSpec::Snappy() : SpringSpec::Smooth();
    if (kind == ThemeKind::Material) {
        // Material 3 baseline (light), per docs/GPT_LOOK_INSTRUCTIONS.md.
        theme.canvas = Hex(0xFEF7FF);
        theme.surface = Hex(0xF3EDF7);
        theme.surface_alt = Hex(0xE6E0E9);
        theme.surface_container_high = Hex(0xECE6F0);
        theme.text = Hex(0x1D1B20);
        theme.muted_text = Hex(0x49454F);
        theme.border = Hex(0x79747E);
        theme.outline_variant = Hex(0xCAC4D0);
        theme.accent = Hex(0x6750A4);
        theme.accent_text = Hex(0xFFFFFF);
        // Hover and pressed are an 8% on_primary state layer over primary.
        theme.accent_hover = Hex(0x735EAB);
        theme.accent_pressed = Hex(0x735EAB);
        theme.primary_container = Hex(0xEADDFF);
        theme.secondary_container = Hex(0xE8DEF8);
        theme.on_secondary_container = Hex(0x1D192B);
        theme.inverse_surface = Hex(0x322F35);
        theme.inverse_on_surface = Hex(0xF5EFF7);
        theme.inverse_primary = Hex(0xD0BCFF);
        theme.inverse_info = Hex(0x8AB4F8);
        theme.inverse_success = Hex(0x81C995);
        theme.inverse_warning = Hex(0xFDD663);
        theme.inverse_danger = Hex(0xF28B82);
        theme.success = Hex(0x386A20);
        theme.warning = Hex(0x7D5700);
        theme.danger = Hex(0xB3261E);
        theme.scrim = Rgb(0.0f, 0.0f, 0.0f, 0.36f);
        theme.shadow = Rgb(0.0f, 0.0f, 0.0f, 0.30f);
        theme.ink = Rgb(1.0f, 1.0f, 1.0f, 0.26f);
        theme.radius = 20.0f;
        theme.caption_size = 14.0f;
        theme.heading_size = 20.0f;
        theme.title_size = 24.0f;
        theme.button_padding = Insets{8.0f, 24.0f, 8.0f, 24.0f};
        theme.color_transition.duration_seconds = 0.15;
        theme.color_transition.curve = Curve::CubicOut();
        theme.text_transition.duration_seconds = 0.15;
        theme.text_transition.curve = Curve::CubicOut();
        theme.ink_transition.duration_seconds = 0.55;
        theme.ink_transition.curve = Curve::QuadOut();
        theme.toggle_transition.duration_seconds = 0.32;
        theme.toggle_transition.curve = Curve::BackOut();
        theme.enter_transition.duration_seconds = 0.55;
        theme.enter_transition.curve = Curve::BackOut();
        theme.exit_transition.duration_seconds = 0.26;
        theme.exit_transition.curve = Curve::CubicIn();
        theme.reflow_transition.duration_seconds = 0.38;
        theme.reflow_transition.curve = Curve::CubicBezier(0.4f, 0.0f, 0.2f, 1.0f);
    }
    return theme;
}

void ResetNodeFrame(Context* context) noexcept {
    if (context == nullptr) return;
    context->root_storage = {};
    context->root_storage.kind = NodeKind::Root;
    context->root_storage.id = context->frame_info.root_id != 0u
        ? context->frame_info.root_id : MakeId(0u, "lumen-root");
    context->root_storage.style.layout = LayoutMode::Column;
    context->root_storage.style.align_items = Align::Stretch;
    context->root_storage.style.gap = 0.0f;
    context->root_storage.style.has_background = true;
    context->root_storage.style.background = context->theme.canvas;
    context->root_storage.rect = InsetRect(context->frame_info.viewport.bounds, context->frame_info.viewport.safe_area);
    context->root_storage.target_rect = context->root_storage.rect;
    context->root_node = &context->root_storage;
    context->building_parent = context->root_node;
    context->nodes_built = 0;
    context->nodes_painted = 0;
    context->node_tree_invalid = false;
    context->portal_count = 0u;
    context->active_portal_count = 0u;
    context->building_portal_root = nullptr;
    for (std::size_t i = 0u; i < LayerStack::kCapacity; ++i) {
        context->portal_nodes[i] = nullptr;
        context->portal_return_parents[i] = nullptr;
        context->portal_layer_ids[i] = 0u;
        context->portal_options[i] = {};
        context->portal_scrims[i] = false;
        context->portal_layout_valid[i] = false;
        context->active_portals[i] = nullptr;
    }
}

bool StartNode(Context* context, const char* key, std::uint64_t numeric_key, bool numeric,
               NodeKind kind, const NodeStyle& source_style, const char* text,
               ClickHandler handler, void* user_data) noexcept {
    if (context == nullptr || !context->in_frame || context->building_parent == nullptr || context->node_tree_invalid) return false;
    Node* parent = context->building_parent;
    void* memory = FrameAlloc(context, sizeof(Node), alignof(Node));
    if (memory == nullptr) return false;
    auto* node = new (memory) Node{};
    node->parent = parent;
    node->id = ResolveKey(parent, key, numeric_key, numeric);
    node->style = source_style;
    node->style.shadow = nullptr;
    if (source_style.shadow != nullptr) {
        // Decorative: if the arena is exhausted the node simply paints without its shadow.
        void* shadow_memory = FrameAlloc(context, sizeof(ShadowStyle), alignof(ShadowStyle));
        if (shadow_memory != nullptr) {
            node->style.shadow = new (shadow_memory) ShadowStyle(*source_style.shadow);
        }
    }
    node->style.custom_paint = nullptr;
    if (source_style.custom_paint != nullptr && source_style.custom_paint->painter != nullptr) {
        void* paint_memory = FrameAlloc(context, sizeof(CustomPaint), alignof(CustomPaint));
        if (paint_memory == nullptr) return false;
        node->style.custom_paint = new (paint_memory) CustomPaint(*source_style.custom_paint);
    }
    node->kind = kind;
    node->text = CopyText(context, text);
    if (text != nullptr && node->text == nullptr) return false;
    node->click_handler = handler;
    node->user_data = user_data;
    if (kind == NodeKind::Button) {
        if (IsZeroInsets(node->style.padding)) node->style.padding = context->theme.button_padding;
        if (node->style.min_height <= 0.0f && node->style.height < 0.0f) node->style.min_height = context->theme.button_min_height;
    }
    const std::size_t previous_children = parent->child_count;
    AppendChild(context, parent, node);
    if (context->node_tree_invalid || parent->child_count == previous_children) return false;
    ++context->nodes_built;
    if (kind == NodeKind::Container) context->building_parent = node;
    return true;
}

void CloseNode(Context* context) noexcept {
    if (context == nullptr || !context->in_frame || context->building_parent == nullptr ||
        context->building_parent == context->root_node) return;
    context->building_parent = context->building_parent->parent;
}

bool FinishNodeFrame(Context* context) noexcept {
    if (context == nullptr || context->root_node == nullptr) return false;
    if (context->building_parent != context->root_node || context->node_tree_invalid) {
        context->node_tree_invalid = true;
        context->building_parent = context->root_node;
        return false;
    }
    ResolveInherited(context->root_node, context->root_node, context->theme);
    MeasureNode(context, context->root_node, context->root_node->target_rect.Width(), context->theme);
    LoadScrollOffsets(context, context->root_node);
    LayoutChildren(context, context->root_node, context->theme, 0u);
    if (ClampScrollOffsets(context, context->root_node)) {
        LoadScrollOffsets(context, context->root_node);
        LayoutChildren(context, context->root_node, context->theme, 0u);
    }
    ResolvePortalLayouts(context);
    BuildActivePortals(context);

    ImGuiIO& io = ImGui::GetIO();
    const Vec2 mouse_position{io.MousePos.x, io.MousePos.y};
    const bool mouse_position_valid = std::isfinite(mouse_position.x) && std::isfinite(mouse_position.y) &&
                                      mouse_position.x > -1.0e30f && mouse_position.y > -1.0e30f;
    context->pointer_position_valid = mouse_position_valid;

    // Modal scopes own keyboard traversal while active and restore the pre-modal focus on close.
    Node* modal_root = nullptr;
    const LayerEntry* top_modal_entry = context->layers.TopModal();
    if (top_modal_entry != nullptr) {
        for (std::size_t i = context->active_portal_count; i > 0u; --i) {
            Node* candidate = context->active_portals[i - 1u];
            if (candidate != nullptr && PortalLayerId(context, candidate) == top_modal_entry->id && PortalLayoutValid(context, candidate)) {
                modal_root = candidate;
                break;
            }
        }
    }
    if (modal_root != nullptr && context->modal_focus_layer_id != PortalLayerId(context, modal_root)) {
        if (context->modal_focus_layer_id == 0u) {
            Node* previous_focus = FindContextNodeById(context, context->focused_node_id);
            context->modal_restore_focus_id = previous_focus != nullptr ? previous_focus->id : 0u;
        }
        const std::uint64_t old_focus = context->focused_node_id;
        context->modal_focus_layer_id = PortalLayerId(context, modal_root);
        context->focused_node_id = 0u;
        SetFocusTarget(context, 1);
        context->keyboard_focus_visible = false;
        if (old_focus != 0u) DispatchFocusEvent(context, old_focus, UIEventType::FocusLost);
        if (context->focused_node_id != 0u) DispatchFocusEvent(context, context->focused_node_id, UIEventType::FocusGained);
    } else if (modal_root != nullptr && FindNodeById(modal_root, context->focused_node_id) == nullptr) {
        const std::uint64_t old_focus = context->focused_node_id;
        context->focused_node_id = 0u;
        SetFocusTarget(context, 1);
        context->keyboard_focus_visible = false;
        if (old_focus != 0u) DispatchFocusEvent(context, old_focus, UIEventType::FocusLost);
        if (context->focused_node_id != 0u) DispatchFocusEvent(context, context->focused_node_id, UIEventType::FocusGained);
    } else if (modal_root == nullptr && context->modal_focus_layer_id != 0u) {
        const std::uint64_t old_focus = context->focused_node_id;
        context->modal_focus_layer_id = 0u;
        context->focused_node_id = 0u;
        Node* restore = FindContextNodeById(context, context->modal_restore_focus_id);
        if (restore != nullptr && IsFocusable(restore)) context->focused_node_id = restore->id;
        context->modal_restore_focus_id = 0u;
        if (old_focus != 0u) DispatchFocusEvent(context, old_focus, UIEventType::FocusLost);
        if (context->focused_node_id != 0u) DispatchFocusEvent(context, context->focused_node_id, UIEventType::FocusGained);
    }

    if (context->captured_node_id != 0u &&
        (context->captured_pointer_id != 1u || FindContextNodeById(context, context->captured_node_id) == nullptr)) {
        context->captured_node_id = 0u;
        context->captured_pointer_id = 0u;
        context->pressed_node_id = 0u;
        context->pointer_press_default_prevented = false;
    }
    if (context->pressed_node_id != 0u && FindContextNodeById(context, context->pressed_node_id) == nullptr) {
        context->pressed_node_id = 0u;
        context->pointer_press_default_prevented = false;
    }

    Node* scroll_target = mouse_position_valid ? FindScrollTargetUI(context, mouse_position) : nullptr;
    if ((io.MouseWheel != 0.0f || io.MouseWheelH != 0.0f) && mouse_position_valid) {
        Node* wheel_target = scroll_target;
        if (wheel_target == nullptr) wheel_target = HitTestUI(context, mouse_position);
        UIEvent wheel{};
        wheel.type = UIEventType::Wheel;
        wheel.position = mouse_position;
        wheel.delta = {io.MouseWheelH, io.MouseWheel};
        DispatchEvent(context, wheel_target, wheel);
        if (scroll_target != nullptr && !wheel.default_prevented) {
            ApplyWheel(context, scroll_target, io, context->theme);
        }
    }

    Node* hit = mouse_position_valid ? HitTestUI(context, mouse_position) : nullptr;
    const bool mouse_clicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    const bool mouse_released = ImGui::IsMouseReleased(ImGuiMouseButton_Left);

    if (mouse_position_valid) {
        const bool pointer_moved = !context->pointer_position_initialized ||
            context->last_pointer_position.x != mouse_position.x || context->last_pointer_position.y != mouse_position.y;
        if (context->hovered_node_id != (hit != nullptr ? hit->id : 0u)) {
            Node* previous_hover = FindContextNodeById(context, context->hovered_node_id);
            if (previous_hover != nullptr) {
                UIEvent leave{};
                leave.type = UIEventType::PointerLeave;
                leave.pointer_id = 1u;
                leave.position = mouse_position;
                DispatchEvent(context, previous_hover, leave);
            }
            if (hit != nullptr) {
                UIEvent enter{};
                enter.type = UIEventType::PointerEnter;
                enter.pointer_id = 1u;
                enter.position = mouse_position;
                DispatchEvent(context, hit, enter);
            }
            context->hovered_node_id = hit != nullptr ? hit->id : 0u;
        }
        if (pointer_moved) {
            Node* move_target = context->captured_pointer_id == 1u
                ? FindContextNodeById(context, context->captured_node_id) : hit;
            if (move_target != nullptr) {
                UIEvent move{};
                move.type = UIEventType::PointerMove;
                move.pointer_id = 1u;
                move.position = mouse_position;
                move.delta = context->pointer_position_initialized
                    ? Vec2{mouse_position.x - context->last_pointer_position.x,
                           mouse_position.y - context->last_pointer_position.y}
                    : Vec2{};
                DispatchEvent(context, move_target, move);
            }
        }
        context->last_pointer_position = mouse_position;
        context->pointer_position_initialized = true;
    } else {
        context->pointer_position_initialized = false;
    }

    // Route key events to the previously focused node. Tab traversal is a default action
    // and can be suppressed by calling PreventDefault() in a KeyDown handler.
    Node* key_target = FindContextNodeById(context, context->focused_node_id);
    if (key_target != nullptr && !IsFocusable(key_target)) key_target = nullptr;
    const std::uint64_t key_target_id = key_target != nullptr ? key_target->id : 0u;
    bool tab_default_prevented = false;
    bool escape_default_prevented = false;
    bool keyboard_activate_requested = false;
    bool keyboard_activation_default_prevented = false;
    const std::uint8_t modifiers = CurrentModifiers(io);
    for (int key_value = static_cast<int>(ImGuiKey_NamedKey_BEGIN);
         key_value < static_cast<int>(ImGuiKey_NamedKey_END); ++key_value) {
        const ImGuiKey key = static_cast<ImGuiKey>(key_value);
        if (ImGui::IsKeyPressed(key, true) && key_target != nullptr) {
            UIEvent key_down{};
            key_down.type = UIEventType::KeyDown;
            key_down.key = key;
            key_down.modifiers = modifiers;
            key_down.repeat = !ImGui::IsKeyPressed(key, false);
            DispatchEvent(context, key_target, key_down);
            if (key_down.repeat) continue;
            if (key == ImGuiKey_Tab) tab_default_prevented = key_down.default_prevented;
            if (key == ImGuiKey_Escape) escape_default_prevented = key_down.default_prevented;
            if (key == ImGuiKey_Enter || key == ImGuiKey_Space) {
                keyboard_activate_requested = true;
                keyboard_activation_default_prevented = key_down.default_prevented;
            }
        }
        if (ImGui::IsKeyReleased(key) && key_target != nullptr) {
            UIEvent key_up{};
            key_up.type = UIEventType::KeyUp;
            key_up.key = key;
            key_up.modifiers = modifiers;
            DispatchEvent(context, key_target, key_up);
        }
    }
    if (key_target != nullptr) {
        for (const ImWchar character : io.InputQueueCharacters) {
            if (character < 0x20u || character == 0x7Fu) continue;
            UIEvent text{};
            text.type = UIEventType::TextInput;
            text.codepoint = character;
            text.modifiers = modifiers;
            DispatchEvent(context, key_target, text);
        }
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && !escape_default_prevented) {
        const LayerEntry* top = context->layers.Top();
        if (top != nullptr && (top->dismiss_policy & DismissOnEscape) != 0u) CloseLayer(context, top->id);
    }

    const std::uint64_t focus_before_input = context->focused_node_id;
    if (ImGui::IsKeyPressed(ImGuiKey_Tab, false) && !tab_default_prevented) {
        SetFocusTarget(context, (io.KeyMods & ImGuiMod_Shift) != 0 ? -1 : 1);
    }

    if (mouse_clicked && mouse_position_valid) {
        DismissOutsideLayer(context, mouse_position);
        // A modal still blocks the background for the click that dismissed it; never activate
        // an underlying control accidentally on the same pointer-down.
        if (context->layers.HasModal()) hit = HitTestUI(context, mouse_position);
        else if (context->modal_focus_layer_id != 0u) hit = nullptr;
    }

    if (mouse_clicked) {
        context->pressed_node_id = hit != nullptr ? hit->id : 0u;
        context->pointer_press_default_prevented = false;
        if (hit != nullptr) {
            UIEvent down{};
            down.type = UIEventType::PointerDown;
            down.pointer_id = 1u;
            down.button = ImGuiMouseButton_Left;
            down.position = mouse_position;
            DispatchEvent(context, hit, down);
            context->pointer_press_default_prevented = down.default_prevented;
            if (IsFocusable(hit)) context->focused_node_id = hit->id;
        }
        context->keyboard_focus_visible = false;
        RequestRedraw(context);
    }

    Node* focused = FindContextNodeById(context, context->focused_node_id);
    if (focused == nullptr || !IsFocusable(focused)) {
        context->focused_node_id = 0u;
        focused = nullptr;
    }
    if (focus_before_input != context->focused_node_id) {
        if (focus_before_input != 0u) DispatchFocusEvent(context, focus_before_input, UIEventType::FocusLost);
        if (context->focused_node_id != 0u) DispatchFocusEvent(context, context->focused_node_id, UIEventType::FocusGained);
    }

    if (mouse_released) {
        Node* up_target = context->captured_pointer_id == 1u
            ? FindContextNodeById(context, context->captured_node_id) : hit;
        bool release_default_prevented = false;
        if (up_target != nullptr) {
            UIEvent up{};
            up.type = UIEventType::PointerUp;
            up.pointer_id = 1u;
            up.button = ImGuiMouseButton_Left;
            up.position = mouse_position;
            DispatchEvent(context, up_target, up);
            release_default_prevented = up.default_prevented;
        }
        const bool pointer_activate = context->pressed_node_id != 0u && hit != nullptr &&
            hit->id == context->pressed_node_id && !context->pointer_press_default_prevented &&
            !release_default_prevented;
        if (pointer_activate) {
            UIEvent click{};
            click.type = UIEventType::Click;
            click.pointer_id = 1u;
            click.button = ImGuiMouseButton_Left;
            click.position = mouse_position;
            DispatchEvent(context, hit, click);
            if (!click.default_prevented) InvokeClickHandler(context, hit);
            RequestRedraw(context);
        }
        context->pressed_node_id = 0u;
        context->pointer_press_default_prevented = false;
        if (context->captured_pointer_id == 1u) {
            context->captured_node_id = 0u;
            context->captured_pointer_id = 0u;
        }
    }

    const bool keyboard_activate = keyboard_activate_requested && !keyboard_activation_default_prevented &&
        key_target_id != 0u && focused != nullptr && focused->id == key_target_id && IsFocusable(focused);
    if (keyboard_activate) {
        UIEvent click{};
        click.type = UIEventType::Click;
        click.key = ImGui::IsKeyPressed(ImGuiKey_Enter, false) ? ImGuiKey_Enter : ImGuiKey_Space;
        DispatchEvent(context, focused, click);
        if (!click.default_prevented) InvokeClickHandler(context, focused);
        RequestRedraw(context);
    }

    Node* active_pointer_node = FindContextNodeById(context, context->pressed_node_id);
    ApplyHoverAndClicks(context->root_node, hit, active_pointer_node,
                        context->focused_node_id, context->keyboard_focus_visible);
    for (std::size_t i = 0u; i < context->portal_count; ++i) {
        ApplyHoverAndClicks(context->portal_nodes[i], hit, active_pointer_node,
                            context->focused_node_id, context->keyboard_focus_visible);
    }
    if (mouse_released) {
        ApplyHoverAndClicks(context->root_node, hit, nullptr,
                            context->focused_node_id, context->keyboard_focus_visible);
        for (std::size_t i = 0u; i < context->portal_count; ++i) {
            ApplyHoverAndClicks(context->portal_nodes[i], hit, nullptr,
                                context->focused_node_id, context->keyboard_focus_visible);
        }
    }

    // Painters run below; the tree is final, so node and portal declaration is closed.
    context->building_parent = nullptr;
    context->render_commands = nullptr;
    context->render_command_count = CountRenderCommands(context, context->root_node, 0u);
    if (context->render_command_count > 0u) {
        auto* commands = static_cast<RenderCommand*>(FrameAlloc(context, sizeof(RenderCommand) * context->render_command_count,
                                                                alignof(RenderCommand)));
        if (commands == nullptr) return false;
        context->render_commands = commands;
        std::size_t cursor = 0u;
        BuildRenderCommands(context, context->root_node, context->theme, 0u, cursor);
        if (cursor != context->render_command_count) return false;
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        const Rect clip = context->frame_info.viewport.bounds;
        draw_list->PushClipRect(ImVec2(clip.min.x, clip.min.y), ImVec2(clip.max.x, clip.max.y), true);
        TranslateRenderList(context, draw_list);
        draw_list->PopClipRect();
    }

    // Draw active portals after the base tree, in LayerStack paint order. A modal scrim is
    // emitted directly into the host-owned draw list immediately before its modal content.
    ImDrawList* portal_draw_list = ImGui::GetWindowDrawList();
    if (portal_draw_list != nullptr) {
        const Rect viewport = context->frame_info.viewport.bounds;
        for (std::size_t i = 0u; i < context->active_portal_count; ++i) {
            Node* portal = context->active_portals[i];
            if (portal == nullptr || !PortalLayoutValid(context, portal) || !context->layers.Find(PortalLayerId(context, portal))) continue;
            const LayerEntry* entry = context->layers.Find(PortalLayerId(context, portal));
            const bool draw_scrim = entry != nullptr && entry->kind == LayerKind::Modal &&
                context->portal_scrims[FindPortalIndex(context, portal)];

            // Build/allocate first. A failed frame must never return with an unbalanced
            // ImDrawList clip stack (which otherwise corrupts subsequent host rendering).
            context->render_commands = nullptr;
            context->render_command_count = CountRenderCommands(context, portal, 0u);
            if (context->render_command_count > 0u) {
                auto* commands = static_cast<RenderCommand*>(FrameAlloc(context, sizeof(RenderCommand) * context->render_command_count, alignof(RenderCommand)));
                if (commands == nullptr) return false;
                context->render_commands = commands;
                std::size_t cursor = 0u;
                BuildRenderCommands(context, portal, context->theme, 0u, cursor);
                if (cursor != context->render_command_count) return false;
            }

            if (draw_scrim) {
                Color scrim = context->theme.scrim;
                const std::uint64_t layer_id = PortalLayerId(context, portal);
                for (const auto& runtime : context->portal_runtime) {
                    if (runtime.used && runtime.id == layer_id) {
                        scrim.a *= runtime.presence.Opacity();
                        break;
                    }
                }
                const auto byte = [](float value) -> int { return static_cast<int>(Clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f); };
                portal_draw_list->PushClipRect(ImVec2(viewport.min.x, viewport.min.y), ImVec2(viewport.max.x, viewport.max.y), true);
                portal_draw_list->AddRectFilled(ImVec2(viewport.min.x, viewport.min.y), ImVec2(viewport.max.x, viewport.max.y),
                    IM_COL32(byte(scrim.r), byte(scrim.g), byte(scrim.b), byte(scrim.a)));
                portal_draw_list->PopClipRect();
            }
            if (context->render_command_count > 0u) {
                portal_draw_list->PushClipRect(ImVec2(viewport.min.x, viewport.min.y), ImVec2(viewport.max.x, viewport.max.y), true);
                TranslateRenderList(context, portal_draw_list);
                portal_draw_list->PopClipRect();
            }
        }
    }
    return true;
}

} // namespace detail

namespace {

Context::PortalRuntime* FindPortalRuntime(Context* context, std::uint64_t layer_id) noexcept {
    if (context == nullptr || layer_id == 0u) return nullptr;
    for (auto& runtime : context->portal_runtime) {
        if (runtime.used && runtime.id == layer_id) return &runtime;
    }
    return nullptr;
}

Context::PortalRuntime* AllocatePortalRuntime(Context* context, std::uint64_t layer_id) noexcept {
    if (Context::PortalRuntime* existing = FindPortalRuntime(context, layer_id)) return existing;
    if (context == nullptr || layer_id == 0u) return nullptr;
    for (auto& runtime : context->portal_runtime) {
        if (!runtime.used) {
            runtime = {};
            runtime.id = layer_id;
            runtime.used = true;
            runtime.presence = Presence(false);
            return &runtime;
        }
    }
    return nullptr;
}

} // namespace

bool OpenLayer(Context* context, std::uint64_t layer_id, LayerKind kind,
               std::uint8_t dismiss_policy, double timeout_seconds) noexcept {
    if (context == nullptr || layer_id == 0u || static_cast<unsigned>(kind) > static_cast<unsigned>(LayerKind::Debug)) return false;
    const bool timeout_valid = std::isfinite(timeout_seconds) && timeout_seconds > 0.0 &&
                               (dismiss_policy & DismissOnTimeout) != 0u;
    const LayerEntry* existing = context->layers.Find(layer_id);
    if (existing != nullptr && (existing->kind != kind || existing->dismiss_policy != dismiss_policy)) {
        context->layers.Close(layer_id);
        if (Context::PortalRuntime* old = FindPortalRuntime(context, layer_id)) *old = {};
        existing = nullptr;
    }
    if (existing == nullptr) {
        // Timeout is coordinated with Presence so the layer remains mounted for its exit transition.
        if (!context->layers.Open(layer_id, kind, dismiss_policy, context->motion_time_seconds, 0.0)) return false;
    }
    Context::PortalRuntime* runtime = AllocatePortalRuntime(context, layer_id);
    if (runtime == nullptr) {
        context->layers.Close(layer_id);
        return false;
    }
    runtime->target_present = true;
    runtime->expires_at = timeout_valid ? context->motion_time_seconds + timeout_seconds : 0.0;
    const double duration = context->reduce_motion ? 0.0 : context->theme.color_transition.duration_seconds;
    runtime->presence.SetPresent(true, context->motion_time_seconds, duration,
                                 context->theme.color_transition.curve);
    RequestRedraw(context);
    return true;
}

bool CloseLayer(Context* context, std::uint64_t layer_id) noexcept {
    if (context == nullptr || layer_id == 0u || context->layers.Find(layer_id) == nullptr) return false;
    Context::PortalRuntime* runtime = FindPortalRuntime(context, layer_id);
    if (runtime == nullptr) {
        context->layers.Close(layer_id);
        RequestRedraw(context);
        return true;
    }
    if (!runtime->target_present) return true;
    runtime->target_present = false;
    runtime->expires_at = 0.0;
    const double duration = context->reduce_motion ? 0.0 : context->theme.color_transition.duration_seconds;
    runtime->presence.SetPresent(false, context->motion_time_seconds, duration,
                                 context->theme.color_transition.curve);
    if (!runtime->presence.IsMounted()) {
        context->layers.Close(layer_id);
        *runtime = {};
    }
    RequestRedraw(context);
    return true;
}

bool IsLayerOpen(const Context* context, std::uint64_t layer_id) noexcept {
    return context != nullptr && layer_id != 0u && context->layers.Find(layer_id) != nullptr;
}

float GetLayerVisibility(const Context* context, std::uint64_t layer_id) noexcept {
    if (!IsLayerOpen(context, layer_id)) return 0.0f;
    const Context::PortalRuntime* runtime = FindPortalRuntime(const_cast<Context*>(context), layer_id);
    return runtime != nullptr ? runtime->presence.Opacity() : 1.0f;
}

bool IsLayerClosing(const Context* context, std::uint64_t layer_id) noexcept {
    if (!IsLayerOpen(context, layer_id)) return false;
    const Context::PortalRuntime* runtime = FindPortalRuntime(const_cast<Context*>(context), layer_id);
    return runtime != nullptr && !runtime->target_present;
}

bool BeginPortal(Context* context, std::uint64_t layer_id, const PortalOptions& options,
                 const NodeStyle& source_style) noexcept {
    if (context == nullptr || !context->in_frame || context->root_node == nullptr ||
        context->building_parent == nullptr ||
        context->node_tree_invalid || context->building_portal_root != nullptr ||
        context->portal_count >= LayerStack::kCapacity || layer_id == 0u ||
        !IsFinite(options.size) || options.size.x <= 0.0f || options.size.y < 0.0f ||
        !IsFinite(options.offset) || !std::isfinite(options.edge_margin) || options.edge_margin < 0.0f ||
        static_cast<unsigned>(options.placement) > static_cast<unsigned>(Placement::Trailing) ||
        static_cast<unsigned>(options.align) > static_cast<unsigned>(PlacementAlign::End) ||
        static_cast<unsigned>(options.pin) > static_cast<unsigned>(PortalPin::BottomRight) ||
        ((options.pin_to_top_right || options.pin != PortalPin::Center) && options.anchor_node_id != 0u)) return false;
    const LayerEntry* layer_entry = context->layers.Find(layer_id);
    Context::PortalRuntime* runtime = FindPortalRuntime(context, layer_id);
    if (layer_entry == nullptr || runtime == nullptr || !runtime->presence.IsMounted() ||
        (options.scrim && layer_entry->kind != LayerKind::Modal)) return false;
    if (options.anchor_node_id != 0u && FindNodeById(context->root_node, options.anchor_node_id) == nullptr) return false;
    for (std::size_t i = 0u; i < context->portal_count; ++i) {
        if (context->portal_nodes[i] != nullptr && context->portal_layer_ids[i] == layer_id) return false;
    }
    Node* return_parent = context->building_parent;
    if (return_parent == nullptr) return false;
    const std::uint64_t node_id = MakeId(context->root_node->id, layer_id);
    if (FindContextNodeById(context, node_id) != nullptr) return false;
    void* memory = FrameAlloc(context, sizeof(Node), alignof(Node));
    if (memory == nullptr) return false;
    auto* portal = new (memory) Node{};
    portal->id = node_id;
    portal->parent = context->root_node;
    portal->kind = NodeKind::Container;
    const std::size_t portal_index = context->portal_count;
    context->portal_layer_ids[portal_index] = layer_id;
    context->portal_options[portal_index] = options;
    context->portal_scrims[portal_index] = options.scrim;
    context->portal_return_parents[portal_index] = return_parent;
    portal->style = source_style;
    portal->style.width = options.size.x;
    portal->style.height = options.size.y;
    portal->style.opacity = Clamp(std::isfinite(source_style.opacity) ? source_style.opacity : 1.0f, 0.0f, 1.0f) * runtime->presence.Opacity();
    // The portal root tracks the anchor/viewport immediately. Child content may animate independently.
    portal->style.animate_layout = false;
    if (!portal->style.has_background) {
        portal->style.has_background = true;
        portal->style.background = context->theme.surface;
    }
    context->portal_nodes[context->portal_count++] = portal;
    ++context->nodes_built;
    context->building_parent = portal;
    context->building_portal_root = portal;
    return true;
}

void EndPortal(Context* context) noexcept {
    if (context == nullptr || !context->in_frame || context->building_portal_root == nullptr) return;
    Node* portal = context->building_portal_root;
    const std::size_t index = FindPortalIndex(context, portal);
    Node* restore = index < LayerStack::kCapacity && context->portal_return_parents[index] != nullptr
        ? context->portal_return_parents[index] : context->root_node;
    if (context->building_parent != portal) context->node_tree_invalid = true;
    context->building_parent = restore;
    context->building_portal_root = nullptr;
}

Vec2 MeasureText(const Context* context, const char* text, float font_size, float wrap_width,
                 const char* text_end) noexcept {
    if (text == nullptr || (font_size != 0.0f && !(font_size > 0.0f && std::isfinite(font_size)))) return {};
    const float wrap = std::isfinite(wrap_width) ? wrap_width : 0.0f;
    return MeasureTextRange(context, text, text_end != nullptr ? text_end : text + std::strlen(text), font_size, wrap);
}

bool SetFontLadder(Context* context, ImFont* const* fonts, std::size_t count) noexcept {
    if (context == nullptr || count > Context::kMaxFonts || (count > 0u && fonts == nullptr)) return false;
    for (std::size_t i = 0u; i < count; ++i) {
        if (fonts[i] == nullptr || !(fonts[i]->FontSize > 0.0f)) return false;
    }
    for (std::size_t i = 0u; i < Context::kMaxFonts; ++i) context->fonts[i] = i < count ? fonts[i] : nullptr;
    context->font_count = count;
    RequestRedraw(context);
    return true;
}

void SetTheme(Context* context, ThemeKind kind) noexcept {
    if (context == nullptr) return;
    detail::BeginThemeTransition(context, detail::DefaultTheme(kind));
}

void SetTheme(Context* context, const Theme& theme) noexcept {
    if (context == nullptr) return;
    detail::BeginThemeTransition(context, theme);
}

Theme GetTheme(const Context* context) noexcept {
    return context != nullptr ? context->theme : Theme{};
}

bool IsThemeTransitioning(const Context* context) noexcept {
    return context != nullptr && context->theme_transition_active;
}

bool GetNodeRect(const Context* context, std::uint64_t node_id, Rect* out_rect) noexcept {
    if (context == nullptr || context->root_node == nullptr || node_id == 0u || out_rect == nullptr) return false;
    Node* node = FindContextNodeById(const_cast<Context*>(context), node_id);
    if (node == nullptr) return false;
    *out_rect = node->rect;
    return true;
}

NodeInteractionState GetNodeState(const Context* context, std::uint64_t node_id) noexcept {
    NodeInteractionState state{};
    if (context == nullptr || context->root_node == nullptr || node_id == 0u) return state;
    const Node* node = FindContextNodeById(const_cast<Context*>(context), node_id);
    if (node == nullptr) return state;
    state.found = true;
    state.hovered = node->hovered;
    state.pressed = node->pressed;
    state.focused = node->focused;
    state.focus_visible = node->focus_visible;
    return state;
}

Canvas::Canvas(ImDrawList* draw_list, std::uint64_t node_id, Rect bounds, float opacity,
               const Context* context) noexcept
    : draw_list_(draw_list), context_(context), node_id_(node_id), bounds_(bounds),
      opacity_(std::isfinite(opacity) ? Clamp(opacity, 0.0f, 1.0f) : 1.0f) {}

Canvas::~Canvas() {
    while (clip_depth_ > 0) PopClip();
}

void Canvas::FillRect(Rect rect, Color color, float radius) noexcept {
    if (draw_list_ == nullptr || !IsFinite(rect) || rect.IsEmpty()) return;
    draw_list_->AddRectFilled(ImVec2(rect.min.x, rect.min.y), ImVec2(rect.max.x, rect.max.y),
                              ToImU32(color, opacity_), std::isfinite(radius) ? std::max(0.0f, radius) : 0.0f);
}

void Canvas::StrokeRect(Rect rect, Color color, float radius, float thickness) noexcept {
    if (draw_list_ == nullptr || !IsFinite(rect) || rect.IsEmpty() || !(thickness > 0.0f) || !std::isfinite(thickness)) return;
    draw_list_->AddRect(ImVec2(rect.min.x, rect.min.y), ImVec2(rect.max.x, rect.max.y), ToImU32(color, opacity_),
                        std::isfinite(radius) ? std::max(0.0f, radius) : 0.0f, 0, thickness);
}

void Canvas::FillCircle(Vec2 center, float radius, Color color) noexcept {
    if (draw_list_ == nullptr || !IsFinite(center) || !(radius > 0.0f) || !std::isfinite(radius)) return;
    draw_list_->AddCircleFilled(ImVec2(center.x, center.y), radius, ToImU32(color, opacity_));
}

void Canvas::StrokeCircle(Vec2 center, float radius, Color color, float thickness) noexcept {
    if (draw_list_ == nullptr || !IsFinite(center) || !(radius > 0.0f) || !std::isfinite(radius) ||
        !(thickness > 0.0f) || !std::isfinite(thickness)) return;
    draw_list_->AddCircle(ImVec2(center.x, center.y), radius, ToImU32(color, opacity_), 0, thickness);
}

void Canvas::Line(Vec2 from, Vec2 to, Color color, float thickness) noexcept {
    if (draw_list_ == nullptr || !IsFinite(from) || !IsFinite(to) || !(thickness > 0.0f) || !std::isfinite(thickness)) return;
    draw_list_->AddLine(ImVec2(from.x, from.y), ImVec2(to.x, to.y), ToImU32(color, opacity_), thickness);
}

void Canvas::Text(Vec2 position, const char* text, Color color, float font_size) noexcept {
    if (draw_list_ == nullptr || text == nullptr || text[0] == '\0' || !IsFinite(position)) return;
    const FontChoice font = PickFont(context_, std::isfinite(font_size) ? font_size : 0.0f);
    if (font.font == nullptr) return;
    draw_list_->AddText(font.font, font.size, ImVec2(position.x, position.y), ToImU32(color, opacity_), text);
}

bool Canvas::PushClip(Rect rect) noexcept {
    if (draw_list_ == nullptr || !IsFinite(rect)) return false;
    draw_list_->PushClipRect(ImVec2(rect.min.x, rect.min.y), ImVec2(rect.max.x, rect.max.y), true);
    ++clip_depth_;
    return true;
}

void Canvas::PopClip() noexcept {
    if (draw_list_ == nullptr || clip_depth_ <= 0) return;
    draw_list_->PopClipRect();
    --clip_depth_;
}

void Canvas::BeginPath() noexcept {
    if (draw_list_ != nullptr) draw_list_->PathClear();
}

void Canvas::MoveTo(Vec2 point) noexcept {
    if (draw_list_ == nullptr || !IsFinite(point)) return;
    // ImDrawList paths have a single contour; MoveTo restarts it.
    draw_list_->PathClear();
    draw_list_->PathLineTo(ImVec2(point.x, point.y));
}

void Canvas::LineTo(Vec2 point) noexcept {
    if (draw_list_ != nullptr && IsFinite(point)) draw_list_->PathLineTo(ImVec2(point.x, point.y));
}

void Canvas::QuadTo(Vec2 control, Vec2 point) noexcept {
    if (draw_list_ == nullptr || !IsFinite(control) || !IsFinite(point) || draw_list_->_Path.Size == 0) return;
    draw_list_->PathBezierQuadraticCurveTo(ImVec2(control.x, control.y), ImVec2(point.x, point.y));
}

void Canvas::CubicTo(Vec2 control1, Vec2 control2, Vec2 point) noexcept {
    if (draw_list_ == nullptr || !IsFinite(control1) || !IsFinite(control2) || !IsFinite(point) ||
        draw_list_->_Path.Size == 0) return;
    draw_list_->PathBezierCubicCurveTo(ImVec2(control1.x, control1.y), ImVec2(control2.x, control2.y),
                                       ImVec2(point.x, point.y));
}

void Canvas::ArcTo(Vec2 center, float radius, float start_angle, float end_angle) noexcept {
    if (draw_list_ == nullptr || !IsFinite(center) || !(radius > 0.0f) || !std::isfinite(radius) ||
        !std::isfinite(start_angle) || !std::isfinite(end_angle)) return;
    draw_list_->PathArcTo(ImVec2(center.x, center.y), radius, start_angle, end_angle);
}

void Canvas::Stroke(Color color, float thickness, bool closed) noexcept {
    if (draw_list_ == nullptr) return;
    if (!(thickness > 0.0f) || !std::isfinite(thickness) || draw_list_->_Path.Size < 2) {
        draw_list_->PathClear();
        return;
    }
    draw_list_->PathStroke(ToImU32(color, opacity_), closed ? ImDrawFlags_Closed : ImDrawFlags_None, thickness);
}

void Canvas::Fill(Color color) noexcept {
    if (draw_list_ == nullptr) return;
    if (draw_list_->_Path.Size < 3) {
        draw_list_->PathClear();
        return;
    }
    draw_list_->PathFillConvex(ToImU32(color, opacity_));
}

bool BeginNode(Context* context, const char* key, const NodeStyle& style, const char* text) noexcept {
    return detail::StartNode(context, key, 0u, false, NodeKind::Container, style, text, nullptr, nullptr);
}

bool BeginNode(Context* context, std::uint64_t key, const NodeStyle& style, const char* text) noexcept {
    return detail::StartNode(context, nullptr, key, true, NodeKind::Container, style, text, nullptr, nullptr);
}

void EndNode(Context* context) noexcept { detail::CloseNode(context); }

std::uint64_t GetCurrentNodeId(const Context* context) noexcept {
    if (context == nullptr || !context->in_frame || context->building_parent == nullptr) return 0u;
    return context->building_parent->id;
}

bool Label(Context* context, const char* key, const char* text, const NodeStyle& style) noexcept {
    return detail::StartNode(context, key, 0u, false, NodeKind::Label, style, text, nullptr, nullptr);
}

bool Label(Context* context, std::uint64_t key, const char* text, const NodeStyle& style) noexcept {
    return detail::StartNode(context, nullptr, key, true, NodeKind::Label, style, text, nullptr, nullptr);
}

bool Image(Context* context, const char* key, ImTextureID texture, Vec2 source_size,
           ImageFit fit, const NodeStyle& style, const char* accessible_label) noexcept {
    if (context == nullptr || key == nullptr || key[0] == '\0' ||
        !IsFinite(source_size) || source_size.x < 0.0f || source_size.y < 0.0f) return false;
    if (!detail::StartNode(context, key, 0u, false, NodeKind::Image, style, nullptr, nullptr, nullptr)) return false;
    Node* image_node = context->building_parent->last_child;
    if (image_node == nullptr || image_node->kind != NodeKind::Image) return false;
    void* image_memory = FrameAlloc(context, sizeof(ImageData), alignof(ImageData));
    if (image_memory == nullptr) return false;
    image_node->image = new (image_memory) ImageData{texture, source_size, fit};
    (void)accessible_label; // Reserved for the accessibility tree; never paint alt text over the image.
    return true;
}

bool Button(Context* context, const char* key, const char* text, ClickHandler handler, void* user_data,
            const NodeStyle& style) noexcept {
    return detail::StartNode(context, key, 0u, false, NodeKind::Button, style, text, handler, user_data);
}

bool Button(Context* context, std::uint64_t key, const char* text, ClickHandler handler, void* user_data,
            const NodeStyle& style) noexcept {
    return detail::StartNode(context, nullptr, key, true, NodeKind::Button, style, text, handler, user_data);
}

bool Collection(Context* context, const char* key, std::size_t item_count, float item_extent,
                CollectionBuilder builder, void* user_data, const NodeStyle& source_style,
                std::size_t cache_extent_items) noexcept {
    if (context == nullptr || !context->in_frame || key == nullptr || key[0] == '\0' ||
        (item_count > 0u && builder == nullptr) || !std::isfinite(item_extent) || item_extent <= 0.0f ||
        item_count > 10000000u || static_cast<double>(item_count) * static_cast<double>(item_extent) > 1.0e30) {
        return false;
    }
    if (context->building_parent == nullptr) return false;

    NodeStyle style = source_style;
    style.layout = LayoutMode::Column;
    style.gap = 0.0f;
    style.align_items = Align::Stretch;
    style.scrollable_y = true;
    style.clip_children = true;
    const std::uint64_t collection_id = MakeId(context->building_parent->id, key);
    ScrollMemory* memory = State<ScrollMemory>(context, MakeId(collection_id, "scroll-offset"));
    if (memory == nullptr) return false;

    const double total_height = static_cast<double>(item_count) * static_cast<double>(item_extent);
    float viewport_height = style.height;
    if (!std::isfinite(viewport_height) || viewport_height < 0.0f) {
        const Node* parent = context->building_parent;
        viewport_height = parent != nullptr && std::isfinite(parent->style.height) && parent->style.height >= 0.0f
            ? parent->style.height : context->frame_info.viewport.bounds.Height();
    }
    if (!std::isfinite(viewport_height)) viewport_height = 1.0f;
    viewport_height = std::max(1.0f, viewport_height - style.padding.top - style.padding.bottom);
    viewport_height = std::min(viewport_height, static_cast<float>(std::min<double>(total_height > 0.0 ? total_height : 1.0, 1.0e9)));
    const float max_offset = static_cast<float>(std::max(0.0, total_height - viewport_height));
    memory->offset.x = 0.0f;
    memory->offset.y = Clamp(memory->offset.y, 0.0f, max_offset);

    const double logical_offset = static_cast<double>(memory->offset.y);
    const std::size_t first_visible = item_count == 0u ? 0u : std::min<std::size_t>(item_count - 1u,
        static_cast<std::size_t>(std::floor(logical_offset / static_cast<double>(item_extent))));
    const double desired_visible = std::ceil(static_cast<double>(viewport_height) / item_extent) + 1.0;
    const std::size_t visible_count = item_count == 0u ? 0u : std::min<std::size_t>(item_count,
        static_cast<std::size_t>(std::min<double>(desired_visible, static_cast<double>(item_count))));
    cache_extent_items = std::min<std::size_t>(cache_extent_items, 1024u);
    const std::size_t start = first_visible > cache_extent_items ? first_visible - cache_extent_items : 0u;
    const std::size_t end = std::min<std::size_t>(item_count,
        first_visible + visible_count + cache_extent_items);

    if (!BeginNode(context, key, style)) return false;
    Node* collection_node = context->building_parent;
    const float lead_height = static_cast<float>(static_cast<double>(start) * item_extent);
    const float tail_height = static_cast<float>(static_cast<double>(item_count - end) * item_extent);
    const auto add_spacer = [&](const char* spacer_key, float height) noexcept -> bool {
        if (height <= 0.0f) return true;
        NodeStyle spacer{};
        spacer.height = height;
        spacer.max_height = 1.0e30f;
        spacer.animate_layout = false;
        spacer.pointer_events = false;
        if (!BeginNode(context, spacer_key, spacer)) return false;
        EndNode(context);
        return true;
    };

    bool succeeded = add_spacer("__lumen_virtual_lead", lead_height);
    if (succeeded && builder != nullptr) {
        for (std::size_t index = start; index < end; ++index) {
            NodeStyle item_style{};
            item_style.height = item_extent;
            item_style.max_height = item_extent;
            item_style.flex_shrink = 0.0f;
            item_style.animate_layout = false;
            item_style.clip_children = true;
            if (!BeginNode(context, static_cast<std::uint64_t>(index), item_style)) {
                succeeded = false;
                break;
            }
            Node* item_node = context->building_parent;
            builder(context, index, user_data);
            if (context->building_parent != item_node || context->node_tree_invalid) {
                context->node_tree_invalid = true;
                context->building_parent = item_node;
                succeeded = false;
            }
            EndNode(context);
            if (!succeeded) break;
        }
    }
    if (succeeded) succeeded = add_spacer("__lumen_virtual_tail", tail_height);
    if (context->building_parent != collection_node) {
        context->node_tree_invalid = true;
        context->building_parent = collection_node;
        succeeded = false;
    }
    EndNode(context);
    if (!succeeded) context->node_tree_invalid = true;
    return succeeded;
}

Vec2 GetScrollOffset(Context* context, std::uint64_t scroll_node_id) noexcept {
    if (context == nullptr || !context->in_frame || scroll_node_id == 0u) return {};
    ScrollMemory* memory = State<ScrollMemory>(context, MakeId(scroll_node_id, "scroll-offset"));
    return memory != nullptr ? memory->offset : Vec2{};
}

bool SetScrollOffset(Context* context, std::uint64_t scroll_node_id, Vec2 offset) noexcept {
    if (context == nullptr || !context->in_frame || scroll_node_id == 0u || !IsFinite(offset)) return false;
    ScrollMemory* memory = State<ScrollMemory>(context, MakeId(scroll_node_id, "scroll-offset"));
    if (memory == nullptr) return false;
    memory->offset = {std::max(0.0f, offset.x), std::max(0.0f, offset.y)};
    RequestRedraw(context);
    return true;
}

bool CapturePointer(Context* context, std::uint64_t node_id, std::uint64_t pointer_id) noexcept {
    if (context == nullptr || !context->in_frame || !context->dispatching_event ||
        context->dispatch_current_target_id != node_id || pointer_id == 0u ||
        context->dispatch_pointer_id != pointer_id || !IsPointerEvent(context->dispatch_event_type) ||
        (context->dispatch_event_type != UIEventType::PointerDown &&
         context->dispatch_event_type != UIEventType::PointerMove)) return false;
    // Mouse capture acquired from a move is only meaningful while a drag is active.
    // PointerDown is allowed immediately, before the host has finished the frame.
    if (context->dispatch_event_type == UIEventType::PointerMove &&
        !ImGui::IsMouseDown(ImGuiMouseButton_Left)) return false;
    if (FindContextNodeById(context, node_id) == nullptr) return false;
    context->captured_node_id = node_id;
    context->captured_pointer_id = pointer_id;
    RequestRedraw(context);
    return true;
}

bool ReleasePointer(Context* context, std::uint64_t node_id, std::uint64_t pointer_id) noexcept {
    if (context == nullptr || !context->in_frame || !context->dispatching_event ||
        context->dispatch_current_target_id != node_id || pointer_id == 0u ||
        context->dispatch_pointer_id != pointer_id || context->captured_node_id != node_id ||
        context->captured_pointer_id != pointer_id) return false;
    context->captured_node_id = 0u;
    context->captured_pointer_id = 0u;
    RequestRedraw(context);
    return true;
}

bool HasPointerCapture(const Context* context, std::uint64_t node_id, std::uint64_t pointer_id) noexcept {
    return context != nullptr && node_id != 0u && pointer_id != 0u &&
           context->captured_node_id == node_id && context->captured_pointer_id == pointer_id;
}

} // namespace Lumen
