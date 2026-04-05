#pragma once

#include <FoundationKitCxxStl/Base/Expected.hpp>
#include <FoundationKitCxxStl/Base/Format.hpp>

namespace FoundationKitCxxStl {

    /// @brief Canonical kernel error codes used across all FoundationKit subsystems.
    ///
    /// Replaces scattered MemoryError / raw bool returns with a single, consistent
    /// error vocabulary. All subsystem APIs that can fail return KernelResult<T>.
    enum class KernelError : u32 {
        OutOfMemory      = 0,  ///< Allocator exhausted or no physical pages available.
        InvalidArgument  = 1,  ///< Caller supplied a value outside the documented contract.
        NotFound         = 2,  ///< Requested resource, key, or descriptor does not exist.
        PermissionDenied = 3,  ///< CPL/DPL check failed or capability not held.
        Timeout          = 4,  ///< Operation did not complete within the deadline.
        DeviceBusy       = 5,  ///< Hardware resource is locked by another owner.
        NotSupported     = 6,  ///< Feature absent on this CPU/platform/configuration.
        Overflow         = 7,  ///< Arithmetic or buffer overflow detected.
    };

    /// @brief Convenience alias: a kernel operation that returns T or a KernelError.
    template <typename T>
    using KernelResult = Expected<T, KernelError>;

    template <>
    struct Formatter<KernelError> {
        template <typename Sink>
        void Format(Sink& sb, const KernelError& e, const FormatSpec& = {}) {
            const char* s = nullptr;
            switch (e) {
                case KernelError::OutOfMemory:      s = "OutOfMemory";      break;
                case KernelError::InvalidArgument:  s = "InvalidArgument";  break;
                case KernelError::NotFound:         s = "NotFound";         break;
                case KernelError::PermissionDenied: s = "PermissionDenied"; break;
                case KernelError::Timeout:          s = "Timeout";          break;
                case KernelError::DeviceBusy:       s = "DeviceBusy";       break;
                case KernelError::NotSupported:     s = "NotSupported";     break;
                case KernelError::Overflow:         s = "Overflow";         break;
            }
            usize len = 0;
            if (s) { while (s[len]) ++len; sb.Append(s, len); }
        }
    };

} // namespace FoundationKitCxxStl
