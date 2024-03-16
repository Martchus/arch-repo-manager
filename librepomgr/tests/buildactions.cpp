#include "./parser_helper.h"

#include "../json.h"
#include "../logging.h"
#include "../serversetup.h"

#include "../buildactions/buildaction.h"
#include "../buildactions/buildactionprivate.h"
#include "../buildactions/subprocess.h"

#include <passwordfile/io/passwordfile.h>

#include <c++utilities/conversion/stringconversion.h>
#include <c++utilities/io/ansiescapecodes.h>
#include <c++utilities/io/misc.h>
#include <c++utilities/io/path.h>
#include <c++utilities/tests/testutils.h>
using CppUtilities::operator<<; // must be visible prior to the call site

#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>

#include <c++utilities/tests/outputcheck.h>

#include <boost/asio/executor_work_guard.hpp>
#include <boost/process/search_path.hpp>

#include <chrono>

using namespace std;
using namespace std::literals;
using namespace CPPUNIT_NS;
using namespace CppUtilities;
using namespace CppUtilities::Literals;

using namespace LibRepoMgr;

/*!
 * \brief The BuildActionsTests class contains tests for classes/functions related to build actions.
 */
class BuildActionsTests : public TestFixture {
    CPPUNIT_TEST_SUITE(BuildActionsTests);
    CPPUNIT_TEST(testLogging);
    CPPUNIT_TEST(testProcessSession);
    CPPUNIT_TEST(testBuildActionProcess);
    CPPUNIT_TEST(testParsingInfoFromPkgFiles);
    CPPUNIT_TEST(testPreparingBuild);
    CPPUNIT_TEST(testConductingBuild);
    CPPUNIT_TEST(testRepoCleanup);
    CPPUNIT_TEST(testBuildServiceCleanup);
    CPPUNIT_TEST_SUITE_END();

public:
    BuildActionsTests();
    void setUp() override;
    void tearDown() override;

    void testLogging();
    void testProcessSession();
    void testBuildActionProcess();
    void testParsingInfoFromPkgFiles();
    void testPreparingBuild();
    void testConductingBuild();
    void testRepoCleanup();
    void testBuildServiceCleanup();

private:
    void initStorage();
    void loadBasicTestSetup();
    void loadTestConfig();
    void logTestSetup();
    template <typename InternalBuildActionType> void setupBuildAction();
    void resetBuildAction();
    void runBuildAction(const char *message, TimeSpan timeout = TimeSpan::fromSeconds(5));
    template <typename InternalBuildActionType> InternalBuildActionType *internalBuildAction();

    std::string m_configDbFile, m_buildingDbFile;
    ServiceSetup m_setup;
    std::unique_ptr<Io::PasswordFile> m_secrets;
    std::shared_ptr<BuildAction> m_buildAction;
    std::filesystem::path m_workingDir;
    double m_timeoutFactor = 0.0;
};

CPPUNIT_TEST_SUITE_REGISTRATION(BuildActionsTests);

BuildActionsTests::BuildActionsTests()
{
    if (const char *noBuildActionTimeout = std::getenv("BUILD_ACTION_TIMEOUT_FACTOR")) {
        m_timeoutFactor = stringToNumber<double>(noBuildActionTimeout);
    }
}

void BuildActionsTests::setUp()
{
    // save the working directory; the code under test might change it and we want to restore the initial working directory later
    m_workingDir = std::filesystem::current_path();
    cerr << EscapeCodes::Phrases::Info << "test working directory: " << m_workingDir.native() << endl;
}

void BuildActionsTests::tearDown()
{
    std::filesystem::current_path(m_workingDir);
}

void BuildActionsTests::initStorage()
{
    if (!m_setup.config.storage()) {
        m_configDbFile = workingCopyPath("test-build-actions-config.db", WorkingCopyMode::Cleanup);
        m_setup.config.initStorage(m_configDbFile.data());
    }
    if (!m_setup.building.hasStorage()) {
        m_buildingDbFile = workingCopyPath("test-build-actions-building.db", WorkingCopyMode::Cleanup);
        m_setup.building.initStorage(m_buildingDbFile.data());
    }
}

/*!
 * \brief Assigns certain build variables to use fake scripts (instead of invoking e.g. the real makepkg).
 * \remarks The fake scripts are essentially no-ops which merely print the script name and the passed arguments.
 */
void BuildActionsTests::loadBasicTestSetup()
{
    m_setup.workingDirectory = TestApplication::instance()->workingDirectory();
    m_setup.building.workingDirectory = m_setup.workingDirectory + "/building";
    m_setup.building.makePkgPath = std::filesystem::absolute(testFilePath("scripts/fake_makepkg.sh"));
    m_setup.building.makeChrootPkgPath = std::filesystem::absolute(testFilePath("scripts/fake_makechrootpkg.sh"));
    m_setup.building.updatePkgSumsPath = std::filesystem::absolute(testFilePath("scripts/fake_updatepkgsums.sh"));
    m_setup.building.repoAddPath = std::filesystem::absolute(testFilePath("scripts/fake_repo_add.sh"));
    m_setup.building.gpgPath = std::filesystem::absolute(testFilePath("scripts/fake_gpg.sh"));
    m_setup.building.defaultGpgKey = "1234567890";
    m_setup.building.packageCacheDir = m_setup.building.workingDirectory + "/test-cache-dir";
    m_setup.configFilePath = std::filesystem::absolute(testFilePath("test-config/server.conf"));

    std::filesystem::remove_all(m_setup.workingDirectory);
    std::filesystem::create_directories(m_setup.building.workingDirectory);
}

/*!
 * \brief Runs the startup code almost like the actual service does.
 * \remarks Changes the current working directory! Make paths obtained via testFilePath() absolute before calling this function
 *          if they are supposed to be used later.
 */
void BuildActionsTests::loadTestConfig()
{
    initStorage();
    m_setup.loadConfigFiles(false);
    m_setup.building.workingDirectory = m_setup.workingDirectory + "/building";
    m_setup.printDatabases();
    cerr << EscapeCodes::Phrases::Info << "current working directory: " << std::filesystem::current_path() << endl;
    cerr << EscapeCodes::Phrases::Info << "setup working directory: " << m_setup.workingDirectory << endl;
    logTestSetup();
}

/*!
 * \brief Logs all databases and packages of the current test setup.
 */
void BuildActionsTests::logTestSetup()
{
    for (auto &db : m_setup.config.databases) {
        cout << EscapeCodes::Phrases::Info << "Packages of " << db.name << ':' << EscapeCodes::Phrases::End;
        db.allPackages([](LibPkg::StorageID, std::shared_ptr<LibPkg::Package> &&package) {
            cout << " - " << package->name << '\n';
            return false;
        });
    }
    cout.flush();
}

/*!
 * \brief Initializes the fixture's build action.helper
 */
template <typename InternalBuildActionType> void BuildActionsTests::setupBuildAction()
{
    m_buildAction = std::make_shared<BuildAction>(0, &m_setup);
}

/*!
 * \brief Resets the fixture's build action.
 */
void BuildActionsTests::resetBuildAction()
{
    m_buildAction->status = BuildActionStatus::Created;
    m_buildAction->result = BuildActionResult::None;
    m_buildAction->resultData = std::string();
}

/*!
 * \brief Runs the fixture's build action (initialized via setupBuildAction()) until it has finished.
 * \param message The message for asserting whether the build action has finished yet.
 * \param timeout The max. time to wait for the build action to finish. It does not interrupt the handler which is currently
 *        executed (so tests can still get stuck).
 *
 */
