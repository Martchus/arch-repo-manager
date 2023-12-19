#include "../data/config.h"

#include <c++utilities/conversion/stringbuilder.h>
#include <c++utilities/conversion/stringconversion.h>
#include <c++utilities/io/misc.h>
#include <c++utilities/tests/testutils.h>

using CppUtilities::operator<<; // must be visible prior to the call site
#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>

#include <filesystem>
#include <initializer_list>
#include <string>
#include <thread>
#include <vector>

using namespace std;
using namespace CPPUNIT_NS;
using namespace CppUtilities;
using namespace CppUtilities::Literals;
using namespace LibPkg;

class DataTests : public TestFixture {
    CPPUNIT_TEST_SUITE(DataTests);
    CPPUNIT_TEST(testPackageVersionComparsion);
    CPPUNIT_TEST(testDependencyStringConversion);
    CPPUNIT_TEST(testDependencyMatching);
    CPPUNIT_TEST(testPackageSearch);
    CPPUNIT_TEST(testComputingFileName);
    CPPUNIT_TEST(testDetectingUnresolved);
    CPPUNIT_TEST(testComputingBuildOrder);
    CPPUNIT_TEST(testComputingDatabaseOrder);
    CPPUNIT_TEST(testComputingDatabasesRequiringDatabase);
    CPPUNIT_TEST(testUpdateCheck);
    CPPUNIT_TEST(testLocatePackage);
    CPPUNIT_TEST(testAddingDepsAndProvidesFromOtherPackage);
    CPPUNIT_TEST(testDependencyExport);
    CPPUNIT_TEST(testPackageUpdater);
    CPPUNIT_TEST(testMisc);
    CPPUNIT_TEST_SUITE_END();

public:
    void setUp() override;
    void tearDown() override;

    void testPackageVersionComparsion();
    void testDependencyStringConversion();
    void testDependencyMatching();
    void testPackageSearch();
    void testComputingFileName();
    void testDetectingUnresolved();
    void testComputingBuildOrder();
    void setupTestDbs(std::size_t dbCount = 5);
    Database &db(std::size_t dbNum);
    std::vector<Database *> dbs(std::initializer_list<std::size_t> dbNums);
    void testComputingDatabaseOrder();
    void testComputingDatabasesRequiringDatabase();
    void testUpdateCheck();
    void testLocatePackage();
    void testAddingDepsAndProvidesFromOtherPackage();
    void testDependencyExport();
    void testPackageUpdater();
    void testMisc();

private:
    void setupPackages();

    std::string m_dbFile;
    Config m_config;
    std::shared_ptr<Package> m_pkg1, m_pkg2, m_pkg3;
    StorageID m_pkgId1, m_pkgId2, m_pkgId3;
};

CPPUNIT_TEST_SUITE_REGISTRATION(DataTests);

void DataTests::setUp()
{
}

void DataTests::tearDown()
{
}

void DataTests::setupPackages()
{
    m_dbFile = workingCopyPath("test-data.db", WorkingCopyMode::Cleanup);
    m_config.initStorage(m_dbFile.data());
    m_pkg1 = std::make_shared<Package>();
    m_pkg1->name = "foo";
    m_pkg1->version = "5.6-6";
    m_pkg1->dependencies.emplace_back("bar>=5.5");
    m_pkg1->dependencies.emplace_back("bar<5.6");
    m_pkg2 = std::make_shared<Package>();
    m_pkg2->name = "bar";
    m_pkg2->version = "5.5-1";
    m_pkg2->provides.emplace_back("foo", "5.8-1");
    m_pkg3 = std::make_shared<Package>();
    m_pkg3->name = "foo";
    m_pkg3->version = "5.7-1";
    auto *const db1 = m_config.findOrCreateDatabase("db1"sv, "x86_64"sv);
    CPPUNIT_ASSERT_MESSAGE("ID for pkg 1 returned", m_pkgId1 = db1->updatePackage(m_pkg1));
    CPPUNIT_ASSERT_MESSAGE("ID for pkg 2 returned", m_pkgId2 = db1->updatePackage(m_pkg2));
    CPPUNIT_ASSERT_EQUAL_MESSAGE("packages added to db 1", 2_st, db1->packageCount());
    auto *const db2 = m_config.findOrCreateDatabase("db2"sv, "x86_64"sv);
    CPPUNIT_ASSERT_MESSAGE("ID for pkg 3 returned", m_pkgId3 = db2->updatePackage(m_pkg3));
    CPPUNIT_ASSERT_EQUAL_MESSAGE("package added to db 2", 1_st, db2->packageCount());
}

