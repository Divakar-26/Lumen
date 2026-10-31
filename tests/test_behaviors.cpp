#include "lumen/behavior.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
int failures = 0;
void Check(bool value, const char* expr, int line) {
    if (!value) { std::fprintf(stderr, "FAIL line %d: %s\n", line, expr); ++failures; }
}
#define CHECK(x) Check((x), #x, __LINE__)

void TestRouter() {
    Lumen::Router router;
    CHECK(router.Current() == nullptr);
    CHECK(!router.Pop());
    CHECK(!router.Push(nullptr));
    CHECK(!router.Push(""));
    CHECK(router.Push("home"));
    CHECK(router.Push("settings"));
    CHECK(std::strcmp(router.Current(), "settings") == 0);
    CHECK(std::strcmp(router.Previous(), "home") == 0);
    CHECK(router.Replace("profile"));
    CHECK(router.Depth() == 2u);
    CHECK(std::strcmp(router.Current(), "profile") == 0);
    CHECK(router.Replace(router.Current())); // Self-alias must be safe.
    CHECK(std::strcmp(router.Current(), "profile") == 0);
    CHECK(router.Pop());
    CHECK(std::strcmp(router.Current(), "home") == 0);
    CHECK(!router.Pop());
    for (std::size_t i = 1; i < Lumen::Router::kMaxDepth; ++i) CHECK(router.Push("route"));
    CHECK(router.Depth() == Lumen::Router::kMaxDepth);
    CHECK(!router.Push("overflow"));
    char too_long[Lumen::Router::kNameCapacity + 1]{};
    std::memset(too_long, 'x', sizeof(too_long) - 1u);
    CHECK(!router.Push(too_long));
    router.Reset();
    CHECK(router.Depth() == 0u && router.Current() == nullptr);
}

void TestPresence() {
    Lumen::Presence presence;
    CHECK(presence.Phase() == Lumen::PresencePhase::Absent);
    CHECK(!presence.IsMounted());
    CHECK(!presence.NeedsFrame());
    presence.SetPresent(true, 1.0, 0.2, Lumen::Curve::Linear());
    CHECK(presence.IsMounted() && presence.NeedsFrame());
    CHECK(presence.Advance(1.1));
    CHECK(std::fabs(presence.Opacity() - 0.5f) < 1.0e-5f);
    const float before_reverse = presence.Opacity();
    presence.SetPresent(false, 1.1, 0.2, Lumen::Curve::Linear());
    CHECK(std::fabs(presence.Opacity() - before_reverse) < 1.0e-5f);
    CHECK(presence.Advance(1.2));
    CHECK(presence.Opacity() < before_reverse);
    CHECK(!presence.Advance(1.4));
    CHECK(presence.Phase() == Lumen::PresencePhase::Absent);
    CHECK(!presence.IsMounted());
    CHECK(presence.Opacity() == 0.0f);

    presence.SetPresent(true, 2.0, 0.0);
    CHECK(presence.Phase() == Lumen::PresencePhase::Present);
    CHECK(presence.Opacity() == 1.0f);
    presence.SetPresent(false, 1.0, 1.0); // time is monotonic; cannot rewind transition clock
    CHECK(presence.Phase() == Lumen::PresencePhase::Exiting);
    CHECK(presence.Advance(3.0) == false);
    CHECK(presence.Opacity() == 0.0f);
}
}

int main() {
    TestRouter();
    TestPresence();
    if (failures != 0) {
        std::fprintf(stderr, "%d behavior assertion(s) failed.\n", failures);
        return EXIT_FAILURE;
    }
    std::puts("All Lumen behavior tests passed.");
    return EXIT_SUCCESS;
}
