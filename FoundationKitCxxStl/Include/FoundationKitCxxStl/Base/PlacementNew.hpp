#pragma once

// ============================================================================
// Placement New Operators
// ============================================================================
// The canonical definitions of non-allocating placement new/delete live in
// FoundationKitCxxAbi/Src/OperatorNew.cpp (always unconditionally defined).
// This header provides the global declarations required by the compiler.
// ============================================================================

[[nodiscard]] void* operator new(unsigned long, void* p) noexcept;
[[nodiscard]] void* operator new[](unsigned long, void* p) noexcept;
void operator delete(void*, void*) noexcept;
void operator delete[](void*, void*) noexcept;