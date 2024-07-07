#include "./parser_helper.h"

#include "../parser/binary.h"
#include "../parser/config.h"
#include "../parser/package.h"

namespace CppUtilities {
inline std::ostream &operator<<(std::ostream &out, const LibPkg::SourceInfo &sourceInfo)
{
    const auto buff = sourceInfo.toJson();
    out.write(buff.GetString(), ReflectiveRapidJSON::JsonReflector::rapidJsonSize(buff.GetSize()));
    return out;
}
} // namespace CppUtilities

#include <c++utilities/conversion/stringbuilder.h>
#include <c++utilities/conversion/stringconversion.h>
#include <c++utilities/io/misc.h>
#include <c++utilities/io/path.h>
#include <c++utilities/tests/testutils.h>

using CppUtilities::operator<<; // must be visible prior to the call site
#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>

#include <ostream>
#include <regex>
#include <string>
#include <vector>

using namespace std;
using namespace CPPUNIT_NS;
using namespace CppUtilities;
using namespace CppUtilities::Literals;
using namespace LibPkg;
using namespace TestHelper;

class ParserTests : public TestFixture {
    CPPUNIT_TEST_SUITE(ParserTests);
    CPPUNIT_TEST(testParsingDependencies);
    CPPUNIT_TEST(testParsingPackageVersion);
    CPPUNIT_TEST(testParsingPackageName);
    CPPUNIT_TEST(testParsingConfig);
    CPPUNIT_TEST(testParsingPlainSrcInfo);
    CPPUNIT_TEST(testParsingSplitPackageSrcInfo);
    CPPUNIT_TEST(testParsingSplitPackageSrcInfoWithDifferentArchs);
    CPPUNIT_TEST(testParsingPkgInfo);
    CPPUNIT_TEST(testParsingPkgName);
    CPPUNIT_TEST(testParsingDatabase);
    CPPUNIT_TEST(testExtractingPkgFile);
    CPPUNIT_TEST(testParsingDescriptions);
    CPPUNIT_TEST(testParsingDatabaseAndOverallStorageBehavior);
    CPPUNIT_TEST(testParsingSignatureLevel);
    CPPUNIT_TEST(testSerializingDatabaseSignatureLevel);
    CPPUNIT_TEST_SUITE_END();

public:
    void setUp() override;
    void tearDown() override;

    void testParsingDependencies();
    void testParsingPackageVersion();
    void testParsingPackageName();
    void testParsingConfig();
    void testParsingPlainSrcInfo();
    void testParsingSplitPackageSrcInfo();
    void testParsingSplitPackageSrcInfoWithDifferentArchs();
    void testParsingPkgInfo();
    void testParsingPkgName();
    void testParsingDatabase();
    void testExtractingPkgFile();
    void testParsingDescriptions();
    void testParsingDatabaseAndOverallStorageBehavior();
    void testParsingSignatureLevel();
    void testSerializingDatabaseSignatureLevel();
};

CPPUNIT_TEST_SUITE_REGISTRATION(ParserTests);

void ParserTests::setUp()
{
}

void ParserTests::tearDown()
{
}

void ParserTests::testParsingDependencies()
{
    const auto ffmpeg = Dependency("ffmpeg>1:4.2-3");
    CPPUNIT_ASSERT_EQUAL("ffmpeg"s, ffmpeg.name);
    CPPUNIT_ASSERT_EQUAL("1:4.2-3"s, ffmpeg.version);
    CPPUNIT_ASSERT_EQUAL(string(), ffmpeg.description);

    const auto ffmpeg2 = Dependency("ffmpeg: support more formats");
    CPPUNIT_ASSERT_EQUAL("ffmpeg"s, ffmpeg2.name);
    CPPUNIT_ASSERT_EQUAL(string(), ffmpeg2.version);
    CPPUNIT_ASSERT_EQUAL("support more formats"s, ffmpeg2.description);

    const auto ffmpeg3 = Dependency("ffmpeg:support more formats");
    CPPUNIT_ASSERT_EQUAL("ffmpeg"s, ffmpeg3.name);
    CPPUNIT_ASSERT_EQUAL(string(), ffmpeg3.version);
    CPPUNIT_ASSERT_EQUAL("support more formats"s, ffmpeg3.description);

    const auto ffmpeg4 = Dependency("ffmpeg=1:4.3:support more formats");
    CPPUNIT_ASSERT_EQUAL("ffmpeg"s, ffmpeg4.name);
    CPPUNIT_ASSERT_EQUAL("1:4.3"s, ffmpeg4.version);
    CPPUNIT_ASSERT_EQUAL("support more formats"s, ffmpeg4.description);
}