void BuildActionsTests::runBuildAction(const char *message, CppUtilities::TimeSpan timeout)
{
    resetBuildAction();
    m_buildAction->start(m_setup, std::move(m_secrets));
    auto &ioc = m_setup.building.ioContext;
    ioc.restart();
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> workGuard = boost::asio::make_work_guard(ioc);
    m_buildAction->setConcludeHandler([&workGuard] { workGuard.reset(); });
    if (m_timeoutFactor == 0.0) {
        ioc.run();
    } else {
        ioc.run_for(std::chrono::microseconds(static_cast<std::chrono::microseconds::rep>((timeout * m_timeoutFactor).totalMicroseconds())));
    }
    CPPUNIT_ASSERT_EQUAL_MESSAGE(message, BuildActionStatus::Finished, m_buildAction->status);
}

/*!
 * \brief Returns the internal build action for the fixture's build action.
 * \remarks Invokes undefined behavior if \tp InternalBuildActionType is not the actual type.
 */
template <typename InternalBuildActionType> InternalBuildActionType *BuildActionsTests::internalBuildAction()
{
    auto *const internalBuildAction = m_buildAction->m_internalBuildAction.get();
    CPPUNIT_ASSERT_MESSAGE("internal build action assigned", internalBuildAction);
    return static_cast<InternalBuildActionType *>(internalBuildAction);
}

/*!
 * \brief Tests basic logging.
 */
void BuildActionsTests::testLogging()
{
    using namespace EscapeCodes;
    m_buildAction = make_shared<BuildAction>(0, &m_setup);
    {
        const auto stderrCheck = OutputCheck(
            [](const std::string &output) {
                TESTUTILS_ASSERT_LIKE_FLAGS(
                    "messages logged on stderr", ".*ERROR.*some error: message.*\n.*info.*\n.*", std::regex::extended, output);
            },
            cerr);
        m_buildAction->log()(Phrases::ErrorMessage, "some error: ", "message", '\n');
        m_buildAction->log()(Phrases::InfoMessage, "info", '\n');
    }
    m_buildAction->conclude(BuildActionResult::Success);
    m_setup.building.ioContext.run();
    const auto output = readFile("logs/build-action-0.log");
    CPPUNIT_ASSERT_EQUAL_MESSAGE(
        "messages added to build action output", "\e[1;31m==> ERROR: \e[0m\e[1msome error: message\n\e[1;37m==> \e[0m\e[1minfo\n"s, output);
}

/*!
 * \brief Tests the ProcessSession class (which is used to spawn processes within build actions capturing the output).
 */
void BuildActionsTests::testProcessSession()
{
    auto &ioc = m_setup.building.ioContext;
    auto session = std::make_shared<ProcessSession>(ioc, [&ioc](boost::process::child &&child, ProcessResult &&result) {
        CPP_UTILITIES_UNUSED(child)
        CPPUNIT_ASSERT_EQUAL(std::error_code(), result.errorCode);
        CPPUNIT_ASSERT_EQUAL(0, result.exitCode);
        CPPUNIT_ASSERT_EQUAL(std::string(), result.error);
        CPPUNIT_ASSERT_EQUAL("line1\nline2"s, result.output);
        ioc.stop();
    });
    session->launch(boost::process::search_path("echo"), "-n", "line1\nline2");
    session.reset();
    ioc.run();
}

/*!
 * \brief Tests the BuildProcessSession class (which is used to spawn processes within build actions creating a log file).
 */
void BuildActionsTests::testBuildActionProcess()
{
    m_buildAction = std::make_shared<BuildAction>(1, &m_setup);

    const auto scriptPath = testFilePath("scripts/print_some_data.sh");
    const auto logFilePath = std::filesystem::path(TestApplication::instance()->workingDirectory()) / "logfile.log";
    std::filesystem::create_directory(logFilePath.parent_path());
    if (std::filesystem::exists(logFilePath)) {
        std::filesystem::remove(logFilePath);
    }

    auto &ioc = m_setup.building.ioContext;
    auto session = std::make_shared<BuildProcessSession>(
        m_buildAction.get(), ioc, "test", std::string(logFilePath), [this](boost::process::child &&child, ProcessResult &&result) {
            CPPUNIT_ASSERT_EQUAL(std::error_code(), result.errorCode);
            CPPUNIT_ASSERT_EQUAL(0, result.exitCode);
            CPPUNIT_ASSERT_GREATER(0, child.native_handle());
            m_buildAction->conclude(BuildActionResult::Success);
        });
    session->launch(scriptPath);
    session.reset();
    ioc.run();

    const auto logFile = readFile(logFilePath);
    const auto logLines = splitStringSimple<std::vector<std::string_view>>(logFile, "\r\n");
    const auto output = readFile("logs/build-action-1.log");
    CPPUNIT_ASSERT_EQUAL(5002_st, logLines.size());
    CPPUNIT_ASSERT_EQUAL("printing some numbers"sv, logLines.front());
    CPPUNIT_ASSERT_EQUAL_MESSAGE("trailing line break", ""sv, logLines.back());
    CPPUNIT_ASSERT_EQUAL_MESSAGE("last line", "line 5000"sv, logLines[logLines.size() - 2u]);
    TESTUTILS_ASSERT_LIKE_FLAGS("PID logged", ".*Launched \"test\", PID: [0-9]+.*\n.*"s, std::regex::extended, output);
}

/*!
 * \brief Tests the ReloadLibraryDependencies build action.
 */
void BuildActionsTests::testParsingInfoFromPkgFiles()
{
    // init config
    initStorage();
    auto &config = m_setup.config;
    for (const auto dbName : { "foo.db"sv, "bar.db"sv, "baz.db"sv }) {
        config.findOrCreateDatabase(dbName, "x86_64"sv);
    }

    // init db object
    auto &fooDb = config.databases[0];
    auto &barDb = config.databases[1];
    const auto harfbuzz = LibPkg::Package::fromPkgFileName("mingw-w64-harfbuzz-1.4.2-1-any.pkg.tar.xz");
    harfbuzz->libprovides = { "harfbuzzlibrary.so" };
    const auto harfbuzzID = fooDb.updatePackage(harfbuzz);
    const auto syncthingtray = LibPkg::Package::fromPkgFileName("syncthingtray-0.6.2-1-x86_64.pkg.tar.xz");
    const auto syncthingtrayID = fooDb.updatePackage(syncthingtray);
    fooDb.localPkgDir = directory(testFilePath("repo/foo/mingw-w64-harfbuzz-1.4.2-1-any.pkg.tar.xz"));
    const auto cmake = LibPkg::Package::fromPkgFileName("cmake-3.8.2-1-x86_64.pkg.tar.xz");
    barDb.updatePackage(cmake);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("origin", LibPkg::PackageOrigin::PackageFileName, cmake->origin);
    barDb.localPkgDir = directory(testFilePath("repo/bar/cmake-3.8.2-1-x86_64.pkg.tar.xz"));
    auto harfbuzzLibraryPresent = false;
    fooDb.providingPackages("harfbuzzlibrary.so", false, [&](LibPkg::StorageID id, const std::shared_ptr<LibPkg::Package> &package) {
        CPPUNIT_ASSERT_EQUAL(harfbuzzID, id);
        CPPUNIT_ASSERT_EQUAL(harfbuzz->name, package->name);
        harfbuzzLibraryPresent = true;
        return true;
    });
    CPPUNIT_ASSERT_MESSAGE("harfbuzz found via \"harfbuzzlibrary.so\" before reload", harfbuzzLibraryPresent);

    auto buildAction = std::make_shared<BuildAction>(0, &m_setup);
    auto reloadLibDependencies = ReloadLibraryDependencies(m_setup, buildAction);
    reloadLibDependencies.run();
    const auto &messages = std::get<BuildActionMessages>(buildAction->resultData);
    CPPUNIT_ASSERT_EQUAL(std::vector<std::string>(), messages.errors);
    CPPUNIT_ASSERT_EQUAL(std::vector<std::string>(), messages.warnings);
    CPPUNIT_ASSERT_EQUAL(std::vector<std::string>(), messages.notes);

    using namespace TestHelper;
    checkHarfbuzzPackagePeDependencies(*harfbuzz);
    checkSyncthingTrayPackageSoDependencies(*syncthingtray);
    checkCmakePackageSoDependencies(*cmake);

    const auto pkgsRequiringLibGCC = config.findPackagesProvidingLibrary("pe-i386::libgcc_s_sjlj-1.dll", true);
    CPPUNIT_ASSERT_EQUAL(1_st, pkgsRequiringLibGCC.size());
    CPPUNIT_ASSERT_EQUAL(harfbuzz->name, pkgsRequiringLibGCC.front().pkg->name);
    CPPUNIT_ASSERT_EQUAL(harfbuzzID, pkgsRequiringLibGCC.front().id);

    const auto pkgsProvidingLibSyncthingConnector = config.findPackagesProvidingLibrary("elf-x86_64::libsyncthingconnector.so.0.6.2", false);
    CPPUNIT_ASSERT_EQUAL(1_st, pkgsProvidingLibSyncthingConnector.size());
    CPPUNIT_ASSERT_EQUAL(syncthingtray->name, pkgsProvidingLibSyncthingConnector.front().pkg->name);
    CPPUNIT_ASSERT_EQUAL(syncthingtrayID, pkgsProvidingLibSyncthingConnector.front().id);

    fooDb.providingPackages("harfbuzzlibrary.so", false, [&](LibPkg::StorageID, const std::shared_ptr<LibPkg::Package> &) {
        CPPUNIT_FAIL("harfbuzz still found via \"harfbuzzlibrary.so\" after reload");
        return true;
    });
}

