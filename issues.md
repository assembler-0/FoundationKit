# FoundationKit Memory Architecture — Comprehensive Audit & Reinforcement Plan

> **Scope:** `FoundationKitMemory` (all subsystems) + `FoundationKitPlatform::Amd64` PTM.  
> **Standard:** C++23 Freestanding, `-fno-exceptions`, `-fno-rtti`.  
> **Method:** PHASE I → II → III → IV as mandated.

---

## PHASE I — REQUIREMENTS ANALYSIS

### 1.0 What FoundationKitCxxAbi Already Provides

Before listing gaps, a critical baseline: `FoundationKitCxxAbi` is a **fully implemented freestanding ABI layer**. The following are already covered and must NOT be re-implemented or mandated in the memory plan:

| Symbol / Feature | Implementation | Notes |
|---|---|---|
| `operator new` / `operator new[]` | `OperatorNew.cpp` — traps by default; bridges `GlobalAllocator` via `FOUNDATIONKITCXXABI_BRIDGE_GLOBAL_ALLOCATOR` | Placement-new overloads also provided |
| `operator delete` / `operator delete[]` | `OperatorNew.cpp` | Sized and unsized forms, both provided |
| `__cxa_pure_virtual` / `__cxa_deleted_virtual` | `CoreAbi.cpp` — calls `FK_BUG` (traps) | No vtable-dispatch runtime required |
| `__cxa_bad_typeid` / `__cxa_bad_cast` | `CoreAbi.cpp` — traps | Already safe for `-fno-rtti` objects that slip through |
| `__cxa_throw` / `__cxa_rethrow` / catch machinery | `CoreAbi.cpp` — all trap | Fully covers `-fno-exceptions` ABI surface |
| `__cxa_guard_acquire/release/abort` | `GuardAbi.cpp` — spin-safe, SMP-correct, deadlock-detecting | Uses `__atomic_*` builtins; `FOUNDATIONKITCXXABI_GUARD_USE_OSL_YIELD` for scheduler-aware spin |
| `__cxa_atexit` / `__cxa_finalize` | `InitAbi.cpp` — static table, SpinLock-protected | Max entries via `FOUNDATIONKITCXXABI_ATEXIT_MAX` (default 256) |
| `__gxx_personality_v0` | `Personality.cpp` — traps | Covers EH unwind ABI symbols |
| `__cxa_demangle` | `DemangleAbi.cpp` — full Itanium demangler, no malloc | Caller supplies buffer |
| `Sync::Atomic<T>` | `Atomic.hpp` — full suite via `__atomic_*` builtins | Load/Store/CAS/FetchAdd/FetchOr/FetchAnd/FetchXor |
| `Sync::SpinLock` | `SpinLock.hpp` — `AtomicFlag`-based TAS lock | Works in ISRs, no scheduler dependency |
| `Sync::SharedSpinLock` | `SharedSpinLock.hpp` — multi-reader/single-writer | CAS-based, no scheduler dependency |
| `Sync::UniqueLock` / `LockGuard` | `Locks.hpp` | RAII guards over any `BasicLockable` |
| `CompilerBuiltins::FullMemoryBarrier/AcquireBarrier/ReleaseBarrier` | `CompilerBuiltins.hpp` — `__atomic_thread_fence(...)` | Full hardware fence suite available |
| `CompilerBuiltins::MemCpy/MemSet/MemMove` | `CompilerBuiltins.hpp` — `__builtin_memcpy/memset/memmove` | Zero libc dependency |
| Global constructors / destructors | `InitAbi.cpp` — `RunGlobalConstructors` / `RunGlobalDestructors` | Walks `__init_array` / `__fini_array` |

> **Implication for the plan:** Any fix that mandates a memory barrier should use `CompilerBuiltins::FullMemoryBarrier()` or `ReleaseBarrier()` — **not** an inline `asm volatile("mfence")`, since the builtins are already architecture-portable (x86_64/ARM64/RISC-V). AMD64-specific `MFENCE` in `Amd64PageTableManager` is a valid exception since that file is already arch-specific.
>
> Any fix involving `Sync::Atomic<T>`, `SpinLock`, `UniqueLock`, etc. can directly use the existing types — **no new synchronization primitives are needed**.

---

### 1.1 Host-Leak Inventory — Remaining Issues

With the ABI layer accounted for, the remaining hosted leaks are:

| # | Location | Issue | Severity |
|---|---|---|---|
| H-1 | `MemoryCore.hpp:183` | `BasicMemoryResource` uses `virtual` dispatch — emits a `vptr` in every instance and a `typeinfo` record for the class. Even with `-fno-rtti`, the compiler still emits the vtable (rtti-exempt but vtable is still there); more importantly, **any translation unit that includes this header will link `__cxa_pure_virtual` for the pure-virtual methods, embedding hosted ABI dependency**. The ABI provides the trap stub, but the design intent of a freestanding kernel allocator should be zero-vtable. This is a **design violation**, not a missing ABI symbol. | 🔴 DESIGN CRITICAL |
| H-2 | `MemoryCore.hpp:186–236` | The `virtual` methods in `BasicMemoryResource` force every concrete object deriving from it to embed a `vptr` (8 bytes / object on AMD64). For allocators used as per-CPU pool objects or embedded in lock-free data structures, this vptr creates cache-line pressure and prevents the struct from being trivially copyable / atomically swapped. | 🟠 HIGH |
| H-3 | `VirtualAddressSpace.hpp:295` | `goto check_trailing` — not a hosted leak, but `goto` is banned by most kernel coding standards when the target is not an error-cleanup label at function end. The `FindFreeFrom()` function should be restructured. | 🟡 LOW |
| H-4 | `Amd64PageTableManager.hpp:138` | `Promote()` returns `false` unconditionally — a silent stub. Should use `FK_BUG` or be removed from the concept. The ABI provides `FK_BUG` via `CoreAbi.cpp`'s `__cxa_pure_virtual` trap, but this is user code, not ABI. | 🟠 HIGH |

### 1.2 Concept Constraints Required

The following concepts are **missing or incomplete** from `VmmConcepts.hpp`. Note that the **synchronization primitives** (`SpinLock`, `SharedSpinLock`, `Atomic`, `UniqueLock`) needed to satisfy these concepts are **already implemented** in `FoundationKitCxxStl::Sync` — these concepts only need to be defined, not backed by new implementations:

```
concept IPhysicalMemoryAccessor  // Missing — platform HHDM bridge (replaces raw PhysToVirt lambda)
concept IReclaimer               // Missing — ties ScanForReclaim to PageQueueSet
concept ISmpTlbShootdown         // Missing — TLB invalidation on SMP
concept IMemoryPressurePolicy    // Missing — decouples OOM policy from MemoryPressureManager
```

`IAddressSpaceAllocator` is **not needed** — AddressSpace already receives its dependencies via template parameters satisfying existing concepts.

### 1.3 Namespace Mapping

| File Path | Required Namespace | Actual Namespace | ✓/✗ |
|---|---|---|---|
| `Management/*.hpp` | `FoundationKitMemory` | `FoundationKitMemory` | ✓ |
| `Allocators/*.hpp` | `FoundationKitMemory` | `FoundationKitMemory` | ✓ |
| `Heap/*.hpp` | `FoundationKitMemory` | `FoundationKitMemory` | ✓ |
| `Support/*.hpp` | `FoundationKitMemory` | `FoundationKitMemory` | ✓ |
| `Ptr/*.hpp` | `FoundationKitMemory` | `FoundationKitMemory` | ✓ |
| `Platform/Amd64/Amd64PageTableManager.hpp` | `FoundationKitPlatform::Amd64` | `FoundationKitPlatform::Amd64` | ✓ |

---

## PHASE II — INTERFACE DEFINITION (BUGS & GAPS, BY LAYER)

---

### Layer 1: `AddressTypes.hpp` — Missing `AlignedPfn` / `HhdmOffset` types

**Bug A-1:** `kPageSize` is hardcoded as `constexpr usize = 4096`.  
For a system claiming to support ARM64 / RISC-V / 16K/64K page kernels, this is a **fatal architecture assumption** baked into the lowest common type file. Every `>> kPageShift` throughout `AddressTypes.hpp`, `PageFrameAllocator`, `PageDescriptorArray`, and `Amd64PageTableManager` embeds this hardcoded 4K constant.  

**Bug A-2:** `PhysicalAddress` and `VirtualAddress` carry no sanity upper-bound checks. A physically impossible address like `0xFFFF_FFFF_FFFF_F000` on a 48-bit AMD64 machine is accepted without a single complaint.

**Bug A-3:** No `HhdmOffset` strong type. The platform communicates its HHDM via a raw `uptr` passed as `PhysToVirt` to `Amd64PageTableManager`. This is the **exact origin** of the physical-page access mechanism being purely convention-based — drift from this convention produces silent memory corruption, not a crash.

---

### Layer 2: `PageDescriptor.hpp` — Missing atomicity on state transitions

**Bug B-1:** `PageDescriptor::TransitionTo()` is **not atomic**. On SMP, two CPUs can race on the state field:
```cpp
// CPU 0: fault handler calling Activate() → sets state = Active
// CPU 1: reclaim thread calling Deactivate() → reads state = Active ✓ → sets Inactive
//        ...but CPU 0 already queued the page into m_active, now it is in m_inactive.
// RESULT: page is queued twice (Active + Inactive) → queue corruption → FREE + REUSE loop.
```
`state` must be `Sync::Atomic<u8>` or all access must be protected by a caller-side lock enforced via concept constraints.

**Bug B-2:** `PageDescriptor::flags` (`PageFlags`) uses non-atomic `u16` bitfield. `SetFlag()` / `ClearFlag()` are non-atomic read-modify-write. On SMP:
```cpp
// CPU 0: head->SetFlag(Dirty)       → RMW on non-atomic flags
// CPU 1: head->ClearFlag(Referenced) → RMW on non-atomic flags
// RESULT: one update is silently lost. "Referenced" stays set or "Dirty" is cleared.
```

**Bug B-3:** `PageDescriptor::owner` (raw `VmObject*`) and `owner_offset` are non-atomic. A racing `ClearOwner()` and `SetOwner()` produce a torn write visible to reverse-mapping lookups.