void ParserTests::testParsingPackageVersion()
{
    PackageVersion ver = PackageVersion::fromString("2:12-3.45a-6");
    CPPUNIT_ASSERT_EQUAL_MESSAGE("epoch", string("2"), ver.epoch);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("upstream", string("12-3.45a"), ver.upstream);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("package", string("6"), ver.package);
    ver = PackageVersion::fromString("12-3.45a-");
    CPPUNIT_ASSERT_EQUAL_MESSAGE("empty epoch", string(), ver.epoch);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("only upstream", string("12-3.45a"), ver.upstream);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("empty package", string(), ver.package);
}

void ParserTests::testParsingPackageName()
{
    const auto gcc = PackageNameData::decompose("gcc");
    CPPUNIT_ASSERT_EQUAL("gcc"sv, gcc.actualName);
    CPPUNIT_ASSERT_EQUAL(""sv, gcc.targetPrefix);
    CPPUNIT_ASSERT_EQUAL(""sv, gcc.vcsSuffix);
    CPPUNIT_ASSERT(!gcc.isVcsPackage());
    const auto mingwGCC = PackageNameData::decompose("mingw-w64-gcc");
    CPPUNIT_ASSERT_EQUAL("gcc"sv, mingwGCC.actualName);
    CPPUNIT_ASSERT_EQUAL("mingw-w64"sv, mingwGCC.targetPrefix);
    CPPUNIT_ASSERT_EQUAL(""sv, mingwGCC.vcsSuffix);
    const auto mingwUcrtCppWinrt = PackageNameData::decompose("mingw-w64-ucrt-cppwinrt");
    CPPUNIT_ASSERT_EQUAL("cppwinrt"sv, mingwUcrtCppWinrt.actualName);
    CPPUNIT_ASSERT_EQUAL("mingw-w64-ucrt"sv, mingwUcrtCppWinrt.targetPrefix);
    CPPUNIT_ASSERT_EQUAL(""sv, mingwUcrtCppWinrt.vcsSuffix);
    const auto mingwClangEnv = PackageNameData::decompose("mingw-w64-clang-environment");
    CPPUNIT_ASSERT_EQUAL("environment"sv, mingwClangEnv.actualName);
    CPPUNIT_ASSERT_EQUAL("mingw-w64-clang"sv, mingwClangEnv.targetPrefix);
    CPPUNIT_ASSERT_EQUAL(""sv, mingwClangEnv.vcsSuffix);
    const auto mingwClangCrt = PackageNameData::decompose("mingw-w64-clang-aarch64-crt");
    CPPUNIT_ASSERT_EQUAL("crt"sv, mingwClangCrt.actualName);
    CPPUNIT_ASSERT_EQUAL("mingw-w64-clang-aarch64"sv, mingwClangCrt.targetPrefix);
    CPPUNIT_ASSERT_EQUAL(""sv, mingwClangCrt.vcsSuffix);
    const auto staticCompatQt6 = PackageNameData::decompose("static-compat-qt6-base");
    CPPUNIT_ASSERT_EQUAL("qt6-base"sv, staticCompatQt6.actualName);
    CPPUNIT_ASSERT_EQUAL("static-compat"sv, staticCompatQt6.targetPrefix);
    CPPUNIT_ASSERT_EQUAL(""sv, staticCompatQt6.vcsSuffix);
    const auto armGCC = PackageNameData::decompose("aarch64-linux-gnu-gcc");
    CPPUNIT_ASSERT_EQUAL("gcc"sv, armGCC.actualName);
    CPPUNIT_ASSERT_EQUAL("aarch64-linux-gnu"sv, armGCC.targetPrefix);
    CPPUNIT_ASSERT_EQUAL(""sv, armGCC.vcsSuffix);
    const auto qt = PackageNameData::decompose("qt5-base-git");
    CPPUNIT_ASSERT_EQUAL("qt5-base"sv, qt.actualName);
    CPPUNIT_ASSERT_EQUAL(""sv, qt.targetPrefix);
    CPPUNIT_ASSERT_EQUAL("git"sv, qt.vcsSuffix);
    const auto mingwQt = PackageNameData::decompose("mingw-w64-qt5-base-git");
    CPPUNIT_ASSERT_EQUAL("qt5-base"sv, mingwQt.actualName);
    CPPUNIT_ASSERT_EQUAL("mingw-w64"sv, mingwQt.targetPrefix);
    CPPUNIT_ASSERT_EQUAL("git"sv, mingwQt.vcsSuffix);
    CPPUNIT_ASSERT(mingwQt.isVcsPackage());
    const auto qwt = PackageNameData::decompose("qwt-qt6-svn");
    CPPUNIT_ASSERT_EQUAL("qwt"sv, qwt.actualName);
    CPPUNIT_ASSERT_EQUAL(""sv, qwt.targetPrefix);
    CPPUNIT_ASSERT_EQUAL("qt6-svn"sv, qwt.vcsSuffix);
    CPPUNIT_ASSERT(qwt.isVcsPackage());
}

