#pragma once

#include <FoundationKitCxxStl/Base/Bug.hpp>
#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitDevice/Bus/BusDescriptor.hpp>

namespace FoundationKitDevice {

    using namespace FoundationKitCxxStl;

    // Forward declaration.
    struct DeviceNode;

    // =========================================================================
    // BusNode — associates a BusDescriptor with a DeviceNode
    //
    // A BusNode is not a separate allocation — it is an inline extension
    // embedded in the DeviceNode via the bus_data pointer. Every DeviceNode
    // whose DeviceClass is in the 0x01xx range (Interconnect/Bus) should
    // have a BusNode attached.
    //
    // The BusNode stores:
    //   - A pointer to the BusDescriptor (the static ops table).
    //   - Bus-specific topology information (segment, bus number, etc.).
    //   - Enumeration state (has the bus been scanned yet?).
    //
    // This is a concrete struct, not a template. Bus-type-specific data
    // (PCI B:D:F, USB topology) is stored in a union.
    // =========================================================================

    struct BusNode {
        /// @brief The bus type descriptor. Must outlive the BusNode.
        const BusDescriptor* descriptor = nullptr;

        /// @brief True after Enumerate() has been called on this bus.
        bool enumerated = false;

        /// @brief Number of child devices discovered on this bus.
        u32 child_count = 0;

        /// @brief Bus-specific topology information.
        union TopologyData {
            /// @brief PCI topology: segment, bus, device, function.
            struct Pci {
                u16 segment;
                u8  bus;
                u8  device;
                u8  function;
            } pci;

            /// @brief USB topology: bus, port, depth.
            struct Usb {
                u8 bus;
                u8 port;
                u8 depth;
            } usb;

            /// @brief Platform bus: base address of MMIO region.
            struct Platform {
                u64 base_address;
            } platform;

            /// @brief I2C/SPI: bus index, chip-select/address.
            struct Simple {
                u8 bus_index;
                u8 address;
            } simple;

            constexpr TopologyData() noexcept : pci{} {}
        } topology;

        // --- Validation ---

        void Validate() const noexcept {
            FK_BUG_ON(descriptor == nullptr,
                "BusNode::Validate: null descriptor — "
                "bus node was not properly initialised");
            FK_BUG_ON(descriptor->name == nullptr,
                "BusNode::Validate: descriptor has null name");
        }
    };

    // =========================================================================
    // BusNode helpers
    // =========================================================================

    /// @brief Create a BusNode for a PCI bus.
    [[nodiscard]] inline BusNode MakePciBusNode(
            const BusDescriptor* desc, u16 segment, u8 bus) noexcept {
        FK_BUG_ON(desc == nullptr, "MakePciBusNode: null descriptor");
        BusNode n;
        n.descriptor = desc;
        n.topology.pci.segment = segment;
        n.topology.pci.bus = bus;
        n.topology.pci.device = 0;
        n.topology.pci.function = 0;
        return n;
    }

    /// @brief Create a BusNode for a platform bus.
    [[nodiscard]] inline BusNode MakePlatformBusNode(
            const BusDescriptor* desc, u64 base_address = 0) noexcept {
        FK_BUG_ON(desc == nullptr, "MakePlatformBusNode: null descriptor");
        BusNode n;
        n.descriptor = desc;
        n.topology.platform.base_address = base_address;
        return n;
    }

} // namespace FoundationKitDevice