/*!
 * \brief Tests the PrepareBuild build action.
 */
void BuildActionsTests::testPreparingBuild()
{
    // get meta info
    auto &metaInfo = m_setup.building.metaInfo;
    const auto &typeInfo = metaInfo.typeInfoForId(BuildActionType::PrepareBuild);
    const auto pkgbuildsDirsSetting = std::string(typeInfo.settings[static_cast<std::size_t>(PrepareBuildSettings::PKGBUILDsDirs)].param);

    // load basic test setup and create build action
    loadBasicTestSetup();
    m_buildAction = std::make_shared<BuildAction>(0, &m_setup);
    m_buildAction->type = BuildActionType::PrepareBuild;
    m_buildAction->directory = "prepare-build-test";
    m_buildAction->flags = static_cast<BuildActionFlagType>(PrepareBuildFlags::CleanSrcDir);
    m_buildAction->settings[pkgbuildsDirsSetting] = std::filesystem::absolute(testDirPath("building/pkgbuilds"));
    m_buildAction->packageNames = { "boost", "mingw-w64-gcc" };

    // prepare test configuration
    // - Pretend all dependencies of boost are there except "zstd-1.4.5-1-x86_64.pkg.tar.zst" (so zstd is supposed pulled into the build automatically).
    // - There's a dummy package for zstd which incurs no further dependencies.
    // - The package mingw-w64-gcc is also just a dummy here to test handling variants; it has no dependencies here.
    loadTestConfig();
    auto coreDb = m_setup.config.findDatabase("core", "x86_64");
    CPPUNIT_ASSERT_MESSAGE("core db exists", coreDb);
    for (const auto pkgFileName :
        { "python-3.8.6-1-x86_64.pkg.tar.zst"sv, "python2-2.7.18-2-x86_64.pkg.tar.zst"sv, "bzip2-1.0.8-4-x86_64.pkg.tar.zst"sv,
            "findutils-4.7.0-2-x86_64.pkg.tar.xz"sv, "icu-67.1-1-x86_64.pkg.tar.zst"sv, "openmpi-4.0.5-2-x86_64.pkg.tar.zst"sv,
            "python-numpy-1.19.4-1-x86_64.pkg.tar.zst"sv, "python2-numpy-1.16.6-1-x86_64.pkg.tar.zst"sv, "zlib-1:1.2.11-4-x86_64.pkg.tar.xz"sv }) {
        coreDb->updatePackage(LibPkg::Package::fromPkgFileName(pkgFileName));
    }

    // run without destination database
    runBuildAction("prepare build without destination db");
    CPPUNIT_ASSERT_EQUAL_MESSAGE("failure without destination db", BuildActionResult::Failure, m_buildAction->result);
    CPPUNIT_ASSERT_EQUAL_MESSAGE(
        "failure without destination db", "not exactly one destination database specified"s, std::get<std::string>(m_buildAction->resultData));

    // run with destination database (yes, the database is called "boost" in this test setup as well)
    m_buildAction->destinationDbs = { "boost" };
    runBuildAction("prepare build: successful preparation");
    CPPUNIT_ASSERT_EQUAL_MESSAGE("success", BuildActionResult::Success, m_buildAction->result);
    CPPUNIT_ASSERT_MESSAGE("build preparation present", std::holds_alternative<BuildPreparation>(m_buildAction->resultData));
    const auto &buildPreparation = std::get<BuildPreparation>(m_buildAction->resultData);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("target db set", "boost"s, buildPreparation.targetDb);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("target arch set", "x86_64"s, buildPreparation.targetArch);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("staging db set", "boost-staging"s, buildPreparation.stagingDb);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("no cyclic leftovers", 0_st, buildPreparation.cyclicLeftovers.size());
    CPPUNIT_ASSERT_EQUAL_MESSAGE("no warnings", 0_st, buildPreparation.warnings.size());
    CPPUNIT_ASSERT_EQUAL_MESSAGE("no error", std::string(), buildPreparation.error);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("manually ordered not set", false, buildPreparation.manuallyOrdered);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("db config has 2 dbs", 2_st, buildPreparation.dbConfig.size());
    CPPUNIT_ASSERT_EQUAL_MESSAGE("first db", "boost"s, buildPreparation.dbConfig[0].first);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("second db", "core"s, buildPreparation.dbConfig[1].first);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("staging db config has 3 dbs", 3_st, buildPreparation.stagingDbConfig.size());
    CPPUNIT_ASSERT_EQUAL_MESSAGE("first staging db", "boost-staging"s, buildPreparation.stagingDbConfig[0].first);
    const auto &batches = buildPreparation.batches;
    CPPUNIT_ASSERT_EQUAL_MESSAGE("two batches present", 2_st, batches.size());
    const auto expectedFirstBatch = std::vector<std::string>{ "mingw-w64-gcc", "zstd" };
    CPPUNIT_ASSERT_EQUAL_MESSAGE("first batch", expectedFirstBatch, batches[0]);
    const auto expectedSecondBatch = std::vector<std::string>{ "boost" };
    CPPUNIT_ASSERT_EQUAL_MESSAGE("second batch", expectedSecondBatch, batches[1]);
    CPPUNIT_ASSERT_MESSAGE(
        "build-preparation.json created", std::filesystem::is_regular_file("building/build-data/prepare-build-test/build-preparation.json"));
    CPPUNIT_ASSERT_MESSAGE(
        "build-progress.json created", std::filesystem::is_regular_file("building/build-data/prepare-build-test/build-progress.json"));
    for (const auto pkg : { "boost"sv, "mingw-w64-gcc"sv, "zstd"sv }) {
        CPPUNIT_ASSERT_MESSAGE(
            "PKGBUILD for " % pkg + " created", std::filesystem::is_regular_file("building/build-data/prepare-build-test/" % pkg + "/src/PKGBUILD"));
    }
}

static void ensureEmptyDir(const std::filesystem::path &path)
{
    if (std::filesystem::exists(path)) {
        std::filesystem::remove_all(path);
    }
    std::filesystem::create_directories(path);
}