void DataTests::testPackageVersionComparsion()
{
    Package pkg1, pkg2;
    pkg1.version = pkg2.version = "1234.5678-9";
    CPPUNIT_ASSERT_EQUAL(PackageVersionComparison::Equal, pkg1.compareVersion(pkg2));
    pkg2.version = "01234.5678-9";
    CPPUNIT_ASSERT_EQUAL(PackageVersionComparison::Equal, pkg1.compareVersion(pkg2));
    pkg2.version = "1235.5678-9";
    CPPUNIT_ASSERT_EQUAL(PackageVersionComparison::SoftwareUpgrade, pkg1.compareVersion(pkg2));
    pkg2.version = "1:12";
    CPPUNIT_ASSERT_EQUAL(PackageVersionComparison::SoftwareUpgrade, pkg1.compareVersion(pkg2));
    pkg2.version = "1235.5679-1";
    CPPUNIT_ASSERT_EQUAL(PackageVersionComparison::SoftwareUpgrade, pkg1.compareVersion(pkg2));
    pkg2.version = "12356.5678-1";
    CPPUNIT_ASSERT_EQUAL(PackageVersionComparison::SoftwareUpgrade, pkg1.compareVersion(pkg2));
    pkg2.version = "1234.5678-10";
    CPPUNIT_ASSERT_EQUAL(PackageVersionComparison::PackageUpgradeOnly, pkg1.compareVersion(pkg2));
    pkg2.version = "1234.5678-9.1";
    CPPUNIT_ASSERT_EQUAL(PackageVersionComparison::PackageUpgradeOnly, pkg1.compareVersion(pkg2));
    pkg2.version = "1234.5678-8";
    CPPUNIT_ASSERT_EQUAL(PackageVersionComparison::NewerThanSyncVersion, pkg1.compareVersion(pkg2));
    pkg2.version = "1224.5678-9";
    CPPUNIT_ASSERT_EQUAL(PackageVersionComparison::NewerThanSyncVersion, pkg1.compareVersion(pkg2));
}

void DataTests::testDependencyStringConversion()
{
    CPPUNIT_ASSERT_EQUAL("foo>=4.5: bar"s, Dependency("foo", "4.5", DependencyMode::GreatherEqual, "bar").toString());
    CPPUNIT_ASSERT_EQUAL("foo=4.5"s, Dependency("foo", "4.5", DependencyMode::Equal).toString());
    CPPUNIT_ASSERT_EQUAL("foo: bar"s, Dependency("foo", "4.5", DependencyMode::Any, "bar").toString());
    CPPUNIT_ASSERT_EQUAL("foo"s, Dependency("foo", "4.5", DependencyMode::Any).toString());
}

