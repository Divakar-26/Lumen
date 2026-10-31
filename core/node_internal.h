#ifndef LUMEN_CORE_NODE_INTERNAL_H
#define LUMEN_CORE_NODE_INTERNAL_H

#include "lumen/ui.h"

namespace Lumen {

enum class NodeKind : std::uint8_t { Root, Container, Label, Button, Image };

// offset is what is displayed; target is the logical position the wheel and clamping move.
struct ScrollMemory {
    Vec2 offset{};
    Vec2 target{};
    double last_activity = -1.0e9;
};
struct ImageData { ImTextureID texture{}; Vec2 source_size{}; ImageFit fit = ImageFit::Contain; };

enum class RenderCommandType : std::uint8_t { FillRect, StrokeRect, Text, Image, PushClip, PopClip, Shadow, Paint };

struct Node;

struct RenderCommand {
    RenderCommandType type = RenderCommandType::FillRect;
    Rect rect{};
    Color color{};
    union {
        const char* text = nullptr;
        const Node* node; // Paint
    };
    float radius = 0.0f;
    float thickness = 0.0f;
    float font_size = 0.0f;
    float wrap_width = 0.0f;
    ImTextureID texture = ImTextureID{};
};

struct Node {
    std::uint64_t id = 0;
    Node* parent = nullptr;
    Node* first_child = nullptr;
    Node* last_child = nullptr;
    Node* next_sibling = nullptr;
    NodeStyle style{};
    const char* text = nullptr;
    ImageData* image = nullptr;
    ClickHandler click_handler = nullptr;
    void* user_data = nullptr;
    Rect rect{};
    Rect target_rect{};
    Vec2 intrinsic{};
    Vec2 content_extent{};
    Vec2 scroll_offset{};
    Color resolved_text_color{};
    float resolved_font_size = 16.0f;
    float resolved_opacity = 1.0f;
    std::uint32_t child_count = 0;
    NodeKind kind = NodeKind::Container;
    bool hovered = false;
    bool pressed = false;
    bool focused = false;
    bool focus_visible = false;
    bool clicked = false;
};

namespace detail {
void ResetNodeFrame(Context* context) noexcept;
bool StartNode(Context* context, const char* key, std::uint64_t numeric_key,
               bool numeric, NodeKind kind, const NodeStyle& style,
               const char* text, ClickHandler handler, void* user_data) noexcept;
void CloseNode(Context* context) noexcept;
bool FinishNodeFrame(Context* context) noexcept;
Theme DefaultTheme(ThemeKind kind) noexcept;
}

} // namespace Lumen

#endif
