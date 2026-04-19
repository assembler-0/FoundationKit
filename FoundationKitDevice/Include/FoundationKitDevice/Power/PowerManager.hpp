#pragma once

#include <FoundationKitCxxStl/Base/Bug.hpp>
#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitCxxStl/Base/Format.hpp>

namespace FoundationKitDevice {

    using namespace FoundationKitCxxStl;

    // Forward declaration.
    struct DeviceNode;

    // =========================================================================
    // DevicePowerState — ACPI-inspired D-state model
    //
    // D0 = fully on, D3 = fully off. D1/D2 are intermediate low-power states
    // whose exact semantics depend on the device class.
    //
    // The kernel never transitions a device directly from D3 → D0 without
    // calling the driver's Resume() — full hardware re-initialisation is
    // required after D3.
    // =========================================================================

    enum class DevicePowerState : u8 {
        D0_Active   = 0,    ///< Fully operational.
        D1_Light    = 1,    ///< Light sleep — fast resume, reduced power.
        D2_Deep     = 2,    ///< Deep sleep — slower resume, lower power.
        D3_Off      = 3,    ///< Powered off — full reinit on resume.
        D_Unknown   = 0xFF, ///< Not yet determined.
    };

    /// @brief Human-readable name for a DevicePowerState.
    [[nodiscard]] inline const char* PowerStateName(DevicePowerState s) noexcept {
        switch (s) {
            case DevicePowerState::D0_Active: return "D0_Active";
            case DevicePowerState::D1_Light:  return "D1_Light";
            case DevicePowerState::D2_Deep:   return "D2_Deep";
            case DevicePowerState::D3_Off:    return "D3_Off";
            case DevicePowerState::D_Unknown: return "D_Unknown";
        }
        return "<invalid>";
    }

    // =========================================================================
    // Power transition legality
    //
    // D0 ↔ D1 ↔ D2 ↔ D3  (forward = suspend, backward = resume)
    // D_Unknown → D0 (initial power-on)
    // D_Unknown → D3 (device discovered but not powered)
    // =========================================================================

    [[nodiscard]] constexpr bool IsLegalPowerTransition(
            DevicePowerState from, DevicePowerState to) noexcept {
        if (from == to) return false; // No-op transitions are bugs.
        if (from == DevicePowerState::D_Unknown) {
            // Initial power-on: may go to D0 or D3.
            return to == DevicePowerState::D0_Active || to == DevicePowerState::D3_Off;
        }
        // Adjacent D-state transitions are legal (D0↔D1, D1↔D2, D2↔D3).
        const auto f = static_cast<u8>(from);
        const auto t = static_cast<u8>(to);
        // Fast path: difference of 1 in either direction.
        return (f < 4 && t < 4) && (f + 1 == t || t + 1 == f);
    }

    inline void ValidatePowerTransition(DevicePowerState from, DevicePowerState to) noexcept {
        FK_BUG_ON(!IsLegalPowerTransition(from, to),
            "PowerManager: illegal power transition {} → {} "
            "(from_raw={}, to_raw={})",
            PowerStateName(from), PowerStateName(to),
            static_cast<u8>(from), static_cast<u8>(to));
    }

    // =========================================================================
    // PowerManager — system-wide power state orchestrator
    //
    // Manages suspend/resume of the entire device tree with correct ordering:
    //   Suspend: leaves → root (children before parents)
    //   Resume:  root → leaves (parents before children)
    //
    // This is a static utility class, not an instantiated object.
    // =========================================================================

    class PowerManager {
    public:
        PowerManager() = delete;  // Static-only API.

        /// @brief Suspend a single device to the target power state.
        ///
        /// @desc  Calls the driver's Suspend() op. FK_BUG_ON if the device
        ///        has no bound driver or Suspend is null.
        ///
        /// @param dev     The device to suspend.
        /// @param target  Target D-state (D1, D2, or D3).
        /// @return KernelError on failure.
        static KernelResult<void> SuspendDevice(DeviceNode& dev, DevicePowerState target) noexcept;

        /// @brief Resume a single device from its current power state.
        ///
        /// @param dev     The device to resume.
        /// @param target  Target D-state (typically D0).
        /// @return KernelError on failure.
        static KernelResult<void> ResumeDevice(DeviceNode& dev, DevicePowerState target) noexcept;

        /// @brief Suspend an entire subtree rooted at `root`, leaves first.
        ///
        /// @desc  Recursively suspends all children before suspending the
        ///        parent. If any child fails, the subtree suspend is aborted
        ///        and already-suspended children are resumed (rollback).
        ///
        /// @param root    Root of the subtree.
        /// @param target  Target D-state for all devices.
        /// @return KernelError on failure.
        static KernelResult<void> SuspendSubtree(DeviceNode& root, DevicePowerState target) noexcept;

        /// @brief Resume an entire subtree rooted at `root`, root first.
        ///
        /// @desc  Resumes the parent before any children. A child's bus
        ///        controller must be active before the child can resume.
        ///
        /// @param root    Root of the subtree.
        /// @param target  Target D-state for all devices (typically D0).
        /// @return KernelError on failure.
        static KernelResult<void> ResumeSubtree(DeviceNode& root, DevicePowerState target) noexcept;
    };

} // namespace FoundationKitDevice

namespace FoundationKitCxxStl {
    template <>
    struct Formatter<FoundationKitDevice::DevicePowerState> {
        template <typename Sink>
        void Format(Sink& sb, const FoundationKitDevice::DevicePowerState& s, const FormatSpec& = {}) {
            const char* name = FoundationKitDevice::PowerStateName(s);
            usize len = 0;
            while (name[len]) ++len;
            sb.Append(name, len);
        }
    };
} // namespace FoundationKitCxxStl