void DataTests::testDependencyMatching()
{
    Package pkg;
    pkg.name = "my-foo";
    pkg.version = "123.2.4-1";
    pkg.provides.emplace_back("foo", "123.2.4-1");

    Dependency dep;
    dep.name = "my-foo";
    CPPUNIT_ASSERT_MESSAGE("without constraint, direct match", pkg.providesDependency(dep));
    dep.name = "bar";
    CPPUNIT_ASSERT_MESSAGE("without constraint, no match", !pkg.providesDependency(dep));
    dep.name = "foo";
    CPPUNIT_ASSERT_MESSAGE("without constraint, match via provides", pkg.providesDependency(dep));
    dep.name = "my-foo";
    dep.version = pkg.version;
    dep.mode = DependencyMode::Equal;
    CPPUNIT_ASSERT_MESSAGE("equal constraint, direct match", pkg.providesDependency(dep));
    dep.mode = DependencyMode::GreatherEqual;
    CPPUNIT_ASSERT_MESSAGE("greater equal constraint, no match", pkg.providesDependency(dep));
    dep.mode = DependencyMode::GreatherThan;
    CPPUNIT_ASSERT_MESSAGE("greater constraint, no match", !pkg.providesDependency(dep));
    dep.mode = DependencyMode::LessEqual;
    CPPUNIT_ASSERT_MESSAGE("less equal constraint, match", pkg.providesDependency(dep));
    dep.mode = DependencyMode::LessThan;
    CPPUNIT_ASSERT_MESSAGE("less constraint, no match", !pkg.providesDependency(dep));
    dep.mode = DependencyMode::Equal;
    pkg.version += '0';
    CPPUNIT_ASSERT_MESSAGE("equal constraint, no match", !pkg.providesDependency(dep));
    dep.mode = DependencyMode::GreatherEqual;
    CPPUNIT_ASSERT_MESSAGE("greater equal constraint, direct match", pkg.providesDependency(dep));
    pkg.version = "123.4";
    CPPUNIT_ASSERT_MESSAGE("greater equal constraint, direct match", pkg.providesDependency(dep));
    dep.name = "foo";
    CPPUNIT_ASSERT_MESSAGE("greater equal constraint, indirect match", pkg.providesDependency(dep));

    Package pkg2;
    pkg2.name = "crypto++";
    pkg2.version = "5.6.5-1";
    const char *depStr = "crypto++=5.6.5-1";
    CPPUNIT_ASSERT_MESSAGE("name with plus signs", pkg2.providesDependency(Dependency::fromString(depStr, 8)));
    CPPUNIT_ASSERT_MESSAGE("equal constraint with explicitly specified pkgrel", pkg2.providesDependency(Dependency::fromString(depStr, 16)));
    CPPUNIT_ASSERT_MESSAGE("equal constraint, default pkgrel should match", pkg2.providesDependency(Dependency::fromString(depStr, 14)));
    Package pkg3;
    pkg3.name = "crypto++";
    pkg3.version = "5.6.5-2";
    CPPUNIT_ASSERT_MESSAGE("equal constraint, any pkgrel should match", pkg3.providesDependency(Dependency::fromString(depStr, 14)));
    CPPUNIT_ASSERT_MESSAGE(
        "equal constraint with explicitly specified pkgrel must not match", !pkg3.providesDependency(Dependency::fromString(depStr, 16)));

    depStr = "crypto++>=5.6.5-1";
    CPPUNIT_ASSERT_MESSAGE("greater equal constraint with explicitly specified pkgrel", pkg2.providesDependency(Dependency::fromString(depStr, 17)));
    CPPUNIT_ASSERT_MESSAGE("greater equal constraint, default pkgrel should match", pkg2.providesDependency(Dependency::fromString(depStr, 15)));
    CPPUNIT_ASSERT_MESSAGE("greater equal constrainer, any pkgrel should match", pkg3.providesDependency(Dependency::fromString(depStr, 15)));
    CPPUNIT_ASSERT_MESSAGE(
        "greater equal constraint with explicitly specified pkgrel must match", pkg3.providesDependency(Dependency::fromString(depStr, 17)));

    pkg3.name = "sphinxbase";
    pkg3.version = "5prealpha-11.1aBc";
    depStr = "sphinxbase=5prealpha";
    CPPUNIT_ASSERT_MESSAGE(
        "equal constraint, any pkgrel should match (even strange one)", pkg3.providesDependency(Dependency::fromString(depStr, 20)));

    pkg3.name = "ffmpeg";
    pkg3.version = "1:4.1-3";
    CPPUNIT_ASSERT_MESSAGE("real-world ffmpeg example (1)", pkg3.providesDependency(Dependency::fromString("ffmpeg<1:4.3")));
    pkg3.version = "1:4.1-2";
    CPPUNIT_ASSERT_MESSAGE("real-world ffmpeg example (2)", !pkg3.providesDependency(Dependency::fromString("ffmpeg>=1:4.1-3")));

    pkg3.name = "python-jinja";
    pkg3.version = "1:3.0.3-1";
    CPPUNIT_ASSERT_MESSAGE(
        "real-world python-jinja example (epoch equal)", pkg3.providesDependency(Dependency::fromString("python-jinja>=1:2.10.3")));
    CPPUNIT_ASSERT_MESSAGE("real-world python-jinja example (epoch absend)", pkg3.providesDependency(Dependency::fromString("python-jinja>=2.10.3")));
    CPPUNIT_ASSERT_MESSAGE(
        "real-world python-jinja example (epoch higher)", !pkg3.providesDependency(Dependency::fromString("python-jinja>=2:2.10.3")));
    pkg3.version = "3.0.3-1";
    CPPUNIT_ASSERT_MESSAGE(
        "real-world python-jinja example (epoch absend in package)", !pkg3.providesDependency(Dependency::fromString("python-jinja>=1:2.10.3")));
    pkg3.version = "3:3.0.3-1";
    CPPUNIT_ASSERT_MESSAGE(
        "real-world python-jinja example (epoch higher in package)", pkg3.providesDependency(Dependency::fromString("python-jinja>=2:2.10.3")));
}

