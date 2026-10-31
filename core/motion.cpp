#include "context_internal.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace Lumen {
namespace {
constexpr double Pi = 3.1415926535897932384626433832795;
constexpr float BackS = 1.70158f;

Curve MakeCurve(CurveFamily family, CurveMode mode) noexcept {
    Curve curve;
    curve.family = family;
    curve.mode = mode;
    return curve;
}

float BounceOut(float t) noexcept {
    constexpr float n1 = 7.5625f;
    constexpr float d1 = 2.75f;
    if (t < 1.0f / d1) return n1 * t * t;
    if (t < 2.0f / d1) { t -= 1.5f / d1; return n1 * t * t + 0.75f; }
    if (t < 2.5f / d1) { t -= 2.25f / d1; return n1 * t * t + 0.9375f; }
    t -= 2.625f / d1;
    return n1 * t * t + 0.984375f;
}

float FamilyIn(CurveFamily family, float t) noexcept {
    switch (family) {
    case CurveFamily::Linear: return t;
    case CurveFamily::Sine: return 1.0f - std::cos(static_cast<float>(Pi * 0.5) * t);
    case CurveFamily::Quad: return t*t;
    case CurveFamily::Cubic: return t*t*t;
    case CurveFamily::Quart: return t*t*t*t;
    case CurveFamily::Quint: return t*t*t*t*t;
    case CurveFamily::Expo: return t <= 0.0f ? 0.0f : std::pow(2.0f, 10.0f*t - 10.0f);
    case CurveFamily::Circ: return 1.0f - std::sqrt(std::max(0.0f, 1.0f-t*t));
    case CurveFamily::Back: return (BackS+1.0f)*t*t*t - BackS*t*t;
    case CurveFamily::Elastic:
        if (t <= 0.0f || t >= 1.0f) return t;
        return -std::pow(2.0f, 10.0f*t-10.0f) * std::sin((t*10.0f-10.75f) * static_cast<float>(2.0*Pi/3.0));
    case CurveFamily::Bounce: return 1.0f - BounceOut(1.0f-t);
    case CurveFamily::Bezier: case CurveFamily::Steps: case CurveFamily::Custom: return t;
    }
    return t;
}

float FamilyOut(CurveFamily family, float t) noexcept {
    switch (family) {
    case CurveFamily::Linear: return t;
    case CurveFamily::Sine: return std::sin(static_cast<float>(Pi * 0.5) * t);
    case CurveFamily::Quad: return 1.0f - (1.0f-t)*(1.0f-t);
    case CurveFamily::Cubic: return 1.0f - std::pow(1.0f-t, 3.0f);
    case CurveFamily::Quart: return 1.0f - std::pow(1.0f-t, 4.0f);
    case CurveFamily::Quint: return 1.0f - std::pow(1.0f-t, 5.0f);
    case CurveFamily::Expo: return t >= 1.0f ? 1.0f : 1.0f - std::pow(2.0f, -10.0f*t);
    case CurveFamily::Circ: return std::sqrt(std::max(0.0f, 1.0f-(t-1.0f)*(t-1.0f)));
    case CurveFamily::Back: { const float u=t-1.0f; return 1.0f+(BackS+1.0f)*u*u*u+BackS*u*u; }
    case CurveFamily::Elastic:
        if (t <= 0.0f || t >= 1.0f) return t;
        return std::pow(2.0f, -10.0f*t) * std::sin((t*10.0f-0.75f) * static_cast<float>(2.0*Pi/3.0)) + 1.0f;
    case CurveFamily::Bounce: return BounceOut(t);
    case CurveFamily::Bezier: case CurveFamily::Steps: case CurveFamily::Custom: return t;
    }
    return t;
}

float BezierCoordinate(float t, float p1, float p2) noexcept {
    const float inv = 1.0f-t;
    return 3.0f*inv*inv*t*p1 + 3.0f*inv*t*t*p2 + t*t*t;
}

float EvaluateBezier(const Curve& curve, float x) noexcept {
    float low = 0.0f;
    float high = 1.0f;
    float t = x;
    for (int i = 0; i < 18; ++i) {
        t = (low+high)*0.5f;
        const float estimate = BezierCoordinate(t, curve.x1, curve.x2);
        if (estimate < x) low = t; else high = t;
    }
    return BezierCoordinate(t, curve.y1, curve.y2);
}

float ToLinear(float channel) noexcept {
    channel = std::max(0.0f, channel);
    return channel <= 0.04045f ? channel/12.92f : std::pow((channel+0.055f)/1.055f, 2.4f);
}
float ToSrgb(float channel) noexcept {
    channel = std::max(0.0f, channel);
    return channel <= 0.0031308f ? 12.92f*channel : 1.055f*std::pow(channel, 1.0f/2.4f)-0.055f;
}
struct Lab { float l; float a; float b; };
Lab ToOKLab(Color c) noexcept {
    const float r=ToLinear(c.r), g=ToLinear(c.g), b=ToLinear(c.b);
    const float l=std::cbrt(0.4122214708f*r+0.5363325363f*g+0.0514459929f*b);
    const float m=std::cbrt(0.2119034982f*r+0.6806995451f*g+0.1073969566f*b);
    const float s=std::cbrt(0.0883024619f*r+0.2817188376f*g+0.6299787005f*b);
    return {0.2104542553f*l+0.7936177850f*m-0.0040720468f*s,
            1.9779984951f*l-2.4285922050f*m+0.4505937099f*s,
            0.0259040371f*l+0.7827717662f*m-0.8086757660f*s};
}
Color FromOKLab(Lab lab, float alpha) noexcept {
    const float l=lab.l+0.3963377774f*lab.a+0.2158037573f*lab.b;
    const float m=lab.l-0.1055613458f*lab.a-0.0638541728f*lab.b;
    const float s=lab.l-0.0894841775f*lab.a-1.2914855480f*lab.b;
    const float l3=l*l*l, m3=m*m*m, s3=s*s*s;
    const float r= 4.0767416621f*l3-3.3077115913f*m3+0.2309699292f*s3;
    const float g=-1.2684380046f*l3+2.6097574011f*m3-0.3413193965f*s3;
    const float b=-0.0041960863f*l3-0.7034186147f*m3+1.7076147010f*s3;
    return {Clamp(ToSrgb(r),0.0f,1.0f), Clamp(ToSrgb(g),0.0f,1.0f), Clamp(ToSrgb(b),0.0f,1.0f), Clamp(alpha,0.0f,1.0f)};
}

float MaxAbs(float a, float b, float c, float d) noexcept {
    return std::max(std::max(std::fabs(a),std::fabs(b)),std::max(std::fabs(c),std::fabs(d)));
}

double CurrentMotionTime(const Context* context) noexcept {
    if (context == nullptr || context->clock == nullptr) return 0.0;
    const double raw_now = context->clock->NowSeconds();
    if (!context->clock_initialized) return raw_now;
    double elapsed = raw_now - context->last_clock_time_seconds;
    if (!std::isfinite(elapsed) || elapsed < 0.0) elapsed = 0.0;
    return context->motion_time_seconds + elapsed * static_cast<double>(context->motion_time_scale);
}
} // namespace

