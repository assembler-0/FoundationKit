#pragma once

#include <FoundationKitCxxStl/Base/Bug.hpp>
#include <FoundationKitCxxStl/Base/KernelError.hpp>
#include <FoundationKitCxxStl/Base/Span.hpp>
#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitCxxStl/Structure/IntrusiveDoublyLinkedList.hpp>
#include <FoundationKitCxxStl/Sync/SpinLock.hpp>
#include <FoundationKitDevice/Bus/BusDescriptor.hpp>
#include <FoundationKitDevice/Bus/BusNode.hpp>
#include <FoundationKitDevice/Core/DeviceClass.hpp>
#include <FoundationKitDevice/Core/DeviceNode.hpp>
#include <FoundationKitDevice/Core/DeviceState.hpp>
#include <FoundationKitDevice/Dma/DmaDescriptor.hpp>
#include <FoundationKitDevice/Driver/DriverDescriptor.hpp>
#include <FoundationKitDevice/Driver/DriverRegistry.hpp>
#include <FoundationKitDevice/Driver/MatchEntry.hpp>
#include <FoundationKitDevice/Power/PowerManager.hpp>
#include <FoundationKitDevice/Resource/ResourceDescriptor.hpp>

namespace FoundationKitDevice {

    using namespace FoundationKitCxxStl;
    using namespace FoundationKitCxxStl::Sync;

    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr usize kMaxGlobalDevices = 512;

    // =========================================================================
    // DeviceManager — Top-level UDM orchestrator
    //
    // Owns:
    //   - The device tree root.
    //   - The global flat list of all DeviceNodes.
    //   - The bus type registry.
    //   - DeviceNode allocation and lifecycle.
    //
    // This is a static class — no instances. All state is global, guarded by
    // a single SpinLock for tree mutations. Individual DeviceNodes have their
    // own per-device locks for state transitions.
    //
    // The DeviceManager does NOT own the DeviceNode storage itself. Storage
    // is provided by the kernel via RegisterAllocator(). The kernel can
    // use ObjectPool, StaticAllocator, or any IAllocator.
    // =========================================================================

    class DeviceManager {
    public:
        DeviceManager() = delete;  // Static-only API.

        // =====================================================================
        // Initialisation
        // =====================================================================

        /// @brief Initialise the DeviceManager with a root DeviceNode.
        ///
        /// @desc  Must be called once during early boot before any other
        ///        DeviceManager operation. Creates the root node representing
        ///        the system itself. The root never has a driver — it is the
        ///        anchor for the device tree.
        ///
        /// @param root  Pre-allocated DeviceNode to use as the root.
        ///              The caller retains ownership of the memory.
        static void Init(DeviceNode* root) noexcept {
            FK_BUG_ON(root == nullptr,
                "DeviceManager::Init: null root node");
            FK_BUG_ON(s_root != nullptr,
                "DeviceManager::Init: already initialised "
                "(root='{}', attempted duplicate init)",
                s_root->name ? s_root->name : "<unnamed>");

            root->name = "system";
            root->device_class = DeviceClass::System;
            root->device_id = AllocateDeviceId();
            root->TransitionTo(DeviceState::Discovered);

            s_root = root;

            // The root goes into the global list.
            s_all_devices.PushBack(&root->global_node);
            ++s_device_count;

            FK_LOG_INFO("DeviceManager: initialised with root node id={}",
                root->device_id);
        }

        // =====================================================================
        // Bus registration
        // =====================================================================