void DataTests::testPackageSearch()
{
    setupPackages();

    auto pkgs = m_config.findPackages("foo"sv);
    CPPUNIT_ASSERT_EQUAL(2_st, pkgs.size());
    CPPUNIT_ASSERT_EQUAL_MESSAGE("package from first db returned first, cached object returned", m_pkg1, pkgs.front().pkg);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("package from first db returned second, cached object returned", m_pkg3, pkgs.back().pkg);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("package id set as expected (1)", m_pkgId1, pkgs.front().id);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("package id set as expected (2)", m_pkgId3, pkgs.back().id);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("db set as expected (1)", &m_config.databases.front(), std::get<Database *>(pkgs.front().db));
    CPPUNIT_ASSERT_EQUAL_MESSAGE("db set as expected (2)", &m_config.databases.back(), std::get<Database *>(pkgs.back().db));

    auto [db, pkg, packageID] = m_config.findPackage(Dependency("foo"));
    CPPUNIT_ASSERT_EQUAL_MESSAGE("expected package for dependency returned", m_pkgId1, packageID);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("find package returns the package from the first database", &m_config.databases.front(), std::get<Database *>(db));
    // FIXME: check whether package is actually (value) equivalent

    pkgs = m_config.findPackages("bar"sv);
    CPPUNIT_ASSERT_EQUAL(1_st, pkgs.size());
    CPPUNIT_ASSERT_EQUAL(m_pkg2, pkgs.front().pkg);

    pkgs = m_config.findPackages("db2/foo"sv);
    CPPUNIT_ASSERT_EQUAL(1_st, pkgs.size());
    CPPUNIT_ASSERT_EQUAL(m_pkg3, pkgs.front().pkg);

    pkgs = m_config.findPackages("db2/bar"sv);
    CPPUNIT_ASSERT_EQUAL(0_st, pkgs.size());

    pkgs = m_config.findPackages(Dependency("foo", "5.7-1", DependencyMode::Any));
    CPPUNIT_ASSERT_EQUAL(3_st, pkgs.size());

    pkgs = m_config.findPackages(Dependency("foo", "5.8-1", DependencyMode::GreatherEqual));
    CPPUNIT_ASSERT_EQUAL(1_st, pkgs.size());
    CPPUNIT_ASSERT_EQUAL(m_pkgId2, pkgs.front().id);
    // FIXME: check whether package is actually (value) equivalent

    pkgs = m_config.findPackages(Dependency("bar", "5.5-1", DependencyMode::Equal));
    CPPUNIT_ASSERT_EQUAL(1_st, pkgs.size());
    CPPUNIT_ASSERT_EQUAL(m_pkgId2, pkgs.front().id);
    // FIXME: check whether package is actually (value) equivalent

    pkgs = m_config.findPackages(Dependency("bar", "5.8-1", DependencyMode::Equal));
    CPPUNIT_ASSERT_EQUAL(0_st, pkgs.size());
}

void DataTests::testComputingFileName()
{
    auto pkg = Package();
    pkg.name = "test";
    pkg.version = "1.2-3";
    pkg.arch = "x86_64";
    CPPUNIT_ASSERT_EQUAL_MESSAGE("file name computed from name, version and arch", "test-1.2-3-x86_64.pkg.tar.zst"s, pkg.computeFileName());
    pkg.packageInfo = std::make_optional<PackageInfo>();
    pkg.packageInfo->fileName = "explicitly-specified-filename";
    CPPUNIT_ASSERT_EQUAL_MESSAGE("explicitly specified filename returned", "explicitly-specified-filename"s, pkg.computeFileName());
}

void DataTests::testDetectingUnresolved()
{
    setupPackages();
    auto &db1 = m_config.databases[0];

    CPPUNIT_ASSERT_EQUAL(0_st, db1.detectUnresolvedPackages(m_config, {}, {}).size());

    // upgrade bar to 5.6, foo should be unresolvable
    m_pkg2->version = "5.6";
    auto removedPackages = DependencySet();
    removedPackages.add(Dependency("bar", "5.5"), m_pkg2);
    const auto failures = db1.detectUnresolvedPackages(m_config, { m_pkg2 }, removedPackages);
    CPPUNIT_ASSERT_EQUAL(1_st, failures.size());
    CPPUNIT_ASSERT_EQUAL(m_pkgId1, failures.begin()->first.id);
}

