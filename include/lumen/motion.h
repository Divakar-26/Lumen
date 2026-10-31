#ifndef LUMEN_MOTION_H
#define LUMEN_MOTION_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "lumen/lumen.h"

namespace Lumen {

struct Context;

using CustomCurveFn = float (*)(float progress) noexcept;

enum class CurveFamily : std::uint8_t {
    Linear, Sine, Quad, Cubic, Quart, Quint, Expo, Circ, Back, Elastic, Bounce, Bezier, Steps, Custom
};
enum class CurveMode : std::uint8_t { In, Out, InOut };

struct Curve {
    CurveFamily family = CurveFamily::Bezier;
    CurveMode mode = CurveMode::Out;
    float x1 = 0.25f;
    float y1 = 0.1f;
    float x2 = 0.25f;
    float y2 = 1.0f;
    std::uint16_t step_count = 1;
    CustomCurveFn custom = nullptr;

    static Curve Linear() noexcept;
    static Curve SineIn() noexcept;
    static Curve SineOut() noexcept;
    static Curve SineInOut() noexcept;
    static Curve QuadIn() noexcept;
    static Curve QuadOut() noexcept;
    static Curve QuadInOut() noexcept;
    static Curve CubicIn() noexcept;
    static Curve CubicOut() noexcept;
    static Curve CubicInOut() noexcept;
    static Curve QuartIn() noexcept;
    static Curve QuartOut() noexcept;
    static Curve QuartInOut() noexcept;
    static Curve QuintIn() noexcept;
    static Curve QuintOut() noexcept;
    static Curve QuintInOut() noexcept;
    static Curve ExpoIn() noexcept;
    static Curve ExpoOut() noexcept;
    static Curve ExpoInOut() noexcept;
    static Curve CircIn() noexcept;
    static Curve CircOut() noexcept;
    static Curve CircInOut() noexcept;
    static Curve BackIn() noexcept;
    static Curve BackOut() noexcept;
    static Curve BackInOut() noexcept;
    static Curve ElasticIn() noexcept;
    static Curve ElasticOut() noexcept;
    static Curve ElasticInOut() noexcept;
    static Curve BounceIn() noexcept;
    static Curve BounceOut() noexcept;
    static Curve BounceInOut() noexcept;
    static Curve Standard() noexcept;
    static Curve EaseIn() noexcept;
    static Curve EaseOut() noexcept;
    static Curve Emphasized() noexcept;
    static Curve Decelerate() noexcept;
    static Curve Accelerate() noexcept;
    static Curve CubicBezier(float x1, float y1, float x2, float y2) noexcept;
    static Curve Steps(std::uint16_t count) noexcept;
    static Curve Custom(CustomCurveFn function) noexcept;
    float Evaluate(float progress) const noexcept;
};

Color LerpColorOKLab(Color from, Color to, float progress) noexcept;

// Specialize MotionOps and/or LerpTraits for application-defined value types.
template <typename T, typename Enable = void>
struct MotionOps {
    static T Add(const T& lhs, const T& rhs) noexcept(noexcept(lhs + rhs)) { return lhs + rhs; }
    static T Subtract(const T& lhs, const T& rhs) noexcept(noexcept(lhs - rhs)) { return lhs - rhs; }
    static T Scale(const T& value, float scalar) noexcept(noexcept(value * scalar)) { return value * scalar; }
    static float Magnitude(const T& value) noexcept { return static_cast<float>(value < T{} ? -value : value); }
    static bool Near(const T& lhs, const T& rhs, float epsilon) noexcept {
        return Magnitude(Subtract(lhs, rhs)) <= epsilon;
    }
};

template <> struct MotionOps<Vec2> {
    static Vec2 Add(Vec2 a, Vec2 b) noexcept { return a + b; }
    static Vec2 Subtract(Vec2 a, Vec2 b) noexcept { return a - b; }
    static Vec2 Scale(Vec2 a, float s) noexcept { return a * s; }
    static float Magnitude(Vec2 a) noexcept;
    static bool Near(Vec2 a, Vec2 b, float epsilon) noexcept;
};
template <> struct MotionOps<Vec3> {
    static Vec3 Add(Vec3 a, Vec3 b) noexcept { return a + b; }
    static Vec3 Subtract(Vec3 a, Vec3 b) noexcept { return a - b; }
    static Vec3 Scale(Vec3 a, float s) noexcept { return a * s; }
    static float Magnitude(Vec3 a) noexcept;
    static bool Near(Vec3 a, Vec3 b, float epsilon) noexcept;
};
template <> struct MotionOps<Vec4> {
    static Vec4 Add(Vec4 a, Vec4 b) noexcept { return a + b; }
    static Vec4 Subtract(Vec4 a, Vec4 b) noexcept { return a - b; }
    static Vec4 Scale(Vec4 a, float s) noexcept { return a * s; }
    static float Magnitude(Vec4 a) noexcept;
    static bool Near(Vec4 a, Vec4 b, float epsilon) noexcept;
};
template <> struct MotionOps<Color> {
    static Color Add(Color a, Color b) noexcept { return {a.r+b.r,a.g+b.g,a.b+b.b,a.a+b.a}; }
    static Color Subtract(Color a, Color b) noexcept { return {a.r-b.r,a.g-b.g,a.b-b.b,a.a-b.a}; }
    static Color Scale(Color a, float s) noexcept { return {a.r*s,a.g*s,a.b*s,a.a*s}; }
    static float Magnitude(Color a) noexcept;
    static bool Near(Color a, Color b, float epsilon) noexcept;
};
template <> struct MotionOps<Insets> {
    static Insets Add(Insets a, Insets b) noexcept { return {a.top+b.top,a.right+b.right,a.bottom+b.bottom,a.left+b.left}; }
    static Insets Subtract(Insets a, Insets b) noexcept { return {a.top-b.top,a.right-b.right,a.bottom-b.bottom,a.left-b.left}; }
    static Insets Scale(Insets a, float s) noexcept { return {a.top*s,a.right*s,a.bottom*s,a.left*s}; }
    static float Magnitude(Insets a) noexcept;
    static bool Near(Insets a, Insets b, float epsilon) noexcept;
};
template <> struct MotionOps<Rect> {
    static Rect Add(Rect a, Rect b) noexcept { return {MotionOps<Vec2>::Add(a.min,b.min),MotionOps<Vec2>::Add(a.max,b.max)}; }
    static Rect Subtract(Rect a, Rect b) noexcept { return {MotionOps<Vec2>::Subtract(a.min,b.min),MotionOps<Vec2>::Subtract(a.max,b.max)}; }
    static Rect Scale(Rect a, float s) noexcept { return {MotionOps<Vec2>::Scale(a.min,s),MotionOps<Vec2>::Scale(a.max,s)}; }
    static float Magnitude(Rect a) noexcept;
    static bool Near(Rect a, Rect b, float epsilon) noexcept;
};
template <> struct MotionOps<Transform> {
    static Transform Add(Transform a, Transform b) noexcept;
    static Transform Subtract(Transform a, Transform b) noexcept;
    static Transform Scale(Transform a, float s) noexcept;
    static float Magnitude(Transform a) noexcept;
    static bool Near(Transform a, Transform b, float epsilon) noexcept;
};

template <typename T, typename Enable = void>
struct LerpTraits {
    static T Lerp(const T& from, const T& to, float t) noexcept {
        return MotionOps<T>::Add(from, MotionOps<T>::Scale(MotionOps<T>::Subtract(to, from), t));
    }
};
template <> struct LerpTraits<float> {
    static float Lerp(float from, float to, float t) noexcept { return from + (to-from)*t; }
};
template <> struct LerpTraits<double> {
    static double Lerp(double from, double to, float t) noexcept { return from + (to-from)*static_cast<double>(t); }
};
template <> struct LerpTraits<Vec2> {
    static Vec2 Lerp(Vec2 from, Vec2 to, float t) noexcept { return Lumen::Lerp(from,to,t); }
};
template <> struct LerpTraits<Vec3> {
    static Vec3 Lerp(Vec3 from, Vec3 to, float t) noexcept { return Lumen::Lerp(from,to,t); }
};
template <> struct LerpTraits<Vec4> {
    static Vec4 Lerp(Vec4 from, Vec4 to, float t) noexcept { return Lumen::Lerp(from,to,t); }
};
template <> struct LerpTraits<Color> {
    static Color Lerp(Color from, Color to, float t) noexcept { return LerpColorOKLab(from,to,t); }
};
template <> struct LerpTraits<Insets> {
    static Insets Lerp(Insets a, Insets b, float t) noexcept { return {Lumen::Lerp(a.top,b.top,t),Lumen::Lerp(a.right,b.right,t),Lumen::Lerp(a.bottom,b.bottom,t),Lumen::Lerp(a.left,b.left,t)}; }
};
template <> struct LerpTraits<Rect> {
    static Rect Lerp(Rect a, Rect b, float t) noexcept { return {Lumen::Lerp(a.min,b.min,t),Lumen::Lerp(a.max,b.max,t)}; }
};
template <> struct LerpTraits<Transform> {
    static Transform Lerp(Transform a, Transform b, float t) noexcept;
};

template <typename T>
struct Value {
    T current{};
    T target{};
    T velocity{};
};

struct TweenSpec {
    double duration_seconds = 0.25;
    double delay_seconds = 0.0;
    Curve curve = Curve::Standard();
};

struct SpringSpec {
    float mass = 1.0f;
    float stiffness = 325.0f;
    float damping = 36.0f;
    float position_epsilon = 0.001f;
    float velocity_epsilon = 0.001f;

