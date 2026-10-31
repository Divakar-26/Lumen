#include "lumen/clock.h"

#include <chrono>

namespace Lumen {

double SteadyClock::NowSeconds() const noexcept {
    using clock = std::chrono::steady_clock;
    return std::chrono::duration<double>(clock::now().time_since_epoch()).count();
}

} // namespace Lumen
