#pragma once

#include <FoundationKitCxxStl/Base/Bug.hpp>
#include <FoundationKitCxxStl/Base/KernelError.hpp>
#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitDevice/Core/DeviceClass.hpp>
#include <FoundationKitDevice/Core/DeviceState.hpp>
#include <FoundationKitDevice/Driver/MatchEntry.hpp>
#include <FoundationKitDevice/Power/PowerManager.hpp>

namespace FoundationKitDevice {

    using namespace FoundationKitCxxStl;

    // Forward declaration.
    struct DeviceNode;

    // =========================================================================
    // DriverOps — function-pointer table for driver lifecycle
    //
    // Follows the IrqChipDescriptor / ClockSourceDescriptor pattern:
    // a POD struct of nullable function pointers. The driver author
    // populates this at compile time (constexpr-initializable).
    //
    // Every pointer is optional. If a driver does not need a particular
    // lifecycle callback, it sets the pointer to nullptr.
    //
    // Error propagation: KernelResult<void> — success or KernelError.
    // Fatal errors MUST use FK_BUG_ON inside the callback body.
    // =========================================================================

    struct DriverOps {
        /// @brief Probe: determine if this driver can handle the device.
        ///
        /// @desc  Called after the matching engine selects this driver.
        ///        The driver inspects the device's properties, resources,
        ///        and hardware registers to confirm compatibility.
        ///
        ///        On success, the device transitions to Bound.
        ///        On failure (KernelError), the device remains Discovered
        ///        and the matching engine may try other drivers.
        ///
        /// @param dev  The device to probe.
        /// @return KernelError on failure.
        KernelResult<void> (*Probe)(DeviceNode& dev) noexcept = nullptr;

        /// @brief Attach: allocate driver-private state and initialise hardware.
        ///
        /// @desc  Called after a successful Probe. The driver allocates its
        ///        private state (via the provided allocator, stored in
        ///        dev.driver_private) and programmes the hardware.
        ///
        ///        After Attach succeeds, the device transitions to Active.
        ///
        /// @param dev  The device to attach.
        /// @return KernelError on failure.
        KernelResult<void> (*Attach)(DeviceNode& dev) noexcept = nullptr;

        /// @brief Detach: tear down hardware and free driver-private state.
        ///
        /// @desc  Called when the device is being removed or the driver is
        ///        being unloaded. Must release all resources and zero out
        ///        dev.driver_private.
        ///
        ///        After Detach, the device transitions to Detached.
        ///
        /// @param dev  The device to detach.
        void (*Detach)(DeviceNode& dev) noexcept = nullptr;

        /// @brief Shutdown: emergency shutdown path (reboot, panic).
        ///
        /// @desc  Called on system shutdown or panic. Must quiesce the device
        ///        as quickly as possible without allocating memory or waiting
        ///        on locks. May be called with interrupts disabled.
        ///
        /// @param dev  The device to shut down.
        void (*Shutdown)(DeviceNode& dev) noexcept = nullptr;

        /// @brief Suspend: save device state for low-power mode.
        ///
        /// @param dev     The device to suspend.
        /// @param target  Target D-state (D1, D2, or D3).
        /// @return KernelError if the device cannot be suspended.
        KernelResult<void> (*Suspend)(DeviceNode& dev, DevicePowerState target) noexcept = nullptr;

        /// @brief Resume: restore device state from low-power mode.
        ///
        /// @param dev     The device to resume.
        /// @param target  Target D-state (typically D0).
        /// @return KernelError if the device cannot be resumed.
        KernelResult<void> (*Resume)(DeviceNode& dev, DevicePowerState target) noexcept = nullptr;
    };

    // =========================================================================
    // DriverDescriptor — the complete static driver identity
    //
    // A driver is fully described by its descriptor. No inheritance. No
    // virtual functions. The kernel registers a pointer to a static
    // DriverDescriptor at boot.
    // =========================================================================

    /// @brief Complete static identity of a compiled-in driver.
    ///
    /// @desc  The driver author declares a static constinit DriverDescriptor
    ///        and calls DriverRegistry::Register() from an init function.
    ///        The descriptor's lifetime must be static — it is never copied.
    ///
    ///        Example:
    ///        ```cpp
    ///        static constinit DriverDescriptor g_ahci_driver = {
    ///            .name         = "ahci",
    ///            .target_class = DeviceClass::AhciController,
    ///            .match_table  = g_ahci_match_table,
    ///            .match_count  = 3,
    ///            .ops = {
    ///                .Probe  = AhciProbe,
    ///                .Attach = AhciAttach,
    ///                .Detach = AhciDetach,
    ///            },
    ///        };
    ///        ```
    struct DriverDescriptor {
        /// @brief Human-readable driver name (e.g., "ahci", "xhci", "virtio-net").
        const char* name = nullptr;

        /// @brief Primary device class this driver targets.
        DeviceClass target_class = DeviceClass::Unknown;

        /// @brief Pointer to the sentinel-terminated match table.
        const MatchEntry* match_table = nullptr;

        /// @brief Number of entries in match_table (excluding the sentinel).
        u32 match_count = 0;

        /// @brief Lifecycle function pointers.
        DriverOps ops{};

        /// @brief Driver priority. Higher value = preferred over lower.
        ///        When multiple drivers match the same device, the one with
        ///        the highest priority wins.
        u16 priority = 100;
    };

    // =========================================================================
    // IDriver concept — for template-constrained registration
    // =========================================================================

    /// @brief A type that can produce a DriverDescriptor.
    template <typename T>
    concept IDriver = requires(T t) {
        { t.Describe() } -> SameAs<DriverDescriptor>;
    };

} // namespace FoundationKitDevice
