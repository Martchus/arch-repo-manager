#include "../globallock.h"
#include "../helper.h"
#include "../logging.h"
#include "../serversetup.h"

#include <c++utilities/conversion/stringbuilder.h>
#include <c++utilities/io/misc.h>
#include <c++utilities/tests/testutils.h>

#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>

#include <filesystem>
#include <fstream>
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
    CPPUNIT_TEST(testCopyDirectoryRecursive);
    CPPUNIT_TEST_SUITE_END();

    void testGlobalLock();
    void testGlobalLockAsync();
    void testLockTable();
    void testCopyDirectoryRecursive();

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

void UtilsTests::testCopyDirectoryRecursive()
{
    const auto tempDir = std::filesystem::temp_directory_path();
    const auto srcDir = tempDir / "librepomgr-test-copy-src";
    const auto destDir = tempDir / "librepomgr-test-copy-dest";

    // clean up any leftovers
    std::filesystem::remove_all(srcDir);
    std::filesystem::remove_all(destDir);

    // create source directory structure
    std::filesystem::create_directories(srcDir / "subdir");

    // create a regular file in source
    const auto srcFile1 = srcDir / "file1.txt";
    std::ofstream(srcFile1) << "Hello, World!";

    // create a read-only file in source
    const auto srcFile2 = srcDir / "subdir/file2.txt";
    std::ofstream(srcFile2) << "Read only source";
    std::filesystem::permissions(
        srcFile2, std::filesystem::perms::owner_read | std::filesystem::perms::group_read | std::filesystem::perms::others_read);

    // copy using helper
    copyDirectoryRecursive(srcDir, destDir);

    // verify files copied correctly
    const auto destFile1 = destDir / "file1.txt";
    const auto destFile2 = destDir / "subdir/file2.txt";
    CPPUNIT_ASSERT(std::filesystem::exists(destFile1));
    CPPUNIT_ASSERT(std::filesystem::exists(destFile2));

    // verify content of destFile1
    auto ifs1 = std::ifstream(destFile1);
    auto content1 = std::string((std::istreambuf_iterator<char>(ifs1)), std::istreambuf_iterator<char>());
    CPPUNIT_ASSERT_EQUAL(std::string("Hello, World!"), content1);

    // verify content of destFile2
    auto ifs2 = std::ifstream(destFile2);
    auto content2 = std::string((std::istreambuf_iterator<char>(ifs2)), std::istreambuf_iterator<char>());
    CPPUNIT_ASSERT_EQUAL(std::string("Read only source"), content2);

    // verify overwriting of read-only files
    // note: Since destFile2 was copied from a read-only source file, it has been created as read-only.
    //       Modify the source file to contain something else, make it writable first to be able to modify,
    //       then read-only again.
    std::filesystem::permissions(srcFile2, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
    std::ofstream(srcFile2, std::ios::trunc) << "Modified read only source";
    std::filesystem::permissions(
        srcFile2, std::filesystem::perms::owner_read | std::filesystem::perms::group_read | std::filesystem::perms::others_read);

    // modify destFile1 as well to make sure we overwrite a writable file as well
    std::ofstream(srcFile1, std::ios::trunc) << "Modified Hello!";

    // run copyDirectoryRecursive again, this must overwrite destFile2 (which is read-only)
    // without throwing a permission denied error!
    CPPUNIT_ASSERT_NO_THROW(copyDirectoryRecursive(srcDir, destDir));

    // verify contents again
    auto ifs1_mod = std::ifstream(destFile1);
    auto content1_mod = std::string((std::istreambuf_iterator<char>(ifs1_mod)), std::istreambuf_iterator<char>());
    CPPUNIT_ASSERT_EQUAL(std::string("Modified Hello!"), content1_mod);

    auto ifs2_mod = std::ifstream(destFile2);
    auto content2_mod = std::string((std::istreambuf_iterator<char>(ifs2_mod)), std::istreambuf_iterator<char>());
    CPPUNIT_ASSERT_EQUAL(std::string("Modified read only source"), content2_mod);

    // clean up
    std::filesystem::permissions(srcFile2, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
    std::filesystem::permissions(destFile2, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
    std::filesystem::remove_all(srcDir);
    std::filesystem::remove_all(destDir);
}