        /// @brief Register a bus type.
        ///
        /// @desc  Called during early boot for each bus type the kernel
        ///        supports (PCI, platform, USB, etc.).
        ///
        /// @param desc  Reference to a static BusDescriptor.
        static void RegisterBus(BusDescriptor& desc) noexcept {
            FK_BUG_ON(desc.name == nullptr,
                "DeviceManager::RegisterBus: bus has null name");
            FK_BUG_ON(s_bus_count >= kMaxBusTypes,
                "DeviceManager::RegisterBus: overflow — {} bus types already "
                "registered (max {}). Bus '{}' cannot be added.",
                s_bus_count, kMaxBusTypes, desc.name);

            // Check for duplicate.
            for (usize i = 0; i < s_bus_count; ++i) {
                FK_BUG_ON(s_buses[i] == &desc,
                    "DeviceManager::RegisterBus: bus '{}' registered twice",
                    desc.name);
            }

            s_buses[s_bus_count] = &desc;
            ++s_bus_count;

            FK_LOG_INFO("DeviceManager: registered bus '{}' class={}",
                desc.name, DeviceClassName(desc.bus_class));
        }

        /// @brief Find a registered bus type by class.
        [[nodiscard]] static BusDescriptor* FindBus(DeviceClass cls) noexcept {
            for (usize i = 0; i < s_bus_count; ++i) {
                if (s_buses[i]->bus_class == cls) return s_buses[i];
            }
            return nullptr;
        }

        // =====================================================================
        // Device creation & destruction
        // =====================================================================

        /// @brief Attach a new DeviceNode to the tree as a child of `parent`.
        ///
        /// @desc  The caller pre-allocates the DeviceNode (e.g., from an
        ///        ObjectPool). DeviceManager assigns an ID, links it into the
        ///        tree, and transitions it to Discovered.
        ///
        /// @param dev     Pre-allocated DeviceNode. Must be in Uninitialized state.
        /// @param parent  Parent node. Must be Discovered, Bound, or Active.
        static void AttachDevice(DeviceNode* dev, DeviceNode* parent) noexcept {
            FK_BUG_ON(dev == nullptr,
                "DeviceManager::AttachDevice: null device");
            FK_BUG_ON(parent == nullptr,
                "DeviceManager::AttachDevice: null parent");
            FK_BUG_ON(s_root == nullptr,
                "DeviceManager::AttachDevice: DeviceManager not initialised");
            FK_BUG_ON(dev->State() != DeviceState::Uninitialized,
                "DeviceManager::AttachDevice: device '{}' is not Uninitialized (state={})",
                dev->name ? dev->name : "<unnamed>",
                DeviceStateName(dev->State()));
            FK_BUG_ON(s_device_count >= kMaxGlobalDevices,
                "DeviceManager::AttachDevice: global device limit reached "
                "({}/{}) — cannot attach '{}'. "
                "Bump kMaxGlobalDevices if legitimate.",
                s_device_count, kMaxGlobalDevices,
                dev->name ? dev->name : "<unnamed>");

            const DeviceState parent_state = parent->State();
            FK_BUG_ON(
                parent_state != DeviceState::Discovered &&
                parent_state != DeviceState::Bound &&
                parent_state != DeviceState::Active,
                "DeviceManager::AttachDevice: parent '{}' is in state {} — "
                "must be Discovered, Bound, or Active to accept children",
                parent->name ? parent->name : "<unnamed>",
                DeviceStateName(parent_state));

            // Assign ID.
            dev->device_id = AllocateDeviceId();

            // Link into tree.
            dev->parent = parent;
            parent->children.PushBack(&dev->sibling_node);

            // Link into global list.
            s_all_devices.PushBack(&dev->global_node);
            ++s_device_count;

            // Transition to Discovered.
            dev->TransitionTo(DeviceState::Discovered);

            FK_LOG_INFO("DeviceManager: attached device[{}] '{}' (class={}) → parent[{}] '{}'",
                dev->device_id,
                dev->name ? dev->name : "<unnamed>",
                DeviceClassName(dev->device_class),
                parent->device_id,
                parent->name ? parent->name : "<unnamed>");
        }