void DataTests::testComputingBuildOrder()
{
    setupPackages();

    // order correctly changed according to dependencies
    auto res = m_config.computeBuildOrder({ "foo", "bar" }, BuildOrderOptions::None);
    CPPUNIT_ASSERT_EQUAL(true, res.success);
    CPPUNIT_ASSERT_EQUAL(2_st, res.order.size());
    CPPUNIT_ASSERT_EQUAL("bar"s, res.order[0].pkg->name);
    CPPUNIT_ASSERT_EQUAL("foo"s, res.order[1].pkg->name);
    CPPUNIT_ASSERT_EQUAL(0_st, res.ignored.size());

    // unknown package ignored
    const auto ignored = std::vector<std::string>{ "baz" };
    res = m_config.computeBuildOrder({ "foo", "bar", ignored[0] }, BuildOrderOptions::None);
    CPPUNIT_ASSERT_EQUAL(false, res.success);
    CPPUNIT_ASSERT_EQUAL(2_st, res.order.size());
    CPPUNIT_ASSERT_EQUAL("bar"s, res.order[0].pkg->name);
    CPPUNIT_ASSERT_EQUAL("foo"s, res.order[1].pkg->name);
    CPPUNIT_ASSERT_EQUAL(ignored, res.ignored);

    // add cycle
    auto &db = m_config.databases[0];
    auto tar = std::make_shared<Package>();
    tar->name = "tar";
    tar->version = "5.6-6";
    tar->dependencies.emplace_back("foo");
    m_pkg2->dependencies.emplace_back("tar"); // let bar depend on tar
    db.updatePackage(tar);
    db.updatePackage(m_pkg2);

    // fail due to cycle
    res = m_config.computeBuildOrder({ "foo", "bar", "tar" }, BuildOrderOptions::None);
    CPPUNIT_ASSERT_EQUAL(false, res.success);
    CPPUNIT_ASSERT_EQUAL(3_st, res.cycle.size());
    CPPUNIT_ASSERT_EQUAL("foo"s, res.cycle[0].pkg->name);
    CPPUNIT_ASSERT_EQUAL("bar"s, res.cycle[1].pkg->name);
    CPPUNIT_ASSERT_EQUAL("tar"s, res.cycle[2].pkg->name);
    CPPUNIT_ASSERT_EQUAL(0_st, res.order.size());
    CPPUNIT_ASSERT_EQUAL(0_st, res.ignored.size());

    // fail due to cycle even if package not in the specified list
    res = m_config.computeBuildOrder({ "foo", "bar" }, BuildOrderOptions::None);
    CPPUNIT_ASSERT_EQUAL(false, res.success);
    CPPUNIT_ASSERT_EQUAL(3_st, res.cycle.size());
    CPPUNIT_ASSERT_EQUAL("foo"s, res.cycle[0].pkg->name);
    CPPUNIT_ASSERT_EQUAL("bar"s, res.cycle[1].pkg->name);
    CPPUNIT_ASSERT_EQUAL("tar"s, res.cycle[2].pkg->name);
    CPPUNIT_ASSERT_EQUAL(0_st, res.order.size());
    CPPUNIT_ASSERT_EQUAL(0_st, res.ignored.size());

    // ignore cycle if not interested in that particular package
    m_pkg2->packageInfo = std::make_optional<PackageInfo>();
    tar->packageInfo = std::make_optional<PackageInfo>();
    tar->dependencies.clear();
    tar->dependencies.emplace_back("bar");
    db.updatePackage(tar);
    db.updatePackage(m_pkg2);
    res = m_config.computeBuildOrder({ "foo" }, BuildOrderOptions::None);
    CPPUNIT_ASSERT_EQUAL(true, res.success);
    CPPUNIT_ASSERT_EQUAL(0_st, res.cycle.size());
    CPPUNIT_ASSERT_EQUAL(1_st, res.order.size());
    CPPUNIT_ASSERT_EQUAL("foo"s, res.order[0].pkg->name);
    CPPUNIT_ASSERT_EQUAL(0_st, res.ignored.size());
}

