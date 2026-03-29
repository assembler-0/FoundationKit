#pragma once

namespace FoundationKit::Osl {

    extern "C" {
        /// @brief Reports a fatal bug and halts execution.
        /// @param msg The message to display.
        [[noreturn]] void FoundationKitOslBug(const char* msg);

        /// @brief Logs a message to the kernel's logging system.
        /// @param msg The message to log.
        void FoundationKitOslLog(const char* msg);

        /// @brief Checks if required CPU features (like SIMD for mem*) are enabled.
        /// @return True if features are enabled, false otherwise.
        bool FoundationKitOslIsCpuFeaturesEnabled();
    }

} // namespace FoundationKit::Osl