void ParserTests::testParsingConfig()
{
    // prepare pacman.conf
    const auto pacmanConfigWorkingCopyPath = workingCopyPath("pacman.conf"s, WorkingCopyMode::NoCopy);
    {
        const auto mirrorListPath = testFilePath("mirrorlist"s);
        auto defaultPacmanConfig = readFile(testFilePath("pacman.conf"s), 5 * 1024);
        findAndReplace(defaultPacmanConfig, "/etc/pacman.d/mirrorlist"s, mirrorListPath);
        auto pacmanConfigWorkingCopy = std::ofstream();
        pacmanConfigWorkingCopy.exceptions(ios_base::failbit | ios_base::badbit);
        pacmanConfigWorkingCopy.open(pacmanConfigWorkingCopyPath, ios_base::out | ios_base::trunc | ios_base::binary);
        pacmanConfigWorkingCopy.write(defaultPacmanConfig.data(), static_cast<streamsize>(defaultPacmanConfig.size()));
    }

    auto config = Config();
    config.initStorage(workingCopyPath("test-parsing-pacman-config.db", WorkingCopyMode::Cleanup).data());
    config.loadPacmanConfig(pacmanConfigWorkingCopyPath.data());
    for (auto &db : config.databases) {
        db.deducePathsFromLocalDirs();
    }

    CPPUNIT_ASSERT_EQUAL_MESSAGE("cache dir"s, std::vector<std::string>{ "/cache/path/"s }, config.packageCacheDirs);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("pacman database path"s, "/db/path/"s, config.pacmanDatabasePath);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("3 databases found"s, 3ul, config.databases.size());
    CPPUNIT_ASSERT_EQUAL("core"s, config.databases[0].name);
    CPPUNIT_ASSERT_EQUAL("extra"s, config.databases[1].name);
    CPPUNIT_ASSERT_EQUAL("community"s, config.databases[2].name);
    const auto mirrorsCore = std::vector<std::string>{ "http://ftp.fau.de/archlinux/core/os/i686"s, "https://ftp.fau.de/archlinux/core/os/i686"s };
    CPPUNIT_ASSERT_EQUAL_MESSAGE("mirrors read correctly in first place"s, mirrorsCore, config.databases[0].mirrors);
    const auto mirrorsExtra = std::vector<std::string>{ "http://ftp.fau.de/archlinux/extra/os/i686"s, "https://ftp.fau.de/archlinux/extra/os/i686"s };
    CPPUNIT_ASSERT_EQUAL_MESSAGE("reusing already parsed mirror list"s, mirrorsExtra, config.databases[1].mirrors);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("regular database file"s, "/db/path/sync/extra.db"s, config.databases[1].path);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("database file containing files"s, "/db/path/sync/extra.files"s, config.databases[1].filesPath);

    // clean working copy
    remove(pacmanConfigWorkingCopyPath.data());
}