Curve Curve::Linear() noexcept { return MakeCurve(CurveFamily::Linear, CurveMode::In); }
Curve Curve::SineIn() noexcept { return MakeCurve(CurveFamily::Sine, CurveMode::In); }
Curve Curve::SineOut() noexcept { return MakeCurve(CurveFamily::Sine, CurveMode::Out); }
Curve Curve::SineInOut() noexcept { return MakeCurve(CurveFamily::Sine, CurveMode::InOut); }
Curve Curve::QuadIn() noexcept { return MakeCurve(CurveFamily::Quad, CurveMode::In); }
Curve Curve::QuadOut() noexcept { return MakeCurve(CurveFamily::Quad, CurveMode::Out); }
Curve Curve::QuadInOut() noexcept { return MakeCurve(CurveFamily::Quad, CurveMode::InOut); }
Curve Curve::CubicIn() noexcept { return MakeCurve(CurveFamily::Cubic, CurveMode::In); }
Curve Curve::CubicOut() noexcept { return MakeCurve(CurveFamily::Cubic, CurveMode::Out); }
Curve Curve::CubicInOut() noexcept { return MakeCurve(CurveFamily::Cubic, CurveMode::InOut); }
Curve Curve::QuartIn() noexcept { return MakeCurve(CurveFamily::Quart, CurveMode::In); }
Curve Curve::QuartOut() noexcept { return MakeCurve(CurveFamily::Quart, CurveMode::Out); }
Curve Curve::QuartInOut() noexcept { return MakeCurve(CurveFamily::Quart, CurveMode::InOut); }
Curve Curve::QuintIn() noexcept { return MakeCurve(CurveFamily::Quint, CurveMode::In); }
Curve Curve::QuintOut() noexcept { return MakeCurve(CurveFamily::Quint, CurveMode::Out); }
Curve Curve::QuintInOut() noexcept { return MakeCurve(CurveFamily::Quint, CurveMode::InOut); }
Curve Curve::ExpoIn() noexcept { return MakeCurve(CurveFamily::Expo, CurveMode::In); }
Curve Curve::ExpoOut() noexcept { return MakeCurve(CurveFamily::Expo, CurveMode::Out); }
Curve Curve::ExpoInOut() noexcept { return MakeCurve(CurveFamily::Expo, CurveMode::InOut); }
Curve Curve::CircIn() noexcept { return MakeCurve(CurveFamily::Circ, CurveMode::In); }
Curve Curve::CircOut() noexcept { return MakeCurve(CurveFamily::Circ, CurveMode::Out); }
Curve Curve::CircInOut() noexcept { return MakeCurve(CurveFamily::Circ, CurveMode::InOut); }
Curve Curve::BackIn() noexcept { return MakeCurve(CurveFamily::Back, CurveMode::In); }
Curve Curve::BackOut() noexcept { return MakeCurve(CurveFamily::Back, CurveMode::Out); }
Curve Curve::BackInOut() noexcept { return MakeCurve(CurveFamily::Back, CurveMode::InOut); }
Curve Curve::ElasticIn() noexcept { return MakeCurve(CurveFamily::Elastic, CurveMode::In); }
Curve Curve::ElasticOut() noexcept { return MakeCurve(CurveFamily::Elastic, CurveMode::Out); }
Curve Curve::ElasticInOut() noexcept { return MakeCurve(CurveFamily::Elastic, CurveMode::InOut); }
Curve Curve::BounceIn() noexcept { return MakeCurve(CurveFamily::Bounce, CurveMode::In); }
Curve Curve::BounceOut() noexcept { return MakeCurve(CurveFamily::Bounce, CurveMode::Out); }
Curve Curve::BounceInOut() noexcept { return MakeCurve(CurveFamily::Bounce, CurveMode::InOut); }
Curve Curve::Standard() noexcept { return CubicBezier(0.25f,0.1f,0.25f,1.0f); }
Curve Curve::EaseIn() noexcept { return CubicBezier(0.42f,0.0f,1.0f,1.0f); }
Curve Curve::EaseOut() noexcept { return CubicBezier(0.0f,0.0f,0.58f,1.0f); }
Curve Curve::Emphasized() noexcept { return CubicBezier(0.2f,0.0f,0.0f,1.0f); }
Curve Curve::Decelerate() noexcept { return CubicOut(); }
Curve Curve::Accelerate() noexcept { return CubicIn(); }
Curve Curve::CubicBezier(float a, float b, float c, float d) noexcept { Curve curve; curve.family=CurveFamily::Bezier; curve.x1=Clamp(a,0.0f,1.0f); curve.y1=b; curve.x2=Clamp(c,0.0f,1.0f); curve.y2=d; return curve; }
Curve Curve::Steps(std::uint16_t count) noexcept { Curve curve; curve.family=CurveFamily::Steps; curve.step_count=std::max<std::uint16_t>(1,count); return curve; }
Curve Curve::Custom(CustomCurveFn function) noexcept { Curve curve; curve.family=CurveFamily::Custom; curve.custom=function; return curve; }

