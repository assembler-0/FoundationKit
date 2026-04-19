#pragma once

#include <FoundationKitCxxStl/Base/Bug.hpp>
#include <FoundationKitCxxStl/Base/KernelError.hpp>
#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitDevice/Core/DeviceClass.hpp>
#include <FoundationKitDevice/Driver/DriverDescriptor.hpp>
#include <FoundationKitMemory/Management/AddressTypes.hpp>

namespace FoundationKitDevice {

    using namespace FoundationKitCxxStl;

    // Forward declaration.
    struct DeviceNode;

    // =========================================================================
    // BusOps — function-pointer table for bus-type operations
    //
    // Each bus type (PCI, USB, platform, VirtIO, I2C, SPI) provides one of
    // these structs to the DeviceManager. It defines how to enumerate children,
    // translate addresses, and match drivers using bus-specific rules.
    //
    // All pointers are optional. A bus that does not support address
    // translation (e.g., platform bus) leaves TranslateAddress null.
    // =========================================================================

    struct BusOps {
        /// @brief Enumerate all children attached to this bus and create DeviceNodes.
        ///
        /// @desc  Called by DeviceManager when the bus device transitions to Active.
        ///        The bus driver reads its topology (PCI config space, device tree
        ///        children, USB descriptors) and calls DeviceManager::CreateDevice()
        ///        for each child discovered.
        ///
        /// @param bus_dev  The bus's own DeviceNode.
        /// @return KernelError on failure.
        KernelResult<void> (*Enumerate)(DeviceNode& bus_dev) noexcept = nullptr;

        /// @brief Translate a bus address to a physical address.
        ///
        /// @desc  PCI BAR addresses and device-tree "reg" ranges are bus-relative.
        ///        The bus driver translates them to system physical addresses.
        ///        If the bus uses identity mapping (platform bus), this can be a
        ///        trivial pass-through or left null.
        ///
        /// @param dev          The child device on the bus.
        /// @param bus_address  Bus-relative address (e.g., BAR value).
        /// @param size         Number of bytes in the range.
        /// @return System physical address.
        KernelResult<FoundationKitMemory::PhysicalAddress> (*TranslateAddress)(
            const DeviceNode& dev, u64 bus_address, usize size) noexcept = nullptr;

        /// @brief Match a device to a driver using bus-specific rules.
        ///
        /// @desc  Overrides the default DeviceManager matching logic. If this
        ///        is non-null, it is called instead of the generic match table
        ///        comparison. PCI buses match on vendor/device ID; platform
        ///        buses match on compatible strings.
        ///
        /// @param dev  The device to match.
        /// @param drv  The driver to test.
        /// @return true if the driver matches the device.
        bool (*MatchDriver)(const DeviceNode& dev,
                            const DriverDescriptor& drv) noexcept = nullptr;

        /// @brief Configure the device for bus operation.
        ///
        /// @desc  Called after the driver's Attach() succeeds. For PCI, this
        ///        enables bus mastering, sets the memory/IO enable bits, etc.
        ///        For simpler buses, this may be a no-op.
        ///
        /// @param dev  The device to configure.
        /// @return KernelError on failure.
        KernelResult<void> (*ConfigureDevice)(DeviceNode& dev) noexcept = nullptr;

        /// @brief Remove a device from the bus.
        ///
        /// @desc  Called during device teardown. Quiesce any bus-level state
        ///        (disable bus mastering, release MSI vectors, etc.).
        ///
        /// @param dev  The device to remove.
        void (*RemoveDevice)(DeviceNode& dev) noexcept = nullptr;
    };

    // =========================================================================
    // BusDescriptor — the complete static identity of a bus type
    // =========================================================================

    /// @brief Describes a bus type and its operations.
    ///
    /// @desc  Each bus type (PCI, USB, platform, etc.) registers one of these
    ///        with DeviceManager::RegisterBus(). The descriptor's lifetime
    ///        must be static.
    struct BusDescriptor {
        /// @brief Human-readable bus name (e.g., "pci", "platform", "usb").
        const char* name = nullptr;

        /// @brief The DeviceClass that identifies this bus type.
        DeviceClass bus_class = DeviceClass::Unknown;

        /// @brief Bus-type lifecycle operations.
        BusOps ops{};
    };

    /// @brief Maximum number of bus types that can be registered.
    static constexpr usize kMaxBusTypes = 16;

    // =========================================================================
    // IBus concept — for template-constrained registration
    // =========================================================================

    /// @brief A type that can produce a BusDescriptor.
    template <typename T>
    concept IBus = requires(T t) {
        { t.Describe() } -> SameAs<BusDescriptor>;
    };

} // namespace FoundationKitDevice