void ParserTests::testParsingPlainSrcInfo()
{
    const auto srcInfo = readFile(testFilePath("c++utilities/SRCINFO"s));
    const auto packages = Package::fromInfo(srcInfo, false);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("1 package present"s, 1ul, packages.size());

    const Package &pkg1 = *packages.front().pkg;
    CPPUNIT_ASSERT_EQUAL_MESSAGE("origin", PackageOrigin::SourceInfo, pkg1.origin);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("name"s, "c++utilities"s, pkg1.name);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("version"s, "4.5.0-1"s, pkg1.version);
    CPPUNIT_ASSERT_MESSAGE("no regular dependencies"s, pkg1.dependencies.empty());
    CPPUNIT_ASSERT_EQUAL_MESSAGE("optional dependencies"s,
        vector<Dependency>{ Dependency("c++utilities-doc"s, string(), DependencyMode::Any, "API documentation"s) }, pkg1.optionalDependencies);
    CPPUNIT_ASSERT_MESSAGE("source info present", pkg1.sourceInfo.has_value());
    CPPUNIT_ASSERT_MESSAGE("no package info present", !pkg1.packageInfo.has_value());
    CPPUNIT_ASSERT_EQUAL_MESSAGE(
        "make dependencies"s, vector<Dependency>{ Dependency("cmake"s, "3.0"s, DependencyMode::GreatherEqual) }, pkg1.sourceInfo->makeDependencies);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("check dependencies"s, vector<Dependency>{ Dependency("cppunit"s) }, pkg1.sourceInfo->checkDependencies);
}

void ParserTests::testParsingSplitPackageSrcInfo()
{
    const auto srcInfo = readFile(testFilePath("mingw-w64-harfbuzz/SRCINFO"s));
    const auto packages = Package::fromInfo(srcInfo, false);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("2 (split) packages present"s, 2ul, packages.size());

    const Package &pkg1 = *packages.front().pkg, &pkg2 = *packages.back().pkg;
    CPPUNIT_ASSERT_EQUAL_MESSAGE("origin (1)", PackageOrigin::SourceInfo, pkg1.origin);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("origin (2)", PackageOrigin::SourceInfo, pkg2.origin);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("name (1)"s, "mingw-w64-harfbuzz"s, pkg1.name);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("name (2)"s, "mingw-w64-harfbuzz-icu"s, pkg2.name);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("version (1)"s, "1.4.2-1"s, pkg1.version);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("version (2)"s, "1.4.2-1"s, pkg2.version);
    const vector<Dependency> dependencies1 = {
        Dependency("mingw-w64-freetype2"s),
        Dependency("mingw-w64-glib2"s),
        Dependency("mingw-w64-graphite"s),
    };
    CPPUNIT_ASSERT_EQUAL_MESSAGE("dependencies (1)"s, dependencies1, pkg1.dependencies);
    const vector<Dependency> dependencies2 = {
        Dependency("mingw-w64-harfbuzz"s),
        Dependency("mingw-w64-icu"s),
    };
    CPPUNIT_ASSERT_EQUAL_MESSAGE("dependencies (2)"s, dependencies2, pkg2.dependencies);
    CPPUNIT_ASSERT_MESSAGE("source info present (1)", pkg1.sourceInfo.has_value());
    CPPUNIT_ASSERT_MESSAGE("source info present (2)", pkg2.sourceInfo.has_value());
    CPPUNIT_ASSERT_MESSAGE("no package info present (1)", !pkg1.packageInfo.has_value());
    CPPUNIT_ASSERT_MESSAGE("no package info present (2)", !pkg2.packageInfo.has_value());
    CPPUNIT_ASSERT_EQUAL_MESSAGE("pkgbase (1)"s, "mingw-w64-harfbuzz"s, pkg1.sourceInfo->name);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("pkgbase (2)"s, "mingw-w64-harfbuzz"s, pkg2.sourceInfo->name);
    const vector<string> archs = { "any"s };
    CPPUNIT_ASSERT_EQUAL_MESSAGE("arch (1)"s, archs, pkg1.sourceInfo->archs);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("arch (2)"s, archs, pkg2.sourceInfo->archs);
}

void ParserTests::testParsingSplitPackageSrcInfoWithDifferentArchs()
{
    const auto srcInfo = readFile(testFilePath("jdk/SRCINFO"s));
    const auto packages = Package::fromInfo(srcInfo, false);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("3 (split) packages present"s, 3ul, packages.size());

    const auto &jre = packages[0].pkg, &jdk = packages[1].pkg, &doc = packages[2].pkg;
    CPPUNIT_ASSERT_MESSAGE("source info present (jdk)", jdk->sourceInfo.has_value());
    CPPUNIT_ASSERT_MESSAGE("source info present (jre)", jre->sourceInfo.has_value());
    CPPUNIT_ASSERT_EQUAL_MESSAGE("jre has same source info as base", jdk->sourceInfo->archs, jre->sourceInfo->archs);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("jdk-doc has same source info as base", jdk->sourceInfo, doc->sourceInfo);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("jre name", "jre"s, jre->name);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("jdk name", "jdk"s, jdk->name);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("jdk-doc name", "jdk-doc"s, doc->name);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("base archs", std::vector{ "x86_64"s }, jre->sourceInfo->archs);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("jre archs (empty, base applies)", std::vector<std::string>{}, jre->archs);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("jdk archs (empty, base applies)", std::vector<std::string>{}, jdk->archs);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("jdk-doc archs (overridden)", std::vector{ "any"s }, doc->archs);
}