void DataTests::setupTestDbs(std::size_t dbCount)
{
    setupPackages();
    m_config.databases.reserve(m_config.databases.size() + dbCount);
    for (std::size_t i = 1; i <= dbCount; ++i) {
        m_config.findOrCreateDatabase(argsToString("db", i), "x86_64");
    }
}

inline Database &DataTests::db(std::size_t dbNum)
{
    return m_config.databases[dbNum - 1];
}

inline std::vector<Database *> DataTests::dbs(std::initializer_list<std::size_t> dbNums)
{
    std::vector<Database *> res;
    res.reserve(dbNums.size());
    for (const auto dbNum : dbNums) {
        res.emplace_back(&db(dbNum));
    }
    return res;
}

void DataTests::testComputingDatabaseOrder()
{
    setupTestDbs();
    db(1).dependencies = { "db2", "db3" };
    db(2).dependencies = { "db4" };
    db(3).dependencies = { "db5", "db2" };

    CPPUNIT_ASSERT_EQUAL(dbs({ 5 }), get<vector<Database *>>(m_config.computeDatabaseDependencyOrder(db(5))));
    CPPUNIT_ASSERT_EQUAL(dbs({ 4 }), get<vector<Database *>>(m_config.computeDatabaseDependencyOrder(db(4))));
    CPPUNIT_ASSERT_EQUAL(dbs({ 5, 4, 2, 3 }), get<vector<Database *>>(m_config.computeDatabaseDependencyOrder(db(3))));
    CPPUNIT_ASSERT_EQUAL(dbs({ 4, 2 }), get<vector<Database *>>(m_config.computeDatabaseDependencyOrder(db(2))));
    CPPUNIT_ASSERT_EQUAL(dbs({ 4, 2, 5, 3, 1 }), get<vector<Database *>>(m_config.computeDatabaseDependencyOrder(db(1))));

    db(4).dependencies = { "db1" };
    db(5).dependencies = { "db6" };

    CPPUNIT_ASSERT_EQUAL("cycle at db1"s, get<string>(m_config.computeDatabaseDependencyOrder(db(1))));
    CPPUNIT_ASSERT_EQUAL(
        "database \"db6\" required by \"db5\" does not exist (architecture x86_64)"s, get<string>(m_config.computeDatabaseDependencyOrder(db(3))));
}

void DataTests::testComputingDatabasesRequiringDatabase()
{
    setupTestDbs();
    db(1).dependencies = { "db2", "db3" };
    db(2).dependencies = { "db4" };
    db(3).dependencies = { "db5" };

    CPPUNIT_ASSERT_EQUAL(dbs({ 1 }), m_config.computeDatabasesRequiringDatabase(db(1)));
    CPPUNIT_ASSERT_EQUAL(dbs({ 2, 1 }), m_config.computeDatabasesRequiringDatabase(db(2)));
    CPPUNIT_ASSERT_EQUAL(dbs({ 3, 1 }), m_config.computeDatabasesRequiringDatabase(db(3)));
    CPPUNIT_ASSERT_EQUAL(dbs({ 4, 2, 1 }), m_config.computeDatabasesRequiringDatabase(db(4)));
    CPPUNIT_ASSERT_EQUAL(dbs({ 5, 3, 1 }), m_config.computeDatabasesRequiringDatabase(db(5)));
}

void DataTests::testUpdateCheck()
{
    setupPackages();
    auto &db1 = m_config.databases.front();
    auto &db2 = m_config.databases.back();
    const auto result = db1.checkForUpdates({ &db2 });
    CPPUNIT_ASSERT_EQUAL(1_st, result.versionUpdates.size());
    CPPUNIT_ASSERT_EQUAL(m_pkgId1, result.versionUpdates.front().oldVersion.id);
    CPPUNIT_ASSERT_EQUAL(m_pkgId3, result.versionUpdates.front().newVersion.id);
    CPPUNIT_ASSERT_EQUAL(0_st, result.packageUpdates.size());
    CPPUNIT_ASSERT_EQUAL(0_st, result.downgrades.size());
    CPPUNIT_ASSERT_EQUAL(1_st, result.orphans.size());
    CPPUNIT_ASSERT_EQUAL(m_pkgId2, result.orphans.front().id);
}

