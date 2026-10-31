#ifndef LUMEN_CLOCK_H
#define LUMEN_CLOCK_H

namespace Lumen {

class Clock {
public:
    virtual ~Clock() = default;
    virtual double NowSeconds() const noexcept = 0;
};

class SteadyClock final : public Clock {
public:
    double NowSeconds() const noexcept override;
};

class ManualClock final : public Clock {
public:
    explicit ManualClock(double initial_seconds = 0.0) noexcept : now_seconds_(initial_seconds) {}

    double NowSeconds() const noexcept override { return now_seconds_; }
    void Set(double seconds) noexcept { now_seconds_ = seconds; }
    void Advance(double seconds) noexcept { now_seconds_ += seconds; }

private:
    double now_seconds_;
};

} // namespace Lumen

#endif
