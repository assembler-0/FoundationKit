#pragma once

namespace FoundationKitCxxStl::Osl {

    extern "C" {
        /// @brief Reports a fatal bug and halts execution.
        /// @param msg The message to display.
        [[noreturn]] void FoundationKitCxxStlOslBug(const char* msg);

        /// @brief Logs a message to the kernel's logging system.
        /// @param msg The message to log.
        void FoundationKitCxxStlOslLog(const char* msg);

        /// @brief Checks if required CPU features (like SIMD for mem*) are enabled.
        /// @return True if features are enabled, false otherwise.
        bool FoundationKitCxxStlOslIsCpuFeaturesEnabled();
    }

} // namespace FoundationKitCxxStl::Osl
