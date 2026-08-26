#include "Rng.h"

#include <chrono>

namespace neoncoil
{
    std::uint64_t Rng::seedFromClock()
    {
        const auto now = std::chrono::steady_clock::now().time_since_epoch();
        const auto ticks = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
        return mix(ticks, 0x5bf03635ull);
    }
}
