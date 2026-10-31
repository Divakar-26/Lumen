#include "lumen/layers.h"
#include "lumen/behavior.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace {
int failures = 0;
void Check(bool condition, const char* expression, int line) {
    if (!condition) { std::fprintf(stderr, "FAIL line %d: %s\n", line, expression); ++failures; }
}
#define CHECK(x) Check((x), #x, __LINE__)

float InvalidCurve(float) noexcept { return std::numeric_limits<float>::quiet_NaN(); }

void TestLayerOrderingAndBounds() {
    Lumen::LayerStack layers;
    CHECK(layers.Empty());
    CHECK(!layers.Open(0u, Lumen::LayerKind::Modal));
    CHECK(layers.Open(1u, Lumen::LayerKind::Base));
    CHECK(layers.Open(2u, Lumen::LayerKind::Popover));
    CHECK(layers.Open(3u, Lumen::LayerKind::Toast));
    CHECK(layers.Open(4u, Lumen::LayerKind::Popover));
    CHECK(layers.Size() == 4u);
    CHECK(layers.CopyPaintOrder(nullptr, 0u) == 4u);
    CHECK(layers.Top() != nullptr && layers.Top()->id == 4u);
    Lumen::LayerEntry ordered[Lumen::LayerStack::kCapacity]{};
    CHECK(layers.CopyPaintOrder(ordered, Lumen::LayerStack::kCapacity) == 4u);
    CHECK(ordered[0].id == 1u);
    CHECK(ordered[1].id == 2u && ordered[2].id == 4u && ordered[3].id == 3u);
    CHECK(layers.Open(2u, Lumen::LayerKind::Drag)); // Reopen raises and updates one entry.
    CHECK(layers.Size() == 4u);
    CHECK(layers.Find(2u)->kind == Lumen::LayerKind::Drag);
    CHECK(layers.Top()->id == 2u);
    for (std::uint64_t id = 10u; id < 10u + Lumen::LayerStack::kCapacity - 4u; ++id)
        CHECK(layers.Open(id, Lumen::LayerKind::Floating));
    CHECK(layers.Size() == Lumen::LayerStack::kCapacity);
    CHECK(!layers.Open(999u, Lumen::LayerKind::Modal));
    CHECK(layers.Close(12u));
    CHECK(!layers.Close(12u));
    CHECK(layers.Open(999u, Lumen::LayerKind::Modal));
}

void TestDismissalAndModalInput() {
    Lumen::LayerStack layers;
    CHECK(layers.Open(1u, Lumen::LayerKind::Base));
    CHECK(layers.Open(2u, Lumen::LayerKind::Popover, Lumen::DismissOnEscape | Lumen::DismissOnOutsideClick));
    CHECK(layers.Open(3u, Lumen::LayerKind::Modal, Lumen::DismissOnEscape));
    CHECK(layers.HasModal());
    CHECK(layers.BlocksInput(1u));
    CHECK(layers.BlocksInput(2u));
    CHECK(!layers.BlocksInput(3u));
    CHECK(layers.Open(4u, Lumen::LayerKind::Toast));
    CHECK(!layers.BlocksInput(4u));
    CHECK(!layers.TryDismissTop(Lumen::DismissReason::OutsideClick)); // Top toast has no such policy.
    CHECK(!layers.TryDismissTop(Lumen::DismissReason::Escape)); // Do not dismiss through a newer layer.
    CHECK(layers.Close(4u));
    CHECK(layers.TryDismissTop(Lumen::DismissReason::Escape));
    CHECK(layers.Find(3u) == nullptr);
    CHECK(!layers.HasModal());
    CHECK(!layers.BlocksInput(1u));
    CHECK(layers.TryDismissTop(Lumen::DismissReason::OutsideClick));
    CHECK(layers.Size() == 1u);
}

void TestTimeoutsMonotonicClock() {
    Lumen::LayerStack layers;
    CHECK(layers.Open(1u, Lumen::LayerKind::Toast, Lumen::DismissOnTimeout, 5.0, 2.0));
    CHECK(layers.Open(2u, Lumen::LayerKind::Toast, Lumen::DismissOnTimeout, 6.0, 4.0));
    CHECK(layers.NeedsFrame());
    CHECK(layers.Advance(6.9) == 0u);
    CHECK(layers.Advance(4.0) == 0u); // Cannot rewind time.
    CHECK(layers.Advance(7.0) == 1u);
    CHECK(layers.Find(1u) == nullptr && layers.Find(2u) != nullptr);
    CHECK(layers.Advance(10.0) == 1u);
    CHECK(layers.Empty());
    CHECK(!layers.NeedsFrame());
    CHECK(layers.Open(3u, Lumen::LayerKind::Toast, Lumen::DismissOnTimeout, 2.0, 3.0));
    CHECK(layers.Find(3u)->opened_at == 10.0); // Opening timestamps cannot rewind after a host clock correction.
    CHECK(layers.Advance(12.9) == 0u);
    CHECK(layers.Advance(13.0) == 1u);
    CHECK(layers.Open(4u, Lumen::LayerKind::Toast, Lumen::DismissOnTimeout, 13.0,
                      std::numeric_limits<double>::infinity()));
    CHECK(layers.Find(4u)->timeout_seconds == 0.0); // Non-finite timeout becomes no timeout.
    CHECK(layers.Advance(std::numeric_limits<double>::quiet_NaN()) == 0u);
}