    static SpringSpec FromDurationBounce(float duration_seconds, float bounce) noexcept;
    static SpringSpec FromResponse(float response_seconds, float damping_ratio, float mass = 1.0f) noexcept;
    static SpringSpec Smooth() noexcept;
    static SpringSpec Snappy() noexcept;
    static SpringSpec Bouncy() noexcept;
    static SpringSpec Interactive() noexcept;
    static SpringSpec Gentle() noexcept;
};

template <typename T>
class Tween {
public:
    Tween() noexcept = default;
    Tween(T from, T to, double start_time_seconds, TweenSpec spec = {}) noexcept {
        Start(from, to, start_time_seconds, spec);
    }
    void Start(T from, T to, double start_time_seconds, TweenSpec spec = {}) noexcept {
        from_ = from; to_ = to; start_time_ = start_time_seconds; spec_ = spec; started_ = true;
        if (spec_.duration_seconds < 0.0) spec_.duration_seconds = 0.0;
        if (spec_.delay_seconds < 0.0) spec_.delay_seconds = 0.0;
    }
    T Sample(double now_seconds) const noexcept {
        if (!started_) return T{};
        const double local = now_seconds - start_time_ - spec_.delay_seconds;
        if (local <= 0.0) return from_;
        if (spec_.duration_seconds <= 0.0 || local >= spec_.duration_seconds) return to_;
        const float t = spec_.curve.Evaluate(static_cast<float>(local / spec_.duration_seconds));
        return LerpTraits<T>::Lerp(from_, to_, t);
    }
    bool IsFinished(double now_seconds) const noexcept {
        return started_ && now_seconds >= start_time_ + spec_.delay_seconds + spec_.duration_seconds;
    }
    double DurationSeconds() const noexcept { return spec_.duration_seconds; }
    double DelaySeconds() const noexcept { return spec_.delay_seconds; }
private:
    T from_{};
    T to_{};
    double start_time_ = 0.0;
    TweenSpec spec_{};
    bool started_ = false;
};

template <typename T>
class Spring {
public:
    explicit Spring(T initial = T{}, SpringSpec spec = SpringSpec::Smooth()) noexcept
        : value_{initial, initial, T{}}, spec_(Normalize(spec)) {}
    Spring(T initial, T target, SpringSpec spec) noexcept
        : value_{initial, target, T{}}, spec_(Normalize(spec)) {}

