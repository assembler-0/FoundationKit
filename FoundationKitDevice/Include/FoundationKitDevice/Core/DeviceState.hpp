#pragma once

#include <FoundationKitCxxStl/Base/Bug.hpp>
#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitCxxStl/Base/Format.hpp>
#include <FoundationKitCxxStl/Sync/Atomic.hpp>

namespace FoundationKitDevice {

    using namespace FoundationKitCxxStl;

    // =========================================================================
    // DeviceState — Lifecycle state machine
    //
    // State graph:
    //
    //   Uninitialized ──► Discovered ──► Probing ──► Bound ──► Active
    //                                       │                    │  ▲
    //                                       │                    ▼  │
    //                                       │                 Suspended
    //                                       ▼                    │
    //                                     Error ◄────────────────┘
    //                                       │                    │
    //                                       ▼                    ▼
    //                                    Detached ◄─── Stopping ◄── Active
    //
    // Every transition is guarded by FK_BUG_ON. An illegal transition is a
    // kernel bug — not a recoverable error.
    // =========================================================================

    enum class DeviceState : u8 {
        Uninitialized = 0,   ///< Just allocated, no identity yet.
        Discovered    = 1,   ///< Identity set, not yet matched to a driver.
        Probing       = 2,   ///< Driver's Probe() is running.
        Bound         = 3,   ///< Driver's Attach() succeeded; device is configured.
        Active        = 4,   ///< Fully operational and serving requests.
        Suspended     = 5,   ///< Power-saved; driver's Suspend() completed.
        Stopping      = 6,   ///< Driver's Detach() is running.
        Detached      = 7,   ///< Driver detached; node may be reused or freed.
        Error         = 8,   ///< Unrecoverable hardware or driver error.
    };

    static constexpr u8 kDeviceStateCount = 9;

    // =========================================================================
    // DeviceState name — debug / logging (declared first for use in Validate)
    // =========================================================================

    /// @brief Human-readable name for a DeviceState enumerator.
    [[nodiscard]] inline const char* DeviceStateName(DeviceState s) noexcept {
        switch (s) {
            case DeviceState::Uninitialized: return "Uninitialized";
            case DeviceState::Discovered:    return "Discovered";
            case DeviceState::Probing:       return "Probing";
            case DeviceState::Bound:         return "Bound";
            case DeviceState::Active:        return "Active";
            case DeviceState::Suspended:     return "Suspended";
            case DeviceState::Stopping:      return "Stopping";
            case DeviceState::Detached:      return "Detached";
            case DeviceState::Error:         return "Error";
        }
        return "<invalid>";
    }

    // =========================================================================
    // Adjacency matrix — compile-time transition legality
    //
    // kTransitionTable[from][to] == true  ⟹  transition is legal.
    // Every other combination is a FK_BUG_ON crash.
    // =========================================================================

    // NOLINTBEGIN(readability-magic-numbers)
    inline constexpr bool kTransitionTable[kDeviceStateCount][kDeviceStateCount] = {
        //                Uninit  Disc    Prob    Bound   Active  Susp    Stop    Det     Err
        /* Uninit    */ { false,  true,   false,  false,  false,  false,  false,  false,  false },
        /* Disc      */ { false,  false,  true,   false,  false,  false,  false,  true,   true  },
        /* Prob      */ { false,  false,  false,  true,   false,  false,  false,  false,  true  },
        /* Bound     */ { false,  false,  false,  false,  true,   false,  false,  false,  true  },
        /* Active    */ { false,  false,  false,  false,  false,  true,   true,   false,  true  },
        /* Susp      */ { false,  false,  false,  false,  true,   false,  true,   false,  true  },
        /* Stop      */ { false,  false,  false,  false,  false,  false,  false,  true,   true  },
        /* Det       */ { false,  true,   false,  false,  false,  false,  false,  false,  false },
        /* Error     */ { false,  false,  false,  false,  false,  false,  true,   true,   false },
    };
    // NOLINTEND(readability-magic-numbers)

    // Compile-time self-test: Uninitialized → Discovered must be legal.
    static_assert(kTransitionTable
        [static_cast<u8>(DeviceState::Uninitialized)]
        [static_cast<u8>(DeviceState::Discovered)],
        "Uninitialized → Discovered must be a legal transition");

    // Compile-time self-test: Active → Uninitialized must be illegal.
    static_assert(!kTransitionTable
        [static_cast<u8>(DeviceState::Active)]
        [static_cast<u8>(DeviceState::Uninitialized)],
        "Active → Uninitialized must be an illegal transition");

    // =========================================================================
    // IsLegalTransition — runtime query with FK_BUG_ON guard variant
    // =========================================================================

    /// @brief Check if a transition from `from` to `to` is legal.
    [[nodiscard]] constexpr bool IsLegalTransition(DeviceState from, DeviceState to) noexcept {
        const auto f = static_cast<u8>(from);
        const auto t = static_cast<u8>(to);
        if (f >= kDeviceStateCount || t >= kDeviceStateCount) return false;
        return kTransitionTable[f][t];
    }

    /// @brief Validate a transition and FK_BUG_ON if illegal.
    ///
    /// @desc  Called before every state change. The crash message identifies
    ///        the exact from/to pair so the developer can trace the call site.
    inline void ValidateTransition(DeviceState from, DeviceState to) noexcept {
        FK_BUG_ON(!IsLegalTransition(from, to),
            "DeviceState: illegal transition {} → {} "
            "(from_raw={}, to_raw={})",
            DeviceStateName(from), DeviceStateName(to),
            static_cast<u8>(from), static_cast<u8>(to));
    }

} // namespace FoundationKitDevice

namespace FoundationKitCxxStl {
    template <>
    struct Formatter<FoundationKitDevice::DeviceState> {
        template <typename Sink>
        void Format(Sink& sb, const FoundationKitDevice::DeviceState& s, const FormatSpec& = {}) {
            const char* name = FoundationKitDevice::DeviceStateName(s);
            usize len = 0;
            while (name[len]) ++len;
            sb.Append(name, len);
        }
    };
} // namespace FoundationKitCxxStl