void ParserTests::testParsingPkgInfo()
{
    const auto pkgInfo = readFile(testFilePath("mingw-w64-harfbuzz/PKGINFO"));
    const auto packages = Package::fromInfo(pkgInfo, true);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("1 package present"s, 1ul, packages.size());
    CPPUNIT_ASSERT_EQUAL_MESSAGE("origin", PackageOrigin::PackageInfo, packages.front().pkg->origin);
    checkHarfbuzzPackage(*packages.front().pkg);
}

void ParserTests::testParsingPkgName()
{
    const auto pkg = Package::fromPkgFileName("texlive-localmanager-git-0.4.6.r0.gd71966e-1-any.pkg");
    CPPUNIT_ASSERT_EQUAL_MESSAGE("origin", PackageOrigin::PackageFileName, pkg->origin);
    CPPUNIT_ASSERT_EQUAL("texlive-localmanager-git"s, pkg->name);
    CPPUNIT_ASSERT_EQUAL("0.4.6.r0.gd71966e-1"s, pkg->version);
    CPPUNIT_ASSERT_EQUAL("any"s, pkg->arch);
}

void ParserTests::testParsingDatabase()
{
    const auto dbPath = testFilePath("extra.files.truncated.tar.gz");
    const auto expectedPackages = std::unordered_set<std::string>{ "389-ds-base", "0ad-data", "0ad" };
    auto parsedPackages = std::unordered_set<std::string>();
    auto error = std::string();
    try {
        LibPkg::Package::fromDatabaseFile(dbPath, [&](const std::shared_ptr<LibPkg::Package> &package) {
            parsedPackages.insert(package->name);
            return false;
        });
    } catch (const std::runtime_error &e) {
        error = e.what();
    }
    TESTUTILS_ASSERT_LIKE_FLAGS("truncation not silently ignored", ".*(unable|error).*extra.files.truncated.tar.gz.*truncated.*",
        std::regex::ECMAScript | std::regex::icase, error);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("was able to parse the first few packages despite truncation", expectedPackages, parsedPackages);
}

void ParserTests::testExtractingPkgFile()
{
    const auto pkgFilePath = testFilePath("mingw-w64-harfbuzz/mingw-w64-harfbuzz-1.4.2-1-any.pkg.tar.xz");
    const auto package = Package::fromPkgFile(pkgFilePath);
    checkHarfbuzzPackage(*package);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("origin", PackageOrigin::PackageContents, package->origin);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("file name"s, "mingw-w64-harfbuzz-1.4.2-1-any.pkg.tar.xz"s, package->packageInfo->fileName);
}

void ParserTests::testParsingDescriptions()
{
    const auto desc = readFile(testFilePath("mingw-w64-harfbuzz/desc"s));
    const auto pkg = Package::fromDescription({ desc });
    checkHarfbuzzPackage(*pkg);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("origin", PackageOrigin::Database, pkg->origin);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("file name"s, "mingw-w64-harfbuzz-1.4.2-1-any.pkg.tar.xz"s, pkg->packageInfo->fileName);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("md5sum"s, "a05d4618090b0294bc075e85791485f8"s, pkg->packageInfo->md5);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("sha256sum"s, "ff62339041c19d2a986eed8231fb8e1be723b3afd354cca833946305456e8ec7"s, pkg->packageInfo->sha256);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("sha256sum"s, "ff62339041c19d2a986eed8231fb8e1be723b3afd354cca833946305456e8ec7"s, pkg->packageInfo->sha256);
}