        /// @brief Detach and tear down a device. Calls driver Detach if bound.
        ///
        /// @desc  Recursively detaches all children first (leaves then parent).
        ///        Does NOT free the DeviceNode memory — the caller is
        ///        responsible for returning it to the allocator.
        ///
        /// @param dev  The device to destroy.
        static void DetachDevice(DeviceNode* dev) noexcept {
            FK_BUG_ON(dev == nullptr,
                "DeviceManager::DetachDevice: null device");
            FK_BUG_ON(dev == s_root,
                "DeviceManager::DetachDevice: cannot detach the root node");

            // Recursively detach children (depth-first).
            while (!dev->children.Empty()) {
                auto* child_node = dev->children.Begin();
                DeviceNode* child = Structure::ContainerOf<
                    DeviceNode, &DeviceNode::sibling_node>(child_node);
                DetachDevice(child);
            }

            // Call driver Detach if bound.
            if (dev->IsBound()) {
                const DeviceState cur_state = dev->State();
                if (cur_state == DeviceState::Active ||
                    cur_state == DeviceState::Suspended ||
                    cur_state == DeviceState::Bound) {

                    if (cur_state != DeviceState::Stopping) {
                        // Transition to Stopping before calling Detach.
                        if (cur_state == DeviceState::Active ||
                            cur_state == DeviceState::Suspended) {
                            dev->TransitionTo(DeviceState::Stopping);
                        }
                    }

                    if (dev->bound_driver->ops.Detach) {
                        FK_LOG_INFO("DeviceManager: calling Detach for '{}' on device[{}] '{}'",
                            dev->bound_driver->name,
                            dev->device_id,
                            dev->name ? dev->name : "<unnamed>");
                        dev->bound_driver->ops.Detach(*dev);
                    }
                }

                dev->bound_driver = nullptr;
                dev->driver_private = nullptr;
                dev->active_match = nullptr;
            }

            // Transition to Detached if not already there.
            const DeviceState final_state = dev->State();
            if (final_state != DeviceState::Detached &&
                final_state != DeviceState::Uninitialized) {
                // Handle Stopping → Detached or Error → Detached.
                if (final_state == DeviceState::Stopping ||
                    final_state == DeviceState::Error ||
                    final_state == DeviceState::Discovered) {
                    dev->TransitionTo(DeviceState::Detached);
                }
            }

            // Unlink from parent's children list.
            if (dev->parent) {
                dev->parent->children.Remove(&dev->sibling_node);
                dev->parent = nullptr;
            }

            // Unlink from global list.
            s_all_devices.Remove(&dev->global_node);
            FK_BUG_ON(s_device_count == 0,
                "DeviceManager::DetachDevice: device count underflow");
            --s_device_count;

            FK_LOG_INFO("DeviceManager: detached device[{}] '{}'",
                dev->device_id,
                dev->name ? dev->name : "<unnamed>");
        }

        // =====================================================================
        // Driver matching & binding
        // =====================================================================

