#pragma once

#include <FoundationKitCxxStl/Base/Bug.hpp>
#include <FoundationKitCxxStl/Base/Span.hpp>
#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitDevice/Driver/DriverDescriptor.hpp>

namespace FoundationKitDevice {

    using namespace FoundationKitCxxStl;

    // Forward declaration.
    struct DeviceNode;

    // =========================================================================
    // DriverRegistry — static fixed-capacity registry of compiled-in drivers
    //
    // No heap allocation. The registry is a flat array of pointers to
    // statically-declared DriverDescriptors. Registration is boot-time only.
    //
    // Match logic:
    //   1. For each driver, check if target_class matches (or is Unknown = any).
    //   2. Iterate the driver's match_table:
    //      a. If compatible is non-null, compare against the device's
    //         "compatible" property.
    //      b. If vendor_id is non-zero, compare against the device's
    //         "vendor-id" property.
    //      c. If all specified fields match, the driver matches.
    //   3. If multiple drivers match, the one with the highest priority wins.
    // =========================================================================

    static constexpr usize kMaxDrivers = 128;

    class DriverRegistry {
    public:
        DriverRegistry() = delete; // Static-only API.

        /// @brief Register a compiled-in driver.
        ///
        /// @desc  Must be called during early boot (single-threaded).
        ///        FK_BUG_ON on overflow, null descriptor, or null name.
        ///
        /// @param desc  Reference to a static DriverDescriptor.
        static void Register(DriverDescriptor& desc) noexcept {
            FK_BUG_ON(desc.name == nullptr,
                "DriverRegistry::Register: driver has null name");
            FK_BUG_ON(desc.ops.Probe == nullptr && desc.ops.Attach == nullptr,
                "DriverRegistry::Register: driver '{}' has neither Probe nor Attach — "
                "at least one lifecycle op is required",
                desc.name);
            FK_BUG_ON(s_count >= kMaxDrivers,
                "DriverRegistry::Register: overflow — {} drivers already registered "
                "(max {}). Driver '{}' cannot be added.",
                s_count, kMaxDrivers, desc.name);

            // Check for duplicate registration.
            for (u32 i = 0; i < s_count; ++i) {
                FK_BUG_ON(s_drivers[i] == &desc,
                    "DriverRegistry::Register: driver '{}' registered twice",
                    desc.name);
            }

            s_drivers[s_count] = &desc;
            ++s_count;

            FK_LOG_INFO("DriverRegistry: registered driver '{}' (class={}, priority={}, {} match entries)",
                desc.name, DeviceClassName(desc.target_class), desc.priority, desc.match_count);
        }

        /// @brief Return a Span over all registered drivers.
        [[nodiscard]] static Span<DriverDescriptor*> All() noexcept {
            return Span<DriverDescriptor*>(s_drivers, s_count);
        }

        /// @brief Number of registered drivers.
        [[nodiscard]] static u32 Count() noexcept {
            return s_count;
        }

        /// @brief Find the best-matching driver for a device.
        ///
        /// @desc  Iterates all registered drivers and returns the highest-
        ///        priority match. Returns nullptr if no driver matches.
        ///
        ///        Matching algorithm:
        ///        1. target_class check (Unknown = wildcard).
        ///        2. For each match_table entry:
        ///           a. compatible string match (if non-null).
        ///           b. vendor_id / device_id match (if non-zero).
        ///        3. Highest priority wins among all matches.
        ///
        /// @param dev              The device to match.
        /// @param out_match_entry  [out] If non-null and a match is found,
        ///                         set to the MatchEntry that matched.
        /// @return Pointer to the best-matching DriverDescriptor, or nullptr.
        [[nodiscard]] static DriverDescriptor* FindMatch(
                const DeviceNode& dev,
                const MatchEntry** out_match_entry = nullptr) noexcept;

        /// @brief Restores the registry to its initial empty state.
        ///
        /// @desc  Useful for soft reboots (e.g., kexec) or complete subsystem
        ///        re-initialisation during panic fallback flows.
        static void ResetSubsystem() noexcept {
            for (u32 i = 0; i < s_count; ++i) s_drivers[i] = nullptr;
            s_count = 0;
        }

    private:
        static DriverDescriptor* s_drivers[kMaxDrivers];
        static u32 s_count;
    };

} // namespace FoundationKitDevice
