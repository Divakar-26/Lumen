#include "lumen/lumen.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>

namespace {

int failures = 0;

void Check(bool condition, const char* expression, int line) {
    if (!condition) {
        std::fprintf(stderr, "FAIL line %d: %s\n", line, expression);
        ++failures;
    }
}

#define CHECK(expression) Check((expression), #expression, __LINE__)

struct ImGuiFrame {
    ImGuiFrame() {
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(1024.0f, 768.0f);
        io.DeltaTime = 1.0f / 60.0f;
        if (!io.Fonts->IsBuilt()) {
            unsigned char* pixels = nullptr;
            int width = 0;
            int height = 0;
            io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        }
        ImGui::NewFrame();
        ImGui::Begin("Lumen M0 tests");
    }
    ~ImGuiFrame() {
        ImGui::End();
        ImGui::Render();
    }
};

bool RunFrame(Lumen::Context* context, const Lumen::Viewport& viewport, float dt,
              void (*body)(Lumen::Context*, void*), void* user = nullptr) {
    ImGuiFrame host_frame;
    if (!Lumen::BeginFrame(context, viewport, dt)) {
        return false;
    }
    if (body != nullptr) {
        body(context, user);
    }
    return Lumen::EndFrame(context);
}

struct StateCapture {
    std::uint64_t id = 0;
    int* observed = nullptr;
    int initial = 0;
};

void CaptureState(Lumen::Context* context, void* user) {
    auto* capture = static_cast<StateCapture*>(user);
    int* value = Lumen::State<int>(context, capture->id, capture->initial);
    CHECK(value != nullptr);
    if (value != nullptr && capture->observed != nullptr) {
        *capture->observed = *value;
    }
}

void TestMathAndIds() {
    const Lumen::Rect rect{10.0f, 20.0f, 30.0f, 40.0f};
    CHECK(rect.Width() == 30.0f);
    CHECK(rect.Height() == 40.0f);
    CHECK(rect.Contains({15.0f, 25.0f}));
    CHECK(!rect.Contains({40.0f, 25.0f}));
    CHECK(Lumen::Lerp(2.0f, 6.0f, 0.5f) == 4.0f);

    const std::uint64_t first = Lumen::MakeId(42, "settings");
    CHECK(first != 0);
    CHECK(first == Lumen::MakeId(42, "settings"));
    CHECK(first != Lumen::MakeId(42, "profile"));
    CHECK(first != Lumen::MakeId(43, "settings"));
    CHECK(Lumen::MakeId(42, std::uint64_t{7}) == Lumen::MakeId(42, std::uint64_t{7}));
}

void TestContextAndFrames() {
    Lumen::SteadyClock default_clock;
    CHECK(default_clock.NowSeconds() >= 0.0);
    CHECK(Lumen::CreateContext() == nullptr);

    ImGui::CreateContext();
    Lumen::ManualClock clock(10.0);
    Lumen::Config config;
    config.frame_arena_bytes = 4096;
    config.state_capacity = 16;
    config.max_state_bytes = 64;
    config.state_ttl_frames = 2;
    config.clock = &clock;

    Lumen::Context* context = Lumen::CreateContext(config);
    CHECK(context != nullptr);
    if (context == nullptr) {
        ImGui::DestroyContext();
        return;
    }

    const Lumen::Viewport viewport{Lumen::Rect{0.0f, 0.0f, 800.0f, 600.0f}, {}, {1.0f, 1.0f}};
    CHECK(Lumen::NeedsFrame(context));

    StateCapture capture{Lumen::MakeId(1, "counter"), nullptr, 7};
    CHECK(RunFrame(context, viewport, 0.016f, CaptureState, &capture));
    Lumen::DebugStats stats = Lumen::GetDebugStats(context);
    CHECK(stats.frames_started == 1);
    CHECK(!stats.in_frame);
    CHECK(stats.state_entries == 1);
    CHECK(stats.state_misses == 1);
    CHECK(stats.arena_failed_allocations == 0);
    CHECK(stats.arena_bytes_used == 0);
    CHECK(!Lumen::NeedsFrame(context));

    int observed = 0;
    capture.observed = &observed;
    capture.initial = 99;
    auto mutate_state = [](Lumen::Context* ui, void* data) {
        auto* cap = static_cast<StateCapture*>(data);
        int* value = Lumen::State<int>(ui, cap->id, cap->initial);
        CHECK(value != nullptr);
        if (value != nullptr) {
            *value += 4;
            if (cap->observed != nullptr) *cap->observed = *value;
        }
        auto* bytes = static_cast<unsigned char*>(Lumen::FrameAlloc(ui, 17, 64));
        CHECK(bytes != nullptr);
        if (bytes != nullptr) {
            CHECK(reinterpret_cast<std::uintptr_t>(bytes) % 64u == 0u);
            std::memset(bytes, 0xA5, 17);
        }
    };
    CHECK(RunFrame(context, viewport, 5.0f, mutate_state, &capture));
    CHECK(observed == 11);
    const Lumen::FrameInfo* info = Lumen::GetFrameInfo(context);
    CHECK(info != nullptr);
    CHECK(info->index == 2);
    CHECK(info->delta_seconds == 0.1f);
    CHECK(info->time_seconds == 10.0);
    CHECK(info->root_id != 0);
    stats = Lumen::GetDebugStats(context);
    CHECK(stats.state_hits == 1);
    CHECK(stats.arena_bytes_used >= 17);
    CHECK(stats.arena_failed_allocations == 0);

    Lumen::RequestRedraw(context);
    CHECK(Lumen::NeedsFrame(context));
    CHECK(RunFrame(context, viewport, 0.016f, nullptr));
    CHECK(!Lumen::NeedsFrame(context));

    Lumen::InjectMousePosition(context, {25.0f, 40.0f});
    Lumen::InjectMouseButton(context, 0, true);
    Lumen::InjectMouseWheel(context, {0.0f, 1.0f});
    Lumen::InjectKey(context, ImGuiKey_Space, true);
    Lumen::InjectText(context, "Lumen");
    Lumen::InjectFocus(context, true);
    CHECK(Lumen::NeedsFrame(context));

    CHECK(RunFrame(context, viewport, 0.016f, nullptr));
    stats = Lumen::GetDebugStats(context);
    CHECK(stats.arena_failed_allocations == 0);
    CHECK(stats.state_evictions == 0);

    // Non-finite and backward host-clock readings must never poison motion time
    // or cause time to be counted twice when the clock advances again.
    clock.Set(std::numeric_limits<double>::quiet_NaN());
    CHECK(RunFrame(context, viewport, 0.016f, nullptr));
    info = Lumen::GetFrameInfo(context);
    CHECK(info != nullptr && std::isfinite(info->time_seconds) && info->time_seconds == 10.0);
    clock.Set(9.0);
    CHECK(RunFrame(context, viewport, 0.016f, nullptr));
    info = Lumen::GetFrameInfo(context);
    CHECK(info != nullptr && info->time_seconds == 10.0);
    clock.Set(10.5);
    CHECK(RunFrame(context, viewport, 0.016f, nullptr));
    info = Lumen::GetFrameInfo(context);
    CHECK(info != nullptr && info->time_seconds == 10.5);

    Lumen::DestroyContext(context);
    ImGui::DestroyContext();
}

void TestNonFiniteInitialClock() {
    ImGui::CreateContext();
    Lumen::ManualClock clock(std::numeric_limits<double>::quiet_NaN());
    Lumen::Config config{};
    config.clock = &clock;
    Lumen::Context* context = Lumen::CreateContext(config);
    CHECK(context != nullptr);
    if (context != nullptr) {
        const Lumen::Viewport viewport{Lumen::Rect{0.0f, 0.0f, 100.0f, 100.0f}, {}, {1.0f, 1.0f}};
        CHECK(RunFrame(context, viewport, 1.0f / 60.0f, nullptr));
        const Lumen::FrameInfo* info = Lumen::GetFrameInfo(context);
        CHECK(info != nullptr && std::isfinite(info->time_seconds) && info->time_seconds == 0.0);
        clock.Set(0.25);
        CHECK(RunFrame(context, viewport, 1.0f / 60.0f, nullptr));
        info = Lumen::GetFrameInfo(context);
        CHECK(info != nullptr && info->time_seconds == 0.25);
        Lumen::DestroyContext(context);
    }
    ImGui::DestroyContext();
}

struct StatePair {
    std::uint64_t pinned_id = 0;
    std::uint64_t expired_id = 0;
};

void CreateBothStates(Lumen::Context* ui, void* user) {
    const auto* ids = static_cast<StatePair*>(user);
    int* pinned = Lumen::State<int>(ui, ids->pinned_id, 10);
    int* expired = Lumen::State<int>(ui, ids->expired_id, 20);
    CHECK(pinned != nullptr && expired != nullptr);
    Lumen::PinState(ui, ids->pinned_id, true);
}

void TestStateEvictionAndPinning() {
    ImGui::CreateContext();
    Lumen::Config config;
    config.frame_arena_bytes = 1024;
    config.state_capacity = 16;
    config.max_state_bytes = 64;
    config.state_ttl_frames = 1;
    Lumen::Context* context = Lumen::CreateContext(config);
    CHECK(context != nullptr);
    if (context == nullptr) {
        ImGui::DestroyContext();
        return;
    }
    const Lumen::Viewport viewport{Lumen::Rect{0.0f, 0.0f, 100.0f, 100.0f}, {}, {1.0f, 1.0f}};
    const std::uint64_t pinned_id = Lumen::MakeId(5, "pinned");
    const std::uint64_t expired_id = Lumen::MakeId(5, "expired");

    StatePair ids{pinned_id, expired_id};
    CHECK(RunFrame(context, viewport, 1.0f / 60.0f, CreateBothStates, &ids));
    CHECK(RunFrame(context, viewport, 1.0f / 60.0f, nullptr));
    CHECK(RunFrame(context, viewport, 1.0f / 60.0f, nullptr));
    CHECK(RunFrame(context, viewport, 1.0f / 60.0f, nullptr));

    int pinned_value = -1;
    StateCapture pinned_capture{pinned_id, &pinned_value, 0};
    CHECK(RunFrame(context, viewport, 1.0f / 60.0f, CaptureState, &pinned_capture));
    CHECK(pinned_value == 10);

    int expired_value = -1;
    StateCapture expired_capture{expired_id, &expired_value, 99};
    CHECK(RunFrame(context, viewport, 1.0f / 60.0f, CaptureState, &expired_capture));
    CHECK(expired_value == 99);
    const Lumen::DebugStats stats = Lumen::GetDebugStats(context);
    CHECK(stats.state_evictions >= 1);
    CHECK(stats.state_entries == 2);

    Lumen::DestroyContext(context);
    ImGui::DestroyContext();
}

void TestEmptyFramesAndFrameOwnership() {
    ImGui::CreateContext();
    Lumen::Config config;
    config.frame_arena_bytes = 1024;
    config.state_capacity = 16;
    config.max_state_bytes = 64;
    Lumen::Context* first = Lumen::CreateContext(config);
    Lumen::Context* second = Lumen::CreateContext(config);
    CHECK(first != nullptr && second != nullptr);
    if (first == nullptr || second == nullptr) {
        Lumen::DestroyContext(first);
        Lumen::DestroyContext(second);
        ImGui::DestroyContext();
        return;
    }

    const Lumen::Viewport viewport{Lumen::Rect{0.0f, 0.0f, 320.0f, 240.0f}, {}, {1.0f, 1.0f}};
    CHECK(Lumen::FrameAlloc(first, 1) == nullptr);
    {
        ImGuiFrame host_frame;
        CHECK(Lumen::BeginFrame(first, viewport, 1.0f / 60.0f));
        CHECK(!Lumen::NeedsFrame(first));
        CHECK(!Lumen::BeginFrame(second, viewport, 1.0f / 60.0f));
        CHECK(Lumen::EndFrame(first));
    }
    const Lumen::DebugStats empty_stats = Lumen::GetDebugStats(first);
    CHECK(empty_stats.frames_started == 1);
    CHECK(empty_stats.arena_bytes_used == 0);
    CHECK(empty_stats.arena_failed_allocations == 0);
    CHECK(empty_stats.state_entries == 0);

    Lumen::DestroyContext(first);
    Lumen::DestroyContext(second);
    ImGui::DestroyContext();
}

void TestArenaAndStateCapacityLimits() {
    ImGui::CreateContext();
    Lumen::Config config;
    config.frame_arena_bytes = 32;
    config.state_capacity = 16;
    config.max_state_bytes = 64;
    config.state_ttl_frames = 120;
    Lumen::Context* context = Lumen::CreateContext(config);
    CHECK(context != nullptr);
    if (context == nullptr) {
        ImGui::DestroyContext();
        return;
    }

    const Lumen::Viewport viewport{Lumen::Rect{0.0f, 0.0f, 320.0f, 240.0f}, {}, {1.0f, 1.0f}};
    auto fill_pools = [](Lumen::Context* ui, void*) {
        CHECK(Lumen::FrameAlloc(ui, 24, 16) != nullptr);
        CHECK(Lumen::FrameAlloc(ui, 16, 16) == nullptr);
        for (std::uint64_t i = 0; i < 16; ++i) {
            int* value = Lumen::State<int>(ui, Lumen::MakeId(88, i + 1), static_cast<int>(i));
            CHECK(value != nullptr);
        }
        CHECK(Lumen::State<int>(ui, Lumen::MakeId(88, 17), 17) == nullptr);
    };
    CHECK(RunFrame(context, viewport, 1.0f / 60.0f, fill_pools));
    const Lumen::DebugStats stats = Lumen::GetDebugStats(context);
    CHECK(stats.arena_failed_allocations == 1);
    CHECK(stats.arena_bytes_used == 24);
    CHECK(stats.state_entries == 16);
    CHECK(stats.state_capacity_exhaustions == 1);

    Lumen::DestroyContext(context);
    ImGui::DestroyContext();
}

struct FloatAnimateCapture {
    std::uint64_t id = 0;
    float target = 0.0f;
    float retarget = 0.0f;
    float result = 0.0f;
    float retarget_result = 0.0f;
    bool retarget_in_same_frame = false;
    Lumen::TweenSpec spec{};
};

void AnimateFloatFrame(Lumen::Context* context, void* user) {
    auto* capture = static_cast<FloatAnimateCapture*>(user);
    capture->result = Lumen::Animate(context, capture->id, capture->target, capture->spec);
    if (capture->retarget_in_same_frame) {
        capture->retarget_result = Lumen::Animate(context, capture->id, capture->retarget, capture->spec);
    }
}

struct FloatSpringCapture {
    std::uint64_t id = 0;
    float target = 0.0f;
    float result = 0.0f;
    Lumen::SpringSpec spec = Lumen::SpringSpec::Smooth();
};
void AnimateFloatSpringFrame(Lumen::Context* context, void* user) {
    auto* capture = static_cast<FloatSpringCapture*>(user);
    capture->result = Lumen::Animate(context, capture->id, capture->target, capture->spec);
}

struct TimelineCapture {
    float first = -1.0f;
    float second = -1.0f;
    float parallel = -1.0f;
    std::size_t stagger_calls = 0;
};
void TimelineFirst(void* user, float progress) { static_cast<TimelineCapture*>(user)->first = progress; }
void TimelineSecond(void* user, float progress) { static_cast<TimelineCapture*>(user)->second = progress; }
void TimelineParallel(void* user, float progress) { static_cast<TimelineCapture*>(user)->parallel = progress; }
void TimelineStagger(void* user, std::size_t, float) { ++static_cast<TimelineCapture*>(user)->stagger_calls; }

struct ControllerCapture {
    int started = 0;
    int completed = 0;
    int cancelled = 0;
    float progress = -1.0f;
};
void ControllerStarted(void* user) { ++static_cast<ControllerCapture*>(user)->started; }
void ControllerUpdated(void* user, float progress) { static_cast<ControllerCapture*>(user)->progress = progress; }
void ControllerCompleted(void* user) { ++static_cast<ControllerCapture*>(user)->completed; }
void ControllerCancelled(void* user) { ++static_cast<ControllerCapture*>(user)->cancelled; }

float RunSpringAtRate(int frames_per_second) {
    Lumen::SpringSpec spec = Lumen::SpringSpec::FromResponse(0.38f, 0.72f);
    spec.position_epsilon = 0.0f;
    spec.velocity_epsilon = 0.0f;
    Lumen::Spring<float> spring(0.0f, 1.0f, spec);
    const double total = 0.7;
    const double frame = 1.0 / static_cast<double>(frames_per_second);
    double elapsed = 0.0;
    while (elapsed + 1.0e-12 < total) {
        const double dt = std::min(frame, total - elapsed);
        spring.Advance(dt);
        elapsed += dt;
    }
    return spring.Current();
}

float NonFiniteCurve(float) noexcept { return std::numeric_limits<float>::quiet_NaN(); }
float OvershootingCurve(float t) noexcept { return 1.25f * t; }

void TestMotionCurvesTweensAndKeyframes() {
    CHECK(std::fabs(Lumen::Curve::Custom(NonFiniteCurve).Evaluate(0.37f) - 0.37f) < 1.0e-6f);
    CHECK(std::fabs(Lumen::Curve::Custom(OvershootingCurve).Evaluate(0.9f) - 1.125f) < 1.0e-6f);
    CHECK(std::fabs(Lumen::Curve::QuadIn().Evaluate(0.5f) - 0.25f) < 1.0e-6f);
    CHECK(std::fabs(Lumen::Curve::QuadOut().Evaluate(0.5f) - 0.75f) < 1.0e-6f);
    CHECK(std::fabs(Lumen::Curve::Standard().Evaluate(0.0f)) < 1.0e-5f);
    CHECK(std::fabs(Lumen::Curve::Standard().Evaluate(1.0f) - 1.0f) < 1.0e-5f);
    CHECK(Lumen::Curve::Steps(4).Evaluate(0.4f) == 0.25f);
    CHECK(std::fabs(Lumen::Curve::CubicBezier(0.0f,0.0f,1.0f,1.0f).Evaluate(0.5f)-0.5f) < 1.0e-4f);

    Lumen::TweenSpec spec;
    spec.duration_seconds = 1.0;
    spec.delay_seconds = 0.5;
    spec.curve = Lumen::Curve::Linear();
    Lumen::Tween<float> tween(0.0f, 10.0f, 2.0, spec);
    CHECK(tween.Sample(2.25) == 0.0f);
    CHECK(std::fabs(tween.Sample(3.0)-5.0f) < 1.0e-6f);
    CHECK(tween.Sample(3.5) == 10.0f);
    CHECK(tween.IsFinished(3.5));

    Lumen::Keyframes<float, 4> keys;
    CHECK(keys.Add(0.0, 0.0f, Lumen::Curve::Linear()));
    CHECK(keys.Add(1.0, 10.0f, Lumen::Curve::Linear()));
    CHECK(keys.Add(2.0, 20.0f, Lumen::Curve::Linear()));
    CHECK(!keys.Add(1.5, 15.0f));
    CHECK(std::fabs(keys.Sample(0.5)-5.0f) < 1.0e-6f);
    CHECK(std::fabs(keys.Sample(1.5)-15.0f) < 1.0e-6f);
    CHECK(keys.Sample(3.0) == 20.0f);

    const Lumen::Color midpoint = Lumen::LerpTraits<Lumen::Color>::Lerp(
        Lumen::Color{1.0f,0.0f,0.0f,1.0f}, Lumen::Color{0.0f,0.0f,1.0f,0.5f}, 0.5f);
    CHECK(midpoint.a > 0.7f && midpoint.a < 0.8f);
    CHECK(midpoint.r >= 0.0f && midpoint.r <= 1.0f);
    CHECK(midpoint.b >= 0.0f && midpoint.b <= 1.0f);
}

void TestMotionSpringsAndFrameRateIndependence() {
    const float at30 = RunSpringAtRate(30);
    const float at60 = RunSpringAtRate(60);
    const float at144 = RunSpringAtRate(144);
    CHECK(std::fabs(at30-at60) < 2.0e-5f);
    CHECK(std::fabs(at60-at144) < 2.0e-5f);

    Lumen::Spring<float> spring(0.0f, 1.0f, Lumen::SpringSpec::Bouncy());
    spring.Advance(0.08);
    const float before_current = spring.Current();
    const float before_velocity = spring.Velocity();
    spring.SetTarget(-0.5f);
    CHECK(spring.Current() == before_current);
    CHECK(spring.Velocity() == before_velocity);
    spring.Advance(0.03);
    CHECK(spring.Current() != before_current);

    Lumen::Spring<Lumen::Vec2> vector_spring({0.0f,0.0f},{100.0f,40.0f},Lumen::SpringSpec::Snappy());
    CHECK(vector_spring.Advance(0.016));
    CHECK(vector_spring.Current().x > 0.0f);
    CHECK(vector_spring.Current().y > 0.0f);
}

void TestTimelineAndController() {
    TimelineCapture data;
    Lumen::Timeline timeline;
    CHECK(timeline.Sequence(TimelineFirst, &data, 0.5, Lumen::Curve::Linear()));
    CHECK(timeline.BeginParallel());
    CHECK(timeline.Parallel(TimelineSecond, &data, 0.4, Lumen::Curve::Linear()));
    CHECK(timeline.Parallel(TimelineParallel, &data, 0.8, Lumen::Curve::Linear()));
    CHECK(timeline.EndParallel());
    CHECK(timeline.Stagger(TimelineStagger, &data, 3, 0.2, 0.1, Lumen::Curve::Linear()));
    CHECK(timeline.TrackCount() == 6);
    CHECK(std::fabs(timeline.DurationSeconds()-1.7) < 1.0e-6);
    timeline.Sample(0.25);
    CHECK(std::fabs(data.first-0.5f) < 1.0e-6f);
    CHECK(data.second < 0.0f);
    timeline.Sample(0.7);
    CHECK(std::fabs(data.second-0.5f) < 1.0e-6f);
    CHECK(std::fabs(data.parallel-0.25f) < 1.0e-6f);
    timeline.Sample(1.6);
    CHECK(data.stagger_calls >= 3);

    ControllerCapture capture;
    Lumen::Controller controller(0.1);
    controller.SetCallbacks({ControllerStarted,ControllerUpdated,ControllerCompleted,ControllerCancelled,&capture});
    controller.Play(5.0);
    CHECK(capture.started == 1);
    CHECK(controller.IsPlaying());
    CHECK(controller.Update(5.05));
    CHECK(capture.progress > 0.49f && capture.progress < 0.51f);
    controller.Pause();
    CHECK(controller.IsPaused());
    CHECK(!controller.Update(5.08));
    controller.Resume(5.08);
    CHECK(controller.Update(5.13) == false);
    CHECK(capture.completed == 1);
    CHECK(!controller.NeedsFrame());
}

void TestImplicitAnimateRetargetAndNeedsFrame() {
    ImGui::CreateContext();
    Lumen::ManualClock clock(0.0);
    Lumen::Config config;
    config.frame_arena_bytes = 4096;
    config.state_capacity = 16;
    config.max_state_bytes = 256;
    config.state_ttl_frames = 120;
    config.clock = &clock;
    Lumen::Context* context = Lumen::CreateContext(config);
    CHECK(context != nullptr);
    if (!context) { ImGui::DestroyContext(); return; }

    const Lumen::Viewport viewport{Lumen::Rect{0.0f,0.0f,320.0f,240.0f},{},{1.0f,1.0f}};
    FloatAnimateCapture capture;
    capture.id = Lumen::MakeId(90,"implicit-tween");
    capture.target = 0.0f;
    capture.spec.duration_seconds = 0.2;
    capture.spec.curve = Lumen::Curve::Linear();
    CHECK(RunFrame(context,viewport,1.0f/60.0f,AnimateFloatFrame,&capture));
    CHECK(capture.result == 0.0f);
    CHECK(!Lumen::NeedsFrame(context));

    capture.target = 1.0f;
    CHECK(RunFrame(context,viewport,1.0f/60.0f,AnimateFloatFrame,&capture));
    CHECK(capture.result == 0.0f);
    CHECK(Lumen::NeedsFrame(context));

    clock.Set(0.1);
    capture.retarget_in_same_frame = true;
    capture.retarget = -1.0f;
    CHECK(RunFrame(context,viewport,1.0f/60.0f,AnimateFloatFrame,&capture));
    CHECK(std::fabs(capture.result-0.5f) < 1.0e-5f);
    CHECK(capture.retarget_result == capture.result);
    CHECK(Lumen::NeedsFrame(context));

    capture.retarget_in_same_frame = false;
    capture.target = -1.0f;
    clock.Set(0.2);
    CHECK(RunFrame(context,viewport,1.0f/60.0f,AnimateFloatFrame,&capture));
    CHECK(std::fabs(capture.result+0.25f) < 1.0e-5f);
    CHECK(Lumen::NeedsFrame(context));
    clock.Set(0.3);
    CHECK(RunFrame(context,viewport,1.0f/60.0f,AnimateFloatFrame,&capture));
    CHECK(capture.result == -1.0f);
    CHECK(!Lumen::NeedsFrame(context));

    FloatSpringCapture spring_capture;
    spring_capture.id = Lumen::MakeId(90,"implicit-spring");
    spring_capture.target = 0.0f;
    CHECK(RunFrame(context,viewport,1.0f/60.0f,AnimateFloatSpringFrame,&spring_capture));
    spring_capture.target = 1.0f;
    CHECK(RunFrame(context,viewport,1.0f/60.0f,AnimateFloatSpringFrame,&spring_capture));
    CHECK(Lumen::NeedsFrame(context));
    clock.Set(0.4);
    CHECK(RunFrame(context,viewport,1.0f/60.0f,AnimateFloatSpringFrame,&spring_capture));
    const float before_retarget = spring_capture.result;
    spring_capture.target = 2.0f;
    clock.Set(0.5);
    CHECK(RunFrame(context,viewport,1.0f/60.0f,AnimateFloatSpringFrame,&spring_capture));
    CHECK(spring_capture.result > before_retarget);
    CHECK(spring_capture.result < 1.2f);
    CHECK(Lumen::NeedsFrame(context));

    Lumen::SetMotionTimeScale(context,0.5f);
    FloatAnimateCapture scaled;
    scaled.id = Lumen::MakeId(90,"scaled-tween");
    scaled.target = 0.0f;
    scaled.spec.duration_seconds = 0.2;
    scaled.spec.curve = Lumen::Curve::Linear();
    clock.Set(0.5);
    CHECK(RunFrame(context,viewport,1.0f/60.0f,AnimateFloatFrame,&scaled));
    scaled.target = 1.0f;
    CHECK(RunFrame(context,viewport,1.0f/60.0f,AnimateFloatFrame,&scaled));
    clock.Set(0.7);
    CHECK(RunFrame(context,viewport,1.0f/60.0f,AnimateFloatFrame,&scaled));
    CHECK(std::fabs(scaled.result-0.5f) < 1.0e-5f);
    Lumen::SetMotionTimeScale(context,1.0f);

    Lumen::SetReduceMotion(context,true);
    FloatAnimateCapture reduced;
    reduced.id = Lumen::MakeId(90,"reduced-motion-tween");
    reduced.target = 0.0f;
    reduced.spec.duration_seconds = 1.0;
    reduced.spec.curve = Lumen::Curve::Linear();
    CHECK(RunFrame(context,viewport,1.0f/60.0f,AnimateFloatFrame,&reduced));
    reduced.target = 1.0f;
    CHECK(RunFrame(context,viewport,1.0f/60.0f,AnimateFloatFrame,&reduced));
    clock.Set(0.8);
    CHECK(RunFrame(context,viewport,1.0f/60.0f,AnimateFloatFrame,&reduced));
    CHECK(reduced.result > 0.8f && reduced.result < 1.0f);
    CHECK(Lumen::NeedsFrame(context));
    clock.Set(0.82);
    CHECK(RunFrame(context,viewport,1.0f/60.0f,AnimateFloatFrame,&reduced));
    CHECK(reduced.result == 1.0f);
    CHECK(!Lumen::NeedsFrame(context));
    Lumen::SetReduceMotion(context,false);

    Lumen::DestroyContext(context);
    ImGui::DestroyContext();
}

void TestContextMotionScheduler() {
    ImGui::CreateContext();
    Lumen::ManualClock clock(5.0);
    Lumen::Config config;
    config.frame_arena_bytes = 1024;
    config.state_capacity = 16;
    config.max_state_bytes = 64;
    config.clock = &clock;
    config.motion_capacity = 2;
    Lumen::Context* context = Lumen::CreateContext(config);
    CHECK(context != nullptr);
    if (!context) { ImGui::DestroyContext(); return; }

    ControllerCapture capture;
    Lumen::Controller controller(0.1);
    controller.SetCallbacks({ControllerStarted,ControllerUpdated,ControllerCompleted,ControllerCancelled,&capture});
    CHECK(Lumen::AttachController(context,&controller));
    CHECK(Lumen::AttachController(context,&controller));
    controller.Play();
    CHECK(capture.started == 1);
    CHECK(Lumen::NeedsFrame(context));
    const Lumen::Viewport viewport{Lumen::Rect{0.0f,0.0f,320.0f,240.0f},{},{1.0f,1.0f}};
    CHECK(RunFrame(context,viewport,1.0f/60.0f,nullptr));
    CHECK(Lumen::NeedsFrame(context));
    clock.Set(5.1);
    CHECK(RunFrame(context,viewport,1.0f/60.0f,nullptr));
    CHECK(capture.completed == 1);
    CHECK(!Lumen::NeedsFrame(context));
    CHECK(Lumen::DetachController(context,&controller));
    CHECK(!Lumen::DetachController(context,&controller));

    Lumen::DestroyContext(context);
    ImGui::DestroyContext();
}

} // namespace

int main() {
    TestMathAndIds();
    TestContextAndFrames();
    TestNonFiniteInitialClock();
    TestStateEvictionAndPinning();
    TestEmptyFramesAndFrameOwnership();
    TestArenaAndStateCapacityLimits();
    TestMotionCurvesTweensAndKeyframes();
    TestMotionSpringsAndFrameRateIndependence();
    TestTimelineAndController();
    TestImplicitAnimateRetargetAndNeedsFrame();
    TestContextMotionScheduler();

    if (failures != 0) {
        std::fprintf(stderr, "%d test assertion(s) failed.\n", failures);
        return EXIT_FAILURE;
    }

    std::puts("All Lumen M0/M1 tests passed.");
    return EXIT_SUCCESS;
}
