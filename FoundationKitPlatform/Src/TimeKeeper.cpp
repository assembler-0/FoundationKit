#include <FoundationKitPlatform/Clocksource/TimeKeeper.hpp>

namespace FoundationKitPlatform::Clocksource {

    ClockSourceDescriptor TimeKeeper::s_active{};
    u64 TimeKeeper::s_last_cycles{0};
    Atomic<u64> TimeKeeper::s_mono_base_ns{0};
    Atomic<i64> TimeKeeper::s_wall_base_ns{0};

} // namespace FoundationKitPlatform::Clocksource