void ParserTests::testParsingDatabaseAndOverallStorageBehavior()
{
    auto dbFile = workingCopyPath("test-parsing-database.db", WorkingCopyMode::Cleanup);

    {
        // init config
        auto config = Config();
        config.initStorage(dbFile.data());

        // init db object
        auto *const db = config.findOrCreateDatabase("test"sv, "x86_64"sv);
        db->path = testFilePath("core.db");
        db->filesPath = testFilePath("core.files");

        // load packages
        config.loadAllPackages(true, false);
        CPPUNIT_ASSERT_EQUAL_MESSAGE("all 215 packages present"s, 215_st, db->packageCount());
        const auto autoreconf = db->findPackage("autoconf");
        CPPUNIT_ASSERT_MESSAGE("autoreconf exists", autoreconf != nullptr);
        CPPUNIT_ASSERT_EQUAL_MESSAGE("origin", PackageOrigin::Database, autoreconf->origin);
        checkAutoconfPackage(*autoreconf);
    }

    {
        // load config again to test persistency of database/storage
        auto config = Config();
        config.initStorage(dbFile.data());
        auto *const db = config.findOrCreateDatabase("test"sv, "x86_64"sv);
        CPPUNIT_ASSERT_EQUAL_MESSAGE("config persistent; all 215 packages still present"s, 215_st, db->packageCount());

        // test clearing storage
        db->clearPackages();
        CPPUNIT_ASSERT_EQUAL_MESSAGE("config cleared; no more packages present"s, 0_st, db->packageCount());

        // insert one package again to test whether setup still works (e.g. not whole database has been dropped)
        auto pkg = std::make_shared<Package>();
        pkg->name = "foo";
        pkg->version = "42";
        const auto pkgID = db->updatePackage(pkg);
        CPPUNIT_ASSERT_EQUAL_MESSAGE("again one package present"s, 1_st, db->packageCount());
        CPPUNIT_ASSERT_EQUAL_MESSAGE("same object returned due to caching", pkg, db->findPackage(pkgID));
    }
}

void ParserTests::testParsingSignatureLevel()
{
    CPPUNIT_ASSERT_EQUAL_MESSAGE("default signature level", "Optional TrustedOnly"s, signatureLevelToString(SignatureLevel::Default));
    CPPUNIT_ASSERT_EQUAL_MESSAGE("invalid signature level", std::string(), signatureLevelToString(SignatureLevel::Invalid));
    CPPUNIT_ASSERT_EQUAL_MESSAGE("explicit 'when'", "Never TrustedOnly"s, signatureLevelToString(SignatureLevel::Never));
    CPPUNIT_ASSERT_EQUAL_MESSAGE(
        "explicit 'when'; explicit 'what'", "Never TrustAll"s, signatureLevelToString(SignatureLevel::Never | SignatureLevel::TrustAll));
    CPPUNIT_ASSERT_EQUAL_MESSAGE("explicit 'what'", "Optional TrustAll"s, signatureLevelToString(SignatureLevel::TrustAll));
    CPPUNIT_ASSERT_EQUAL_MESSAGE(
        "same config for DBs and package", "Never TrustAll"s, SignatureLevelConfig(SignatureLevel::Never | SignatureLevel::TrustAll).toString());
    CPPUNIT_ASSERT_EQUAL_MESSAGE("different config for DBs and package", "DatabaseOptional DatabaseTrustedOnly PackageNever PackageTrustAll"s,
        SignatureLevelConfig(SignatureLevel::Default, SignatureLevel::Never | SignatureLevel::TrustAll).toString());
}

void ParserTests::testSerializingDatabaseSignatureLevel()
{
    CPPUNIT_ASSERT_EQUAL_MESSAGE(
        "default signature level", SignatureLevelConfig(SignatureLevel::Default), SignatureLevelConfig::fromString(std::string_view()));
    CPPUNIT_ASSERT_EQUAL_MESSAGE(
        "invalid signature level", SignatureLevelConfig(SignatureLevel::Invalid), SignatureLevelConfig::fromString("foo bar"));
    CPPUNIT_ASSERT_EQUAL_MESSAGE(
        "explicit 'when'", SignatureLevelConfig(SignatureLevel::Never | SignatureLevel::TrustedOnly), SignatureLevelConfig::fromString("Never"));
    CPPUNIT_ASSERT_EQUAL_MESSAGE("explicit 'when'; explicit 'what'", SignatureLevelConfig(SignatureLevel::Never | SignatureLevel::TrustAll),
        SignatureLevelConfig::fromString("Never TrustAll"));
    CPPUNIT_ASSERT_EQUAL_MESSAGE(
        "explicit 'what'", SignatureLevelConfig(SignatureLevel::Optional | SignatureLevel::TrustAll), SignatureLevelConfig::fromString("TrustAll"));
}
