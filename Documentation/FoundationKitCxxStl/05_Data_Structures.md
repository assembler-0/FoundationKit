# FoundationKitCxxStl — Part 5: Advanced Data Structures

> **Standard:** C++23 Freestanding | **Namespace:** `FoundationKitCxxStl::Structure` | **Header dir:** `FoundationKitCxxStl/Structure/`

---

## 5.1 Design Philosophy

All data structures in the `Structure` namespace are:

1. **Allocator-aware** — parameterised on `IAllocator`. Nodes are dynamically allocated through whatever allocator is provided.
2. **Move-only** — copy construction is deleted. Ownership transfer is via `Move`.
3. **Safe by default** — bounds checks and preconditions are enforced via `FK_BUG_ON`.
4. **Intrusive variants available** — zero-allocation alternatives using embedded link pointers.

---

## 5.2 `HashMap<K, V, H, Alloc>` — Open-Addressing Hash Map

The `HashMap` uses **open addressing with linear probing** and a **Robin Hood / tombstone deletion** strategy. The capacity is always a power of two; the load factor triggers rehash at 70%.

```cpp
template <typename K, typename V,
          typename H    = Hash,
          IAllocator Alloc = AnyAllocator>
class HashMap {
public:
    explicit HashMap(Alloc alloc = Alloc());

    // Insert or update. Returns false only on OOM during rehash.
    template <typename... Args>
    bool Insert(const K& key, Args&&... args);

    // Returns a reference to value, or empty.
    Optional<V&>       Get(const K& key);
    Optional<const V&> Get(const K& key) const;

    // Mark bucket as Deleted tombstone. Returns false if key not found.
    bool Remove(const K& key);

    [[nodiscard]] usize Size()  const noexcept;
    [[nodiscard]] bool  Empty() const noexcept;
};
```

### Bucket State Machine

```
[Empty] ──── Insert ───► [Occupied]
[Occupied] ── Remove ──► [Deleted]  (tombstone)
[Deleted] ── Insert ───► [Occupied] (reuse slot)
```

During **probe** for a key:
- If `Empty` is reached and key not found: it does not exist.
- `Deleted` slots are skipped and the **first** deleted slot is remembered as the insertion point.

During **insert**:
- If key already exists in an `Occupied` slot: update value in-place.
- Otherwise: use the first `Deleted` slot or the first `Empty` slot.

### Rehash Policy

When `size >= capacity * 0.7`:
1. Move existing bucket array to `old_buckets`.
2. Allocate new array of `old_capacity * 2`.
3. Re-insert all `Occupied` entries into the new array.

If the new allocation fails, the old buckets are moved back — the map remains valid but the insertion fails.

### Correctness Invariants

Every internal `FindBucket` call is guarded by:

```cpp
FK_BUG_ON(m_buckets.Empty(), "HashMap: FindBucket called on empty bucket vector");
FK_BUG_ON((m_buckets.Size() & mask) != 0,
    "HashMap: capacity ({}) is not a power of two", m_buckets.Size());
FK_BUG_ON(probed >= m_buckets.Size(),
    "HashMap: probe sequence exceeded capacity; potential infinite loop");
```

---

## 5.3 `BitSet<N>` — Fixed-Size Bit Array

`BitSet<N>` manages `N` bits stored in an array of `usize` words. Operations on individual bits use bitwise manipulation; bulk operations exploit `PopCount` and `CountTrailingZeros` from `Bit.hpp`.