        /// @brief Attempt to match and bind a driver to a single device.
        ///
        /// @return true if a driver was successfully bound.
        static bool ProbeAndBind(DeviceNode& dev) noexcept {
            FK_BUG_ON(dev.State() != DeviceState::Discovered,
                "DeviceManager::ProbeAndBind: device[{}] '{}' is not Discovered (state={})",
                dev.device_id,
                dev.name ? dev.name : "<unnamed>",
                DeviceStateName(dev.State()));

            // Use bus-specific matching if the device's parent is a bus.
            const MatchEntry* match_entry = nullptr;
            DriverDescriptor* best = nullptr;

            if (dev.parent && dev.parent->bus_data) {
                const BusNode* bus = dev.parent->bus_data;
                if (bus->descriptor->ops.MatchDriver) {
                    // Bus-specific match: iterate all drivers, use bus matcher.
                    u16 best_prio = 0;
                    for (auto* drv : DriverRegistry::All()) {
                        if (bus->descriptor->ops.MatchDriver(dev, *drv)) {
                            if (!best || drv->priority > best_prio) {
                                best = drv;
                                best_prio = drv->priority;
                            }
                        }
                    }
                }
            }

            // Fall back to generic matching.
            if (!best) {
                best = DriverRegistry::FindMatch(dev, &match_entry);
            }

            if (!best) {
                FK_LOG_INFO("DeviceManager: no driver found for device[{}] '{}' (class={})",
                    dev.device_id,
                    dev.name ? dev.name : "<unnamed>",
                    DeviceClassName(dev.device_class));
                return false;
            }

            FK_LOG_INFO("DeviceManager: matched driver '{}' → device[{}] '{}' (priority={})",
                best->name,
                dev.device_id,
                dev.name ? dev.name : "<unnamed>",
                best->priority);

            // Probe.
            dev.TransitionTo(DeviceState::Probing);
            dev.active_match = match_entry;

            if (best->ops.Probe) {
                auto result = best->ops.Probe(dev);
                if (!result) {
                    FK_LOG_WARN("DeviceManager: Probe failed for '{}' on device[{}] '{}': {}",
                        best->name, dev.device_id,
                        dev.name ? dev.name : "<unnamed>",
                        result.Error());
                    dev.active_match = nullptr;
                    // Probe failure → Error state; the device stays in the tree
                    // but cannot be used until the cause is resolved.
                    dev.state.Store(static_cast<u8>(DeviceState::Error),
                        MemoryOrder::Release);
                    return false;
                }
            }

            // Bind.
            dev.bound_driver = best;
            dev.TransitionTo(DeviceState::Bound);

            // Attach.
            if (best->ops.Attach) {
                auto result = best->ops.Attach(dev);
                if (!result) {
                    FK_LOG_ERR("DeviceManager: Attach failed for '{}' on device[{}] '{}': {}",
                        best->name, dev.device_id,
                        dev.name ? dev.name : "<unnamed>",
                        result.Error());
                    dev.bound_driver = nullptr;
                    dev.driver_private = nullptr;
                    dev.active_match = nullptr;
                    dev.state.Store(static_cast<u8>(DeviceState::Error),
                        MemoryOrder::Release);
                    return false;
                }
            }

            // Active.
            dev.TransitionTo(DeviceState::Active);

            // If the device is a bus, configure bus and enumerate children.
            if (dev.parent && dev.parent->bus_data) {
                const BusNode* bus = dev.parent->bus_data;
                if (bus->descriptor->ops.ConfigureDevice) {
                    auto cfg = bus->descriptor->ops.ConfigureDevice(dev);
                    FK_WARN_ON(!cfg,
                        "DeviceManager: bus ConfigureDevice failed for device[{}] '{}'",
                        dev.device_id, dev.name ? dev.name : "<unnamed>");
                }
            }

            // If this device IS a bus, enumerate its children.
            if (dev.bus_data && dev.bus_data->descriptor &&
                dev.bus_data->descriptor->ops.Enumerate &&
                !dev.bus_data->enumerated) {
                FK_LOG_INFO("DeviceManager: enumerating bus device[{}] '{}'",
                    dev.device_id, dev.name ? dev.name : "<unnamed>");
                auto enum_result = dev.bus_data->descriptor->ops.Enumerate(dev);
                if (enum_result) {
                    dev.bus_data->enumerated = true;
                } else {
                    FK_LOG_ERR("DeviceManager: enumeration failed for bus device[{}] '{}': {}",
                        dev.device_id,
                        dev.name ? dev.name : "<unnamed>",
                        enum_result.Error());
                }
            }

            return true;
        }

