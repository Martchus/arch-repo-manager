#include "./parser_helper.h"

#ifdef LIBREPOMGR_BUILD
#include "../../libpkg/data/package.h"
#else
#include "../data/package.h"
#endif

#include <c++utilities/tests/testutils.h>

using CppUtilities::operator<<; // must be visible prior to the call site
#include <cppunit/extensions/HelperMacros.h>

using namespace std;
using namespace CPPUNIT_NS;
using namespace LibPkg;

namespace TestHelper {

void checkHarfbuzzPackage(const Package &pkg)
{
    CPPUNIT_ASSERT_EQUAL_MESSAGE("name"s, "mingw-w64-harfbuzz"s, pkg.name);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("version"s, "1.4.2-1"s, pkg.version);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("license"s, vector<string>{ "MIT"s }, pkg.licenses);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("upstream URL"s, "http://www.freedesktop.org/wiki/Software/HarfBuzz"s, pkg.upstreamUrl);
    const vector<Dependency> dependencies = {
        Dependency("mingw-w64-freetype2"s),
        Dependency("mingw-w64-glib2"s),
        Dependency("mingw-w64-graphite"s),
    };
    CPPUNIT_ASSERT_EQUAL_MESSAGE("dependencies"s, dependencies, pkg.dependencies);
    CPPUNIT_ASSERT_MESSAGE("source info present"s, pkg.sourceInfo);
    CPPUNIT_ASSERT_MESSAGE("package info present"s, pkg.packageInfo);
    CPPUNIT_ASSERT_MESSAGE("no source archs present"s, pkg.sourceInfo->archs.empty());
    CPPUNIT_ASSERT_EQUAL_MESSAGE("package arch"s, "Martchus <@.net>"s, pkg.packageInfo->packager);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("packer"s, "any"s, pkg.arch);
    const vector<Dependency> makeDependencies = {
        Dependency("mingw-w64-configure"s),
        Dependency("mingw-w64-cairo"s),
        Dependency("mingw-w64-icu"s),
        Dependency("mingw-w64-graphite"s),
        Dependency("mingw-w64-freetype2"s),
    };
    CPPUNIT_ASSERT_EQUAL_MESSAGE("make dependencies"s, makeDependencies, pkg.sourceInfo->makeDependencies);
}

void checkHarfbuzzPackagePeDependencies(const Package &package)
{
    CPPUNIT_ASSERT_EQUAL_MESSAGE("origin", PackageOrigin::PackageContents, package.origin);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("file name"s, "mingw-w64-harfbuzz-1.4.2-1-any.pkg.tar.xz"s, package.packageInfo->fileName);
    const set<string> dllnames({ "pe-i386::libharfbuzz-0.dll", "pe-i386::libharfbuzz-gobject-0.dll", "pe-x86_64::libharfbuzz-0.dll",
        "pe-x86_64::libharfbuzz-gobject-0.dll" });
    CPPUNIT_ASSERT_EQUAL_MESSAGE("library provides from DLL names"s, dllnames, package.libprovides);
    const set<string> required({ "pe-i386::kernel32.dll", "pe-i386::user32.dll", "pe-i386::libfreetype-6.dll", "pe-i386::libgcc_s_sjlj-1.dll",
        "pe-i386::libglib-2.0-0.dll", "pe-i386::libgobject-2.0-0.dll", "pe-i386::libgraphite2.dll", "pe-i386::libharfbuzz-0.dll",
        "pe-i386::msvcrt.dll", "pe-x86_64::kernel32.dll", "pe-x86_64::user32.dll", "pe-x86_64::libfreetype-6.dll", "pe-x86_64::libgcc_s_seh-1.dll",
        "pe-x86_64::libglib-2.0-0.dll", "pe-x86_64::libgobject-2.0-0.dll", "pe-x86_64::libgraphite2.dll", "pe-x86_64::libharfbuzz-0.dll",
        "pe-x86_64::msvcrt.dll" });
    CPPUNIT_ASSERT_EQUAL_MESSAGE("library dependencies from requires"s, required, package.libdepends);
}

void checkAutoconfPackage(const Package &pkg)
{
    CPPUNIT_ASSERT_EQUAL_MESSAGE("name"s, "autoconf"s, pkg.name);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("version"s, "2.69-4"s, pkg.version);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("license"s, (vector<string>{ "GPL2", "GPL3", "custom" }), pkg.licenses);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("groups"s, (vector<string>{ "base-devel" }), pkg.groups);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("upstream URL"s, "http://www.gnu.org/software/autoconf"s, pkg.upstreamUrl);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("description"s, "A GNU tool for automatically configuring source code"s, pkg.description);
    const vector<Dependency> dependencies = {
        Dependency("awk"s),
        Dependency("m4"s),
        Dependency("diffutils"s),
        Dependency("sh"s),
    };
    CPPUNIT_ASSERT_EQUAL_MESSAGE("dependencies"s, dependencies, pkg.dependencies);
    CPPUNIT_ASSERT_MESSAGE("no optional dependencies"s, pkg.optionalDependencies.empty());
    CPPUNIT_ASSERT_MESSAGE("install info present"s, pkg.installInfo);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("install size correct"s, 2098176u, pkg.installInfo->installedSize);
    CPPUNIT_ASSERT_MESSAGE("source info present"s, pkg.sourceInfo);
    CPPUNIT_ASSERT_MESSAGE("package info present"s, pkg.packageInfo);
    CPPUNIT_ASSERT_MESSAGE("no source archs present"s, pkg.sourceInfo->archs.empty());
    CPPUNIT_ASSERT_EQUAL_MESSAGE("packer"s, "Allan McRae <allan@archlinux.org>"s, pkg.packageInfo->packager);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("package arch"s, "any"s, pkg.arch);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("package file name"s, "autoconf-2.69-4-any.pkg.tar.xz"s, pkg.packageInfo->fileName);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("package build date"s, 636089958990000000ul, pkg.packageInfo->buildDate.totalTicks());
    CPPUNIT_ASSERT_EQUAL_MESSAGE("package files"s, 74ul, pkg.packageInfo->files.size());
    CPPUNIT_ASSERT_EQUAL("usr/"s, pkg.packageInfo->files.front());
    const vector<Dependency> makeDependencies = { Dependency("help2man"s) };
    CPPUNIT_ASSERT_EQUAL_MESSAGE("make dependencies"s, makeDependencies, pkg.sourceInfo->makeDependencies);
}

void checkCmakePackageSoDependencies(const Package &package)
{
    CPPUNIT_ASSERT_EQUAL_MESSAGE("origin", PackageOrigin::PackageContents, package.origin);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("library provides from sonames"s, set<string>(), package.libprovides);
    const set<string> requires2({ "elf-x86_64::libQt5Core.so.5", "elf-x86_64::libQt5Gui.so.5", "elf-x86_64::libQt5Widgets.so.5",
        "elf-x86_64::libarchive.so.13", "elf-x86_64::libc.so.6", "elf-x86_64::libcurl.so.4", "elf-x86_64::libgcc_s.so.1",
        "elf-x86_64::libjsoncpp.so.11", "elf-x86_64::libstdc++.so.6", "elf-x86_64::libdl.so.2", "elf-x86_64::libz.so.1", "elf-x86_64::libexpat.so.1",
        "elf-x86_64::libformw.so.6", "elf-x86_64::libncursesw.so.6", "elf-x86_64::libuv.so.1" });
    CPPUNIT_ASSERT_EQUAL_MESSAGE("library dependencies from requires"s, requires2, package.libdepends);
}

void checkSyncthingTrayPackageSoDependencies(const Package &package)
{
    CPPUNIT_ASSERT_EQUAL_MESSAGE("origin", PackageOrigin::PackageContents, package.origin);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("file name"s, "syncthingtray-0.6.2-1-x86_64.pkg.tar.xz"s, package.packageInfo->fileName);
    const set<string> sonames({ "elf-x86_64::libsyncthingwidgets.so.0.6.2", "elf-x86_64::libsyncthingmodel.so.0.6.2",
        "elf-x86_64::libsyncthingconnector.so.0.6.2", "elf-x86_64::libsyncthingfileitemaction.so" });
    CPPUNIT_ASSERT_EQUAL_MESSAGE("library provides from sonames"s, sonames, package.libprovides);
    const set<string> required({ "elf-x86_64::libqtutilities.so.5", "elf-x86_64::libsyncthingmodel.so.0.6.2", "elf-x86_64::libKF5KIOWidgets.so.5",
        "elf-x86_64::libsyncthingconnector.so.0.6.2", "elf-x86_64::libsyncthingwidgets.so.0.6.2", "elf-x86_64::libKF5KIOCore.so.5",
        "elf-x86_64::libQt5Network.so.5", "elf-x86_64::libQt5Widgets.so.5", "elf-x86_64::libQt5Gui.so.5", "elf-x86_64::libKF5CoreAddons.so.5",
        "elf-x86_64::libQt5Core.so.5", "elf-x86_64::libQt5DBus.so.5", "elf-x86_64::libQt5Svg.so.5", "elf-x86_64::libQt5WebKit.so.5",
        "elf-x86_64::libQt5WebKitWidgets.so.5", "elf-x86_64::libc++utilities.so.4", "elf-x86_64::libstdc++.so.6", "elf-x86_64::libgcc_s.so.1",
        "elf-x86_64::libc.so.6" });
    CPPUNIT_ASSERT_EQUAL_MESSAGE("library dependencies from requires"s, required, package.libdepends);
}

} // namespace TestHelper
