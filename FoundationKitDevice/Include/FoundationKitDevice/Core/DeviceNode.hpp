#pragma once

#include <FoundationKitCxxStl/Base/Bug.hpp>
#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitCxxStl/Structure/IntrusiveDoublyLinkedList.hpp>
#include <FoundationKitCxxStl/Sync/Atomic.hpp>
#include <FoundationKitCxxStl/Sync/SpinLock.hpp>
#include <FoundationKitDevice/Bus/BusNode.hpp>
#include <FoundationKitDevice/Core/DeviceClass.hpp>
#include <FoundationKitDevice/Core/DeviceProperty.hpp>
#include <FoundationKitDevice/Core/DeviceState.hpp>
#include <FoundationKitDevice/Dma/DmaDescriptor.hpp>
#include <FoundationKitDevice/Resource/ResourceDescriptor.hpp>

namespace FoundationKitDevice {

    using namespace FoundationKitCxxStl;
    using namespace FoundationKitCxxStl::Sync;

    // Forward declarations.
    struct DriverDescriptor;

    // =========================================================================
    // DeviceNode — the central registry entry
    //
    // Every device in the system is represented by exactly one DeviceNode.
    // The nodes form an N-ary tree (parent → children list) and are also
    // threaded in a global flat list for O(n) enumeration.
    //
    // === Memory layout ===
    //
    // DeviceNode is a concrete struct — not a template, not polymorphic.
    // It is allocated from an ObjectPool or a StaticAllocator by the
    // DeviceManager. The driver's private state lives behind the
    // driver_private pointer, which the driver allocates and owns.
    //
    // === Intrusive linkage ===
    //
    // Two IntrusiveDoublyLinkedListNode members:
    //   sibling_node:  links this node into its parent's children list.
    //   global_node:   links this node into DeviceManager's global list.
    //
    // Children are stored as an IntrusiveDoublyLinkedList owned by the parent.
    //
    // === Thread safety ===
    //
    // Per-device SpinLock protects state transitions. The state field itself
    // is an Atomic<u8> for lock-free read access from hot paths (e.g.,
    // checking if a device is Active before dispatching I/O).
    //
    // The lock MUST be held for:
    //   - State transitions (TransitionTo)
    //   - Binding/unbinding a driver
    //   - Modifying the children list
    //   - Modifying the property store
    //
    // The lock need NOT be held for:
    //   - Reading the current state (atomic)
    //   - Reading immutable fields (name, device_class, device_id)
    //   - Reading driver_private (the driver owns synchronisation for its data)
    // =========================================================================

    struct DeviceNode {
        // --- Identification ---

        /// @brief Human-readable device name (e.g., "ahci0", "pci0000:00:1f.2").
        ///        Source-lifetime — not copied.
        const char* name = nullptr;

        /// @brief Device tree / ACPI compatible string for matching.
        ///        Source-lifetime — not copied.
        const char* compatible = nullptr;

        /// @brief Hierarchical device class.
        DeviceClass device_class = DeviceClass::Unknown;

        /// @brief Auto-incremented unique ID assigned by DeviceManager.
        u32 device_id = 0;

        // --- Lifecycle state ---

        /// @brief Atomic state for lock-free reads. Transitions require the lock.
        Atomic<u8> state{static_cast<u8>(DeviceState::Uninitialized)};

        /// @brief Current power state.
        Atomic<u8> power_state{static_cast<u8>(DevicePowerState::D_Unknown)};

        // --- Driver binding ---

        /// @brief The driver currently bound to this device. nullptr if unbound.
        DriverDescriptor* bound_driver = nullptr;

        /// @brief Opaque driver-private state. The driver allocates and frees this.
        void* driver_private = nullptr;

        /// @brief The MatchEntry that caused this driver to bind (for driver_data access).
        const MatchEntry* active_match = nullptr;

        // --- Hardware resources ---

        /// @brief Resource table (MMIO, IRQ, DMA channels, port I/O).
        ResourceTable resources;

        /// @brief DMA configuration (ops, masks, bounce requirement).
        DmaDescriptor dma;

        // --- Properties ---

        /// @brief Inline key-value property store.
        DevicePropertyStore properties;

        // --- Bus affiliation ---

        /// @brief If this device IS a bus, points to its BusNode.
        ///        Otherwise nullptr. The BusNode is allocated alongside the
        ///        DeviceNode by the DeviceManager.
        BusNode* bus_data = nullptr;

        // --- Tree structure ---

        /// @brief Parent device. nullptr for the root node.
        DeviceNode* parent = nullptr;

        /// @brief Children list. Each child's sibling_node is linked here.
        Structure::IntrusiveDoublyLinkedList children;

        /// @brief Linkage node for the parent's children list.
        Structure::IntrusiveDoublyLinkedListNode sibling_node;

        /// @brief Linkage node for the DeviceManager's global flat list.
        Structure::IntrusiveDoublyLinkedListNode global_node;

        // --- Synchronisation ---

        /// @brief Per-device lock for state transitions and tree mutations.
        mutable SpinLock lock;

        // =====================================================================
        // State management
        // =====================================================================

        /// @brief Get the current device state (lock-free).
        [[nodiscard]] DeviceState State() const noexcept {
            return static_cast<DeviceState>(state.Load(MemoryOrder::Acquire));
        }

        /// @brief Get the current power state (lock-free).
        [[nodiscard]] DevicePowerState PowerState() const noexcept {
            return static_cast<DevicePowerState>(power_state.Load(MemoryOrder::Acquire));
        }