```cpp
template <usize N>
class BitSet {
public:
    static constexpr usize WordSize  = sizeof(usize) * 8;
    static constexpr usize WordCount = (N + WordSize - 1) / WordSize;

    constexpr BitSet() noexcept;  // all zeros

    void Set  (usize pos, bool value = true) noexcept;
    void Reset(usize pos) noexcept;           // clear single bit
    void Reset()          noexcept;           // clear all
    void Flip (usize pos) noexcept;

    [[nodiscard]] bool  Test(usize pos)  const noexcept;
    [[nodiscard]] bool  All()            const noexcept;
    [[nodiscard]] bool  Any()            const noexcept;
    [[nodiscard]] bool  None()           const noexcept;
    [[nodiscard]] usize Count()          const noexcept;
    [[nodiscard]] static constexpr usize Size() noexcept { return N; }

    // Returns index of first set bit, or N if none.
    [[nodiscard]] constexpr usize FindFirstSet()   const noexcept;

    // Returns index of first clear bit, or N if none.
    [[nodiscard]] constexpr usize FindFirstUnset() const noexcept;
};
```

### Implementation Notes

- `All()` checks all full words for `~usize(0)` and masks the last word for partial words.
- `Count()` sums `PopCount(word)` over all words using `CountTrailingZeros`.
- `FindFirstSet()` scans words for non-zero, then applies `CountTrailingZeros` for the bit index.
- `FindFirstUnset()` inverts each word and applies the same scan, masking the last word for validity.

**Typical kernel use:** CPU affinity masks, page-frame bitmaps, IRQ assignment tables.

```cpp
BitSet<256> irq_mask;          // 256-bit mask for IRQ lines
irq_mask.Set(42);              // assign IRQ 42
auto next_free = irq_mask.FindFirstUnset();  // find next unallocated IRQ
```

---

## 5.4 `SinglyLinkedList<T, Alloc>` — Allocation-Backed SLL

A forward-only linked list where each node is heap-allocated through the configured allocator.

```cpp
template <typename T, IAllocator Alloc = AnyAllocator>
class SinglyLinkedList {
public:
    // PushFront: O(1). Returns false on OOM.
    template <typename... Args>
    bool PushFront(Args&&... args);

    void PopFront();  // panics if empty

    [[nodiscard]] T&       Front();
    [[nodiscard]] const T& Front() const;

    [[nodiscard]] usize Size()  const;
    [[nodiscard]] bool  Empty() const;

    void Clear();  // destroys all nodes

    // Forward iterator (begin/end pair)
    Iterator begin();
    Iterator end();
};
```

**Node lifecycle:** `PushFront` calls `alloc.Allocate(sizeof(Node), alignof(Node))` and placement-news the node in-place. `PopFront` explicitly calls `node->~Node()` then `alloc.Deallocate(node, sizeof(Node))`. No `new`/`delete` operators.

---

## 5.5 `DoublyLinkedList<T, Alloc>` — Allocation-Backed DLL

Bidirectional linked list. Provides O(1) insertion and removal at both ends.

```cpp
template <typename T, IAllocator Alloc = AnyAllocator>
class DoublyLinkedList {
public:
    template <typename... Args> bool PushFront(Args&&... args);
    template <typename... Args> bool PushBack (Args&&... args);

    void PopFront();
    void PopBack();

    [[nodiscard]] T&       Front(); [[nodiscard]] const T& Front() const;
    [[nodiscard]] T&       Back();  [[nodiscard]] const T& Back()  const;

    [[nodiscard]] usize Size()  const;
    [[nodiscard]] bool  Empty() const;

    void Clear();

    // Bidirectional iterator (begin/end pair, supports --)
    Iterator begin();
    Iterator end();
};
```

`DoublyLinkedList` is useful when removals from the middle (via `Iterator.GetNode()`) are required. Its bidirectional iterator supports both `++` and `--`.

---

## 5.6 `CircularLinkedList<T, Alloc>` — Circular Singly Linked

A circular list maintains a single pointer `m_last` to the **tail** node; `m_last->next` is the **head**. This gives O(1) access to both front and back without a second pointer.

```cpp
template <typename T, IAllocator Alloc = AnyAllocator>
class CircularLinkedList {
public:
    template <typename... Args> bool PushFront(Args&&... args);
    template <typename... Args> bool PushBack (Args&&... args);

    void PopFront();

    // Advance the tail pointer (making current head the new tail)
    void Rotate() noexcept;

    [[nodiscard]] T& Front(); [[nodiscard]] const T& Front() const;
    [[nodiscard]] T& Back();  [[nodiscard]] const T& Back()  const;

    [[nodiscard]] usize Size()  const;
    [[nodiscard]] bool  Empty() const;
};
```

