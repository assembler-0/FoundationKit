#pragma once

#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitCxxStl/Base/Logger.hpp>
#include <FoundationKitCxxStl/Base/CompilerBuiltins.hpp>
#include <FoundationKitOsl/Osl.hpp>

namespace FoundationKitOsl {

using namespace FoundationKitCxxStl;
using namespace FoundationKitCxxStl::Base::CompilerBuiltins;

// High-level system monotonic clock.
class SystemClock {
public:
    // Get the current ticks from the OSL.
    [[nodiscard]] static u64 NowTicks() noexcept {
        return OslGetSystemTicks();
    }

    // Convert ticks to nanoseconds with strict overflow detection.
    [[nodiscard]] static u64 TicksToNanoseconds(u64 ticks) noexcept {
        const u64 freq = OslGetSystemFrequency();
        if (Expect(freq == 0, false)) [[unlikely]] return 0;

#if defined(FOUNDATIONKITCXXSTL_HAS_INT128)
        const u128 res = (static_cast<u128>(ticks) * 1000000000ULL) / freq;
        if (Expect(res > 0xFFFFFFFFFFFFFFFFULL, false)) [[unlikely]] {
            FK_BUG("TicksToNanoseconds: Result ({}) overflows u64 (uptime > 584 years)", res);
        }
        return static_cast<u64>(res);
#else
        const u64 seconds = ticks / freq;
        const u64 fraction = ticks % freq;

        u64 s_ns = 0;
        if (Expect(MulOverflow(seconds, 1000000000ULL, &s_ns), false)) [[unlikely]] {
            FK_BUG("TicksToNanoseconds: Seconds ({}) part overflows u64", seconds);
        }

        u64 f_scaled = 0;
        if (Expect(MulOverflow(fraction, 1000000000ULL, &f_scaled), false)) [[unlikely]] {
            FK_BUG("TicksToNanoseconds: Fractional ({}) part overflows (CPU frequency > 18.4GHz?)", fraction);
        }

        u64 total = 0;
        if (Expect(AddOverflow(s_ns, f_scaled / freq, &total), false)) [[unlikely]] {
            FK_BUG("TicksToNanoseconds: Total nanoseconds sum ({}) overflows u64", s_ns);
        }
        return total;
#endif
    }

    // Get current time in nanoseconds since boot.
    [[nodiscard]] static u64 NowNanoseconds() noexcept {
        return TicksToNanoseconds(NowTicks());
    }

    // Get current time in microseconds since boot.
    [[nodiscard]] static u64 NowMicroseconds() noexcept {
        return NowNanoseconds() / 1000ULL;
    }

    // Get current time in milliseconds since boot.
    [[nodiscard]] static u64 NowMilliseconds() noexcept {
        return NowNanoseconds() / 1000000ULL;
    }

    // Busy-wait for a specified duration in microseconds.
    static void DelayMicroseconds(u64 us) noexcept {
        OslMicroDelay(us);
    }
};

} // namespace FoundationKitOsl
