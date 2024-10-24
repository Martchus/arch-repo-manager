#include "../globallock.h"
#include "../logging.h"
#include "../serversetup.h"

#include <c++utilities/conversion/stringbuilder.h>
#include <c++utilities/io/misc.h>
#include <c++utilities/tests/testutils.h>

#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>

#include <thread>

using namespace std;
using namespace CPPUNIT_NS;
using namespace CppUtilities;
using namespace CppUtilities::Literals;

using namespace LibRepoMgr;

class UtilsTests : public TestFixture {
    CPPUNIT_TEST_SUITE(UtilsTests);
    CPPUNIT_TEST(testGlobalLock);
    CPPUNIT_TEST(testGlobalLockAsync);
    CPPUNIT_TEST(testLockTable);
    CPPUNIT_TEST_SUITE_END();

    void testGlobalLock();
    void testGlobalLockAsync();
    void testLockTable();

public:
    UtilsTests();
    void setUp() override;
    void tearDown() override;

private:
};

CPPUNIT_TEST_SUITE_REGISTRATION(UtilsTests);

UtilsTests::UtilsTests()
{
}

void UtilsTests::setUp()
{
}

void UtilsTests::tearDown()
{
}

void UtilsTests::testGlobalLock()
{
    auto mutex = GlobalSharedMutex();
    auto sharedLock1 = std::shared_lock(mutex);
    auto sharedLock2 = std::shared_lock(mutex); // locking twice is not a problem, also not from the same thread
    auto thread1 = std::thread([&sharedLock1] {
        sharedLock1.unlock(); // unlocking from another thread is ok
    });
    auto thread2 = std::thread([&mutex] { mutex.lock(); });
    sharedLock2.unlock();
    thread1.join();
    thread2.join(); // thread2 should be able to acquire the mutex exclusively (and then terminate)
    CPPUNIT_ASSERT_MESSAGE("try_lock_shared() returns false if mutex exclusively locked", !mutex.try_lock_shared());
    auto thread3 = std::thread([&mutex] { mutex.lock_shared(); });
    mutex.unlock();
    thread3.join(); // thread3 should be able to acquire the mutex (and then terminate)
    CPPUNIT_ASSERT_MESSAGE("try_lock_shared() possible if mutex only shared locked", mutex.try_lock_shared());
    mutex.unlock_shared();
    CPPUNIT_ASSERT_MESSAGE("try_lock() returns false if mutex has still shared locked", !mutex.try_lock());
    mutex.unlock_shared();
    CPPUNIT_ASSERT_MESSAGE("try_lock() possible if mutex not locked", mutex.try_lock());
    mutex.unlock();
}

void UtilsTests::testGlobalLockAsync()
{
    auto mutex = GlobalSharedMutex();
    auto sharedLock1 = false, sharedLock2 = false;
    mutex.lock_shared_async([&sharedLock1] { sharedLock1 = true; });
    CPPUNIT_ASSERT(sharedLock1);
    mutex.lock_shared_async([&sharedLock2] { sharedLock2 = true; }); // locking twice is not a problem, also not from the same thread
    CPPUNIT_ASSERT(sharedLock2);
    auto thread1 = std::thread([&mutex] {
        mutex.unlock_shared(); // unlocking from another thread is ok
    });
    auto lock1 = false, lock2 = false;
    auto thread2 = std::thread([&mutex, &lock2] {
        mutex.lock();
        lock2 = true;
    });
    mutex.lock_async([&lock1] { lock1 = true; });
    CPPUNIT_ASSERT_MESSAGE("lock_async() not yet invoked", !lock1);
    CPPUNIT_ASSERT_MESSAGE("blocking lock() not yet invoked", !lock2);
    thread1.join();
    mutex.unlock_shared();
    CPPUNIT_ASSERT_MESSAGE("lock_async() callback invoked via unlock_shared()", lock1);
    CPPUNIT_ASSERT_MESSAGE("blocking lock() not yet invoked (async callbacks are handled first)", !lock2);
    mutex.unlock(); // release async lock so …
    thread2.join(); // … thread2 is able to acquire the mutex exclusively (and then terminate)
    CPPUNIT_ASSERT_MESSAGE("try_lock_shared() returns false if mutex exclusively locked", !mutex.try_lock_shared());
    auto sharedLock3 = false;
    mutex.lock_shared_async([&sharedLock3] { sharedLock3 = true; });
    mutex.unlock();
    CPPUNIT_ASSERT_MESSAGE("lock_async() callback invoked via unlock()", lock1);
    CPPUNIT_ASSERT_MESSAGE("try_lock_shared() possible if mutex only shared locked", mutex.try_lock_shared());
    mutex.unlock_shared();
    CPPUNIT_ASSERT_MESSAGE("try_lock() returns false if mutex has still shared locked", !mutex.try_lock());
    mutex.unlock_shared();
    CPPUNIT_ASSERT_MESSAGE("try_lock() possible if mutex not locked", mutex.try_lock());
    mutex.unlock();
}

void UtilsTests::testLockTable()
{
    auto log = LogContext();
    auto locks = ServiceSetup::Locks();
    auto readLock = locks.acquireToRead(log, "foo");
    locks.clear(); // should not deadlock (and simply ignore the still acquired lock)
    readLock.lock().unlock();
    auto lockTable = locks.acquireLockTable();
    CPPUNIT_ASSERT_EQUAL_MESSAGE("read lock still present", 1_st, lockTable.first->size());
    lockTable.second.unlock();
    locks.clear(); // should free up all locks now
    CPPUNIT_ASSERT_EQUAL_MESSAGE("read lock cleared", 0_st, lockTable.first->size());
}