**`Rotate()`** advances `m_last = m_last->next`. Repeated calls cycle through all elements — useful for round-robin scheduling.

---

## 5.7 `IntrusiveDoublyLinkedList` — Zero-Allocation DLL

An intrusive list embeds the link pointers inside the object itself. No separate node allocation is needed. This is the pattern used in the Linux kernel's `list_head`.

### Node Structure

```cpp
struct IntrusiveDoublyLinkedListNode {
    IntrusiveDoublyLinkedListNode* next;
    IntrusiveDoublyLinkedListNode* prev;

    constexpr IntrusiveDoublyLinkedListNode() noexcept : next(this), prev(this) {}

    [[nodiscard]] constexpr bool IsShared() const noexcept { return next != this; }
};
```

By default, an unlinked node points to itself (circular sentinel). `IsShared()` checks whether the node is part of a list.

### Obtaining the Owning Object

```cpp
template <typename T, IntrusiveDoublyLinkedListNode T::* Member>
[[nodiscard]] T* ContainerOf(IntrusiveDoublyLinkedListNode* node) noexcept;
```

`ContainerOf` computes the address of the owning object from the node pointer:

```cpp
struct Task {
    i32 priority;
    IntrusiveDoublyLinkedListNode run_queue_node;
};

IntrusiveDoublyLinkedListNode* node = run_queue.PopFront();
Task* task = ContainerOf<Task, &Task::run_queue_node>(node);
task->Execute();
```

### `IntrusiveDoublyLinkedList` — The List Itself

```cpp
class IntrusiveDoublyLinkedList {
public:
    void PushFront(IntrusiveDoublyLinkedListNode* node) noexcept;
    void PushBack (IntrusiveDoublyLinkedListNode* node) noexcept;

    IntrusiveDoublyLinkedListNode* PopFront() noexcept;
    IntrusiveDoublyLinkedListNode* PopBack()  noexcept;

    void Remove(IntrusiveDoublyLinkedListNode* node) noexcept;

    [[nodiscard]] bool  Empty() const noexcept;
    [[nodiscard]] usize Size()  const noexcept;

    [[nodiscard]] IntrusiveDoublyLinkedListNode* Begin() const noexcept;
    [[nodiscard]] IntrusiveDoublyLinkedListNode* End()         noexcept;
};
```

A sentinel `m_head` node is used as the list anchor. The list is circular: `m_head.next` is the first element, `m_head.prev` is the last. `Empty()` checks `m_head.next == &m_head`.

**Why intrusive?** In a scheduler or memory subsystem, objects are often on **multiple lists simultaneously** (e.g., a process on both the run queue and a wait queue). Intrusive lists make this possible by embedding multiple `Node` members in the object.

---

## 5.8 `IntrusiveSinglyLinkedList<T>` — Zero-Allocation SLL

Simpler intrusive variant. The caller's object must contain an embedded `Node`:

```cpp
template <typename T>
class IntrusiveSinglyLinkedList {
public:
    struct Node {
        Node* next = nullptr;
    };

    void PushFront(Node* node);
    Node* PopFront();

    [[nodiscard]] Node*       Front();
    [[nodiscard]] const Node* Front() const;

    [[nodiscard]] bool  Empty() const;
    [[nodiscard]] usize Size()  const;

    void Clear();  // does NOT call destructors; just resets the list state
};
```

Used internally by `PoolAllocator` to manage its free-chunk list. The `Node` is overlaid on top of the chunk memory itself when the chunk is free — no extra metadata, no extra allocation.

```cpp
// PoolAllocator stores the free-list Node IN the free chunk itself:
auto* node = reinterpret_cast<Node*>(free_chunk_ptr);
m_free_list.PushFront(node);
```