float Curve::Evaluate(float progress) const noexcept {
    if (!std::isfinite(progress)) return 0.0f;
    const float t=Clamp(progress,0.0f,1.0f);
    if (family==CurveFamily::Bezier) return EvaluateBezier(*this,t);
    if (family==CurveFamily::Steps) {
        if (t<=0.0f || t>=1.0f) return t;
        return std::floor(t*std::max<std::uint16_t>(1,step_count))/std::max<std::uint16_t>(1,step_count);
    }
    if (family==CurveFamily::Custom) {
        const float value = custom ? custom(t) : t;
        return std::isfinite(value) ? value : t;
    }
    if (mode==CurveMode::In) return FamilyIn(family,t);
    if (mode==CurveMode::Out) return FamilyOut(family,t);
    if (t<0.5f) return 0.5f*FamilyIn(family,2.0f*t);
    return 0.5f+0.5f*FamilyOut(family,2.0f*t-1.0f);
}

Color LerpColorOKLab(Color from, Color to, float progress) noexcept {
    const Lab a=ToOKLab(from), b=ToOKLab(to);
    const Lab blend{Lumen::Lerp(a.l,b.l,progress),Lumen::Lerp(a.a,b.a,progress),Lumen::Lerp(a.b,b.b,progress)};
    return FromOKLab(blend,Lumen::Lerp(from.a,to.a,progress));
}