static void createPackageDirs(bool empty = true)
{
    constexpr auto buildDir = "building/build-data/conduct-build-test/"sv;
    for (const auto pkg : { "foo-1-1"sv, "bar-2-1"sv, "baz-3-1"sv }) {
        const auto pkgName = pkg.substr(0, 3);
        ensureEmptyDir(argsToString(buildDir, pkgName, "/src"sv));
        ensureEmptyDir(argsToString(buildDir, pkgName, "/pkg"sv));
        if (!empty) {
            const auto relativeFakePackagePath = argsToString(buildDir, pkgName, "/src/"sv, pkg, "-x86_64.pkg.tar.zst"sv);
            try {
                std::filesystem::copy_file(testFilePath(relativeFakePackagePath), relativeFakePackagePath);
            } catch (const std::runtime_error &) {
                writeFile(relativeFakePackagePath, pkg);
            }
            writeFile(argsToString(buildDir, pkgName, "/src/PKGBUILD"sv), pkg);
            writeFile(argsToString(buildDir, pkgName, "/src/"sv, pkg, ".src.tar.gz"sv), pkg);
        }
    }
}

/*!
 * \brief Tests the ConductBuild build action.
 */
void BuildActionsTests::testConductingBuild()
{
    // load basic test setup and create build action
    loadBasicTestSetup();
    initStorage();
    m_buildAction = std::make_shared<BuildAction>(0, &m_setup);
    m_buildAction->type = BuildActionType::ConductBuild;
    m_buildAction->directory = "conduct-build-test";
    m_buildAction->packageNames = { "boost" }; // ignore packages foo/bar/baz for first tests
    m_buildAction->flags = static_cast<BuildActionFlagType>(ConductBuildFlags::BuildAsFarAsPossible | ConductBuildFlags::SaveChrootOfFailures
        | ConductBuildFlags::UpdateChecksums | ConductBuildFlags::AutoStaging);

    // run without build preparation
    runBuildAction("conduct build without build preparation");
    CPPUNIT_ASSERT_EQUAL_MESSAGE("failure without preparation JSON", BuildActionResult::Failure, m_buildAction->result);
    TESTUTILS_ASSERT_LIKE(
        "no preparation JSON", "Unable to restore build-preparation.json:.*not exist.*", std::get<std::string>(m_buildAction->resultData));

    // create fake build preparation
    const auto origPkgbuildFile = workingCopyPathAs("building/build-data/conduct-build-test/boost/src/PKGBUILD", "orig-src-dir/boost/PKGBUILD");
    const auto origSourceDir = std::filesystem::absolute(directory(origPkgbuildFile));
    auto prepData = readFile(testFilePath("building/build-data/conduct-build-test/build-preparation.json"));
    findAndReplace(prepData, "$ORIGINAL_SOURCE_DIRECTORY", origSourceDir.native());
    findAndReplace(prepData, "$TEST_FILES_PATH", "TODO");
    const auto buildDir = std::filesystem::absolute(workingCopyPath("building", WorkingCopyMode::NoCopy));
    const auto prepFile
        = std::filesystem::absolute(workingCopyPath("building/build-data/conduct-build-test/build-preparation.json", WorkingCopyMode::NoCopy));
    writeFile(prepFile.native(), prepData);
    auto progressData = readFile(testFilePath("building/build-data/conduct-build-test/build-progress.json"));
    const auto progressFile
        = std::filesystem::absolute(workingCopyPath("building/build-data/conduct-build-test/build-progress.json", WorkingCopyMode::NoCopy));
    writeFile(progressFile.native(), progressData);
    std::filesystem::copy(testDirPath("building/build-data/conduct-build-test/boost"),
        m_setup.workingDirectory + "/building/build-data/conduct-build-test/boost", std::filesystem::copy_options::recursive);

    // run without chroot configuration
    runBuildAction("conduct build without chroot configuration");
    CPPUNIT_ASSERT_EQUAL_MESSAGE("failure without chroot configuration", BuildActionResult::Failure, m_buildAction->result);
    CPPUNIT_ASSERT_EQUAL_MESSAGE(
        "no chroot configuration", "The chroot directory is not configured."s, std::get<std::string>(m_buildAction->resultData));

    // configure chroot directory
    m_setup.building.chrootDir = testDirPath("test-config/chroot-dir");

    // run with misconfigured destination db
    runBuildAction("conduct build with misconfigured destination db (1)");
    CPPUNIT_ASSERT_EQUAL_MESSAGE("failure without destination db (1)", BuildActionResult::Failure, m_buildAction->result);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("destination db missing (1)",
        "Auto-staging is enabled but the staging database \"boost-staging@x86_64\" specified in build-preparation.json can not be found."s,
        std::get<std::string>(m_buildAction->resultData));
    loadTestConfig();
    runBuildAction("conduct build with misconfigured destination db (2)");
    CPPUNIT_ASSERT_EQUAL_MESSAGE("failure without destination db (2)", BuildActionResult::Failure, m_buildAction->result);
    TESTUTILS_ASSERT_LIKE("destination db missing (2)", "Destination repository \"repos/boost/os/x86_64\" does not exist.*"s,
        std::get<std::string>(m_buildAction->resultData));

    // create repositories
    const auto reposPath = testDirPath("test-config/repos");
    const auto reposWorkingCopyPath = std::filesystem::path(m_setup.workingDirectory + "/repos");
    std::filesystem::create_directory(reposWorkingCopyPath);
    std::filesystem::copy(reposPath, reposWorkingCopyPath, std::filesystem::copy_options::recursive);

    // run without chroot directory
    runBuildAction("conduct build without chroot directory");
    CPPUNIT_ASSERT_EQUAL_MESSAGE("no chroot directory: results in failure", BuildActionResult::Failure, m_buildAction->result);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("no chroot directory: result data states affected packages", "failed to build packages: boost"s,
        std::get<std::string>(m_buildAction->resultData));
    auto *internalData = internalBuildAction<ConductBuild>();
    TESTUTILS_ASSERT_LIKE("no chroot directory: package-level error message",
        "Chroot directory \".*/test-config/chroot-dir/arch-x86_64/root\" is no directory."s,
        internalData->m_buildProgress.progressByPackage["boost"].error);

    // create chroot directory
    const auto chrootSkelPath = testDirPath("test-config/chroot-skel");
    const auto chrootDirWorkingCopyPath = std::filesystem::path(m_setup.workingDirectory + "/chroot-dir");
    const auto rootChrootWorkingCopyPath = chrootDirWorkingCopyPath / "arch-x86_64/root";
    std::filesystem::create_directory(chrootDirWorkingCopyPath);
    std::filesystem::copy(m_setup.building.chrootDir, chrootDirWorkingCopyPath, std::filesystem::copy_options::recursive);
    std::filesystem::create_directories(rootChrootWorkingCopyPath);
    std::filesystem::copy(chrootSkelPath, rootChrootWorkingCopyPath, std::filesystem::copy_options::recursive);
    m_setup.building.chrootDir = chrootDirWorkingCopyPath.string(); // assign the created chroot directory
    writeFile(progressFile.native(), progressData); // reset "build-progress.json" so the new chroot directory is actually used

    // run without having makepkg actually producing any packages
    runBuildAction("conduct build without producing any packages");
    CPPUNIT_ASSERT_EQUAL_MESSAGE("no packages produced: results in failure", BuildActionResult::Failure, m_buildAction->result);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("no packages produced: result data states affected packages", "failed to build packages: boost"s,
        std::get<std::string>(m_buildAction->resultData));
    internalData = internalBuildAction<ConductBuild>();
    TESTUTILS_ASSERT_LIKE("no packages produced: package-level error message",
        "not all.*packages exist.*boost-1.73.0-1.src.tar.gz.*boost-libs-1\\.73\\.0-1-x86_64\\.pkg\\.tar\\.zst.*boost-1\\.73\\.0-1-x86_64\\.pkg\\.tar\\.zst"s,
        internalData->m_buildProgress.progressByPackage["boost"].error);
    CPPUNIT_ASSERT_MESSAGE(
        "no packages produced: package considered finished", !internalData->m_buildProgress.progressByPackage["boost"].finished.isNull());
    CPPUNIT_ASSERT_MESSAGE("no packages produced: package not added to repo", !internalData->m_buildProgress.progressByPackage["boost"].addedToRepo);

    // prepare some actual packages
    std::filesystem::copy(testFilePath("test-config/fake-build-artefacts/boost-1.73.0-1.src.tar.gz"),
        buildDir / "build-data/conduct-build-test/boost/pkg/boost-1.73.0-1.src.tar.gz");
    std::filesystem::copy(testFilePath("test-config/fake-build-artefacts/boost-1.73.0-1-x86_64.pkg.tar.zst"),
        buildDir / "build-data/conduct-build-test/boost/pkg/boost-1.73.0-1-x86_64.pkg.tar.zst");
    std::filesystem::copy(testFilePath("test-config/fake-build-artefacts/boost-libs-1.73.0-1-x86_64.pkg.tar.zst"),
        buildDir / "build-data/conduct-build-test/boost/pkg/boost-libs-1.73.0-1-x86_64.pkg.tar.zst");

    // conduct build without staging
    runBuildAction("conduct build without staging");
    CPPUNIT_ASSERT_EQUAL_MESSAGE("no staging needed: success", BuildActionResult::Success, m_buildAction->result);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("no staging needed: no result data present", ""s, std::get<std::string>(m_buildAction->resultData));
    internalData = internalBuildAction<ConductBuild>();
    CPPUNIT_ASSERT_MESSAGE("no staging needed: rebuild list empty", internalData->m_buildProgress.rebuildList.empty());
    CPPUNIT_ASSERT_MESSAGE(
        "no staging needed: package considered finished", !internalData->m_buildProgress.progressByPackage["boost"].finished.isNull());
    CPPUNIT_ASSERT_MESSAGE("no staging needed: package added to repo", internalData->m_buildProgress.progressByPackage["boost"].addedToRepo);

    // check whether log files have been created accordingly
    CPPUNIT_ASSERT_EQUAL_MESSAGE("no staging needed: download log", "fake makepkg: -f --nodeps --nobuild --source\n"s,
        readFile("building/build-data/conduct-build-test/boost/pkg/download.log"));
    CPPUNIT_ASSERT_EQUAL_MESSAGE(
        "no staging needed: updpkgsums log", "fake updatepkgsums: \n"s, readFile("building/build-data/conduct-build-test/boost/pkg/updpkgsums.log"));
    TESTUTILS_ASSERT_LIKE("no staging needed: build log",
        "fake makechrootpkg: -c -u -Y .*building/test-cache-dir/x86_64 -r .*chroot-dir/arch-x86_64 -l buildservice --\n"s,
        readFile("building/build-data/conduct-build-test/boost/pkg/build.log"));
    TESTUTILS_ASSERT_LIKE("no staging needed: repo-add log",
        "fake repo-add: boost.db.tar.zst boost(-libs)?-1\\.73\\.0-1-x86_64.pkg.tar.zst boost(-libs)?-1\\.73\\.0-1-x86_64.pkg.tar.zst\n"s,
        readFile("building/build-data/conduct-build-test/boost/pkg/repo-add.log"));

    // check whether packages have actually been added to repo
    CPPUNIT_ASSERT_MESSAGE(
        "no staging needed: package added to repo (0)", std::filesystem::is_regular_file("repos/boost/os/src/boost-1.73.0-1.src.tar.gz"));
    CPPUNIT_ASSERT_MESSAGE(
        "no staging needed: package added to repo (1)", std::filesystem::is_regular_file("repos/boost/os/x86_64/boost-1.73.0-1-x86_64.pkg.tar.zst"));
    CPPUNIT_ASSERT_MESSAGE("no staging needed: package added to repo (2)",
        std::filesystem::is_regular_file("repos/boost/os/x86_64/boost-libs-1.73.0-1-x86_64.pkg.tar.zst"));
    CPPUNIT_ASSERT_MESSAGE("no staging needed: signature added to repo (0)",
        std::filesystem::is_regular_file("repos/boost/os/x86_64/boost-1.73.0-1-x86_64.pkg.tar.zst.sig"));
    CPPUNIT_ASSERT_MESSAGE("no staging needed: signature added to repo (1)",
        std::filesystem::is_regular_file("repos/boost/os/x86_64/boost-libs-1.73.0-1-x86_64.pkg.tar.zst.sig"));
    CPPUNIT_ASSERT_EQUAL_MESSAGE("no staging needed: signature looks as expected",
        "fake signature with GPG key 1234567890 boost-libs-1.73.0-1-x86_64.pkg.tar.zst\n"s,
        readFile("repos/boost/os/x86_64/boost-libs-1.73.0-1-x86_64.pkg.tar.zst.sig"));

    // add packages needing a rebuild to trigger auto-staging
    m_setup.config.loadAllPackages(false, true);
    auto *const boostDb = m_setup.config.findDatabase("boost"sv, "x86_64"sv);
    auto *const miscDb = m_setup.config.findDatabase("misc"sv, "x86_64"sv);
    CPPUNIT_ASSERT_MESSAGE("boost database present", boostDb);
    CPPUNIT_ASSERT_MESSAGE("misc database present", miscDb);
    auto boostLibsPackage = boostDb->findPackage("boost-libs");
    CPPUNIT_ASSERT_MESSAGE("boost-libs package present", boostLibsPackage);
    boostLibsPackage->libprovides = { "elf-x86_64::libboost_regex.so.1.72.0" };
    boostLibsPackage->libdepends = { "elf-x86_64::libstdc++.so.6" };
    boostDb->updatePackage(boostLibsPackage);
    auto sourceHighlightPackage = miscDb->findPackage("source-highlight");
    CPPUNIT_ASSERT_MESSAGE("source-highlight package present", sourceHighlightPackage);
    sourceHighlightPackage->libprovides = { "elf-x86_64::libsource-highlight.so.4" };
    sourceHighlightPackage->libdepends
        = { "elf-x86_64::libboost_regex.so.1.72.0", "elf-x86_64::libsource-highlight.so.4", "elf-x86_64::libstdc++.so.6" };
    miscDb->updatePackage(sourceHighlightPackage);
    m_setup.printDatabases();
    logTestSetup();

    // conduct build with staging only with boost
    {
        writeFile(progressFile.native(), progressData); // reset "build-progress.json" so the package is re-considered
        runBuildAction("conduct build with staging");
        CPPUNIT_ASSERT_EQUAL_MESSAGE("staging needed: success", BuildActionResult::Success, m_buildAction->result);
        CPPUNIT_ASSERT_EQUAL_MESSAGE("staging needed: no result data present", ""s, std::get<std::string>(m_buildAction->resultData));
        internalData = internalBuildAction<ConductBuild>();
        const auto &rebuildList = internalData->m_buildProgress.rebuildList;
        const auto rebuildInfoForMisc = rebuildList.find("misc");
        CPPUNIT_ASSERT_EQUAL_MESSAGE("staging needed: rebuild list contains 1 database", 1_st, rebuildList.size());
        CPPUNIT_ASSERT_MESSAGE("staging needed: rebuild info for misc present", rebuildInfoForMisc != rebuildList.end());
        const auto rebuildInfoForSourceHighlight = rebuildInfoForMisc->second.find("source-highlight");
        const auto expectedLibprovides = std::vector<std::string>{ "elf-x86_64::libboost_regex.so.1.72.0" };
        CPPUNIT_ASSERT_MESSAGE(
            "staging needed: rebuild info for source-highlight present", rebuildInfoForSourceHighlight != rebuildInfoForMisc->second.end());
        CPPUNIT_ASSERT_EQUAL_MESSAGE(
            "staging needed: libprovides for source-highlight present", expectedLibprovides, rebuildInfoForSourceHighlight->second.libprovides);

        // check whether log files have been created accordingly
        TESTUTILS_ASSERT_LIKE("no staging needed: repo-add log",
            "fake repo-add: boost-staging.db.tar.zst boost(-libs)?-1\\.73\\.0-1-x86_64.pkg.tar.zst boost(-libs)?-1\\.73\\.0-1-x86_64.pkg.tar.zst\n"s,
            readFile("building/build-data/conduct-build-test/boost/pkg/repo-add.log"));

        // check whether package have been added to staging repo
        CPPUNIT_ASSERT_MESSAGE(
            "staging needed: package added to repo (0)", std::filesystem::is_regular_file("repos/boost-staging/os/src/boost-1.73.0-1.src.tar.gz"));
        CPPUNIT_ASSERT_MESSAGE("staging needed: package added to repo (1)",
            std::filesystem::is_regular_file("repos/boost-staging/os/x86_64/boost-1.73.0-1-x86_64.pkg.tar.zst"));
        CPPUNIT_ASSERT_MESSAGE("staging needed: package added to repo (2)",
            std::filesystem::is_regular_file("repos/boost-staging/os/x86_64/boost-libs-1.73.0-1-x86_64.pkg.tar.zst"));
        CPPUNIT_ASSERT_MESSAGE("staging needed: signature added to repo (0)",
            std::filesystem::is_regular_file("repos/boost-staging/os/x86_64/boost-1.73.0-1-x86_64.pkg.tar.zst.sig"));
        CPPUNIT_ASSERT_MESSAGE("staging needed: signature added to repo (1)",
            std::filesystem::is_regular_file("repos/boost-staging/os/x86_64/boost-libs-1.73.0-1-x86_64.pkg.tar.zst.sig"));
    }

    // create directories for further packages with dummy PKGBUILDs and build results
    createPackageDirs(false);

    // conduct build with staging and multiple batches
    {
        writeFile(progressFile.native(), progressData); // reset "build-progress.json" so the packages are re-considered
        m_buildAction->packageNames.clear(); // don't build only "boost"
        runBuildAction("conduct build with staging");
        CPPUNIT_ASSERT_EQUAL_MESSAGE(
            "staging needed: failure as build result of baz is no valid archive", BuildActionResult::Failure, m_buildAction->result);
        CPPUNIT_ASSERT_EQUAL_MESSAGE(
            "staging needed: failed packages listed", "failed to build packages: baz"s, std::get<std::string>(m_buildAction->resultData));
        internalData = internalBuildAction<ConductBuild>();
        const auto &rebuildList = internalData->m_buildProgress.rebuildList;
        const auto rebuildInfoForMisc = rebuildList.find("misc");
        CPPUNIT_ASSERT_EQUAL_MESSAGE("staging needed: rebuild list contains 1 database", 1_st, rebuildList.size());
        CPPUNIT_ASSERT_MESSAGE("staging needed: rebuild info for misc present", rebuildInfoForMisc != rebuildList.end());
        const auto rebuildInfoForSourceHighlight = rebuildInfoForMisc->second.find("source-highlight");
        const auto expectedLibprovides = std::vector<std::string>{ "elf-x86_64::libboost_regex.so.1.72.0" };
        CPPUNIT_ASSERT_MESSAGE(
            "staging needed: rebuild info for source-highlight present", rebuildInfoForSourceHighlight != rebuildInfoForMisc->second.end());
        CPPUNIT_ASSERT_EQUAL_MESSAGE(
            "staging needed: libprovides for source-highlight present", expectedLibprovides, rebuildInfoForSourceHighlight->second.libprovides);

        // check whether log files have been created accordingly
        TESTUTILS_ASSERT_LIKE("no staging needed: repo-add log",
            "fake repo-add: boost-staging.db.tar.zst boost(-libs)?-1\\.73\\.0-1-x86_64.pkg.tar.zst boost(-libs)?-1\\.73\\.0-1-x86_64.pkg.tar.zst\n"s,
            readFile("building/build-data/conduct-build-test/boost/pkg/repo-add.log"));

        // check whether package have been added to staging repo
        CPPUNIT_ASSERT_MESSAGE("staging needed: boost package added to staging repo (0)",
            std::filesystem::is_regular_file("repos/boost-staging/os/src/boost-1.73.0-1.src.tar.gz"));
        CPPUNIT_ASSERT_MESSAGE("staging needed: boost package added to staging repo (1)",
            std::filesystem::is_regular_file("repos/boost-staging/os/x86_64/boost-1.73.0-1-x86_64.pkg.tar.zst"));
        CPPUNIT_ASSERT_MESSAGE("staging needed: boost package added to staging repo (2)",
            std::filesystem::is_regular_file("repos/boost-staging/os/x86_64/boost-libs-1.73.0-1-x86_64.pkg.tar.zst"));
        CPPUNIT_ASSERT_MESSAGE("staging needed: boost signature added to staging repo (0)",
            std::filesystem::is_regular_file("repos/boost-staging/os/x86_64/boost-1.73.0-1-x86_64.pkg.tar.zst.sig"));
        CPPUNIT_ASSERT_MESSAGE("staging needed: boost signature added to staging repo (1)",
            std::filesystem::is_regular_file("repos/boost-staging/os/x86_64/boost-libs-1.73.0-1-x86_64.pkg.tar.zst.sig"));
        CPPUNIT_ASSERT_MESSAGE("staging needed: foo package from first batch still added to normal repo (0)",
            std::filesystem::is_regular_file("repos/boost/os/src/foo-1-1.src.tar.gz"));
        CPPUNIT_ASSERT_MESSAGE("staging needed: foo package from first batch still added to normal repo (1)",
            std::filesystem::is_regular_file("repos/boost/os/x86_64/foo-1-1-x86_64.pkg.tar.zst"));
        CPPUNIT_ASSERT_MESSAGE("staging needed: bar package from next batch added to staging repo as well (0)",
            std::filesystem::is_regular_file("repos/boost-staging/os/src/bar-2-1.src.tar.gz"));
        CPPUNIT_ASSERT_MESSAGE("staging needed: bar package from next batch added to staging repo as well (1)",
            std::filesystem::is_regular_file("repos/boost-staging/os/x86_64/bar-2-1-x86_64.pkg.tar.zst"));
    }

    // define expected errors for subsequent tests
    const auto expectedFooError = "not all source/binary packages exist after the build as expected: foo-1-1.src.tar.gz, foo-1-1-x86_64.pkg.tar.zst"s;
    const auto expectedBarError = "not all source/binary packages exist after the build as expected: bar-2-1.src.tar.gz, bar-2-1-x86_64.pkg.tar.zst"s;
    const auto expectedBazError = "not all source/binary packages exist after the build as expected: baz-3-1.src.tar.gz, baz-3-1-x86_64.pkg.tar.zst"s;
    const auto expectedDependencyError = "unable to build because dependency failed"s;

    // empty directories for further packages again, further tests are conducted with empty src dirs to let builds fail
    createPackageDirs();

    // conduct build again with all packages/batches
    {
        writeFile(progressFile.native(), progressData); // reset "build-progress.json" so the packages are re-considered
        m_buildAction->flags = noBuildActionFlags;
        runBuildAction("conduct build with all packages");
        CPPUNIT_ASSERT_EQUAL_MESSAGE(
            "failure as packages foo/bar/baz are not actually sufficiently configured", BuildActionResult::Failure, m_buildAction->result);
        internalData = internalBuildAction<ConductBuild>();
        const auto &progressByPackage = internalData->m_buildProgress.progressByPackage;
        CPPUNIT_ASSERT_EQUAL_MESSAGE("build of foo attempted", expectedFooError, progressByPackage.at("foo").error);
        CPPUNIT_ASSERT_EQUAL_MESSAGE("build of foo attempted", expectedFooError, progressByPackage.at("foo").error);
        CPPUNIT_ASSERT_EQUAL_MESSAGE("build of bar skipped (as the previous batch failed)", std::string(), progressByPackage.at("bar").error);
        CPPUNIT_ASSERT_EQUAL_MESSAGE("build of baz skipped (as the previous batch failed)", std::string(), progressByPackage.at("baz").error);
    }

    // conduct build again with all packages/batches, building as far as possible
    {
        writeFile(progressFile.native(), progressData); // reset "build-progress.json" so the packages are re-considered
        m_buildAction->flags = static_cast<BuildActionFlagType>(ConductBuildFlags::BuildAsFarAsPossible);
        runBuildAction("conduct build with all packages, building as far as possible");
        CPPUNIT_ASSERT_EQUAL_MESSAGE("failure, same as before", BuildActionResult::Failure, m_buildAction->result);
        internalData = internalBuildAction<ConductBuild>();
        const auto &progressByPackage = internalData->m_buildProgress.progressByPackage;
        CPPUNIT_ASSERT_EQUAL_MESSAGE("build of foo still attempted", expectedFooError, progressByPackage.at("foo").error);
        CPPUNIT_ASSERT_EQUAL_MESSAGE("build of bar attempted now", expectedBarError, progressByPackage.at("bar").error);
        CPPUNIT_ASSERT_EQUAL_MESSAGE("build of baz attempted now", expectedBazError, progressByPackage.at("baz").error);
    }

    // conduct build again with all packages/batches, building as far as possible assuming dependency between packages
    {
        // reset "build-progress.json" so the packages are re-considered
        writeFile(progressFile.native(), progressData);

        // introduce dependencies
        auto &buildPreparation = internalData->m_buildPreparation;
        auto &barBuildData = buildPreparation.buildData["bar"];
        barBuildData.sourceInfo->checkDependencies.emplace_back("foo");
        auto &bazBuildData = buildPreparation.buildData["baz"];
        bazBuildData.packages.at(0).pkg->dependencies.emplace_back("boost");
        const auto buildPreparationJson = buildPreparation.toJson();
        writeFile(prepFile.native(), std::string_view(buildPreparationJson.GetString(), buildPreparationJson.GetSize()));

        m_buildAction->flags = static_cast<BuildActionFlagType>(ConductBuildFlags::BuildAsFarAsPossible);
        runBuildAction("conduct build with all packages, building as far as possible");
        CPPUNIT_ASSERT_EQUAL_MESSAGE("failure, same as before", BuildActionResult::Failure, m_buildAction->result);
        internalData = internalBuildAction<ConductBuild>();
        const auto &progressByPackage = internalData->m_buildProgress.progressByPackage;
        CPPUNIT_ASSERT_EQUAL_MESSAGE("build of boost succeeded", std::string(), progressByPackage.at("boost").error);
        CPPUNIT_ASSERT_EQUAL_MESSAGE("build of foo still attempted", expectedFooError, progressByPackage.at("foo").error);
        CPPUNIT_ASSERT_EQUAL_MESSAGE("build of bar skipped because foo failed", expectedDependencyError, progressByPackage.at("bar").error);
        CPPUNIT_ASSERT_EQUAL_MESSAGE("build of baz still attempted", expectedBazError, progressByPackage.at("baz").error);
    }

    // conduct build again with all packages/batches, building as far as possible assuming dependency between packages
    {
        // reset "build-progress.json" so the packages are re-considered
        writeFile(progressFile.native(), progressData);

        // assume boost fails as well
        std::filesystem::remove(buildDir / "build-data/conduct-build-test/boost/pkg/boost-libs-1.73.0-1-x86_64.pkg.tar.zst"); // assume boost fails

        m_buildAction->flags = static_cast<BuildActionFlagType>(ConductBuildFlags::BuildAsFarAsPossible);
        runBuildAction("conduct build with all packages, building as far as possible");
        CPPUNIT_ASSERT_EQUAL_MESSAGE("failure, same as before", BuildActionResult::Failure, m_buildAction->result);
        internalData = internalBuildAction<ConductBuild>();
        const auto &progressByPackage = internalData->m_buildProgress.progressByPackage;
        const auto expectedBoostError = "not all source/binary packages exist after the build as expected: boost-libs-1.73.0-1-x86_64.pkg.tar.zst"s;
        CPPUNIT_ASSERT_EQUAL_MESSAGE("build of boost failed", expectedBoostError, progressByPackage.at("boost").error);
        CPPUNIT_ASSERT_EQUAL_MESSAGE("build of foo still attempted", expectedFooError, progressByPackage.at("foo").error);
        CPPUNIT_ASSERT_EQUAL_MESSAGE("build of bar skipped because foo failed", expectedDependencyError, progressByPackage.at("bar").error);
        CPPUNIT_ASSERT_EQUAL_MESSAGE("build of baz skipped because boost failed", expectedDependencyError, progressByPackage.at("baz").error);
    }
}