        /// @brief Transition to a new state. FK_BUG_ON on illegal transition.
        ///
        /// @desc  MUST be called with lock held. The adjacency matrix is
        ///        validated and the atomic state is updated with Release
        ///        ordering so that concurrent readers see the new state.
        void TransitionTo(DeviceState new_state) noexcept {
            const DeviceState old = State();
            ValidateTransition(old, new_state);
            state.Store(static_cast<u8>(new_state), MemoryOrder::Release);
            FK_LOG_INFO("DeviceNode[{}] '{}': {} → {}",
                device_id,
                name ? name : "<unnamed>",
                DeviceStateName(old),
                DeviceStateName(new_state));
        }

        /// @brief Transition to a new power state. FK_BUG_ON on illegal transition.
        void TransitionPowerTo(DevicePowerState new_ps) noexcept {
            const DevicePowerState old = PowerState();
            ValidatePowerTransition(old, new_ps);
            power_state.Store(static_cast<u8>(new_ps), MemoryOrder::Release);
            FK_LOG_INFO("DeviceNode[{}] '{}': power {} → {}",
                device_id,
                name ? name : "<unnamed>",
                PowerStateName(old),
                PowerStateName(new_ps));
        }

        /// @brief Force-set the error state (e.g., hardware fault detected).
        void SetError() noexcept {
            const DeviceState old = State();
            // Error is reachable from most states — if it's somehow already
            // Error, that's a double-fault which is itself a bug.
            FK_BUG_ON(old == DeviceState::Error,
                "DeviceNode[{}] '{}': SetError called but already in Error state — "
                "double-fault or missing recovery",
                device_id, name ? name : "<unnamed>");
            FK_BUG_ON(old == DeviceState::Uninitialized,
                "DeviceNode[{}] '{}': SetError on Uninitialized device — "
                "the device was never discovered",
                device_id, name ? name : "<unnamed>");
            state.Store(static_cast<u8>(DeviceState::Error), MemoryOrder::Release);
            FK_LOG_ERR("DeviceNode[{}] '{}': forced to Error from {}",
                device_id, name ? name : "<unnamed>",
                DeviceStateName(old));
        }

        // =====================================================================
        // Driver binding queries
        // =====================================================================

        /// @brief True if a driver is bound to this device.
        [[nodiscard]] bool IsBound() const noexcept {
            return bound_driver != nullptr;
        }

        /// @brief True if the device is Active (fully operational).
        [[nodiscard]] bool IsActive() const noexcept {
            return State() == DeviceState::Active;
        }

        /// @brief True if the device is a bus (has bus_data).
        [[nodiscard]] bool IsBus() const noexcept {
            return bus_data != nullptr;
        }

        // =====================================================================
        // Tree queries
        // =====================================================================

        /// @brief True if this is the root node (no parent).
        [[nodiscard]] bool IsRoot() const noexcept {
            return parent == nullptr;
        }

        /// @brief True if this node has children.
        [[nodiscard]] bool HasChildren() const noexcept {
            return !children.Empty();
        }

        /// @brief Number of direct children.
        [[nodiscard]] usize ChildCount() const noexcept {
            return children.Size();
        }

        /// @brief Iterate over direct children.
        ///
        /// @desc  Do NOT add or remove children during iteration.
        ///        The callback receives a DeviceNode&.
        template <typename Func>
        void ForEachChild(Func&& func) noexcept {
            auto* node = children.Begin();
            while (node != children.End()) {
                auto* next = node->next;
                DeviceNode* child = Structure::ContainerOf<DeviceNode, &DeviceNode::sibling_node>(node);
                func(*child);
                node = next;
            }
        }

        // =====================================================================
        // Property convenience accessors
        // =====================================================================

        /// @brief Get the "vendor-id" property as u16. FK_BUG_ON if not set.
        [[nodiscard]] u16 VendorId() const noexcept {
            return static_cast<u16>(properties.GetU64("vendor-id"));
        }

        /// @brief Get the "device-id" property as u16. FK_BUG_ON if not set.
        [[nodiscard]] u16 DeviceIdProp() const noexcept {
            return static_cast<u16>(properties.GetU64("device-id"));
        }

        /// @brief Try to get vendor-id (returns 0 if not set).
        [[nodiscard]] u16 TryVendorId() const noexcept {
            u64 v = 0;
            if (properties.TryGetU64("vendor-id", v)) return static_cast<u16>(v);
            return 0;
        }

        /// @brief Try to get device-id (returns 0 if not set).
        [[nodiscard]] u16 TryDeviceIdProp() const noexcept {
            u64 v = 0;
            if (properties.TryGetU64("device-id", v)) return static_cast<u16>(v);
            return 0;
        }

        // =====================================================================
        // Diagnostic
        // =====================================================================

        /// @brief Dump this device's summary to FK_LOG_INFO.
        void DumpInfo() const noexcept {
            FK_LOG_INFO("  DeviceNode[{}] '{}' class={} state={} power={} "
                "driver={} children={} resources={} properties={}",
                device_id,
                name ? name : "<unnamed>",
                DeviceClassName(device_class),
                DeviceStateName(State()),
                PowerStateName(PowerState()),
                bound_driver ? "(bound)" : "(none)",
                children.Size(),
                resources.count,
                properties.Count());
        }
    };

    static_assert(!__is_polymorphic(DeviceNode),
        "DeviceNode must NOT be polymorphic — no vtable in freestanding");

} // namespace FoundationKitDevice