**Bug B-4:** `Folio::MapCountInc()` and `MapCountDec()` dispatch through the head, correctly. However `Folio::SetOwner()` writes directly to `head->owner` bypassing any lock — no memory barrier after the store, violating the observer contract for `Lookup()`.

---

### Layer 3: `PageQueue.hpp` — Critical lock omission

**Bug C-1:** `PageQueue` and `PageQueueSet` have **zero synchronization**. Every `Enqueue`, `Dequeue`, `Remove`, `ScanInactive`, `ScanActive` operates on the intrusive list without any lock or atomic. In an SMP scenario (workqueue thread + fault handler + reclaim thread), the `IntrusiveDoublyLinkedList` is trivially corrupted.

**Bug C-2:** `ScanInactive()`: after `Dequeue()`, the page state is still `Inactive` but the page is no longer in any queue. If the fault handler concurrently finds this page via `m_pd_array` and calls `m_queues.Activate()` → `QueueForState(page->state).Remove(page)` → `m_inactive.Remove(page)` → **UAF/double-remove** on the intrusive node.

**Bug C-3:** `ScanActive` / `ScanInactive` re-set `page->state = PageState::Active` **directly** (line 291, 299, 343) instead of calling `TransitionTo()`. This bypasses the state machine validation and creates invisible illegal transitions.

---

### Layer 4: `PageFrameAllocator.hpp` — Virtual ↔ PFN translation fragility

**Bug D-1:** `PageFrameAllocator::VirtToPfn()` and `PfnToVirt()` rely on the invariant that `virt_base` was set and that virtual memory is a **contiguous linear mapping** of the physical zone. If the HHDM is not identity-mapped (e.g. non-unity-mapped early boot, or a zone split across non-contiguous virtual regions), the arithmetic silently produces wrong PFNs.

**Bug D-2:** `PageFrameAllocator::FindZoneIndex(RegionType zone)` returns the **first** matching zone. If two zones of the same `RegionType` are registered (e.g. two `Generic` ranges from a discontiguous physical memory map), `AllocatePages` correctly iterates both but `SetZoneVirtualBase` and `FreePages` silently use only the first. Split zones of same type → silent corruption.

**Bug D-3:** `IsPowerOfTwo` is called in `AllocatePages` on line 123 but `IsPowerOfTwo` is not defined anywhere in the visible headers — it is presumably in `Math.hpp`. This is a missing explicit include dependency.

---

### Layer 5: `VmObject.hpp` — Shadow chain depth bomb & lock-ordering hazard

**Bug E-1:** `Lookup()` traverses the shadow chain without depth limiting:
```cpp
const VmObject* current = this;
while (current) { ... current = current->m_shadow.Get(); }
```
After `N` forks without `Collapse()`, the chain is O(N) deep. Every page fault on a deeply-forked process walks the entire chain under `SharedLock`. This is unbounded in latency and a denial-of-service vector.

**Bug E-2:** `Collapse()` holds `m_lock` (UniqueLock on `this`), then locks `parent->m_lock`. The lock ordering is documeted as "shadow → this" but the code **acquires `this` first, then parent**. If any other code path holds the parent lock and tries to acquire a child's lock (e.g. a scan walking the chain top-down), this is a **deadlock**.

**Bug E-3:** `Collapse()` checks `RefCount() != 1` but `RefCount()` uses `Relaxed` ordering. Between the check and the collapse body, another thread can grab a new reference to this object (e.g. another VMA creates a new shadow), making the collapse unsafe. Needs `AcqRel` CAS on ref_count.

**Bug E-4:** `InsertBlock()` calls `FK_BUG_ON(FindBlockContaining(page->offset) != nullptr, ...)` with `m_lock` held (UniqueLock). `FindBlockContaining()` reads `m_pages` — which is correct — but there is no FK_BUG ensuring `page->size_bytes` doesn't cause the new block to overlap a subsequent block. Only the start offset is checked, not the end.

---

### Layer 6: `VmFault.hpp` — Atomicity gap in CoW and PageIn

**Bug F-1:** `ResolveZeroFill()` / `ResolveCopyOnWrite()`: After `m_pfa.AllocatePages()` succeeds and before `m_ptm.Map()`, a fault on the same VA from another CPU results in **double allocation** of different physical pages for the same `(VmObject, offset)` pair. There is no lock held across the window between `Lookup()` returning empty and `InsertBlock()`.

**Bug F-2:** `ResolveCopyOnWrite()`: calls `m_ptm.Shatter(ctx.aligned_va)` then `m_ptm.Unmap(ctx.aligned_va, kPageSize)` then `m_ptm.Map(...)`. Between `Unmap` and `Map`, another CPU's TLB may still have a stale read-only PTE pointing to the source page. A load from that CPU would read the **old** (shared) page content after the copy has started, producing torn reads if the src page is also being written. An IPI shootdown is needed before the remap.

