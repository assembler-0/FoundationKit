#include <FoundationKitPlatform/Clocksource/TimeKeeper.hpp>

namespace FoundationKitPlatform::Clocksource {

    // SeqLock initial value: default-constructed TimeState (all zeros, null Read).
    SeqLock<TimeKeeper::TimeState> TimeKeeper::s_state{TimeKeeper::TimeState{}};

} // namespace FoundationKitPlatform::Clocksource