    const Value<T>& GetValue() const noexcept { return value_; }
    T Current() const noexcept { return value_.current; }
    T Target() const noexcept { return value_.target; }
    T Velocity() const noexcept { return value_.velocity; }
    void SetCurrent(T current, T velocity = T{}) noexcept { value_.current = current; value_.velocity = velocity; }
    void SetTarget(T target) noexcept { value_.target = target; }
    void SetSpec(SpringSpec spec) noexcept { spec_ = Normalize(spec); }
    const SpringSpec& Spec() const noexcept { return spec_; }
    bool IsAtRest() const noexcept {
        return MotionOps<T>::Near(value_.current, value_.target, spec_.position_epsilon) &&
               MotionOps<T>::Magnitude(value_.velocity) <= spec_.velocity_epsilon;
    }
    bool Advance(double delta_seconds) noexcept {
        if (delta_seconds <= 0.0 || !std::isfinite(delta_seconds)) return !IsAtRest();
        const double dt = delta_seconds;
        const double mass = spec_.mass;
        const double omega = std::sqrt(static_cast<double>(spec_.stiffness) / mass);
        const double zeta = static_cast<double>(spec_.damping) /
            (2.0 * std::sqrt(static_cast<double>(spec_.stiffness) * mass));
        const T displacement = MotionOps<T>::Subtract(value_.current, value_.target);
        const T initial_velocity = value_.velocity;
        T new_displacement{};
        T new_velocity{};
        if (zeta < 1.0 - 1.0e-4) {
            const double wd = omega * std::sqrt(1.0 - zeta*zeta);
            const double a = 1.0;
            (void)a;
            const T b = MotionOps<T>::Scale(
                MotionOps<T>::Add(initial_velocity, MotionOps<T>::Scale(displacement, static_cast<float>(zeta*omega))),
                static_cast<float>(1.0 / wd));
            const double cosine = std::cos(wd*dt);
            const double sine = std::sin(wd*dt);
            const T oscillation = MotionOps<T>::Add(MotionOps<T>::Scale(displacement, static_cast<float>(cosine)),
                                                    MotionOps<T>::Scale(b, static_cast<float>(sine)));
            const double envelope = std::exp(-zeta*omega*dt);
            new_displacement = MotionOps<T>::Scale(oscillation, static_cast<float>(envelope));
            const T derivative = MotionOps<T>::Add(
                MotionOps<T>::Scale(displacement, static_cast<float>(-wd*sine)),
                MotionOps<T>::Scale(b, static_cast<float>(wd*cosine)));
            new_velocity = MotionOps<T>::Scale(
                MotionOps<T>::Subtract(derivative, MotionOps<T>::Scale(oscillation, static_cast<float>(zeta*omega))),
                static_cast<float>(envelope));
        } else if (zeta <= 1.0 + 1.0e-4) {
            const T a = MotionOps<T>::Add(initial_velocity, MotionOps<T>::Scale(displacement, static_cast<float>(omega)));
            const T polynomial = MotionOps<T>::Add(displacement, MotionOps<T>::Scale(a, static_cast<float>(dt)));
            const double envelope = std::exp(-omega*dt);
            new_displacement = MotionOps<T>::Scale(polynomial, static_cast<float>(envelope));
            new_velocity = MotionOps<T>::Scale(
                MotionOps<T>::Subtract(a, MotionOps<T>::Scale(polynomial, static_cast<float>(omega))),
                static_cast<float>(envelope));
        } else {
            const double root = std::sqrt(zeta*zeta-1.0);
            const double r1 = -omega*(zeta-root);
            const double r2 = -omega*(zeta+root);
            const double denom = r1-r2;
            const T c1 = MotionOps<T>::Scale(
                MotionOps<T>::Subtract(initial_velocity, MotionOps<T>::Scale(displacement, static_cast<float>(r2))),
                static_cast<float>(1.0/denom));
            const T c2 = MotionOps<T>::Subtract(displacement, c1);
            const double e1 = std::exp(r1*dt);
            const double e2 = std::exp(r2*dt);
            new_displacement = MotionOps<T>::Add(MotionOps<T>::Scale(c1, static_cast<float>(e1)),
                                                 MotionOps<T>::Scale(c2, static_cast<float>(e2)));
            new_velocity = MotionOps<T>::Add(MotionOps<T>::Scale(c1, static_cast<float>(r1*e1)),
                                             MotionOps<T>::Scale(c2, static_cast<float>(r2*e2)));
        }
        value_.current = MotionOps<T>::Add(value_.target, new_displacement);
        value_.velocity = new_velocity;
        if (IsAtRest()) {
            value_.current = value_.target;
            value_.velocity = T{};
            return false;
        }
        return true;
    }
private:
    static SpringSpec Normalize(SpringSpec spec) noexcept {
        if (!std::isfinite(spec.mass) || spec.mass <= 0.0f) spec.mass = 1.0f;
        if (!std::isfinite(spec.stiffness) || spec.stiffness <= 0.0f) spec.stiffness = 1.0f;
        if (!std::isfinite(spec.damping) || spec.damping < 0.0f) spec.damping = 0.0f;
        if (!std::isfinite(spec.position_epsilon) || spec.position_epsilon < 0.0f) spec.position_epsilon = 0.001f;
        if (!std::isfinite(spec.velocity_epsilon) || spec.velocity_epsilon < 0.0f) spec.velocity_epsilon = 0.001f;
        return spec;
    }
    Value<T> value_{};
    SpringSpec spec_{};
};

template <typename T, std::size_t Capacity = 32>
class Keyframes {
public:
    struct Frame { double time_seconds = 0.0; T value{}; Curve curve_to_next = Curve::Standard(); };
    bool Add(double time_seconds, T value, Curve curve_to_next = Curve::Standard()) noexcept {
        if (count_ >= Capacity || !std::isfinite(time_seconds) || time_seconds < 0.0 ||
            (count_ > 0 && time_seconds < frames_[count_-1].time_seconds)) return false;
        frames_[count_++] = Frame{time_seconds, value, curve_to_next};
        return true;
    }
    T Sample(double time_seconds) const noexcept {
        if (count_ == 0) return T{};
        if (time_seconds <= frames_[0].time_seconds) return frames_[0].value;
        for (std::size_t i = 0; i + 1 < count_; ++i) {
            const Frame& from = frames_[i];
            const Frame& to = frames_[i+1];
            if (time_seconds <= to.time_seconds) {
                const double duration = to.time_seconds - from.time_seconds;
                const float t = duration <= 0.0 ? 1.0f : static_cast<float>((time_seconds-from.time_seconds)/duration);
                return LerpTraits<T>::Lerp(from.value, to.value, from.curve_to_next.Evaluate(t));
            }
        }
        return frames_[count_-1].value;
    }
    std::size_t Size() const noexcept { return count_; }
    bool Empty() const noexcept { return count_ == 0; }
    double DurationSeconds() const noexcept { return count_ == 0 ? 0.0 : frames_[count_-1].time_seconds; }
private:
    std::array<Frame, Capacity> frames_{};
    std::size_t count_ = 0;
};

using TimelineUpdate = void (*)(void* user, float progress);
using StaggerUpdate = void (*)(void* user, std::size_t index, float progress);

class Timeline {
public:
    static constexpr std::size_t MaxTracks = 128;
    bool Sequence(TimelineUpdate callback, void* user, double duration_seconds, Curve curve = Curve::Standard()) noexcept;
    bool Parallel(TimelineUpdate callback, void* user, double duration_seconds, Curve curve = Curve::Standard()) noexcept;
    bool BeginParallel() noexcept;
    bool EndParallel() noexcept;
    bool Stagger(StaggerUpdate callback, void* user, std::size_t count, double duration_seconds,
                 double delay_per_item, Curve curve = Curve::Standard()) noexcept;
    void Sample(double time_seconds) const noexcept;
    double DurationSeconds() const noexcept { return duration_seconds_; }
    std::size_t TrackCount() const noexcept { return count_; }
    void Clear() noexcept;
private:
    struct Track {
        TimelineUpdate callback = nullptr;
        StaggerUpdate stagger_callback = nullptr;
        void* user = nullptr;
        double start = 0.0;
        double duration = 0.0;
        Curve curve = Curve::Standard();
        std::size_t stagger_index = 0;
        bool is_stagger = false;
    };
    bool Add(Track track) noexcept;
    std::array<Track, MaxTracks> tracks_{};
    std::size_t count_ = 0;
    double sequence_cursor_ = 0.0;
    double parallel_start_ = 0.0;
    double parallel_end_ = 0.0;
    double duration_seconds_ = 0.0;
    bool in_parallel_ = false;
};

struct ControllerCallbacks {
    void (*started)(void* user) = nullptr;
    void (*updated)(void* user, float progress) = nullptr;
    void (*completed)(void* user) = nullptr;
    void (*cancelled)(void* user) = nullptr;
    void* user = nullptr;
};

class Controller {
public:
    explicit Controller(double duration_seconds = 0.25) noexcept;
    Controller(const Controller&) = delete;
    Controller& operator=(const Controller&) = delete;
    void Bind(Timeline* timeline) noexcept;
    void SetCallbacks(ControllerCallbacks callbacks) noexcept { callbacks_ = callbacks; }
    void Play() noexcept;
    void Play(double now_seconds) noexcept;
    void Reverse() noexcept;
    void Pause() noexcept;
    void Resume() noexcept;
    void Resume(double now_seconds) noexcept;
    void Cancel() noexcept;
    void Seek(double time_seconds) noexcept;
    void Repeat(std::size_t repeat_count) noexcept { repeat_count_ = repeat_count; }
    void SetPingPong(bool enabled) noexcept { ping_pong_ = enabled; }
    void SetSpeed(float speed) noexcept;
    void SetDelay(double delay_seconds) noexcept;
    bool Update(double now_seconds, bool reduce_motion = false) noexcept;
    bool IsPlaying() const noexcept { return playing_; }
    bool IsPaused() const noexcept { return paused_; }
    bool NeedsFrame() const noexcept { return playing_ && !paused_ && speed_ > 0.0f; }
    float Progress() const noexcept;
    double CurrentTimeSeconds() const noexcept { return elapsed_seconds_; }
    double DurationSeconds() const noexcept { return duration_seconds_; }
private:
    friend class MotionScheduler;
    friend bool AttachController(Context* context, Controller* controller) noexcept;
    friend bool DetachController(Context* context, Controller* controller) noexcept;
    void NotifyUpdate() noexcept;
    Context* owner_context_ = nullptr;
    double duration_seconds_ = 0.25;
    double elapsed_seconds_ = 0.0;
    double delay_seconds_ = 0.0;
    double delay_remaining_ = 0.0;
    double last_time_seconds_ = 0.0;
    float speed_ = 1.0f;
    std::size_t repeat_count_ = 0;
    std::size_t completed_repeats_ = 0;
    bool playing_ = false;
    bool paused_ = false;
    bool reversed_ = false;
    bool ping_pong_ = false;
    Timeline* timeline_ = nullptr;
    ControllerCallbacks callbacks_{};
};

class MotionScheduler {
public:
    MotionScheduler() noexcept = default;
    ~MotionScheduler();
    MotionScheduler(const MotionScheduler&) = delete;
    MotionScheduler& operator=(const MotionScheduler&) = delete;
    bool Initialize(std::size_t capacity) noexcept;
    void Shutdown() noexcept;
    bool Attach(Controller* controller) noexcept;
    bool Detach(Controller* controller) noexcept;
    bool Advance(double now_seconds, bool reduce_motion = false) noexcept;
    std::size_t ActiveCount() const noexcept;
    std::size_t Capacity() const noexcept { return capacity_; }
private:
    Controller** controllers_ = nullptr;
    std::size_t capacity_ = 0;
    std::size_t count_ = 0;
};


template <typename T>
struct ImplicitTweenState {
    T start{};
    T target{};
    T current{};
    TweenSpec spec{};
    double start_time = 0.0;
    bool initialized = false;
    bool active = false;
};

template <typename T>
struct ImplicitSpringState {
    Spring<T> spring{};
    double last_time = 0.0;
    bool initialized = false;
};

template <typename T>
T Animate(Context* context, std::uint64_t id, const T& target, const TweenSpec& requested_spec) noexcept {
    static_assert(std::is_trivially_copyable<T>::value, "Lumen Animate values must be trivially copyable");
    static_assert(std::is_trivially_copyable<ImplicitTweenState<T>>::value,
                  "Lumen Animate tween state must be trivially copyable");
    const FrameInfo* frame = GetFrameInfo(context);
    if (context == nullptr || frame == nullptr) return target;
    ImplicitTweenState<T>* state = State<ImplicitTweenState<T>>(context, id);
    if (state == nullptr) return target;

    const double now = frame->time_seconds;
    TweenSpec spec = requested_spec;
    if (IsReduceMotionEnabled(context)) {
        spec.duration_seconds = spec.duration_seconds > 0.0 ? (spec.duration_seconds < 0.12 ? spec.duration_seconds : 0.12) : 0.0;
        spec.delay_seconds = 0.0;
        spec.curve = Curve::EaseOut();
    }
    if (!state->initialized) {
        state->start = target;
        state->target = target;
        state->current = target;
        state->spec = spec;
        state->start_time = now;
        state->initialized = true;
        state->active = false;
        return state->current;
    }

    if (state->active) {
        const double elapsed = now - state->start_time - state->spec.delay_seconds;
        if (elapsed <= 0.0) {
            state->current = state->start;
        } else if (state->spec.duration_seconds <= 0.0 || elapsed + 1.0e-9 >= state->spec.duration_seconds) {
            state->current = state->target;
            state->active = false;
        } else {
            const float progress = state->spec.curve.Evaluate(static_cast<float>(elapsed / state->spec.duration_seconds));
            state->current = LerpTraits<T>::Lerp(state->start, state->target, progress);
        }
    } else {
        state->current = state->target;
    }

    if (!MotionOps<T>::Near(target, state->target, 1.0e-6f)) {
        state->start = state->current;
        state->target = target;
        state->spec = spec;
        state->start_time = now;
        if (state->spec.duration_seconds <= 0.0) {
            state->current = target;
            state->active = false;
        } else {
            state->active = true;
        }
    }
    if (state->active) detail::MarkImplicitMotionActive(context);
    return state->current;
}

template <typename T>
T Animate(Context* context, std::uint64_t id, const T& target, const SpringSpec& requested_spec) noexcept {
    static_assert(std::is_trivially_copyable<T>::value, "Lumen Animate values must be trivially copyable");
    static_assert(std::is_trivially_copyable<ImplicitSpringState<T>>::value,
                  "Lumen Animate spring state must be trivially copyable");
    const FrameInfo* frame = GetFrameInfo(context);
    if (context == nullptr || frame == nullptr) return target;
    ImplicitSpringState<T>* state = State<ImplicitSpringState<T>>(context, id);
    if (state == nullptr) return target;

    const double now = frame->time_seconds;
    SpringSpec spec = requested_spec;
    if (IsReduceMotionEnabled(context)) spec = SpringSpec::FromDurationBounce(0.12f, 0.0f);
    if (!state->initialized) {
        state->spring = Spring<T>(target, spec);
        state->last_time = now;
        state->initialized = true;
        return target;
    }
    const bool target_changed = !MotionOps<T>::Near(target, state->spring.Target(), 1.0e-6f);
    double delta = now - state->last_time;
    if (delta < 0.0 || !std::isfinite(delta)) delta = 0.0;
    state->last_time = now;
    bool active = false;
    if (target_changed) {
        state->spring.Advance(delta);
        state->spring.SetSpec(spec);
        state->spring.SetTarget(target);
        active = !state->spring.IsAtRest();
    } else {
        state->spring.SetSpec(spec);
        active = state->spring.Advance(delta);
    }
    if (active) detail::MarkImplicitMotionActive(context);
    return state->spring.Current();
}

template <typename T>
T Animate(Context* context, std::uint64_t id, const T& target) noexcept {
    return Animate(context, id, target, SpringSpec::Smooth());
}

} // namespace Lumen

#endif
