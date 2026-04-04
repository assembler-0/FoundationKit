#include <FoundationKitCxxStl/Sync/SpinLock.hpp>
#include <FoundationKitCxxStl/Sync/TicketLock.hpp>
#include <FoundationKitCxxStl/Sync/SharedSpinLock.hpp>
#include <FoundationKitCxxStl/Sync/Mutex.hpp>
#include <FoundationKitCxxStl/Sync/Locks.hpp>
#include <FoundationKitCxxStl/Sync/SharedLock.hpp>
#include <FoundationKitCxxStl/Sync/InterruptSafe.hpp>
#include <FoundationKitCxxStl/Sync/MCSLock.hpp>
#include <FoundationKitOsl/Osl.hpp>
#include <Test/TestFramework.hpp>

using namespace FoundationKitCxxStl::Sync;

TEST_CASE(Sync_SpinLock_Stress) {
    SpinLock lock;
    int counter = 0;
    const int iterations = 10000;

    for (int i = 0; i < iterations; ++i) {
        LockGuard guard(lock);
        counter++;
    }

    ASSERT_EQ(counter, iterations);
}

TEST_CASE(Sync_InterruptGuard) {
    ASSERT_TRUE(FoundationKitOsl::OslIsInterruptEnabled());

    {
        InterruptGuard guard;
        ASSERT_FALSE(FoundationKitOsl::OslIsInterruptEnabled());

        {
            InterruptGuard nested;
            ASSERT_FALSE(FoundationKitOsl::OslIsInterruptEnabled());
        }

        ASSERT_FALSE(FoundationKitOsl::OslIsInterruptEnabled());
    }

    ASSERT_TRUE(FoundationKitOsl::OslIsInterruptEnabled());
}

TEST_CASE(Sync_InterruptSafeSpinLock) {
    InterruptSafeSpinLock lock;
    int counter = 0;

    ASSERT_TRUE(FoundationKitOsl::OslIsInterruptEnabled());

    {
        LockGuard guard(lock);
        ASSERT_FALSE(FoundationKitOsl::OslIsInterruptEnabled());
        counter++;

        // Test TryLock while held
        ASSERT_FALSE(lock.TryLock());
    }

    ASSERT_TRUE(FoundationKitOsl::OslIsInterruptEnabled());
    ASSERT_EQ(counter, 1);

    // Test manual Lock/Unlock
    lock.Lock();
    ASSERT_FALSE(FoundationKitOsl::OslIsInterruptEnabled());
    counter++;
    lock.Unlock();
    ASSERT_TRUE(FoundationKitOsl::OslIsInterruptEnabled());

    ASSERT_EQ(counter, 2);
}

TEST_CASE(Sync_MCSLock_Basic) {
    MCSLock lock;
    MCSNode node;
    int counter = 0;

    lock.Lock(node);
    counter++;
    lock.Unlock(node);

    ASSERT_EQ(counter, 1);
}

TEST_CASE(Sync_TicketLock_Stress) {
    TicketLock lock;
    int counter = 0;
    const int iterations = 10000;

    for (int i = 0; i < iterations; ++i) {
        LockGuard guard(lock);
        counter++;
    }

    ASSERT_EQ(counter, iterations);
}

TEST_CASE(Sync_SharedSpinLock_Stress) {
    SharedSpinLock lock;
    int counter = 0;
    const int iterations = 10000;

    // Test exclusive access
    for (int i = 0; i < iterations; ++i) {
        LockGuard guard(lock);
        counter++;
    }
    ASSERT_EQ(counter, iterations);

    // Test shared access
    for (int i = 0; i < iterations; ++i) {
        SharedLock guard(lock);
        ASSERT_EQ(counter, iterations);
    }
}

TEST_CASE(Sync_Mutex_Stress) {
    Mutex lock;
    int counter = 0;
    const int iterations = 1000;

    for (int i = 0; i < iterations; ++i) {
        LockGuard guard(lock);
        counter++;
    }

    ASSERT_EQ(counter, iterations);
}

TEST_CASE(Sync_UniqueLock) {
    SpinLock lock;
    int counter = 0;

    {
        UniqueLock guard(lock);
        ASSERT_TRUE(guard.IsOwned());
        counter++;

        guard.Unlock();
        ASSERT_FALSE(guard.IsOwned());

        guard.Lock();
        ASSERT_TRUE(guard.IsOwned());
        counter++;
    }

    ASSERT_EQ(counter, 2);
}
