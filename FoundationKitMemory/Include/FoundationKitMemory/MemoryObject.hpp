#pragma once

#include <FoundationKitMemory/MemoryCore.hpp>

namespace FoundationKitMemory {

    using namespace FoundationKitCxxStl;

    // =========================================================================
    // MemoryObjectType — user-extensible type tag
    // =========================================================================

    /// @brief Type tag for all managed objects.
    /// @desc  Values 0x0000–0x00FF are reserved by FoundationKit.
    ///        Values 0x0100–0xFFFF are available for user-defined types.
    enum class MemoryObjectType : u16 {
        Unknown       = 0x0000,
        KernelStack   = 0x0001,
        PageTable     = 0x0002,
        TaskControl       = 0x0003,
        NetworkBuffer     = 0x0004,
        FileHandle        = 0x0005,
        IrqContext        = 0x0006,
        VirtualMemoryArea = 0x0007,
        VmObject      = 0x0008,
        UserBase      = 0x0100,
    };

    // =========================================================================
    // MemoryObjectFlags
    // =========================================================================

    enum class MemoryObjectFlags : u16 {
        None        = 0,
        Zeroed      = 1 << 0, ///< Zero-initialise payload before construction.
        Pinned      = 1 << 1, ///< Must not be reclaimed under memory pressure.
        DmaCoherent = 1 << 2, ///< Must not be cached in a write-back region.
        Executable  = 1 << 3, ///< Mapped in executable memory.
    };

    [[nodiscard]] constexpr MemoryObjectFlags operator|(MemoryObjectFlags a, MemoryObjectFlags b) noexcept {
        return static_cast<MemoryObjectFlags>(static_cast<u16>(a) | static_cast<u16>(b));
    }

    [[nodiscard]] constexpr bool HasFlag(MemoryObjectFlags flags, MemoryObjectFlags flag) noexcept {
        return (static_cast<u16>(flags) & static_cast<u16>(flag)) != 0;
    }

    // =========================================================================
    // MemoryObjectBase<Tag> — compile-time type-tag mixin
    // =========================================================================

    /// @brief Mixin that binds a C++ type to a fixed MemoryObjectType tag.
    /// @desc  Derive from this to satisfy IMemoryObject<T>.
    ///
    /// @example
    ///     struct TaskControlBlock
    ///         : MemoryObjectBase<MemoryObjectType::TaskControl> { ... };
    template <MemoryObjectType Tag>
    struct MemoryObjectBase {
        static constexpr MemoryObjectType kObjectType = Tag;
    };

    // =========================================================================
    // IMemoryObject concept
    // =========================================================================

    /// @brief Concept: T exposes a static constexpr MemoryObjectType kObjectType.
    template <typename T>
    concept IMemoryObject = requires {
        { T::kObjectType } -> SameAs<const MemoryObjectType&>;
    };

    // =========================================================================
    // Compile-time tag accessor
    // =========================================================================

    template <IMemoryObject T>
    inline constexpr MemoryObjectType ObjectTypeOf = T::kObjectType;

} // namespace FoundationKitMemory