        /// @brief Probe and bind all unbound devices in the tree.
        ///
        /// @desc  Iterates the global device list and attempts to bind a
        ///        driver to each Discovered device. Repeats until no new
        ///        bindings occur (cascading enumeration may create new
        ///        Discovered devices).
        ///
        /// @return Total number of devices successfully bound.
        static usize ProbeAndBindAll() noexcept {
            FK_BUG_ON(s_root == nullptr,
                "DeviceManager::ProbeAndBindAll: not initialised");

            usize total_bound = 0;
            bool progress = true;

            // Iterate until no more progress (handles cascading bus enumeration).
            while (progress) {
                progress = false;

                // Collect discoverable devices — we cannot modify the global
                // list while iterating it (ProbeAndBind may attach new children
                // via bus enumeration). Snapshot the pointers first.
                static constexpr usize kBatchSize = 64;
                DeviceNode* batch[kBatchSize];
                usize batch_count = 0;

                auto* node = s_all_devices.Begin();
                while (node != s_all_devices.End()) {
                    DeviceNode* dev = Structure::ContainerOf<
                        DeviceNode, &DeviceNode::global_node>(node);
                    node = node->next;

                    if (dev->State() == DeviceState::Discovered) {
                        if (batch_count < kBatchSize) {
                            batch[batch_count++] = dev;
                        }
                    }
                }

                for (usize i = 0; i < batch_count; ++i) {
                    if (batch[i]->State() == DeviceState::Discovered) {
                        if (ProbeAndBind(*batch[i])) {
                            ++total_bound;
                            progress = true;
                        }
                    }
                }
            }

            FK_LOG_INFO("DeviceManager: ProbeAndBindAll completed — {} devices bound, "
                "{} total devices",
                total_bound, s_device_count);

            return total_bound;
        }

        // =====================================================================
        // Enumeration & lookup
        // =====================================================================

        /// @brief Find the first device matching a device class.
        [[nodiscard]] static DeviceNode* FindByClass(DeviceClass cls) noexcept {
            auto* node = s_all_devices.Begin();
            while (node != s_all_devices.End()) {
                DeviceNode* dev = Structure::ContainerOf<
                    DeviceNode, &DeviceNode::global_node>(node);
                if (dev->device_class == cls) return dev;
                node = node->next;
            }
            return nullptr;
        }

        /// @brief Find a device by its unique ID.
        [[nodiscard]] static DeviceNode* FindById(u32 id) noexcept {
            auto* node = s_all_devices.Begin();
            while (node != s_all_devices.End()) {
                DeviceNode* dev = Structure::ContainerOf<
                    DeviceNode, &DeviceNode::global_node>(node);
                if (dev->device_id == id) return dev;
                node = node->next;
            }
            return nullptr;
        }

        /// @brief Find a device by name.
        [[nodiscard]] static DeviceNode* FindByName(const char* name) noexcept {
            FK_BUG_ON(name == nullptr,
                "DeviceManager::FindByName: null name");
            auto* node = s_all_devices.Begin();
            while (node != s_all_devices.End()) {
                DeviceNode* dev = Structure::ContainerOf<
                    DeviceNode, &DeviceNode::global_node>(node);
                if (dev->name && CompatibleMatch(dev->name, name)) return dev;
                node = node->next;
            }
            return nullptr;
        }

        /// @brief Find all devices of a given class category.
        ///
        /// @param category   A DeviceClass whose high byte defines the category.
        /// @param out        Output array.
        /// @param max_out    Size of the output array.
        /// @return Number of devices found.
        static usize FindAllInCategory(DeviceClass category,
                                       DeviceNode** out, usize max_out) noexcept {
            FK_BUG_ON(out == nullptr && max_out > 0,
                "DeviceManager::FindAllInCategory: null output array with non-zero max");
            usize found = 0;
            auto* node = s_all_devices.Begin();
            while (node != s_all_devices.End() && found < max_out) {
                DeviceNode* dev = Structure::ContainerOf<
                    DeviceNode, &DeviceNode::global_node>(node);
                if (DeviceClassInCategory(dev->device_class, category)) {
                    out[found++] = dev;
                }
                node = node->next;
            }
            return found;
        }

        /// @brief Iterate all devices with a callback.
        template <typename Func>
        static void ForEach(Func&& func) noexcept {
            auto* node = s_all_devices.Begin();
            while (node != s_all_devices.End()) {
                auto* next = node->next;
                DeviceNode* dev = Structure::ContainerOf<
                    DeviceNode, &DeviceNode::global_node>(node);
                func(*dev);
                node = next;
            }
        }

        // =====================================================================
        // Power management
        // =====================================================================

        /// @brief Suspend all active devices (leaves first).
        static KernelResult<void> SuspendAll(DevicePowerState target) noexcept {
            FK_BUG_ON(s_root == nullptr,
                "DeviceManager::SuspendAll: not initialised");
            return PowerManager::SuspendSubtree(*s_root, target);
        }

