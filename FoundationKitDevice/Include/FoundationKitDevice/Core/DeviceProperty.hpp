#pragma once

#include <FoundationKitCxxStl/Base/Bug.hpp>
#include <FoundationKitCxxStl/Base/Hash.hpp>
#include <FoundationKitCxxStl/Base/StringView.hpp>
#include <FoundationKitCxxStl/Base/Types.hpp>

namespace FoundationKitDevice {

    using namespace FoundationKitCxxStl;

    // =========================================================================
    // DevicePropertyType — discriminated union tag
    // =========================================================================

    enum class DevicePropertyType : u8 {
        None   = 0,
        U64    = 1,
        I64    = 2,
        String = 3,
        Bool   = 4,
        Blob   = 5,   ///< Raw bytes with explicit length.
    };

    // =========================================================================
    // DevicePropertyValue — the payload union
    // =========================================================================

    union DevicePropertyValue {
        u64         as_u64;
        i64         as_i64;
        const char* as_string;
        bool        as_bool;
        struct {
            const void* ptr;
            usize       len;
        } as_blob;

        constexpr DevicePropertyValue() noexcept : as_u64(0) {}
    };

    // =========================================================================
    // DevicePropertyEntry — one key-value pair
    // =========================================================================

    struct DevicePropertyEntry {
        u32                 key_hash = 0;        ///< FNV-1a hash of the key name.
        const char*         key_name = nullptr;  ///< Source-lifetime key (e.g., "compatible").
        DevicePropertyType  type     = DevicePropertyType::None;
        DevicePropertyValue value{};
    };

    // =========================================================================
    // Hash helper — FNV-1a for property key lookup
    // =========================================================================

    /// @brief Compute FNV-1a hash of a null-terminated string.
    ///
    /// @desc  Used for O(1) property key comparison. FNV-1a has excellent
    ///        distribution for short ASCII strings (device property names).
    ///        Deliberately constexpr so compile-time property lookups are free.
    [[nodiscard]] constexpr u32 PropertyKeyHash(const char* key) noexcept {
        u32 hash = 2166136261u; // FNV offset basis
        while (*key) {
            hash ^= static_cast<u32>(static_cast<u8>(*key));
            hash *= 16777619u;  // FNV prime
            ++key;
        }
        return hash;
    }

    // =========================================================================
    // DevicePropertyStore — inline fixed-capacity property dictionary
    //
    // No heap allocation. No dynamic growth. If a device needs more than
    // kMaxDeviceProperties, bump the constant — do not silently drop.
    // =========================================================================

    static constexpr usize kMaxDeviceProperties = 32;

    class DevicePropertyStore {
    public:
        constexpr DevicePropertyStore() noexcept = default;

        // --- Setters ---

        /// @brief Set a u64 property.
        /// @param key  Null-terminated, source-lifetime key.
        /// @param val  The value.
        void SetU64(const char* key, u64 val) noexcept {
            FK_BUG_ON(key == nullptr, "DevicePropertyStore::SetU64: null key");
            DevicePropertyEntry& e = FindOrCreate(key);
            e.type = DevicePropertyType::U64;
            e.value.as_u64 = val;
        }

        /// @brief Set an i64 property.
        void SetI64(const char* key, i64 val) noexcept {
            FK_BUG_ON(key == nullptr, "DevicePropertyStore::SetI64: null key");
            DevicePropertyEntry& e = FindOrCreate(key);
            e.type = DevicePropertyType::I64;
            e.value.as_i64 = val;
        }

        /// @brief Set a string property (source-lifetime — not copied).
        void SetString(const char* key, const char* val) noexcept {
            FK_BUG_ON(key == nullptr, "DevicePropertyStore::SetString: null key");
            FK_BUG_ON(val == nullptr, "DevicePropertyStore::SetString: null value for key '{}'", key);
            DevicePropertyEntry& e = FindOrCreate(key);
            e.type = DevicePropertyType::String;
            e.value.as_string = val;
        }

        /// @brief Set a boolean property.
        void SetBool(const char* key, bool val) noexcept {
            FK_BUG_ON(key == nullptr, "DevicePropertyStore::SetBool: null key");
            DevicePropertyEntry& e = FindOrCreate(key);
            e.type = DevicePropertyType::Bool;
            e.value.as_bool = val;
        }

        /// @brief Set a raw blob property (source-lifetime — not copied).
        void SetBlob(const char* key, const void* ptr, usize len) noexcept {
            FK_BUG_ON(key == nullptr, "DevicePropertyStore::SetBlob: null key");
            FK_BUG_ON(ptr == nullptr && len > 0,
                "DevicePropertyStore::SetBlob: null pointer with non-zero length for key '{}'", key);
            DevicePropertyEntry& e = FindOrCreate(key);
            e.type = DevicePropertyType::Blob;
            e.value.as_blob.ptr = ptr;
            e.value.as_blob.len = len;
        }

        // --- Getters ---

        /// @brief Get a u64 property. FK_BUG_ON if not found or wrong type.
        [[nodiscard]] u64 GetU64(const char* key) const noexcept {
            const DevicePropertyEntry* e = Find(key);
            FK_BUG_ON(e == nullptr,
                "DevicePropertyStore::GetU64: key '{}' not found", key);
            FK_BUG_ON(e->type != DevicePropertyType::U64,
                "DevicePropertyStore::GetU64: key '{}' has type {} (expected U64)",
                key, static_cast<u8>(e->type));
            return e->value.as_u64;
        }

