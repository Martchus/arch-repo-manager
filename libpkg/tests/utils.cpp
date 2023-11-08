#include <ostream>
#include <string>
#include <vector>

#include "../parser/database.h"
#include "../parser/package.h"
#include "../parser/utils.h"

#include <c++utilities/conversion/stringbuilder.h>
#include <c++utilities/conversion/stringconversion.h>
#include <c++utilities/io/misc.h>
#include <c++utilities/io/path.h>
#include <c++utilities/tests/testutils.h>

using CppUtilities::operator<<; // must be visible prior to the call site
#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>

#include <iostream>

using namespace std;
using namespace CPPUNIT_NS;
using namespace CppUtilities;
using namespace LibPkg;

class UtilsTests : public TestFixture {
    CPPUNIT_TEST_SUITE(UtilsTests);
    CPPUNIT_TEST(testFileExtraction);
    CPPUNIT_TEST(testAmendingPkgbuild);
    CPPUNIT_TEST_SUITE_END();

public:
    void setUp() override;
    void tearDown() override;

    void testFileExtraction();
    void testAmendingPkgbuild();
};

CPPUNIT_TEST_SUITE_REGISTRATION(UtilsTests);

void UtilsTests::setUp()
{
}

void UtilsTests::tearDown()
{
}

void UtilsTests::testFileExtraction()
{
    const auto dirs = extractFiles(testFilePath("core.files"), &Database::isFileRelevant);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("215 dirs present", 215ul, dirs.size());
    const auto &linuxDir = dirs.at("linux-4.7.6-1");
    const auto expectedDesc = readFile(testFilePath("linux-4.7.6-1-desc"));
    const auto expectedFiles = readFile(testFilePath("linux-4.7.6-1-files"));
    CPPUNIT_ASSERT_EQUAL_MESSAGE("2 files present", 2ul, linuxDir.size());
    CPPUNIT_ASSERT_EQUAL_MESSAGE("desc name", "desc"s, linuxDir[0].name);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("files name", "files"s, linuxDir[1].name);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("desc content", expectedDesc, linuxDir[0].content);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("files content", expectedFiles, linuxDir[1].content);
    const auto &zlibDir = dirs.at("zlib-1.2.8-4");
    CPPUNIT_ASSERT_EQUAL_MESSAGE("3 files present", 3ul, zlibDir.size());
    CPPUNIT_ASSERT_EQUAL_MESSAGE("also depends present", "depends"s, zlibDir[1].name);
}

void UtilsTests::testAmendingPkgbuild()
{
    const auto pkgbuildPath = workingCopyPath("c++utilities/PKGBUILD");

    amendPkgbuild(pkgbuildPath, PackageVersion{ .upstream = "5.0.2", .package = "3" }, PackageAmendment{});
    CPPUNIT_ASSERT_EQUAL_MESSAGE("no changes when amendment empty", readFile(testFilePath("c++utilities/PKGBUILD")), readFile(pkgbuildPath));

    amendPkgbuild(pkgbuildPath, PackageVersion{ .upstream = "5.0.2", .package = "3" },
        PackageAmendment{ .bumpDownstreamVersion = PackageAmendment::VersionBump::PackageVersion });
    CPPUNIT_ASSERT_EQUAL_MESSAGE("pkgrel bumped", readFile(testFilePath("c++utilities/PKGBUILD.newpkgrel")), readFile(pkgbuildPath));

    amendPkgbuild(pkgbuildPath, PackageVersion{ .upstream = "5.0.2", .package = "3" },
        PackageAmendment{ .bumpDownstreamVersion = PackageAmendment::VersionBump::Epoch });
    CPPUNIT_ASSERT_EQUAL_MESSAGE("epoch bumped, pkgrel reset", readFile(testFilePath("c++utilities/PKGBUILD.newepoch")), readFile(pkgbuildPath));

    auto amendment
        = amendPkgbuild(pkgbuildPath, PackageVersion{ .upstream = "5.0.2", .package = "3" }, PackageAmendment{ .setUpstreamVersion = true });
    CPPUNIT_ASSERT_EQUAL_MESSAGE("upstream version set", readFile(testFilePath("c++utilities/PKGBUILD.newpkgver")), readFile(pkgbuildPath));
    CPPUNIT_ASSERT_EQUAL_MESSAGE("new upstream version returned", "5.0.2"s, amendment.newUpstreamVersion);

    const auto pkgbuildWithQuotingPath = workingCopyPath("perl-data-dumper-concise/PKGBUILD");
    amendPkgbuild(pkgbuildWithQuotingPath, PackageVersion{ .upstream = "2.023", .package = "1" },
        PackageAmendment{ .bumpDownstreamVersion = PackageAmendment::VersionBump::PackageVersion });
    CPPUNIT_ASSERT_EQUAL_MESSAGE("pkgrel bumped when quotes were used", readFile(testFilePath("perl-data-dumper-concise/PKGBUILD.newpkgrel")),
        readFile(pkgbuildWithQuotingPath));
}