float MotionOps<Vec2>::Magnitude(Vec2 v) noexcept { return std::sqrt(v.x*v.x+v.y*v.y); }
bool MotionOps<Vec2>::Near(Vec2 a, Vec2 b, float e) noexcept { return Magnitude(a-b)<=e; }
float MotionOps<Vec3>::Magnitude(Vec3 v) noexcept { return std::sqrt(v.x*v.x+v.y*v.y+v.z*v.z); }
bool MotionOps<Vec3>::Near(Vec3 a, Vec3 b, float e) noexcept { return Magnitude(a-b)<=e; }
float MotionOps<Vec4>::Magnitude(Vec4 v) noexcept { return std::sqrt(v.x*v.x+v.y*v.y+v.z*v.z+v.w*v.w); }
bool MotionOps<Vec4>::Near(Vec4 a, Vec4 b, float e) noexcept { return Magnitude(a-b)<=e; }
float MotionOps<Color>::Magnitude(Color c) noexcept { return MaxAbs(c.r,c.g,c.b,c.a); }
bool MotionOps<Color>::Near(Color a, Color b, float e) noexcept { return Magnitude(Subtract(a,b))<=e; }
float MotionOps<Insets>::Magnitude(Insets a) noexcept { return MaxAbs(a.top,a.right,a.bottom,a.left); }
bool MotionOps<Insets>::Near(Insets a, Insets b, float e) noexcept { return Magnitude(Subtract(a,b))<=e; }
float MotionOps<Rect>::Magnitude(Rect a) noexcept { return std::max(MotionOps<Vec2>::Magnitude(a.min),MotionOps<Vec2>::Magnitude(a.max)); }
bool MotionOps<Rect>::Near(Rect a, Rect b, float e) noexcept { return Magnitude(Subtract(a,b))<=e; }
Transform MotionOps<Transform>::Add(Transform a, Transform b) noexcept { return {a.translation+b.translation,a.scale+b.scale,a.rotation+b.rotation,a.anchor+b.anchor}; }
Transform MotionOps<Transform>::Subtract(Transform a, Transform b) noexcept { return {a.translation-b.translation,a.scale-b.scale,a.rotation-b.rotation,a.anchor-b.anchor}; }
Transform MotionOps<Transform>::Scale(Transform a, float s) noexcept { return {a.translation*s,a.scale*s,a.rotation*s,a.anchor*s}; }
float MotionOps<Transform>::Magnitude(Transform a) noexcept { return std::max({MotionOps<Vec2>::Magnitude(a.translation),MotionOps<Vec2>::Magnitude(a.scale),std::fabs(a.rotation),MotionOps<Vec2>::Magnitude(a.anchor)}); }
bool MotionOps<Transform>::Near(Transform a, Transform b, float e) noexcept { return Magnitude(Subtract(a,b))<=e; }
Transform LerpTraits<Transform>::Lerp(Transform a, Transform b, float t) noexcept {
    float delta=std::fmod(b.rotation-a.rotation+static_cast<float>(Pi),static_cast<float>(2.0*Pi));
    if (delta<0.0f) delta+=static_cast<float>(2.0*Pi);
    delta-=static_cast<float>(Pi);
    return {Lumen::Lerp(a.translation,b.translation,t),Lumen::Lerp(a.scale,b.scale,t),a.rotation+delta*t,Lumen::Lerp(a.anchor,b.anchor,t)};
}

