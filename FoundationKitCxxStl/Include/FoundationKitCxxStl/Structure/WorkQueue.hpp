#pragma once

#include <FoundationKitCxxStl/Base/Types.hpp>
#include <FoundationKitCxxStl/Base/Utility.hpp>
#include <FoundationKitCxxStl/Structure/IntrusiveDoublyLinkedList.hpp>
#include <FoundationKitCxxStl/Sync/Mutex.hpp>
#include <FoundationKitCxxStl/Sync/ConditionVariable.hpp>

namespace FoundationKitCxxStl {

    /// @brief A work item for the WorkQueue.
    /// @desc Intrusive: must embed WorkItem in your class.
    struct WorkItem {
        Structure::IntrusiveDoublyLinkedListNode node;
        void (*func)(void*);
        void* arg;

        constexpr WorkItem() noexcept : node(), func(nullptr), arg(nullptr) {}
        constexpr WorkItem(void (*f)(void*), void* a) noexcept : node(), func(f), arg(a) {}
    };

    /// @brief A simple intrusive producer-consumer WorkQueue.
    /// @desc Uses a Mutex and ConditionVariable for synchronization.
    class WorkQueue {
    public:
        WorkQueue() = default;

        /// @brief Enqueue a work item.
        void Enqueue(WorkItem* item) noexcept {
            Sync::UniqueLock lock(m_mutex);
            m_list.PushBack(&item->node);
            m_cv.NotifyOne();
        }

        /// @brief Dequeue a work item, blocking if empty.
        /// @return The next work item, or nullptr if the queue is shutting down.
        WorkItem* Dequeue() noexcept {
            Sync::UniqueLock lock(m_mutex);
            while (m_list.Empty() && !m_shutdown) {
                m_cv.Wait(lock);
            }
            if (m_shutdown && m_list.Empty()) return nullptr;

            auto* node = m_list.PopFront();
            return Structure::ContainerOf<WorkItem, &WorkItem::node>(node);
        }

        /// @brief Try to dequeue without blocking.
        WorkItem* TryDequeue() noexcept {
            Sync::UniqueLock lock(m_mutex);
            if (m_list.Empty()) return nullptr;

            auto* node = m_list.PopFront();
            return Structure::ContainerOf<WorkItem, &WorkItem::node>(node);
        }

        /// @brief Signal shutdown to all waiting consumers.
        void Shutdown() noexcept {
            Sync::UniqueLock lock(m_mutex);
            m_shutdown = true;
            m_cv.NotifyAll();
        }

        [[nodiscard]] bool IsShutdown() const noexcept {
            Sync::UniqueLock lock(m_mutex);
            return m_shutdown;
        }

        [[nodiscard]] usize Size() const noexcept {
            Sync::UniqueLock lock(m_mutex);
            return m_list.Size();
        }

    private:
        Structure::IntrusiveDoublyLinkedList m_list;
        mutable Sync::Mutex m_mutex;
        Sync::ConditionVariable m_cv;
        bool m_shutdown = false;
    };

} // namespace FoundationKitCxxStl