        /// @brief Resume all suspended devices (root first).
        static KernelResult<void> ResumeAll(DevicePowerState target) noexcept {
            FK_BUG_ON(s_root == nullptr,
                "DeviceManager::ResumeAll: not initialised");
            return PowerManager::ResumeSubtree(*s_root, target);
        }

        /// @brief Emergency shutdown all devices.
        static void ShutdownAll() noexcept {
            FK_BUG_ON(s_root == nullptr,
                "DeviceManager::ShutdownAll: not initialised");
            // Depth-first shutdown: leaves before parents.
            ForEach([](DeviceNode& dev) noexcept {
                if (dev.IsBound() && dev.bound_driver->ops.Shutdown) {
                    FK_LOG_INFO("DeviceManager: shutting down device[{}] '{}'",
                        dev.device_id, dev.name ? dev.name : "<unnamed>");
                    dev.bound_driver->ops.Shutdown(dev);
                }
            });
        }

        // =====================================================================
        // Diagnostics
        // =====================================================================

        /// @brief Dump the entire device tree to FK_LOG_INFO.
        static void DumpTree() noexcept {
            FK_BUG_ON(s_root == nullptr,
                "DeviceManager::DumpTree: not initialised");

            FK_LOG_INFO("=== FoundationKitDevice Tree ({} devices, {} drivers, {} bus types) ===",
                s_device_count, DriverRegistry::Count(), s_bus_count);

            DumpSubtree(s_root, 0);

            FK_LOG_INFO("=== End Device Tree ===");
        }

        /// @brief Total number of devices in the tree.
        [[nodiscard]] static usize DeviceCount() noexcept { return s_device_count; }

        /// @brief The root node.
        [[nodiscard]] static DeviceNode* Root() noexcept { return s_root; }

        /// @brief Restores the manager to its initial uninitialised state.
        ///
        /// @desc  Useful for soft reboots (e.g., kexec), complete subsystem
        ///        re-initialisation, or fast teardown during kernel panic.
        static void ResetSubsystem() noexcept {
            s_root = nullptr;
            s_device_count = 0;
            s_next_device_id = 1;
            s_bus_count = 0;
            s_all_devices.Reset();
        }

    private:
        /// @brief Allocate a new unique device ID. Thread-safe.
        static u32 AllocateDeviceId() noexcept {
            // Simple monotonic counter. Acceptable for device IDs since they
            // are never freed/reused in practice.
            const u32 id = s_next_device_id;
            ++s_next_device_id;
            FK_BUG_ON(s_next_device_id == 0,
                "DeviceManager: device ID counter wrapped to zero — "
                "more than 4 billion devices created (impossible, this is corruption)");
            return id;
        }

        /// @brief Recursive tree dump with indentation.
        static void DumpSubtree(const DeviceNode* node, u32 depth) noexcept {
            if (!node) return;

            // Build indent: 2 spaces per depth level.
            char indent[65] = {};
            const u32 indent_len = depth * 2 < 64 ? depth * 2 : 64;
            for (u32 i = 0; i < indent_len; ++i) indent[i] = ' ';

            FK_LOG_INFO("{}{}", indent, "");
            node->DumpInfo();

            // Iterate children.
            auto* child_node = node->children.Begin();
            while (child_node != const_cast<DeviceNode*>(node)->children.End()) {
                DeviceNode* child = Structure::ContainerOf<
                    DeviceNode, &DeviceNode::sibling_node>(child_node);
                child_node = child_node->next;
                DumpSubtree(child, depth + 1);
            }
        }

        // --- Static state (defined in DeviceManager.cpp) ---
        static DeviceNode*       s_root;
        static Structure::IntrusiveDoublyLinkedList s_all_devices;
        static usize             s_device_count;
        static u32               s_next_device_id;
        static BusDescriptor*    s_buses[kMaxBusTypes];
        static usize             s_bus_count;
    };

} // namespace FoundationKitDevice