void TestAnchorPlacement() {
    Lumen::AnchorPlacementOptions options{};
    options.viewport = {0.0f, 0.0f, 800.0f, 600.0f};
    options.safe_area = {20.0f, 20.0f, 20.0f, 20.0f};
    options.anchor = {300.0f, 200.0f, 80.0f, 30.0f};
    options.overlay_size = {180.0f, 120.0f};
    options.placement = Lumen::Placement::BelowAnchor;
    options.align = Lumen::PlacementAlign::Center;
    options.offset = {0.0f, 8.0f};
    Lumen::AnchorPlacementResult result{};
    CHECK(Lumen::ResolveAnchorPlacement(options, &result));
    CHECK(result.placement == Lumen::Placement::BelowAnchor && !result.flipped);
    CHECK(std::fabs(result.rect.min.x - 250.0f) < 1e-4f);
    CHECK(std::fabs(result.rect.min.y - 238.0f) < 1e-4f);

    options.anchor = {350.0f, 520.0f, 60.0f, 30.0f};
    options.placement = Lumen::Placement::BelowAnchor;
    options.align = Lumen::PlacementAlign::Start;
    CHECK(Lumen::ResolveAnchorPlacement(options, &result));
    CHECK(result.placement == Lumen::Placement::AboveAnchor && result.flipped);
    CHECK(result.rect.max.y <= 580.0f + 1e-4f);

    options.anchor = {760.0f, 250.0f, 20.0f, 25.0f};
    options.overlay_size = {200.0f, 120.0f};
    options.placement = Lumen::Placement::Trailing;
    options.flip = false;
    CHECK(Lumen::ResolveAnchorPlacement(options, &result));
    CHECK(result.clamped);
    CHECK(result.rect.max.x <= 780.0f + 1e-4f);

    options.placement = Lumen::Placement::Trailing;
    options.flip = false;
    options.anchor = {300.0f, 280.0f, 20.0f, 20.0f};
    options.overlay_size = {120.0f, 80.0f};
    options.align = Lumen::PlacementAlign::Center;
    CHECK(Lumen::ResolveAnchorPlacement(options, &result));
    CHECK(result.placement == Lumen::Placement::Trailing);
    CHECK(std::fabs(result.rect.min.x - 320.0f) < 1e-4f);
    CHECK(std::fabs(result.rect.Center().y - 298.0f) < 1e-4f);

    options.placement = Lumen::Placement::Leading;
    CHECK(Lumen::ResolveAnchorPlacement(options, &result));
    CHECK(result.placement == Lumen::Placement::Leading);
    CHECK(std::fabs(result.rect.max.x - 300.0f) < 1e-4f);

    options.placement = Lumen::Placement::Auto;
    options.anchor = {390.0f, 280.0f, 20.0f, 20.0f};
    CHECK(Lumen::ResolveAnchorPlacement(options, &result));
    CHECK(result.placement != Lumen::Placement::Auto);
    CHECK(!Lumen::ResolveAnchorPlacement(options, nullptr));
    options.overlay_size.x = std::numeric_limits<float>::quiet_NaN();
    CHECK(!Lumen::ResolveAnchorPlacement(options, &result));
}

void TestPresenceInvalidCurveFallback() {
    Lumen::Presence presence;
    presence.SetPresent(true, 0.0, 1.0, Lumen::Curve::Custom(InvalidCurve));
    CHECK(presence.Advance(0.25));
    CHECK(std::isfinite(presence.Opacity()));
    CHECK(std::fabs(presence.Opacity() - 0.25f) < 1.0e-5f);
    CHECK(presence.Advance(0.5));
    CHECK(std::fabs(presence.Opacity() - 0.5f) < 1.0e-5f);
    CHECK(!presence.Advance(1.0));
    CHECK(presence.Opacity() == 1.0f);
}
}

int main() {
    TestLayerOrderingAndBounds();
    TestDismissalAndModalInput();
    TestTimeoutsMonotonicClock();
    TestAnchorPlacement();
    TestPresenceInvalidCurveFallback();
    if (failures != 0) {
        std::fprintf(stderr, "%d layer assertion(s) failed.\n", failures);
        return EXIT_FAILURE;
    }
    std::puts("All Lumen layer tests passed.");
    return EXIT_SUCCESS;
}