SpringSpec SpringSpec::FromDurationBounce(float duration, float bounce) noexcept {
    if (!std::isfinite(duration) || duration<=0.0f) duration=0.35f;
    bounce=Clamp(bounce,-1.0f,0.99f);
    const float omega=static_cast<float>(2.0*Pi)/duration;
    return {1.0f,omega*omega,static_cast<float>(4.0*Pi)*(1.0f-bounce)/duration,0.001f,0.001f};
}
SpringSpec SpringSpec::FromResponse(float response, float ratio, float mass_value) noexcept {
    if (!std::isfinite(response) || response<=0.0f) response=0.35f;
    if (!std::isfinite(mass_value) || mass_value<=0.0f) mass_value=1.0f;
    if (!std::isfinite(ratio) || ratio<0.0f) ratio=1.0f;
    const float omega=static_cast<float>(2.0*Pi)/response;
    const float k=mass_value*omega*omega;
    return {mass_value,k,2.0f*ratio*std::sqrt(mass_value*k),0.001f,0.001f};
}
SpringSpec SpringSpec::Smooth() noexcept { return FromDurationBounce(0.42f,0.0f); }
SpringSpec SpringSpec::Snappy() noexcept { return FromDurationBounce(0.28f,0.15f); }
SpringSpec SpringSpec::Bouncy() noexcept { return FromDurationBounce(0.45f,0.3f); }
SpringSpec SpringSpec::Interactive() noexcept { return FromResponse(0.38f,0.72f); }
SpringSpec SpringSpec::Gentle() noexcept { return FromResponse(0.55f,1.0f); }

bool Timeline::Add(Track track) noexcept {
    if (count_>=tracks_.size() || !std::isfinite(track.start) || !std::isfinite(track.duration) || track.duration<0.0) return false;
    tracks_[count_++]=track;
    duration_seconds_=std::max(duration_seconds_,track.start+track.duration);
    return true;
}
bool Timeline::Sequence(TimelineUpdate cb, void* user, double duration, Curve curve) noexcept {
    if (cb==nullptr || duration<0.0 || !std::isfinite(duration) || in_parallel_) return false;
    const double start=sequence_cursor_;
    if (!Add(Track{cb,nullptr,user,start,duration,curve,0,false})) return false;
    sequence_cursor_=start+duration;
    return true;
}
bool Timeline::Parallel(TimelineUpdate cb, void* user, double duration, Curve curve) noexcept {
    if (cb==nullptr || duration<0.0 || !std::isfinite(duration)) return false;
    const double start=in_parallel_ ? parallel_start_ : sequence_cursor_;
    if (!Add(Track{cb,nullptr,user,start,duration,curve,0,false})) return false;
    if (in_parallel_) parallel_end_=std::max(parallel_end_,start+duration);
    else sequence_cursor_=start+duration;
    return true;
}
bool Timeline::BeginParallel() noexcept {
    if (in_parallel_) return false;
    in_parallel_=true;
    parallel_start_=sequence_cursor_;
    parallel_end_=sequence_cursor_;
    return true;
}
bool Timeline::EndParallel() noexcept {
    if (!in_parallel_) return false;
    in_parallel_=false;
    sequence_cursor_=parallel_end_;
    duration_seconds_=std::max(duration_seconds_,sequence_cursor_);
    return true;
}
bool Timeline::Stagger(StaggerUpdate cb, void* user, std::size_t count, double duration, double delay, Curve curve) noexcept {
    if (cb==nullptr || duration<0.0 || delay<0.0 || !std::isfinite(duration) || !std::isfinite(delay) || in_parallel_) return false;
    if (count>tracks_.size()-count_) return false;
    for (std::size_t i=0;i<count;++i) {
        const double start=sequence_cursor_+static_cast<double>(i)*delay;
        if (!Add(Track{nullptr,cb,user,start,duration,curve,i,true})) return false;
    }
    if (count>0) sequence_cursor_ += static_cast<double>(count-1)*delay+duration;
    duration_seconds_=std::max(duration_seconds_,sequence_cursor_);
    return true;
}
void Timeline::Sample(double time) const noexcept {
    for (std::size_t i=0;i<count_;++i) {
        const Track& t=tracks_[i];
        const double local=time-t.start;
        if (local<0.0) continue;
        const float p=t.duration<=0.0 ? 1.0f : Clamp(static_cast<float>(local/t.duration),0.0f,1.0f);
        const float eased=t.curve.Evaluate(p);
        if (t.is_stagger) { if (t.stagger_callback) t.stagger_callback(t.user,t.stagger_index,eased); }
        else if (t.callback) t.callback(t.user,eased);
    }
}
void Timeline::Clear() noexcept {
    count_=0; sequence_cursor_=0.0; parallel_start_=0.0; parallel_end_=0.0; duration_seconds_=0.0; in_parallel_=false;
}