static void hardlinkOrCopy(
    const std::filesystem::path &from, const std::filesystem::path &to, std::filesystem::copy_options options = std::filesystem::copy_options::none)
{
    try {
        std::filesystem::copy(from, to, options | std::filesystem::copy_options::create_hard_links);
    } catch (const std::filesystem::filesystem_error &e) {
        if (e.code() != std::errc::cross_device_link) {
            throw;
        }
        std::filesystem::copy(from, to, options);
    }
}

static void copyRepo(const std::filesystem::path &origRepoDir, const std::filesystem::path &destRepoDir)
{
    const auto origFiles = std::filesystem::directory_iterator(origRepoDir);
    std::filesystem::create_directories(destRepoDir);
    for (const auto &origFile : origFiles) {
        // preserve "../any/…" symlinks, otherwise create hard links to avoid copies (we won't modify any files here, just possibly delete
        // them again)
        if (origFile.is_symlink()) {
            const auto target = std::filesystem::read_symlink(origFile);
            if (const auto parentPath = target.parent_path(); parentPath.empty() || parentPath == "../any") {
                std::filesystem::copy(origFile.path(), destRepoDir / origFile.path().filename(), std::filesystem::copy_options::copy_symlinks);
            } else {
                hardlinkOrCopy(std::filesystem::absolute(origRepoDir / target), destRepoDir / origFile.path().filename());
            }
        } else {
            hardlinkOrCopy(origFile.path(), destRepoDir / origFile.path().filename());
        }
    }
}

