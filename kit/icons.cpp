#include "lumen/kit/icons.h"

#include <algorithm>
#include <new>

namespace Lumen::Kit {
namespace {

constexpr float kGrid = 24.0f;
constexpr float kPi = 3.14159265358979f;

// Path opcodes followed by grid coordinates: M x y, L x y, C x1 y1 x2 y2 x y, O cx cy r (circle
// outline), D cx cy r (filled dot), S (stroke open), Z (stroke closed), E (end).
enum Op : unsigned char { M, L, C, O, D, S, Z, E };

struct Command {
    Op op;
    float v[6];
};

constexpr Command kCheck[] = {{M, {5, 12.5f}}, {L, {10, 17.5f}}, {L, {19, 7}}, {S, {}}, {E, {}}};
constexpr Command kClose[] = {{M, {6, 6}}, {L, {18, 18}}, {S, {}}, {M, {18, 6}}, {L, {6, 18}}, {S, {}}, {E, {}}};
constexpr Command kInfo[] = {{D, {12, 6.5f, 1.6f}}, {M, {12, 10.5f}}, {L, {12, 18}}, {S, {}}, {E, {}}};
constexpr Command kWarning[] = {{M, {12, 5}}, {L, {12, 13.5f}}, {S, {}}, {D, {12, 18, 1.6f}}, {E, {}}};
constexpr Command kChevronDown[] = {{M, {6, 9}}, {L, {12, 15}}, {L, {18, 9}}, {S, {}}, {E, {}}};
constexpr Command kChevronRight[] = {{M, {9, 6}}, {L, {15, 12}}, {L, {9, 18}}, {S, {}}, {E, {}}};
constexpr Command kPlus[] = {{M, {12, 5}}, {L, {12, 19}}, {S, {}}, {M, {5, 12}}, {L, {19, 12}}, {S, {}}, {E, {}}};
constexpr Command kMinus[] = {{M, {5, 12}}, {L, {19, 12}}, {S, {}}, {E, {}}};
constexpr Command kSearch[] = {{O, {11, 11, 6.5f}}, {M, {16, 16}}, {L, {20, 20}}, {S, {}}, {E, {}}};
constexpr Command kMenu[] = {{M, {4, 7}}, {L, {20, 7}}, {S, {}}, {M, {4, 12}}, {L, {20, 12}}, {S, {}},
                             {M, {4, 17}}, {L, {20, 17}}, {S, {}}, {E, {}}};
constexpr Command kHeart[] = {{M, {12, 20}},
                              {C, {5, 15, 2.5f, 11, 4, 7.5f}},
                              {C, {5.5f, 4.2f, 10, 4.5f, 12, 7.5f}},
                              {C, {14, 4.5f, 18.5f, 4.2f, 20, 7.5f}},
                              {C, {21.5f, 11, 19, 15, 12, 20}},
                              {Z, {}},
                              {E, {}}};

const Command* IconData(IconId icon) noexcept {
    switch (icon) {
        case IconId::Check: return kCheck;
        case IconId::Close: return kClose;
        case IconId::Info: return kInfo;
        case IconId::Warning: return kWarning;
        case IconId::ChevronDown: return kChevronDown;
        case IconId::ChevronRight: return kChevronRight;
        case IconId::Plus: return kPlus;
        case IconId::Minus: return kMinus;
        case IconId::Search: return kSearch;
        case IconId::Menu: return kMenu;
        case IconId::Heart: return kHeart;
    }
    return nullptr;
}

struct IconPaint {
    IconId icon = IconId::Check;
    Color color{};
};

void PaintIcon(Context*, Canvas& canvas, void* user_data) noexcept {
    const auto* paint = static_cast<const IconPaint*>(user_data);
    DrawIcon(canvas, paint->icon, canvas.Bounds(), paint->color);
}

} // namespace

void DrawIcon(Canvas& canvas, IconId icon, Rect box, Color color, float stroke_width) noexcept {
    const Command* command = IconData(icon);
    if (command == nullptr || !IsFinite(box) || box.IsEmpty()) return;
    const float side = std::min(box.Width(), box.Height());
    const float scale = side / kGrid;
    const Vec2 origin{box.min.x + 0.5f * (box.Width() - side), box.min.y + 0.5f * (box.Height() - side)};
    const auto at = [&](float x, float y) { return Vec2{origin.x + x * scale, origin.y + y * scale}; };
    const float stroke = std::max(0.5f, stroke_width * scale);
    for (; command->op != E; ++command) {
        const float* v = command->v;
        switch (command->op) {
            case M: canvas.MoveTo(at(v[0], v[1])); break;
            case L: canvas.LineTo(at(v[0], v[1])); break;
            case C: canvas.CubicTo(at(v[0], v[1]), at(v[2], v[3]), at(v[4], v[5])); break;
            case O:
                canvas.BeginPath();
                canvas.ArcTo(at(v[0], v[1]), v[2] * scale, 0.0f, 2.0f * kPi);
                canvas.Stroke(color, stroke, true);
                break;
            case D: canvas.FillCircle(at(v[0], v[1]), v[2] * scale, color); break;
            case S: canvas.Stroke(color, stroke, false); break;
            case Z: canvas.Stroke(color, stroke, true); break;
            case E: break;
        }
    }
}

bool Icon(Context* context, const char* key, IconId icon, float size, Color color, const NodeStyle& style) noexcept {
    void* memory = FrameAlloc(context, sizeof(IconPaint), alignof(IconPaint));
    if (memory == nullptr) return false;
    auto* paint = new (memory) IconPaint{icon, color};
    const CustomPaint custom{PaintIcon, paint};
    NodeStyle node = style;
    node.width = size;
    node.height = size;
    node.custom_paint = &custom;
    if (node.event_handler == nullptr && !node.focusable) node.pointer_events = false;
    if (!BeginNode(context, key, node)) return false;
    EndNode(context);
    return true;
}

} // namespace Lumen::Kit