Controller::Controller(double duration) noexcept : duration_seconds_(std::isfinite(duration) && duration>0.0 ? duration : 0.0) {}
void Controller::Bind(Timeline* timeline) noexcept { timeline_=timeline; if (timeline_) duration_seconds_=timeline_->DurationSeconds(); }
void Controller::Play() noexcept {
    Play(owner_context_ ? CurrentMotionTime(owner_context_) : 0.0);
}
void Controller::Play(double now) noexcept {
    playing_=true; paused_=false; completed_repeats_=0; delay_remaining_=delay_seconds_;
    elapsed_seconds_=reversed_ ? duration_seconds_ : 0.0; last_time_seconds_=now;
    if (callbacks_.started) callbacks_.started(callbacks_.user);
    NotifyUpdate();
    if (owner_context_) RequestRedraw(owner_context_);
}
void Controller::Reverse() noexcept { reversed_=!reversed_; if (owner_context_ && playing_) RequestRedraw(owner_context_); }
void Controller::Pause() noexcept { if (playing_) { paused_=true; if (owner_context_) RequestRedraw(owner_context_); } }
void Controller::Resume() noexcept { Resume(owner_context_ ? CurrentMotionTime(owner_context_) : 0.0); }
void Controller::Resume(double now) noexcept { if (playing_ && paused_) { paused_=false; last_time_seconds_=now; if (owner_context_) RequestRedraw(owner_context_); } }
void Controller::Cancel() noexcept {
    const bool was_running=playing_;
    playing_=false; paused_=false;
    if (was_running && callbacks_.cancelled) callbacks_.cancelled(callbacks_.user);
    if (was_running && owner_context_) RequestRedraw(owner_context_);
}
void Controller::Seek(double time) noexcept {
    elapsed_seconds_=Clamp(static_cast<float>(time),0.0f,static_cast<float>(duration_seconds_));
    NotifyUpdate();
    if (owner_context_ && playing_) RequestRedraw(owner_context_);
}
void Controller::SetSpeed(float speed) noexcept {
    speed_=std::isfinite(speed) ? std::max(0.0f,speed) : 1.0f;
    if (owner_context_) {
        last_time_seconds_=CurrentMotionTime(owner_context_);
        if (playing_ && !paused_ && speed_>0.0f) RequestRedraw(owner_context_);
    }
}
void Controller::SetDelay(double delay) noexcept { delay_seconds_=std::isfinite(delay) ? std::max(0.0,delay) : 0.0; }
float Controller::Progress() const noexcept { return duration_seconds_<=0.0 ? 1.0f : Clamp(static_cast<float>(elapsed_seconds_/duration_seconds_),0.0f,1.0f); }
void Controller::NotifyUpdate() noexcept {
    const float p=Progress();
    if (timeline_) timeline_->Sample(static_cast<double>(p)*timeline_->DurationSeconds());
    if (callbacks_.updated) callbacks_.updated(callbacks_.user,p);
}
bool Controller::Update(double now, bool reduce_motion) noexcept {
    if (!playing_ || paused_) return false;
    if (speed_ <= 0.0f) { last_time_seconds_=now; return false; }
    double delta=now-last_time_seconds_;
    last_time_seconds_=now;
    if (!std::isfinite(delta) || delta<0.0) delta=0.0;
    delta=std::min(delta,0.1);
    if (delay_remaining_>0.0) {
        if (reduce_motion) delay_remaining_=0.0;
        else {
            const double used=std::min(delta,delay_remaining_);
            delay_remaining_-=used; delta-=used;
            if (delay_remaining_>0.0) return true;
        }
    }
    if (reduce_motion && duration_seconds_>0.12) delta*=duration_seconds_/0.12;
    delta*=speed_;
    if (duration_seconds_<=0.0) {
        elapsed_seconds_=reversed_ ? 0.0 : duration_seconds_;
        NotifyUpdate(); playing_=false;
        if (callbacks_.completed) callbacks_.completed(callbacks_.user);
        return false;
    }
    while (delta>0.0 && playing_) {
        const double distance=reversed_ ? elapsed_seconds_ : duration_seconds_-elapsed_seconds_;
        if (delta + 1.0e-9 < distance) {
            elapsed_seconds_ += reversed_ ? -delta : delta;
            delta=0.0;
            NotifyUpdate();
            break;
        }
        elapsed_seconds_=reversed_ ? 0.0 : duration_seconds_;
        delta-=std::max(0.0,distance);
        NotifyUpdate();
        if (completed_repeats_<repeat_count_) {
            ++completed_repeats_;
            if (ping_pong_) reversed_=!reversed_;
            else elapsed_seconds_=reversed_ ? duration_seconds_ : 0.0;
            if (distance==0.0 && delta==0.0) break;
        } else {
            playing_=false;
            if (callbacks_.completed) callbacks_.completed(callbacks_.user);
        }
    }
    return playing_;
}