**Bug F-3:** `ResolvePageIn()`: Strips `Writable` from `map_flags` for shared pages (correct for CoW). However it then calls `m_ptm.Map()` — which returns `false` if the PTE already exists. The return value is **ignored**. If the page is already mapped (e.g. a race with another CPU's PageIn for the same VA), the map count increment has already happened — the page now has map_count incremented but is not actually mapped. Protected pages / map count mismatch.

---

### Layer 7: `AddressSpace.hpp` — Unmap, Fork, SplitVma

**Bug G-1:** `Unmap()`: The VMA iteration uses `next_vma = vas.Find(vma->End())`. If the unmap range contains large VMAs or fragmented descriptors, `Find(vma->End())` returns null if the next VMA doesn't start exactly at `vma->End()` (gap in address space). VMAs after the gap are silently skipped — partial unmap.

**Bug G-2:** `Fork()`: Child TLBs are not flushed after the parent PTEs are downgraded to read-only. The child address space uses the same physical PML4 root OR a new one (implementation unclear — `child.m_ptm` vs `m_ptm`). If `child.m_ptm` is the **same** object as `m_ptm` (aliased), `child.m_ptm.Map()` modifies the same page tables. This is architecturally underspecified.

**Bug G-3:** `SplitVmaInternal()`: calls `m_ptm.Shatter(split_point)` without checking the return value (`Expected<void, MemoryError>`). If Shatter fails (OOM while allocating new PT), the VMA is still split in software but the page table remains as a huge page — the `right` VMA's `backing_offset` is wrong, producing silent wrong-page faults on every access.

**Bug G-4:** `Protect()`: The inner loop calls `m_ptm.Translate(page_va)` + `m_ptm.Protect(page_va, ...)` per page. For a 1GB mapping this is 262,144 iterations. More critically `m_ptm.Protect()` on a huge-page entry changes the entire huge page, not just `page_va`'s 4K slice. The result is that a `mprotect(va, 4096)` on the head of a 2M VMA silently changes protection for the entire 2M region.

---

### Layer 8: `KernelMemoryManager.hpp` — Reclaim & double-free

**Bug H-1:** `ScanForReclaim()`: After `m_queues.ScanInactive()`, each candidate is transitioned to Free and its PFN freed via `m_pfa.FreePages()`. However, the page's reverse-map (`owner->VmObject`) is **not unlinked** and its PTE is **not unmapped**. The physical page is returned to the free pool but the VmObject still holds a `VmPage` at that offset pointing to the PFN — the next `Lookup()` for that offset returns a valid physical address for a **free page**. Classic use-after-free on physical frames.

**Bug H-2:** `AllocateKernel()` calls `MapAnonymous()` then immediately calls `m_ptm.Map()`. But `MapAnonymous()` already expects demand-paging — the pages will fault in lazily. The first eager call to `m_ptm.Map()` maps `pa` into the VMA's VA, but `MapAnonymous` did not record any `VmPage` in the backing `VmObject` for that offset. The VMA says it's anonymous/demand-paged, yet a PTE is manually installed. A subsequent `mprotect` → `Translate` will find the PTE but `VmObject::Lookup()` returns `NullOpt` — the manager is split-brained about whether the page is demand-paged or eager.

**Bug H-3:** `MemoryPressureManager::CheckAndReclaim()`: The "re-evaluate after reclaim" comment acknowledges it does **not re-query** the actual free page count after reclaim completes. If reclaim produced enough pages, the subsequent code block `if (level == Watermark::Min)` still fires and calls the OOM policy — a spurious OOM kill when memory is in fact available.

---

### Layer 9: `Amd64PageTableManager.hpp` — Critical SMP and flag issues

**Bug I-1:** **No SFENCE/MFENCE** after `MapAtLevel()` and after `Shatter()`. On AMD64, stores to page table entries require `MFENCE` + `INVLPG` (or at minimum `SFENCE` before `INVLPG`) to guarantee the CPU's page-table walker sees the new entry. Without a memory fence, the hardware walker can observe stale entries.

**Bug I-2:** `Shatter()` line 112: `~kPageMask1G` and `~kPageMask2M` are used to mask out the page offset — but these are the **page offset** masks (low bits), so `~kPageMask1G = ~(1G-1)`. The intent is to extract `phys_base` of the huge page. This is correct **only if** the entry's physical address field has already been extracted. The code calls `EntryPhysAddr(walk.entry) & ~kPageMask1G` — `EntryPhysAddr` should already return the page-aligned base. The masking is redundant but not wrong. However, for a 1G shatter producing 2M pages, the child should have `PageSize` set **in L2 entries** (level 2), but the code sets `PageSize` in child entries "if `walk.level - 1 > 1`" — for `walk.level == 3` → `walk.level - 1 == 2 > 1` → true, so L2 entries get `PageSize`. **This is correct.** But for `walk.level == 2` → `walk.level - 1 == 1`, which is NOT `> 1`, so L1 entries do NOT get `PageSize`. **This is also correct** for 4K pages. ✓ — No bug here on closer inspection.

**Bug I-3:** `Promote()` returns `false` silently (line 140). This means any write to a region that was `mprotect`'d write-only (which triggers a CoW → Shatter → eventual Promote to recombine the huge page) will **never** re-promote. Memory stays fragmented into 4K entries forever after the first CoW event. The concept says `Promote()` must be implemented — a silent stub is a **concept lie**.

**Bug I-4:** `MapAtLevel()` new page table zeroing:
```cpp
for (u32 i = 0; i < kPageTableEntries; ++i) new_table_ptr[i] = 0;
```
Uses a software zero loop instead of `MemoryZero()` which presumably uses `rep stosd`. On the hot path (every page fault allocation of a new PT) this wastes ~512 * 8 = 4KB of stores in a loop. Minor but measurable.

**Bug I-5:** `ToAmd64Flags()` does not handle `RegionFlags::Cacheable`. On AMD64, cache is on by default (PAT/PWT/PCD all 0). To disable caching (for device memory), you must set `PWT | PCD` or use PAT. Since the flag is silently ignored, `MapDevice()` maps MMIO as **cacheable** — reading a cacheable MMIO register produces wrong results from cache.

**Bug I-6:** `FlushTlbRange()` uses a per-page `INVLPG` loop. For large ranges, this is O(range / 4K). AMD64 provides `INVPCID` and — on sufficiently modern CPUs — `INVLPG` on a 2M-aligned address invalidates the 2M huge PTE directly. The current implementation always uses 4K granularity.

---

### Layer 10: `PhysicalMemoryMap.hpp` — XArray placement-new footgun

**Bug J-1:** `BuildPfnIndex()` line 123:
```cpp
new (&m_pfn_index) XArray<usize, Alloc>(alloc);
```
This placement-new runs on a `m_pfn_index` that was constructed in its default state. If `XArray::Clear()` was called (line 122), and `XArray` has a non-trivial destructor that frees internal nodes, calling Clear before the placement-new is **not** equivalent to calling the destructor. The stale internal pointers from the default-constructed `m_pfn_index` are leaked — they were never allocated (default ctor = empty), so this is only a logic smell, not a memory leak. **However**, if `BuildPfnIndex` is ever called a second time (denied by the FK_BUG_ON, but defensive analysis), the placement-new over a live XArray **skips the destructor** → memory leak. The `FK_BUG_ON(m_pfn_index_built, ...)` guard prevents this, but the code comment should call this out.

**Bug J-2:** `FindZone()` on line 67: `const usize pfn = reinterpret_cast<uptr>(ptr) >> kPageShift;` — `ptr` here is a `void*` virtual address, but the XArray is keyed by **physical** PFN. On a system where virtual != physical (which is every non-identity-mapped kernel), this lookup is **always wrong**. The physical PFN cannot be derived from a virtual pointer without going through `Translate()`.

---

### Layer 11: `VmaDescriptor.hpp` + `VirtualAddressSpace.hpp`

**Bug K-1:** `VmaDescriptor` has both `prot` (`VmaProt`) and `flags` (`RegionFlags`) — the comment says `flags` is "derived from prot + vma_flags". They can drift: `AddressSpace::Protect()` calls `vma->SetProtection(new_prot)` correctly, updates `vma->flags`, AND calls `m_ptm.Protect()`. But `SplitVmaInternal()` copies `flags` from the parent VMA directly — if the parent VMA had `flags` out of sync with `prot`, the split inherits the inconsistency silently.

**Bug K-2:** `VirtualAddressSpace::FindFreeFrom()` falls through to `check_trailing` via a `goto`. The label and goto could be replaced with a cleaner structure, but more importantly: the `O(log N)` complexity claim is overstated. The implementation falls back to an **O(N) in-order walk** when `root->subtree_max_gap >= size`. True O(log N) gap search requires following the augmented BST structure, not a linear scan.

---

### Layer 12: `MemoryPressureManager` + `ReclaimChain`

**Bug L-1:** `CheckAndReclaim()` passes `free_pages` **before** the allocation is committed. The docstring says "after the allocation has been tentatively deducted" — but `KernelMemoryManager::AllocatePage()` calls `m_pressure.CheckAndReclaim(m_pfa.FreePages(), 1)` where `m_pfa.FreePages()` returns the **current** free count before the allocation. The deduction happens **after** the check. So watermark is evaluated one step optimistically.

**Bug L-2:** `ReclaimChain` lambda trampoline:
```cpp
ReclaimFn trampoline = [](usize target, void* ctx) noexcept -> usize { ... };
```
This is a non-capturing lambda — converted to a function pointer correctly. ✓. However the lambda captures nothing, meaning the `ctx` void* MUST be the allocator pointer. If the allocator is moved (e.g. `m_chain` outlives the allocator address), `ctx` is dangling. No lifetime assertion. An `FK_BUG_ON` at `Reclaim` call time is needed: "participant pointer should be non-null."

---

## PHASE III — REFINEMENT & SIMPLIFICATION

### The "Cleverness" Check
- `VirtualAddressSpace::FindFreeFrom()` is overly complex AND incorrect. The O(log N) claim with `subtree_max_gap` is plausible with a correct recursive descent but the current implementation is essentially a linear walk with an early-exit. **Simplify to honest O(N) linear scan OR implement true O(log N) augmented search.**
- `Amd64PageTableManager::Map()` → `MapAtLevel()`: complex multi-level descent is correct and necessary. Not over-clever.
- `VmObject::Collapse()`: Locking two objects simultaneously is the right approach, but the ordering comment contradicts the code. **Must be fixed** to enforce strict `child → parent` (or `parent → child`) ordering documented with a big block comment.

### The "Freestanding" Check
- `BasicMemoryResource` uses `virtual` → **full rewrite required**, not optional.
- All `SharedPtr` / `WeakPtr` / control blocks use placement-new and function pointers → ✓ correct.
- `PageDescriptor` uses `Sync::Atomic<u32>` for `map_count` → ✓ correct.
- `PageDescriptor::state` and `flags` are plain non-atomic → ❌ must be atomic.
- `VmObject::m_lock` (SharedSpinLock) is used correctly with RAII guards → ✓.
- `ReclaimChain` uses a C-style function pointer table → ✓ correct.

---

## PHASE IV — FINAL IMPLEMENTATION PLAN

### P-2: Locking in `PageQueue` and `PageQueueSet`

#### [MODIFY] [PageQueue.hpp](file:///Users/assembler-0/CLionProjects/untitled1/FoundationKitMemory/Include/FoundationKitMemory/Management/PageQueue.hpp)

**Available primitives (already in FoundationKitCxxStl::Sync):**
- `SpinLock` — `AtomicFlag`-based TAS, ISR-safe
- `UniqueLock<SpinLock>` — RAII, movable
- `Atomic<usize>` — for lock-free count reads

Each `PageQueue` gains a `Sync::SpinLock m_lock` member. The `Count()` stat accessor becomes `Sync::Atomic<usize>` so it can be read lock-free. All structural operations (`Enqueue`, `Dequeue`, `Remove`) become `private` unlocked variants; `public` variants acquire the lock.

`PageQueueSet` transitions use **two separate per-queue lock acquisitions**, never held simultaneously (avoiding ABBA):

```cpp
void Activate(PageDescriptor* page) noexcept {
    FK_BUG_ON(page == nullptr, "PageQueueSet::Activate: null page");
    // Phase 1: remove from source queue (under source lock).
    auto& src = QueueForState(page->state.Load(Sync::MemoryOrder::Relaxed));
    {
        Sync::UniqueLock<Sync::SpinLock> src_guard(src.m_lock);
        src.RemoveUnlocked(page);
        page->TransitionTo(PageState::Active);  // atomic AcqRel store
    }  // src lock released
    // Phase 2: enqueue into destination queue (under dest lock).
    {
        Sync::UniqueLock<Sync::SpinLock> dst_guard(m_active.m_lock);
        m_active.EnqueueUnlocked(page);
    }
}
```

> **Why per-queue, not one global lock?** A single global `SpinLock` for all five queues serializes the fault handler (active→ path) against the reclaim thread (inactive-scan path). Per-queue locks allow independent concurrent progress on disjoint queue pairs.

---

### P-3: New concept `IPhysicalMemoryAccessor` + Platform Bridge

#### [NEW] `FoundationKitMemory/Management/PhysicalAccessor.hpp`

The `Amd64PageTableManager` today captures its physical→virtual translation as a raw `PhysToVirt&&` template parameter — an unauditable convention. This replaces it with a typed concept so `VmFault` and the PTM both have a single, checkable interface for physical-page access.

**Note:** The zeroing and copying inside this accessor must use `CompilerBuiltins::MemSet` / `CompilerBuiltins::MemCpy` (already in `FoundationKitCxxStl`) — not `__builtin_bzero` or a hand-rolled loop.

```cpp
namespace FoundationKitMemory {
    /// @brief Concept: platform-supplied accessor for physical memory via HHDM or window mapping.
    template <typename PA>
    concept IPhysicalMemoryAccessor = requires(PA& acc, PhysicalAddress phys) {
        /// @brief Translate a physical address to a kernel-virtual pointer via HHDM.
        { acc.ToVirtual(phys) } -> SameAs<void*>;
        /// @brief Translate a kernel-virtual pointer back to a physical address.
        { acc.ToPhysical(static_cast<const void*>(nullptr)) } -> SameAs<PhysicalAddress>;
        /// @brief Zero exactly one page at `phys` via the HHDM mapping.
        { acc.ZeroPage(phys) } -> SameAs<void>;
        /// @brief Copy one page from `src` to `dst` via the HHDM mapping.
        { acc.CopyPage(phys, phys) } -> SameAs<void>;
    };
}
```

All `ZeroPhysical` / `CopyPhysical` methods in `Amd64PageTableManager` and `VmFault` route through this concept. The raw `m_p2v` lambda in `Amd64PageTableManager` is replaced by a `PhysicalAccessor&` member satisfying `IPhysicalMemoryAccessor`.

---

### P-4: Fence Insertion in `Amd64PageTableManager`

#### [MODIFY] [Amd64PageTableManager.hpp](file:///Users/assembler-0/CLionProjects/untitled1/FoundationKitPlatform/Include/FoundationKitPlatform/Amd64/Amd64PageTableManager.hpp)

`FoundationKitCxxStl::Base::CompilerBuiltins` already exposes `FullMemoryBarrier()` (`__atomic_thread_fence(__ATOMIC_SEQ_CST)`). However, since `Amd64PageTableManager` is an **architecture-specific** file, using an explicit `MFENCE` inline is acceptable here as it is clearer and correct for the TSO model. Both approaches are valid; pick the explicit one for auditability:

```cpp
// Wrap all PT entry stores in this helper — makes the fence obligation visible at call sites.
FOUNDATIONKITCXXSTL_ALWAYS_INLINE
static void PtEntryStore(u64* slot, u64 entry) noexcept {
    *slot = entry;
    asm volatile("mfence" ::: "memory");  // AMD64 SDM §4.10.4.1: required before INVLPG
}
```

Replace all direct `table[idx] = ...` assignments in `MapAtLevel()`, `Shatter()`, and `Protect()` with `PtEntryStore(&table[idx], entry)`.

`ToAmd64Flags()` must handle the `Cacheable` flag — currently silently ignored:
```cpp
if (!HasRegionFlag(flags, RegionFlags::Cacheable))
    res |= PageEntryFlags::WriteThrough | PageEntryFlags::CacheDisable;
```

`Promote()` must either be **implemented** or replaced with a `static constexpr bool kSupportsHugePagePromotion = false` trait that callers check. A silent `return false` is a concept lie.

---

### P-5: Fix `VmObject` Shadow Depth Bomb + Lock Order

#### [MODIFY] [VmObject.hpp](file:///Users/assembler-0/CLionProjects/untitled1/FoundationKitMemory/Include/FoundationKitMemory/Management/VmObject.hpp)

Add `static constexpr usize kMaxShadowDepth = 64;` and assert in `Lookup()`:
```cpp
usize depth = 0;
while (current) {
    FK_BUG_ON(++depth > kMaxShadowDepth,
        "VmObject::Lookup: shadow chain depth {} exceeded maximum {} — "
        "likely infinite loop or unbounded fork depth (offset={:#x})",
        depth, kMaxShadowDepth, offset);
    ...
}
```

`Collapse()` lock order fix — uses `Sync::UniqueLock<SharedSpinLock>` (already available) with address-based ordering to enforce a consistent lock hierarchy:
```cpp
// Lock order invariant: always acquire the object with the lower address first.
// This prevents ABBA deadlock with any other two-object lock acquisition.
VmObject* parent = m_shadow.Get();  // shadow ptr is stable once set (write-once)
FK_BUG_ON(parent == nullptr, "VmObject::Collapse: shadow is null");

VmObject* first  = (reinterpret_cast<uptr>(parent) < reinterpret_cast<uptr>(this)) ? parent : this;
VmObject* second = (first == parent) ? this : parent;
Sync::UniqueLock<Sync::SharedSpinLock> first_guard(first->m_lock);
Sync::UniqueLock<Sync::SharedSpinLock> second_guard(second->m_lock);
// Now proceed with collapse under both locks.
```

`Sync::UniqueLock<SharedSpinLock>` acquires the **exclusive** (writer) side via `SharedSpinLock::Lock()` — the `Lockable` concept in `Locks.hpp` already routes `UniqueLock<T>` to `T::Lock()` / `T::Unlock()`.

---

### P-6: Fix VmFault Race Window + Map Return Value

#### [MODIFY] [VmFault.hpp](file:///Users/assembler-0/CLionProjects/untitled1/FoundationKitMemory/Include/FoundationKitMemory/Management/VmFault.hpp)

`ResolvePageIn()` — check `Map()` return value:
```cpp
const bool mapped = m_ptm.Map(ctx.aligned_va, pa, kPageSize, map_flags);
FK_BUG_ON(!mapped,
    "VmFault::ResolvePageIn: Map failed for VA={:#x} PA={:#x} — "
    "either pre-existing PTE (concurrent fault race) or PTM OOM",
    ctx.aligned_va.value, pa.value);
```

`ResolveCopyOnWrite()` — TLB shootdown before remap:
```cpp
m_ptm.Unmap(ctx.aligned_va, kPageSize);
m_ptm.FlushTlbAll();  // IPI broadcast on SMP — must shootdown ALL CPUs before remap
m_ptm.Map(ctx.aligned_va, new_pa, kPageSize, VmaProtToRegionFlags(...));
m_ptm.FlushTlb(ctx.aligned_va);
```

---

### P-7: Fix Reclaim — Unlink VmObject & Unmap PTE Before Free

#### [MODIFY] [KernelMemoryManager.hpp](file:///Users/assembler-0/CLionProjects/untitled1/FoundationKitMemory/Include/FoundationKitMemory/Management/KernelMemoryManager.hpp)

`ScanForReclaim()` must, for each candidate page:
1. Unmap the PTE for every mapping (via `page->owner->RemoveBlock(...)` and `m_ptm.Unmap(...) + FlushTlb(...)`).
2. `page->MapCountDec()` per removed PTE.
3. `FK_BUG_ON(!page->IsUnmapped(), ...)` before freeing physical page.
4. Only then call `m_pfa.FreePages(page->pfn, 1)`.

Also fix `CheckAndReclaim()` — re-query after reclaim:
```cpp
if (level == Watermark::Low || level == Watermark::Min) {
    const usize target_bytes = ...;
    if (target_bytes > 0)
        m_chain.Reclaim(target_bytes);
    // Re-query actual free pages AFTER reclaim (caller must pass a re-read fn or
    // MemoryPressureManager must be given a free-page-count functor):
    free_pages = m_free_pages_fn();  // New member: std::function<usize()> noexcept stored at SetWatermarks time
}
if (free_pages <= m_wmark_min) { /* OOM */ }
```

---

### P-8: Page-size Abstraction — Kill `kPageSize` Hardcode

#### [MODIFY] [AddressTypes.hpp](file:///Users/assembler-0/CLionProjects/untitled1/FoundationKitMemory/Include/FoundationKitMemory/Management/AddressTypes.hpp)

```cpp
// Per-target page size configured at compile time via -DFK_PAGE_SHIFT=12/14/16.
// Freestanding targets that don't use 4K pages define FK_PAGE_SHIFT differently.
#ifndef FK_PAGE_SHIFT
#   define FK_PAGE_SHIFT 12
#endif

static constexpr usize kPageShift = FK_PAGE_SHIFT;
static constexpr usize kPageSize  = usize(1) << kPageShift;
static constexpr usize kPageMask  = kPageSize - 1;
```

Then replace all `>> 12`, `<< 12`, and `& 0xFFF` literals in `PageFrameAllocator` / `Amd64PageTableManager` with `kPageShift`/`kPageSize`/`kPageMask`.

---

### P-9: `PhysicalMemoryMap::FindZone()` — Use Physical PFN, Not Virtual Ptr

#### [MODIFY] [PhysicalMemoryMap.hpp](file:///Users/assembler-0/CLionProjects/untitled1/FoundationKitMemory/Include/FoundationKitMemory/Management/PhysicalMemoryMap.hpp)

Change `FindZone(const void* ptr)` → `FindZone(PhysicalAddress pa)` throughout. All callers (primarily `ZoneAllocator`) pass virtual pointers today — they must be updated to pass the physical address obtained via the allocator's zone base arithmetic or via `IPhysicalMemoryAccessor::ToPhysical()`.

---

### P-10: `AddressSpace::Fork()` — Clarify PTM Ownership

#### [MODIFY] [AddressSpace.hpp](file:///Users/assembler-0/CLionProjects/untitled1/FoundationKitMemory/Include/FoundationKitMemory/Management/AddressSpace.hpp)

The `Fork()` API must receive an **explicit new PTM** for the child, not re-use the parent's:
```cpp
[[nodiscard]] Expected<void, MemoryError>
Fork(AddressSpace& child, PageTableMgr& child_ptm) noexcept;
```
This makes the architectural invariant explicit: child gets its own page table root (different CR3), and `child.m_ptm.Map()` installs into child's tables only.

---

## Priority Table

| # | Bug | Impact | Fix Phase |
|---|---|---|---|
| B-1/B-2/B-3 | Non-atomic state/flags/owner in PageDescriptor | 🔴 SMP data corruption | P-1 |
| C-1/C-2/C-3 | No locks in PageQueue/PageQueueSet | 🔴 Queue corruption, UAF | P-2 |
| H-1 | `BasicMemoryResource` virtual dispatch | 🔴 Hosted ABI violation | P-0 |
| H-1 (KMM) | Reclaim without PTE unmap → physical UAF | 🔴 Physical frame UAF | P-7 |
| F-3 | `ResolvePageIn` ignores `Map()` return | 🔴 map_count mismatch | P-6 |
| F-2 | CoW remap without TLB shootdown on SMP | 🔴 Torn reads | P-6 |
| I-1 | No MFENCE after PT store | 🔴 CPU sees stale PTE | P-4 |
| I-5 | `Cacheable` flag not mapped → MMIO cached | 🔴 MMIO correctness | P-4 |
| E-2 | Lock ordering inversion in `Collapse()` | 🔴 Deadlock under load | P-5 |
| G-3 | `SplitVmaInternal` ignores `Shatter()` result | 🟠 Wrong PT state | P-7 |
| J-2 | `FindZone(void*)` uses virtual addr as PFN key | 🟠 Wrong zone lookup | P-9 |
| E-1 | Unbounded shadow chain walk | 🟠 Latency bomb | P-5 |
| H-2 (KMM) | `AllocateKernel` split-brain anonymous/eager mapping | 🟠 Wrong fault handling | P-7 |
| H-3 (MPM) | `CheckAndReclaim` no re-query after reclaim | 🟡 Spurious OOM | P-7 |
| G-2 | Fork PTM alias unclear | 🟡 Architectural ambiguity | P-10 |
| I-3 | `Promote()` silent stub | 🟡 No huge-page recombination | P-4 |
| A-1 | `kPageSize` hardcoded | 🟡 ARM64/RISC-V incompatibility | P-8 |
| D-2 | `FindZoneIndex` first-match for duplicate zone types | 🟡 Multi-zone zone confusion | P-9 |
| K-2 | O(N) gap search mislabeled as O(log N) | 🟡 Misleading complexity | — |