void DataTests::testLocatePackage()
{
    const auto fakePkgPath = testFilePath("repo/foo/fake-0-any.pkg.tar.zst");
    const auto syncthingTrayPkgPath = testFilePath("repo/foo/syncthingtray-0.6.2-1-x86_64.pkg.tar.xz");
    const auto syncthingTrayStorageLocation = std::filesystem::canonical(testFilePath("syncthingtray/syncthingtray-0.6.2-1-x86_64.pkg.tar.xz"));

    setupPackages();
    auto &db = m_config.databases.front();
    db.localPkgDir = std::filesystem::path(fakePkgPath).parent_path();
    auto missingPath = std::filesystem::path(db.localPkgDir + "/missing-0-any.pkg.tar.zst");
    if (!std::filesystem::exists(std::filesystem::symlink_status(missingPath))) {
        std::filesystem::create_symlink("does_not_exist", missingPath);
    }

    const auto emptyPkg = db.locatePackage(std::string());
    CPPUNIT_ASSERT_EQUAL(std::string(), emptyPkg.pathWithinRepo.string());
    CPPUNIT_ASSERT_EQUAL(std::string(), emptyPkg.storageLocation.string());
    CPPUNIT_ASSERT(!emptyPkg.exists);
    CPPUNIT_ASSERT(!emptyPkg.error.has_value());

    const auto missingPkg = db.locatePackage("missing-0-any.pkg.tar.zst");
    CPPUNIT_ASSERT_EQUAL(missingPath, missingPkg.pathWithinRepo);
    CPPUNIT_ASSERT_EQUAL(db.localPkgDir + "/does_not_exist"s, missingPkg.storageLocation.string());
    CPPUNIT_ASSERT(!missingPkg.exists);
    CPPUNIT_ASSERT(!missingPkg.error.has_value());

    const auto fakePkg = db.locatePackage("fake-0-any.pkg.tar.zst");
    CPPUNIT_ASSERT_EQUAL(fakePkgPath, fakePkg.pathWithinRepo.string());
    CPPUNIT_ASSERT_EQUAL(std::string(), fakePkg.storageLocation.string());
    CPPUNIT_ASSERT(fakePkg.exists);
    CPPUNIT_ASSERT(!fakePkg.error.has_value());

    const auto syncthingPkg = db.locatePackage("syncthingtray-0.6.2-1-x86_64.pkg.tar.xz");
    CPPUNIT_ASSERT_EQUAL(syncthingTrayPkgPath, syncthingPkg.pathWithinRepo.string());
    CPPUNIT_ASSERT_EQUAL(syncthingTrayStorageLocation, syncthingPkg.storageLocation);
    CPPUNIT_ASSERT(fakePkg.exists);
    CPPUNIT_ASSERT(!fakePkg.error.has_value());
}

void DataTests::testAddingDepsAndProvidesFromOtherPackage()
{
    const std::unordered_set<Dependency> dependenciesToTakeOver = {
        Dependency{ "python2", "2.18", DependencyMode::LessThan },
        Dependency{ "python", "3.5", DependencyMode::LessThan },
        Dependency{ "perl", "5.32", DependencyMode::GreatherEqual },
    };
    setupPackages();
    m_pkg1->origin = PackageOrigin::PackageContents;
    m_pkg1->dependencies.insert(m_pkg1->dependencies.end(), dependenciesToTakeOver.begin(), dependenciesToTakeOver.end());
    m_pkg1->libdepends.emplace("foo");
    m_pkg1->libprovides.emplace("bar");

    Package pkg1;
    CPPUNIT_ASSERT_MESSAGE("adding from itself always prevented", !pkg1.addDepsAndProvidesFromOtherPackage(pkg1, true));
    CPPUNIT_ASSERT_MESSAGE("not adding on version mismatch", !pkg1.addDepsAndProvidesFromOtherPackage(*m_pkg1));
    CPPUNIT_ASSERT_MESSAGE("adding from other package forced", pkg1.addDepsAndProvidesFromOtherPackage(*m_pkg1, true));
    CPPUNIT_ASSERT_EQUAL(PackageOrigin::PackageContents, pkg1.origin);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("libdeps taken over", m_pkg1->libdepends, pkg1.libdepends);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("libprovides taken over", m_pkg1->libprovides, pkg1.libprovides);
    CPPUNIT_ASSERT_EQUAL_MESSAGE(
        "relevant deps taken over", dependenciesToTakeOver, std::unordered_set<Dependency>(pkg1.dependencies.begin(), pkg1.dependencies.end()));

    pkg1.version = m_pkg1->version;
    CPPUNIT_ASSERT_MESSAGE("adding from other package when version matches", pkg1.addDepsAndProvidesFromOtherPackage(*m_pkg1));
    CPPUNIT_ASSERT_EQUAL_MESSAGE("no duplicate libdeps after 2nd run", m_pkg1->libdepends, pkg1.libdepends);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("no duplicate libprovides after 2nd run", m_pkg1->libprovides, pkg1.libprovides);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("no duplicate deps after 2nd run", dependenciesToTakeOver,
        std::unordered_set<Dependency>(pkg1.dependencies.begin(), pkg1.dependencies.end()));
}