MotionScheduler::~MotionScheduler() { Shutdown(); }
bool MotionScheduler::Initialize(std::size_t capacity) noexcept {
    Shutdown();
    if (capacity==0 || capacity>std::numeric_limits<std::size_t>::max()/sizeof(Controller*)) return false;
    controllers_=static_cast<Controller**>(std::calloc(capacity,sizeof(Controller*)));
    if (!controllers_) return false;
    capacity_=capacity; count_=0; return true;
}
void MotionScheduler::Shutdown() noexcept {
    for (std::size_t i=0;i<count_;++i) {
        if (controllers_[i] != nullptr) controllers_[i]->owner_context_ = nullptr;
    }
    std::free(controllers_); controllers_=nullptr; capacity_=0; count_=0;
}
bool MotionScheduler::Attach(Controller* controller) noexcept {
    if (!controller || !controllers_) return false;
    for (std::size_t i=0;i<count_;++i) if (controllers_[i]==controller) return true;
    if (count_>=capacity_) return false;
    controllers_[count_++]=controller; return true;
}
bool MotionScheduler::Detach(Controller* controller) noexcept {
    if (!controller) return false;
    for (std::size_t i=0;i<count_;++i) if (controllers_[i]==controller) {
        for (std::size_t j=i+1;j<count_;++j) controllers_[j-1]=controllers_[j];
        controllers_[--count_]=nullptr; return true;
    }
    return false;
}
bool MotionScheduler::Advance(double now, bool reduce_motion) noexcept {
    bool active=false;
    for (std::size_t i=0;i<count_;++i) {
        if (controllers_[i] && controllers_[i]->Update(now,reduce_motion)) active=true;
    }
    return active;
}
std::size_t MotionScheduler::ActiveCount() const noexcept {
    std::size_t result=0;
    for (std::size_t i=0;i<count_;++i) if (controllers_[i] && controllers_[i]->NeedsFrame()) ++result;
    return result;
}

} // namespace Lumen
