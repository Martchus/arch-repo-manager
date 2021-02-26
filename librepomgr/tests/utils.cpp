#include "../globallock.h"
#include "../logging.h"

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
    CPPUNIT_TEST_SUITE_END();

    void testGlobalLock();

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