void DataTests::testDependencyExport()
{
    setupPackages();
    m_pkg2->provides.emplace_back("yet-another-dependency");
    m_pkg2->libprovides.emplace("libfoo");
    m_pkg2->libprovides.emplace("libbar");
    DependencySet provides;
    std::unordered_set<std::string> libProvides;
    Package::exportProvides(m_pkg2, provides, libProvides);
    CPPUNIT_ASSERT_EQUAL(3_st, provides.size());
    CPPUNIT_ASSERT(provides.provides("foo", DependencyDetail()));
    CPPUNIT_ASSERT(provides.provides("bar", DependencyDetail()));
    CPPUNIT_ASSERT(provides.provides("yet-another-dependency", DependencyDetail()));
    CPPUNIT_ASSERT_EQUAL(2_st, libProvides.size());
    CPPUNIT_ASSERT(libProvides.contains("libfoo"));
    CPPUNIT_ASSERT(libProvides.contains("libbar"));
}

void DataTests::testPackageUpdater()
{
    m_pkg1 = std::make_shared<LibPkg::Package>();
    m_pkg1->name = "foo";
    m_pkg2 = std::make_shared<LibPkg::Package>();
    m_pkg2->name = "autoconf";
    m_pkg2->version = "1-1";
    m_dbFile = workingCopyPath("test-data.db", WorkingCopyMode::Cleanup);
    m_config.initStorage(m_dbFile.data());
    auto *const db = m_config.findOrCreateDatabase("test"sv, "x86_64"sv);
    db->path = testFilePath("core.db");
    db->updatePackage(m_pkg1);
    db->updatePackage(m_pkg2);
    auto updater = LibPkg::PackageUpdater(*db, true);
    updater.insertFromDatabaseFile(db->path);
    auto thread = std::thread([db] {
        auto autoconf = db->findPackage("autoconf");
        CPPUNIT_ASSERT_EQUAL_MESSAGE("two packages before commit", 2_st, db->packageCount());
        CPPUNIT_ASSERT_MESSAGE("cache not wiped before commit (so removed package still present)", db->findPackage("foo"));
        CPPUNIT_ASSERT_MESSAGE("cache contains autoconf", autoconf);
        CPPUNIT_ASSERT_EQUAL_MESSAGE("cache populated before commit (so new version already visible)", "2.69-4"s, autoconf->version);
    });
    thread.join();
    updater.commit();

    CPPUNIT_ASSERT_EQUAL_MESSAGE("package count after commit", 220_st, db->packageCount());
    CPPUNIT_ASSERT_MESSAGE("old package gone after commit", !db->findPackage("foo"));

    auto newPkg = db->findPackage("autoconf");
    CPPUNIT_ASSERT_MESSAGE("new package present after commit", newPkg);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("search returns correct package", "autoconf"s, newPkg->name);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("the package version was updated", "2.69-4"s, newPkg->version);
    CPPUNIT_ASSERT(newPkg = db->findPackage("acl"));
    CPPUNIT_ASSERT_EQUAL("acl"s, newPkg->name);
    CPPUNIT_ASSERT(newPkg = db->findPackage("zlib"));
    CPPUNIT_ASSERT_EQUAL("zlib"s, newPkg->name);
}

void DataTests::testMisc()
{
    CPPUNIT_ASSERT_EQUAL("123.4"s, PackageVersion::trimPackageVersion("123.4"s));
    CPPUNIT_ASSERT_EQUAL("123"s, PackageVersion::trimPackageVersion("123-4"s));
    CPPUNIT_ASSERT_EQUAL("123"s, PackageVersion::trimPackageVersion("123"s));
}