        [[nodiscard]] i64 GetI64(const char* key) const noexcept {
            const DevicePropertyEntry* e = Find(key);
            FK_BUG_ON(e == nullptr,
                "DevicePropertyStore::GetI64: key '{}' not found", key);
            FK_BUG_ON(e->type != DevicePropertyType::I64,
                "DevicePropertyStore::GetI64: key '{}' has type {} (expected I64)",
                key, static_cast<u8>(e->type));
            return e->value.as_i64;
        }

        [[nodiscard]] const char* GetString(const char* key) const noexcept {
            const DevicePropertyEntry* e = Find(key);
            FK_BUG_ON(e == nullptr,
                "DevicePropertyStore::GetString: key '{}' not found", key);
            FK_BUG_ON(e->type != DevicePropertyType::String,
                "DevicePropertyStore::GetString: key '{}' has type {} (expected String)",
                key, static_cast<u8>(e->type));
            return e->value.as_string;
        }

        [[nodiscard]] bool GetBool(const char* key) const noexcept {
            const DevicePropertyEntry* e = Find(key);
            FK_BUG_ON(e == nullptr,
                "DevicePropertyStore::GetBool: key '{}' not found", key);
            FK_BUG_ON(e->type != DevicePropertyType::Bool,
                "DevicePropertyStore::GetBool: key '{}' has type {} (expected Bool)",
                key, static_cast<u8>(e->type));
            return e->value.as_bool;
        }

        // --- Optional getters (no crash on missing) ---

        /// @brief Try to get a u64 property.
        /// @return true if found and type matches.
        [[nodiscard]] bool TryGetU64(const char* key, u64& out) const noexcept {
            const DevicePropertyEntry* e = Find(key);
            if (!e || e->type != DevicePropertyType::U64) return false;
            out = e->value.as_u64;
            return true;
        }

        [[nodiscard]] bool TryGetString(const char* key, const char*& out) const noexcept {
            const DevicePropertyEntry* e = Find(key);
            if (!e || e->type != DevicePropertyType::String) return false;
            out = e->value.as_string;
            return true;
        }

        [[nodiscard]] bool TryGetBool(const char* key, bool& out) const noexcept {
            const DevicePropertyEntry* e = Find(key);
            if (!e || e->type != DevicePropertyType::Bool) return false;
            out = e->value.as_bool;
            return true;
        }

        // --- Lookup ---

        /// @brief Check if a property exists.
        [[nodiscard]] bool Has(const char* key) const noexcept {
            return Find(key) != nullptr;
        }

        /// @brief Remove a property by key.
        /// @return true if the property existed and was removed.
        bool Remove(const char* key) noexcept {
            FK_BUG_ON(key == nullptr, "DevicePropertyStore::Remove: null key");
            const u32 hash = PropertyKeyHash(key);
            for (usize i = 0; i < m_count; ++i) {
                if (m_entries[i].key_hash == hash && KeysEqual(m_entries[i].key_name, key)) {
                    // Swap with last to keep array compact.
                    m_entries[i] = m_entries[m_count - 1];
                    m_entries[m_count - 1] = {};
                    --m_count;
                    return true;
                }
            }
            return false;
        }

        /// @brief Number of properties currently stored.
        [[nodiscard]] usize Count() const noexcept { return m_count; }

        /// @brief Direct access to the entry array for iteration.
        [[nodiscard]] const DevicePropertyEntry* Entries() const noexcept { return m_entries; }

    private:
        DevicePropertyEntry m_entries[kMaxDeviceProperties]{};
        usize m_count = 0;

        /// @brief Find an existing entry by key.
        [[nodiscard]] const DevicePropertyEntry* Find(const char* key) const noexcept {
            FK_BUG_ON(key == nullptr, "DevicePropertyStore::Find: null key");
            const u32 hash = PropertyKeyHash(key);
            for (usize i = 0; i < m_count; ++i) {
                if (m_entries[i].key_hash == hash && KeysEqual(m_entries[i].key_name, key)) {
                    return &m_entries[i];
                }
            }
            return nullptr;
        }

        /// @brief Find an existing entry or create a new one. FK_BUG_ON on overflow.
        DevicePropertyEntry& FindOrCreate(const char* key) noexcept {
            const u32 hash = PropertyKeyHash(key);

            // Check for existing key — overwrite.
            for (usize i = 0; i < m_count; ++i) {
                if (m_entries[i].key_hash == hash && KeysEqual(m_entries[i].key_name, key)) {
                    return m_entries[i];
                }
            }

            // New key — append.
            FK_BUG_ON(m_count >= kMaxDeviceProperties,
                "DevicePropertyStore: overflow — {} properties already stored "
                "(max {}). Key '{}' cannot be added. "
                "Bump kMaxDeviceProperties if legitimate.",
                m_count, kMaxDeviceProperties, key);

            DevicePropertyEntry& e = m_entries[m_count];
            e.key_hash = hash;
            e.key_name = key;
            ++m_count;
            return e;
        }

        /// @brief Byte-wise string comparison (no libc dependency).
        [[nodiscard]] static constexpr bool KeysEqual(const char* a, const char* b) noexcept {
            if (a == b) return true;
            if (!a || !b) return false;
            while (*a && *b) {
                if (*a != *b) return false;
                ++a; ++b;
            }
            return *a == *b;
        }
    };

} // namespace FoundationKitDevice