static std::set<std::string> listFiles(const std::filesystem::path &dir)
{
    auto res = std::set<std::string>();
    for (const auto &entry : std::filesystem::recursive_directory_iterator(dir)) {
        if (!entry.is_directory()) {
            res.emplace(entry.path().lexically_relative(dir).string());
        }
    }
    return res;
}

void BuildActionsTests::testRepoCleanup()
{
    // create a working copy of the test repo "misc" which this test is going to run the cleanup on
    const auto workingDir = std::filesystem::absolute(TestApplication::instance()->workingDirectory()) / "cleanup-test";
    std::filesystem::remove_all(workingDir);
    m_setup.workingDirectory = workingDir;
    m_setup.configFilePath = std::filesystem::absolute(testFilePath("test-config/server.conf"));
    const auto origRepoDir = std::filesystem::absolute(testDirPath("test-config/repos/misc/os"));
    const auto repoDir = workingDir / "misc/os";
    const auto repoDirAny = repoDir / "any", repoDirSrc = repoDir / "src";
    const auto repoDir32 = repoDir / "i686", repoDir64 = repoDir / "x86_64";
    copyRepo(origRepoDir / "any", repoDirAny);
    copyRepo(origRepoDir / "src", repoDirSrc);
    copyRepo(origRepoDir / "i686", repoDir32);
    copyRepo(origRepoDir / "x86_64", repoDir64);

    // parse db
    // note: The db actually only contains source-highlight and mingw-w64-harfbuzz
    initStorage();
    auto *const miscDb = m_setup.config.findOrCreateDatabase("misc"sv, "x86_64"sv);
    miscDb->path = repoDir64 / "misc.db";
    miscDb->localDbDir = miscDb->localPkgDir = repoDir64;
    miscDb->loadPackagesFromConfiguredPaths();

    // create and run build action
    m_buildAction = std::make_shared<BuildAction>(0, &m_setup);
    m_buildAction->type = BuildActionType::CleanRepository;
    m_buildAction->flags = static_cast<BuildActionFlagType>(CleanRepositoryFlags::DryRun);
    m_buildAction->destinationDbs = { "misc" };
    runBuildAction("repo cleanup, dry run");

    // check generated messages
    auto &messages = std::get<BuildActionMessages>(m_buildAction->resultData);
    std::sort(messages.notes.begin(), messages.notes.end());
    CPPUNIT_ASSERT_EQUAL_MESSAGE("no warnings", std::vector<std::string>(), messages.warnings);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("no errors", std::vector<std::string>(), messages.errors);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("notes present", 6_st, messages.notes.size());
    TESTUTILS_ASSERT_LIKE("archived (in any)", "Archived .*misc/os/any/mingw-w64-crt-6\\.0\\.0-1-any\\.pkg\\.tar\\.xz \\(current version: removed\\)",
        messages.notes[0]);
    TESTUTILS_ASSERT_LIKE("archived (in x86_64)",
        "Archived .*misc/os/x86_64/mingw-w64-crt-6\\.0\\.0-1-any\\.pkg\\.tar\\.xz \\(current version: removed\\)", messages.notes[1]);
    TESTUTILS_ASSERT_LIKE("archived (only in x86_64, file in any preserved)",
        "Archived .*misc/os/x86_64/perl-linux-desktopfiles-0\\.22-2-any\\.pkg\\.tar\\.xz \\(current version: removed\\)", messages.notes[2]);
    TESTUTILS_ASSERT_LIKE("archived (only in x86_64, no file in any)",
        "Archived .*misc/os/x86_64/syncthingtray-0\\.6\\.2-1-x86_64\\.pkg\\.tar\\.xz \\(current version: removed\\)", messages.notes[3]);
    TESTUTILS_ASSERT_LIKE("deleted chunk file", "Deleted .*misc/os/x86_64/chunk-file", messages.notes[4]);
    TESTUTILS_ASSERT_LIKE(
        "deleted orphaned signature", "Deleted .*x86_64/mingw-w64-harfbuzz-1\\.4\\.1-1-any\\.pkg\\.tar\\.xz\\.sig", messages.notes[5]);

    // check whether dry-run preserved all files
    const auto presentFiles = listFiles(repoDir);
    const auto expectedFiles = std::set<std::string>({
        "any/mingw-w64-crt-6.0.0-1-any.pkg.tar.xz",
        "any/mingw-w64-crt-6.0.0-1-any.pkg.tar.xz.sig",
        "any/mingw-w64-harfbuzz-1.4.2-1-any.pkg.tar.xz",
        "any/mingw-w64-harfbuzz-1.4.2-1-any.pkg.tar.xz.sig",
        "any/perl-linux-desktopfiles-0.22-2-any.pkg.tar.xz",
        "any/perl-linux-desktopfiles-0.22-2-any.pkg.tar.xz.sig",
        "i686/misc.db",
        "i686/misc.db.tar.zst",
        "i686/perl-linux-desktopfiles-0.22-2-any.pkg.tar.xz",
        "i686/perl-linux-desktopfiles-0.22-2-any.pkg.tar.xz.sig",
        "src/mingw-w64-crt-6.0.0-1.src.tar.xz",
        "x86_64/chunk-file",
        "x86_64/mingw-w64-crt-6.0.0-1-any.pkg.tar.xz",
        "x86_64/mingw-w64-crt-6.0.0-1-any.pkg.tar.xz.sig",
        "x86_64/mingw-w64-harfbuzz-1.4.1-1-any.pkg.tar.xz.sig",
        "x86_64/mingw-w64-harfbuzz-1.4.2-1-any.pkg.tar.xz",
        "x86_64/mingw-w64-harfbuzz-1.4.2-1-any.pkg.tar.xz.sig",
        "x86_64/misc.db",
        "x86_64/misc.db.tar.zst",
        "x86_64/perl-linux-desktopfiles-0.22-2-any.pkg.tar.xz",
        "x86_64/perl-linux-desktopfiles-0.22-2-any.pkg.tar.xz.sig",
        "x86_64/source-highlight-3.1.9-2-x86_64.pkg.tar.zst",
        "x86_64/syncthingtray-0.6.2-1-x86_64.pkg.tar.xz",
        "x86_64/syncthingtray-0.6.2-1-x86_64.pkg.tar.xz.sig",
    });
    CPPUNIT_ASSERT_EQUAL_MESSAGE("no files deleted after dry run", expectedFiles, presentFiles);

    // perform real cleanup
    m_buildAction->flags = BuildActionFlagType(); // no dry-run
    resetBuildAction();
    runBuildAction("repo cleanup, normal run");

    // check whether files have been preserved/archived/deleted as expected
    const auto presentFiles2 = listFiles(repoDir);
    const auto expectedFiles2 = std::set<std::string>({
        "any/archive/mingw-w64-crt-6.0.0-1-any.pkg.tar.xz",
        "any/archive/mingw-w64-crt-6.0.0-1-any.pkg.tar.xz.sig",
        "any/mingw-w64-harfbuzz-1.4.2-1-any.pkg.tar.xz",
        "any/mingw-w64-harfbuzz-1.4.2-1-any.pkg.tar.xz.sig",
        "any/perl-linux-desktopfiles-0.22-2-any.pkg.tar.xz",
        "any/perl-linux-desktopfiles-0.22-2-any.pkg.tar.xz.sig",
        "i686/misc.db",
        "i686/misc.db.tar.zst",
        "i686/perl-linux-desktopfiles-0.22-2-any.pkg.tar.xz",
        "i686/perl-linux-desktopfiles-0.22-2-any.pkg.tar.xz.sig",
        "src/mingw-w64-crt-6.0.0-1.src.tar.xz", // skipped for now
        "x86_64/archive/mingw-w64-crt-6.0.0-1-any.pkg.tar.xz",
        "x86_64/archive/mingw-w64-crt-6.0.0-1-any.pkg.tar.xz.sig",
        "x86_64/mingw-w64-harfbuzz-1.4.2-1-any.pkg.tar.xz",
        "x86_64/mingw-w64-harfbuzz-1.4.2-1-any.pkg.tar.xz.sig",
        "x86_64/misc.db",
        "x86_64/misc.db.tar.zst",
        "x86_64/archive/perl-linux-desktopfiles-0.22-2-any.pkg.tar.xz",
        "x86_64/archive/perl-linux-desktopfiles-0.22-2-any.pkg.tar.xz.sig",
        "x86_64/source-highlight-3.1.9-2-x86_64.pkg.tar.zst",
        "x86_64/archive/syncthingtray-0.6.2-1-x86_64.pkg.tar.xz",
        "x86_64/archive/syncthingtray-0.6.2-1-x86_64.pkg.tar.xz.sig",
    });
    CPPUNIT_ASSERT_EQUAL_MESSAGE("files preserved/archived/deleted", expectedFiles2, presentFiles2);
}

void BuildActionsTests::testBuildServiceCleanup()
{
    initStorage();

    // create and run build action
    // TODO: add build actions so it'll actually delete something
    m_buildAction = std::make_shared<BuildAction>(0, &m_setup);
    m_buildAction->type = BuildActionType::BuildServiceCleanup;
    runBuildAction("buildservice cleanup");
    CPPUNIT_ASSERT_EQUAL_MESSAGE("failure", BuildActionResult::Failure, m_buildAction->result);
    const auto &messages = std::get<BuildActionMessages>(m_buildAction->resultData);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("one error", 1_st, messages.errors.size());
    CPPUNIT_ASSERT_EQUAL_MESSAGE("one note", 1_st, messages.notes.size());
    CPPUNIT_ASSERT_EQUAL_MESSAGE("no warnings", std::vector<std::string>(), messages.warnings);
    TESTUTILS_ASSERT_LIKE("expected error", "unable to locate package cache directories:.*No such file or directory.*", messages.errors.front());
    TESTUTILS_ASSERT_LIKE("expected note", "deleted 0 build actions", messages.notes.front());
}
