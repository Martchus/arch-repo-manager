#include "./parser_helper.h"

#include "../parser/binary.h"
#include "../parser/config.h"
#include "../parser/package.h"

#include <c++utilities/conversion/stringbuilder.h>
#include <c++utilities/conversion/stringconversion.h>
#include <c++utilities/io/path.h>
#include <c++utilities/tests/testutils.h>

using CppUtilities::operator<<; // must be visible prior to the call site
#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>

#include <iostream>
#include <string>
#include <vector>

using namespace std;
using namespace CPPUNIT_NS;
using namespace CppUtilities;
using namespace LibPkg;
using namespace TestHelper;

class BinaryParserTests : public TestFixture {
    CPPUNIT_TEST_SUITE(BinaryParserTests);
    CPPUNIT_TEST(testParsingElf);
    CPPUNIT_TEST(testParsingPe);
    CPPUNIT_TEST(testParsingAr);
    CPPUNIT_TEST(testParsingElfFromPkgFile);
    CPPUNIT_TEST(testParsingPeFromPkgFile);
    CPPUNIT_TEST(testParsingArFromPkgFile);
    CPPUNIT_TEST_SUITE_END();

public:
    void setUp() override;
    void tearDown() override;

    void testParsingElf();
    void testParsingPe();
    void testParsingAr();
    void testParsingElfFromPkgFile();
    void testParsingPeFromPkgFile();
    void testParsingArFromPkgFile();
};

CPPUNIT_TEST_SUITE_REGISTRATION(BinaryParserTests);

void BinaryParserTests::setUp()
{
}

void BinaryParserTests::tearDown()
{
}

void BinaryParserTests::testParsingElf()
{
    Binary bin;
    bin.load(testFilePath("c++utilities/libc++utilities.so.4.5.0"));
    CPPUNIT_ASSERT_EQUAL_MESSAGE("type", BinaryType::Elf, bin.type);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("class", BinaryClass::Class64Bit, bin.binaryClass);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("sub type", BinarySubType::SharedObject, bin.subType);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("arch", "x86_64"s, bin.architecture);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("name", "libc++utilities.so.4"s, bin.name);
    const set<string> requiredLibs({ "libstdc++.so.6"s, "libm.so.6"s, "libgcc_s.so.1"s, "libc.so.6"s });
    CPPUNIT_ASSERT_EQUAL_MESSAGE("requires", requiredLibs, bin.requiredLibs);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("rpath", "/test/rpath:/foo/bar"s, bin.rpath);
}

void BinaryParserTests::testParsingPe()
{
    Binary bin;
    bin.load(testFilePath("c++utilities/c++utilities.dll"));
    CPPUNIT_ASSERT_EQUAL_MESSAGE("type", BinaryType::Pe, bin.type);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("arch", "x86_64"s, bin.architecture);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("name", "c++utilities.dll"s, bin.name);
    const set<string> requiredLibs({ "libgcc_s_seh-1.dll"s, "KERNEL32.dll"s, "msvcrt.dll"s, "SHELL32.dll"s, "libstdc++-6.dll"s, "libiconv-2.dll"s });
    CPPUNIT_ASSERT_EQUAL_MESSAGE("requires", requiredLibs, bin.requiredLibs);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("rpath", string(), bin.rpath);
}

void BinaryParserTests::testParsingAr()
{
    Binary bin;
    bin.load(testFilePath("mingw-w64-crt/libkernel32.a"));
    CPPUNIT_ASSERT_EQUAL_MESSAGE("type", BinaryType::Ar, bin.type);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("arch", "x86_64"s, bin.architecture);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("name", "KERNEL32.dll"s, bin.name);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("rpath", string(), bin.rpath);
}

void BinaryParserTests::testParsingElfFromPkgFile()
{
    const auto pkgFilePath = testFilePath("syncthingtray/syncthingtray-0.6.2-1-x86_64.pkg.tar.xz");
    const auto package = Package::fromPkgFile(pkgFilePath);
    checkSyncthingTrayPackageSoDependencies(*package);

    const auto pkgFilePath2 = testFilePath("cmake/cmake-3.8.2-1-x86_64.pkg.tar.xz");
    const auto package2 = Package::fromPkgFile(pkgFilePath2);
    checkCmakePackageSoDependencies(*package2);

    const auto pkgFilePath3 = testFilePath("perl/perl-linux-desktopfiles-0.22-2-any.pkg.tar.xz");
    const auto package3 = Package::fromPkgFile(pkgFilePath3);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("origin", PackageOrigin::PackageContents, package3->origin);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("arch", "any"s, package3->packageInfo->arch);
    const unordered_set<Dependency> expectedPerlDependencies{
        Dependency("perl", "5.28", DependencyMode::GreatherEqual), // because contained module built against Perl 5.28
        Dependency("perl", "5.29", DependencyMode::LessThan), // because contained module built against Perl 5.28
    };
    // note: Dependency perl>=5.14.0 from PKGBUILD has been removed. I guess that's ok.
    CPPUNIT_ASSERT_EQUAL_MESSAGE(
        "perl dependencies", expectedPerlDependencies, unordered_set<Dependency>(package3->dependencies.begin(), package3->dependencies.end()));
    const set<string> expectedPerlLibProvides;
    CPPUNIT_ASSERT_EQUAL_MESSAGE("perl lib provides", expectedPerlLibProvides, package3->libprovides);
    const set<string> expectedPerlLibDepends;
    CPPUNIT_ASSERT_EQUAL_MESSAGE("perl lib depends", expectedPerlLibDepends, package3->libdepends);

    const auto pkgFilePath4 = testFilePath("python/sphinxbase-5prealpha-7-i686.pkg.tar.xz");
    const auto package4 = Package::fromPkgFile(pkgFilePath4);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("origin", PackageOrigin::PackageContents, package4->origin);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("arch", "i686"s, package4->packageInfo->arch);
    const unordered_set<Dependency> expectedPythonDependencies{
        Dependency("libpulse"), // from PKGBUILD
        Dependency("lapack"), // from PKGBUILD
        Dependency("python2>=2.7"), // because contains Python module built against Python 2.7
        Dependency("python>=3.6"), // because contains Python module built against Python 3.6
        Dependency("python2<2.8"), // because contains Python module built against Python 2.7
        Dependency("python<3.7"), // because contains Python module built against Python 3.6
    };
    CPPUNIT_ASSERT_EQUAL_MESSAGE(
        "python dependencies", expectedPythonDependencies, unordered_set<Dependency>(package4->dependencies.begin(), package4->dependencies.end()));
    const set<string> expectedPythonLibProvides{
        "elf-i386::_sphinxbase.so.0",
        "elf-i386::libsphinxad.so.3",
        "elf-i386::libsphinxbase.so.3",
    };
    CPPUNIT_ASSERT_EQUAL_MESSAGE("python lib provides", expectedPythonLibProvides, package4->libprovides);
    const set<string> expectedPythonLibDepends{
        "elf-i386::libblas.so.3",
        "elf-i386::libc.so.6",
        "elf-i386::liblapack.so.3",
        "elf-i386::libm.so.6",
        "elf-i386::libpthread.so.0",
        "elf-i386::libpulse-simple.so.0",
        "elf-i386::libpulse.so.0",
        "elf-i386::libsphinxad.so.3",
        "elf-i386::libsphinxbase.so.3",
    };
    CPPUNIT_ASSERT_EQUAL_MESSAGE("python lib depends", expectedPythonLibDepends, package4->libdepends);
}

void BinaryParserTests::testParsingPeFromPkgFile()
{
    const auto pkgFilePath = testFilePath("mingw-w64-harfbuzz/mingw-w64-harfbuzz-1.4.2-1-any.pkg.tar.xz");
    const auto package = Package::fromPkgFile(pkgFilePath);
    checkHarfbuzzPackagePeDependencies(*package);
}

void BinaryParserTests::testParsingArFromPkgFile()
{
    const auto pkgFilePath = testFilePath("mingw-w64-crt/mingw-w64-crt-6.0.0-1-any.pkg.tar.xz");
    const auto package = Package::fromPkgFile(pkgFilePath);
    CPPUNIT_ASSERT_EQUAL("mingw-w64-crt"s, package->name);
    CPPUNIT_ASSERT_EQUAL("6.0.0-1"s, package->version);
    const set<string> expectedDLLs{ "pe-i386::aclui.dll", "pe-i386::activeds.dll", "pe-i386::advapi32.dll", "pe-i386::authz.dll",
        "pe-i386::avicap32.dll", "pe-i386::avifil32.dll", "pe-i386::avrt.dll", "pe-i386::bootvid.dll", "pe-i386::cap.dll", "pe-i386::cfgmgr32.dll",
        "pe-i386::classpnp.sys", "pe-i386::clusapi.dll", "pe-i386::comctl32.dll", "pe-i386::comdlg32.dll", "pe-i386::crypt32.dll",
        "pe-i386::cryptnet.dll", "pe-i386::cryptsp.dll", "pe-i386::cryptxml.dll", "pe-i386::cscapi.dll", "pe-i386::ctl3d32.dll",
        "pe-i386::cabinet.dll", "pe-i386::d3dcompiler_43.dll", "pe-i386::d3dcompiler_46.dll", "pe-i386::d3dcompiler_47.dll",
        "pe-i386::d3dcompiler.dll", "pe-i386::d3dcompiler_37.dll", "pe-i386::d3dcompiler_38.dll", "pe-i386::d3dcompiler_39.dll",
        "pe-i386::d3dcompiler_40.dll", "pe-i386::d3dcompiler_41.dll", "pe-i386::d3dcompiler_42.dll", "pe-i386::davhlpr.dll", "pe-i386::devmgr.dll",
        "pe-i386::devobj.dll", "pe-i386::devrtl.dll", "pe-i386::dhcpcsvc.dll", "pe-i386::dhcpsapi.dll", "pe-i386::dlcapi.dll", "pe-i386::dnsapi.dll",
        "pe-i386::dpapi.dll", "pe-i386::dssec.dll", "pe-i386::dwrite.dll", "pe-i386::esent.dll", "pe-i386::evr.dll", "pe-i386::gdi32.dll",
        "pe-i386::glaux.dll", "pe-i386::glu32.dll", "pe-i386::gpapi.dll", "pe-i386::gpedit.dll", "pe-i386::gpscript.dll", "pe-i386::gptext.dll",
        "pe-i386::genericui.dll", "pe-i386::hal.dll", "pe-i386::hidclass.sys", "pe-i386::hidparse.sys", "pe-i386::httpapi.dll", "pe-i386::icmui.dll",
        "pe-i386::imagehlp.dll", "pe-i386::imm32.dll", "pe-i386::iphlpapi.dll", "pe-i386::iscsidsc.dll", "pe-i386::kernel32.dll", "pe-i386::lz32.dll",
        "pe-i386::mapi32.dll", "pe-i386::mf.dll", "pe-i386::mfcuia32.dll", "pe-i386::mfplat.dll", "pe-i386::mgmtapi.dll", "pe-i386::mpr.dll",
        "pe-i386::mprapi.dll", "pe-i386::mqrt.dll", "pe-i386::msacm32.dll", "pe-i386::msctf.dll", "pe-i386::mshtml.dll", "pe-i386::msimg32.dll",
        "pe-i386::msvcp60.dll", "pe-i386::msvcr110.dll", "pe-i386::msvcr120.dll", "pe-i386::msvcr120d.dll", "pe-i386::msvcr90d.dll",
        "pe-i386::msvfw32.dll", "pe-i386::mswsock.dll", "pe-i386::nddeapi.dll", "pe-i386::ndfapi.dll", "pe-i386::ndis.sys", "pe-i386::netapi32.dll",
        "pe-i386::ntdsapi.dll", "pe-i386::ntmsapi.dll", "pe-i386::normaliz.dll", "pe-i386::odbc32.dll", "pe-i386::odbccp32.dll",
        "pe-i386::oleacc.dll", "pe-i386::oleaut32.dll", "pe-i386::olecli32.dll", "pe-i386::olepro32.dll", "pe-i386::olesvr32.dll",
        "pe-i386::olethk32.dll", "pe-i386::opengl32.dll", "pe-i386::p2p.dll", "pe-i386::p2pcollab.dll", "pe-i386::p2pgraph.dll",
        "pe-i386::penwin32.dll", "pe-i386::pkpd32.dll", "pe-i386::powrprof.dll", "pe-i386::psapi.dll", "pe-i386::qutil.dll", "pe-i386::rapi.dll",
        "pe-i386::rasapi32.dll", "pe-i386::rasdlg.dll", "pe-i386::resutils.dll", "pe-i386::rpcdce4.dll", "pe-i386::rpcns4.dll", "pe-i386::rpcrt4.dll",
        "pe-i386::rtm.dll", "pe-i386::rtutils.dll", "pe-i386::rpcdiag.dll", "pe-i386::rstrtmgr.dll", "pe-i386::schannel.dll", "pe-i386::setupapi.dll",
        "pe-i386::shell32.dll", "pe-i386::shfolder.dll", "pe-i386::shlwapi.dll", "pe-i386::slwga.dll", "pe-i386::spoolss.dll", "pe-i386::svrapi.dll",
        "pe-i386::secur32.dll", "pe-i386::security.dll", "pe-i386::sspicli.dll", "pe-i386::tapi32.dll", "pe-i386::url.dll", "pe-i386::usbcamd.sys",
        "pe-i386::usbcamd2.sys", "pe-i386::usbd.sys", "pe-i386::usbport.sys", "pe-i386::user32.dll", "pe-i386::userenv.dll", "pe-i386::usp10.dll",
        "pe-i386::uxtheme.dll", "pe-i386::vdmdbg.dll", "pe-i386::version.dll", "pe-i386::vssapi.dll", "pe-i386::virtdisk.dll",
        "pe-i386::wdsclientapi.dll", "pe-i386::wdscore.dll", "pe-i386::wdscsl.dll", "pe-i386::wdstptc.dll", "pe-i386::wdsutil.dll",
        "pe-i386::wevtfwd.dll", "pe-i386::wiadss.dll", "pe-i386::win32spl.dll", "pe-i386::winhttp.dll", "pe-i386::wininet.dll", "pe-i386::winmm.dll",
        "pe-i386::winspool.drv", "pe-i386::winstrm.dll", "pe-i386::wintrust.dll", "pe-i386::winusb.dll", "pe-i386::wmilib.sys", "pe-i386::wow32.dll",
        "pe-i386::ws2_32.dll", "pe-i386::wsock32.dll", "pe-i386::wst.dll", "pe-i386::wtsapi32.dll", "pe-i386::wdsclient.dll", "pe-i386::wdsimage.dll",
        "pe-i386::wdsupgcompl.dll", "pe-i386::wecapi.dll", "pe-i386::winscard.dll", "pe-i386::wlanapi.dll", "pe-i386::x3daudio1_2.dll",
        "pe-i386::x3daudio1_3.dll", "pe-i386::x3daudio1_4.dll", "pe-i386::x3daudio1_5.dll", "pe-i386::x3daudio1_6.dll", "pe-i386::x3daudio1_7.dll",
        "pe-i386::x3daudiod1_7.dll", "pe-i386::xapofx1_0.dll", "pe-i386::xapofx1_1.dll", "pe-i386::xapofx1_2.dll", "pe-i386::xapofx1_3.dll",
        "pe-i386::xapofx1_4.dll", "pe-i386::xapofx1_5.dll", "pe-i386::xapofxd1_5.dll", "pe-i386::xaudio2_8.dll", "pe-i386::xinput1_1.dll",
        "pe-i386::xinput1_2.dll", "pe-i386::xinput1_3.dll", "pe-i386::xinput1_4.dll", "pe-i386::xinput9_1_0.dll", "pe-i386::adsldpc.dll",
        "pe-i386::apcups.dll", "pe-i386::bcrypt.dll", "pe-i386::browcli.dll", "pe-i386::bthprops.cpl", "pe-i386::clfsw32.dll", "pe-i386::cmutil.dll",
        "pe-i386::connect.dll", "pe-i386::credui.dll", "pe-i386::crtdll.dll", "pe-i386::d2d1.dll", "pe-i386::d3d11.dll", "pe-i386::d3d8.dll",
        "pe-i386::d3d9.dll", "pe-i386::d3dcsx_46.dll", "pe-i386::d3dcsxd_43.dll", "pe-i386::d3dim.dll", "pe-i386::d3drm.dll",
        "pe-i386::d3dx10_33.dll", "pe-i386::d3dx10_34.dll", "pe-i386::d3dx10_35.dll", "pe-i386::d3dx10_36.dll", "pe-i386::d3dx10_37.dll",
        "pe-i386::d3dx10_38.dll", "pe-i386::d3dx10_39.dll", "pe-i386::d3dx10_40.dll", "pe-i386::d3dx10_41.dll", "pe-i386::d3dx10_42.dll",
        "pe-i386::d3dx10_43.dll", "pe-i386::d3dx11_42.dll", "pe-i386::d3dx11_43.dll", "pe-i386::d3dx8d.dll", "pe-i386::d3dx9_24.dll",
        "pe-i386::d3dx9_25.dll", "pe-i386::d3dx9_26.dll", "pe-i386::d3dx9_27.dll", "pe-i386::d3dx9_28.dll", "pe-i386::d3dx9_29.dll",
        "pe-i386::d3dx9_30.dll", "pe-i386::d3dx9_31.dll", "pe-i386::d3dx9_32.dll", "pe-i386::d3dx9_33.dll", "pe-i386::d3dx9_34.dll",
        "pe-i386::d3dx9_35.dll", "pe-i386::d3dx9_36.dll", "pe-i386::d3dx9_37.dll", "pe-i386::d3dx9_38.dll", "pe-i386::d3dx9_39.dll",
        "pe-i386::d3dx9_40.dll", "pe-i386::d3dx9_41.dll", "pe-i386::d3dx9_42.dll", "pe-i386::d3dx9_43.dll", "pe-i386::d3dx9d.dll",
        "pe-i386::d3dxof.dll", "pe-i386::davclnt.dll", "pe-i386::dbgeng.dll", "pe-i386::dbghelp.dll", "pe-i386::ddraw.dll", "pe-i386::dfscli.dll",
        "pe-i386::dhcpcsvc6.dll", "pe-i386::dinput.dll", "pe-i386::dinput8.dll", "pe-i386::dplayx.dll", "pe-i386::dpnaddr.dll", "pe-i386::dpnet.dll",
        "pe-i386::dpnlobby.def", "pe-i386::dpvoice.dll", "pe-i386::dsetup.dll", "pe-i386::dsound.dll", "pe-i386::dsrole.dll", "pe-i386::dwmapi.dll",
        "pe-i386::dxapi.sys", "pe-i386::dxgi.dll", "pe-i386::dxva2.dll", "pe-i386::eappcfg.dll", "pe-i386::eapphost.dll", "pe-i386::eappprxy.dll",
        "pe-i386::elscore.dll", "pe-i386::faultrep.dll", "pe-i386::fwpuclnt.dll", "pe-i386::gdiplus.dll", "pe-i386::glut.dll", "pe-i386::glut32.dll",
        "pe-i386::hid.dll", "pe-i386::igmpagnt.dll", "pe-i386::ks.sys", "pe-i386::ksproxy.ax", "pe-i386::ksuser.dll", "pe-i386::ktmw32.dll",
        "pe-i386::logoncli.dll", "pe-i386::mcd.sys", "pe-i386::mscms.dll", "pe-i386::msdmo.dll", "pe-i386::msdrm.dll", "pe-i386::mshtmled.dll",
        "pe-i386::msi.dll", "pe-i386::mstask.dll", "pe-i386::msvcp120_app.dll", "pe-i386::msvcr100.dll", "pe-i386::msvcr120_app.dll",
        "pe-i386::msvcr80.dll", "pe-i386::msvcr90.dll", "pe-i386::msvcrt.dll", "pe-i386::ncrypt.dll", "pe-i386::netjoin.dll", "pe-i386::netutils.dll",
        "pe-i386::newdev.dll", "pe-i386::ntdll.dll", "pe-i386::ntoskrnl.exe", "pe-i386::ole32.dll", "pe-i386::oledlg.dll", "pe-i386::pcwum.dll",
        "pe-i386::pdh.dll", "pe-i386::pdhui.dll", "pe-i386::polprocl.dll", "pe-i386::quartz.dll", "pe-i386::qwave.dll", "pe-i386::rpchttp.dll",
        "pe-i386::samcli.dll", "pe-i386::schedcli.dll", "pe-i386::scsiport.sys", "pe-i386::slc.dll", "pe-i386::slcext.dll", "pe-i386::snmpapi.dll",
        "pe-i386::srvcli.dll", "pe-i386::sxs.dll", "pe-i386::t2embed.dll", "pe-i386::tbs.dll", "pe-i386::tdh.dll", "pe-i386::tdi.sys",
        "pe-i386::txfw32.dll", "pe-i386::ucrtbase.dll", "pe-i386::urlmon.dll", "pe-i386::vcruntime140_app.dll", "pe-i386::videoprt.sys",
        "pe-i386::vss_ps.dll", "pe-i386::wer.dll", "pe-i386::wevtapi.dll", "pe-i386::win32k.sys", "pe-i386::windowscodecs.dll", "pe-i386::wkscli.dll",
        "pe-i386::wlanui.dll", "pe-i386::wlanutil.dll", "pe-i386::wldap32.dll", "pe-i386::wsdapi.dll", "pe-i386::wsnmp32.dll",
        "pe-x86_64::acledit.dll", "pe-x86_64::aclui.dll", "pe-x86_64::activeds.dll", "pe-x86_64::admwprox.dll", "pe-x86_64::adsiisex.dll",
        "pe-x86_64::advapi32.dll", "pe-x86_64::advpack.dll", "pe-x86_64::aksclass.dll", "pe-x86_64::appmgmts.dll", "pe-x86_64::asycfilt.dll",
        "pe-x86_64::atl.dll", "pe-x86_64::atmlib.dll", "pe-x86_64::audiosrv.dll", "pe-x86_64::authz.dll", "pe-x86_64::autodial.dll",
        "pe-x86_64::avicap32.dll", "pe-x86_64::avifil32.dll", "pe-x86_64::avrt.dll", "pe-x86_64::agentanm.dll", "pe-x86_64::autodisc.dll",
        "pe-x86_64::basesrv.dll", "pe-x86_64::bootvid.dll", "pe-x86_64::batmeter.dll", "pe-x86_64::cabview.dll", "pe-x86_64::cards.dll",
        "pe-x86_64::cdm.dll", "pe-x86_64::cfgmgr32.dll", "pe-x86_64::cintime.dll", "pe-x86_64::classpnp.sys", "pe-x86_64::clbcatq.dll",
        "pe-x86_64::cliconfg.dll", "pe-x86_64::clusapi.dll", "pe-x86_64::cnetcfg.dll", "pe-x86_64::coadmin.dll", "pe-x86_64::comctl32.dll",
        "pe-x86_64::compstui.dll", "pe-x86_64::comres.dll", "pe-x86_64::crypt32.dll", "pe-x86_64::cryptdlg.dll", "pe-x86_64::cryptext.dll",
        "pe-x86_64::cryptnet.dll", "pe-x86_64::cryptsp.dll", "pe-x86_64::cryptsvc.dll", "pe-x86_64::cryptui.dll", "pe-x86_64::cryptxml.dll",
        "pe-x86_64::cscapi.dll", "pe-x86_64::cscdll.dll", "pe-x86_64::cscui.dll", "pe-x86_64::csrsrv.dll", "pe-x86_64::cufat.dll",
        "pe-x86_64::cabinet.dll", "pe-x86_64::cdfview.dll", "pe-x86_64::cfgbkend.dll", "pe-x86_64::comsnap.dll", "pe-x86_64::comuid.dll",
        "pe-x86_64::console.dll", "pe-x86_64::d3dcompiler_43.dll", "pe-x86_64::d3dcompiler_46.dll", "pe-x86_64::d3dcompiler_47.dll",
        "pe-x86_64::d3dcompiler.dll", "pe-x86_64::d3dcompiler_37.dll", "pe-x86_64::d3dcompiler_38.dll", "pe-x86_64::d3dcompiler_39.dll",
        "pe-x86_64::d3dcompiler_40.dll", "pe-x86_64::d3dcompiler_41.dll", "pe-x86_64::d3dcompiler_42.dll", "pe-x86_64::dbnetlib.dll",
        "pe-x86_64::dbnmpntw.dll", "pe-x86_64::dciman32.dll", "pe-x86_64::ddraw.dll", "pe-x86_64::devmgr.dll", "pe-x86_64::devobj.dll",
        "pe-x86_64::devrtl.dll", "pe-x86_64::dhcpsapi.dll", "pe-x86_64::digest.dll", "pe-x86_64::dinput.dll", "pe-x86_64::dinput8.dll",
        "pe-x86_64::diskcopy.dll", "pe-x86_64::dmdskmgr.dll", "pe-x86_64::dnsapi.dll", "pe-x86_64::dpapi.dll", "pe-x86_64::dpnhupnp.dll",
        "pe-x86_64::dpnlobby.dll", "pe-x86_64::dpnet.dll", "pe-x86_64::dpvoice.dll", "pe-x86_64::ds32gt.dll", "pe-x86_64::dsauth.dll",
        "pe-x86_64::dskquota.dll", "pe-x86_64::dsound.dll", "pe-x86_64::dsound3d.dll", "pe-x86_64::dssec.dll", "pe-x86_64::dssenh.dll",
        "pe-x86_64::duser.dll", "pe-x86_64::dwrite.dll", "pe-x86_64::efsadu.dll", "pe-x86_64::es.dll", "pe-x86_64::esent.dll",
        "pe-x86_64::esentprf.dll", "pe-x86_64::evr.dll", "pe-x86_64::exprfdll.dll", "pe-x86_64::fcachdll.dll", "pe-x86_64::filemgmt.dll",
        "pe-x86_64::fltlib.dll", "pe-x86_64::fmifs.dll", "pe-x86_64::fontsub.dll", "pe-x86_64::ftpctrs2.dll", "pe-x86_64::ftpmib.dll",
        "pe-x86_64::fxsapi.dll", "pe-x86_64::fxscfgwz.dll", "pe-x86_64::fxsocm.dll", "pe-x86_64::fxsperf.dll", "pe-x86_64::fxsst.dll",
        "pe-x86_64::fxst30.dll", "pe-x86_64::fxstiff.dll", "pe-x86_64::fxswzrd.dll", "pe-x86_64::fastprox.dll", "pe-x86_64::feclient.dll",
        "pe-x86_64::fldrclnr.dll", "pe-x86_64::fxsdrv.dll", "pe-x86_64::fxsroute.dll", "pe-x86_64::gdi32.dll", "pe-x86_64::glmf32.dll",
        "pe-x86_64::glu32.dll", "pe-x86_64::gpedit.dll", "pe-x86_64::gpkcsp.dll", "pe-x86_64::gptext.dll", "pe-x86_64::guitrn.dll",
        "pe-x86_64::genericui.dll", "pe-x86_64::getuname.dll", "pe-x86_64::hal.dll", "pe-x86_64::hbaapi.dll", "pe-x86_64::hid.dll",
        "pe-x86_64::hidclass.sys", "pe-x86_64::hidparse.sys", "pe-x86_64::hmmapi.dll", "pe-x86_64::hnetwiz.dll", "pe-x86_64::hnetcfg.dll",
        "pe-x86_64::htrn_jis.dll", "pe-x86_64::httpapi.dll", "pe-x86_64::httpext.dll", "pe-x86_64::httpmib.dll", "pe-x86_64::httpodbc.dll",
        "pe-x86_64::hypertrm.dll", "pe-x86_64::icaapi.dll", "pe-x86_64::icfgnt.dll", "pe-x86_64::icm32.dll", "pe-x86_64::icmui.dll",
        "pe-x86_64::icwconn.dll", "pe-x86_64::icwutil.dll", "pe-x86_64::idq.dll", "pe-x86_64::ieakeng.dll", "pe-x86_64::iernonce.dll",
        "pe-x86_64::igmpagnt.dll", "pe-x86_64::iis.dll", "pe-x86_64::iisadmin.dll", "pe-x86_64::iiscfg.dll", "pe-x86_64::iisprov.dll",
        "pe-x86_64::iisutil.dll", "pe-x86_64::imeskdic.dll", "pe-x86_64::imm32.dll", "pe-x86_64::imsinsnt.dll", "pe-x86_64::inetcfg.dll",
        "pe-x86_64::inetcomm.dll", "pe-x86_64::infoadmn.dll", "pe-x86_64::infocomm.dll", "pe-x86_64::infoctrs.dll", "pe-x86_64::initpki.dll",
        "pe-x86_64::iphlpapi.dll", "pe-x86_64::ipmontr.dll", "pe-x86_64::ipnathlp.dll", "pe-x86_64::iprop.dll", "pe-x86_64::ipsecspd.dll",
        "pe-x86_64::irclass.dll", "pe-x86_64::isatq.dll", "pe-x86_64::iscomlog.dll", "pe-x86_64::iscsidsc.dll", "pe-x86_64::iyuv_32.dll",
        "pe-x86_64::iisrtl.dll", "pe-x86_64::imgutil.dll", "pe-x86_64::input.dll", "pe-x86_64::jet500.dll", "pe-x86_64::jsproxy.dll",
        "pe-x86_64::kd1394.dll", "pe-x86_64::kernel32.dll", "pe-x86_64::keymgr.dll", "pe-x86_64::kerberos.dll", "pe-x86_64::linkinfo.dll",
        "pe-x86_64::log.dll", "pe-x86_64::lonsint.dll", "pe-x86_64::lpk.dll", "pe-x86_64::lprhelp.dll", "pe-x86_64::lsasrv.dll",
        "pe-x86_64::lz32.dll", "pe-x86_64::localspl.dll", "pe-x86_64::loghours.dll", "pe-x86_64::mapi32.dll", "pe-x86_64::mcastmib.dll",
        "pe-x86_64::mcd32.dll", "pe-x86_64::mcdsrv32.dll", "pe-x86_64::mciavi32.dll", "pe-x86_64::mcicda.dll", "pe-x86_64::mciole32.dll",
        "pe-x86_64::mciqtz32.dll", "pe-x86_64::mciseq.dll", "pe-x86_64::mciwave.dll", "pe-x86_64::mdminst.dll", "pe-x86_64::mf.dll",
        "pe-x86_64::mfc42.dll", "pe-x86_64::mfc42u.dll", "pe-x86_64::mfplat.dll", "pe-x86_64::midimap.dll", "pe-x86_64::migism.dll",
        "pe-x86_64::miglibnt.dll", "pe-x86_64::mlang.dll", "pe-x86_64::mll_hp.dll", "pe-x86_64::mll_mtf.dll", "pe-x86_64::mll_qic.dll",
        "pe-x86_64::mmfutil.dll", "pe-x86_64::mpr.dll", "pe-x86_64::mprapi.dll", "pe-x86_64::mprddm.dll", "pe-x86_64::mprui.dll",
        "pe-x86_64::mqcertui.dll", "pe-x86_64::mqperf.dll", "pe-x86_64::mqrtdep.dll", "pe-x86_64::mqupgrd.dll", "pe-x86_64::msacm32.dll",
        "pe-x86_64::msadcs.dll", "pe-x86_64::msado15.dll", "pe-x86_64::msafd.dll", "pe-x86_64::msasn1.dll", "pe-x86_64::mscat32.dll",
        "pe-x86_64::msctf.dll", "pe-x86_64::msdart.dll", "pe-x86_64::msdtclog.dll", "pe-x86_64::msdtcprx.dll", "pe-x86_64::msdtctm.dll",
        "pe-x86_64::msdtcuiu.dll", "pe-x86_64::msftedit.dll", "pe-x86_64::msgina.dll", "pe-x86_64::mshtml.dll", "pe-x86_64::msimg32.dll",
        "pe-x86_64::mslbui.dll", "pe-x86_64::msmqocm.dll", "pe-x86_64::msobdl.dll", "pe-x86_64::msoe.dll", "pe-x86_64::msoeacct.dll",
        "pe-x86_64::msoert2.dll", "pe-x86_64::msports.dll", "pe-x86_64::msrating.dll", "pe-x86_64::msrle32.dll", "pe-x86_64::mssign32.dll",
        "pe-x86_64::mssip32.dll", "pe-x86_64::msutb.dll", "pe-x86_64::msvcr110.dll", "pe-x86_64::msvcr120.dll", "pe-x86_64::msvcr120d.dll",
        "pe-x86_64::msvcr90d.dll", "pe-x86_64::msvfw32.dll", "pe-x86_64::msvidc32.dll", "pe-x86_64::msw3prt.dll", "pe-x86_64::mswsock.dll",
        "pe-x86_64::msyuv.dll", "pe-x86_64::mtxclu.dll", "pe-x86_64::mtxdm.dll", "pe-x86_64::mtxoci.dll", "pe-x86_64::mag_hook.dll",
        "pe-x86_64::ncobjapi.dll", "pe-x86_64::ncxp.dll", "pe-x86_64::nddenb32.dll", "pe-x86_64::ndfapi.dll", "pe-x86_64::ndis.sys",
        "pe-x86_64::ndisnpp.dll", "pe-x86_64::nddeapi.dll", "pe-x86_64::netapi32.dll", "pe-x86_64::netid.dll", "pe-x86_64::netlogon.dll",
        "pe-x86_64::netplwiz.dll", "pe-x86_64::netrap.dll", "pe-x86_64::netui0.dll", "pe-x86_64::netui1.dll", "pe-x86_64::netui2.dll",
        "pe-x86_64::nntpapi.dll", "pe-x86_64::npptools.dll", "pe-x86_64::nshipsec.dll", "pe-x86_64::ntdsapi.dll", "pe-x86_64::ntlanman.dll",
        "pe-x86_64::ntlanui.dll", "pe-x86_64::ntmarta.dll", "pe-x86_64::ntmsapi.dll", "pe-x86_64::ntoc.dll", "pe-x86_64::ntprint.dll",
        "pe-x86_64::nwprovau.dll", "pe-x86_64::normaliz.dll", "pe-x86_64::occache.dll", "pe-x86_64::ocmanage.dll", "pe-x86_64::ocmsn.dll",
        "pe-x86_64::ocsbs.dll", "pe-x86_64::odbc32.dll", "pe-x86_64::odbc32gt.dll", "pe-x86_64::odbccp32.dll", "pe-x86_64::odbccr32.dll",
        "pe-x86_64::odbctrac.dll", "pe-x86_64::oeimport.dll", "pe-x86_64::oemiglib.dll", "pe-x86_64::oleacc.dll", "pe-x86_64::oleaut32.dll",
        "pe-x86_64::olecli32.dll", "pe-x86_64::olecnv32.dll", "pe-x86_64::oledb32.dll", "pe-x86_64::olesvr32.dll", "pe-x86_64::opengl32.dll",
        "pe-x86_64::osuninst.dll", "pe-x86_64::p2p.dll", "pe-x86_64::p2pcollab.dll", "pe-x86_64::p2pgraph.dll", "pe-x86_64::pautoenr.dll",
        "pe-x86_64::photowiz.dll", "pe-x86_64::pidgen.dll", "pe-x86_64::policman.dll", "pe-x86_64::polstore.dll", "pe-x86_64::powrprof.dll",
        "pe-x86_64::printui.dll", "pe-x86_64::profmap.dll", "pe-x86_64::proppage.dll", "pe-x86_64::psapi.dll", "pe-x86_64::psbase.dll",
        "pe-x86_64::pstorec.dll", "pe-x86_64::pstorsvc.dll", "pe-x86_64::perfdisk.dll", "pe-x86_64::perfnet.dll", "pe-x86_64::perfos.dll",
        "pe-x86_64::perfproc.dll", "pe-x86_64::perfts.dll", "pe-x86_64::pschdprf.dll", "pe-x86_64::quartz.dll", "pe-x86_64::qutil.dll",
        "pe-x86_64::rasapi32.dll", "pe-x86_64::raschap.dll", "pe-x86_64::rasdlg.dll", "pe-x86_64::rasmontr.dll", "pe-x86_64::rasrad.dll",
        "pe-x86_64::rassapi.dll", "pe-x86_64::rdpcfgex.dll", "pe-x86_64::rdpsnd.dll", "pe-x86_64::rdpwsx.dll", "pe-x86_64::regapi.dll",
        "pe-x86_64::resutils.dll", "pe-x86_64::riched20.dll", "pe-x86_64::rnr20.dll", "pe-x86_64::routemsg.dll", "pe-x86_64::routetab.dll",
        "pe-x86_64::rpcns4.dll", "pe-x86_64::rpcref.dll", "pe-x86_64::rpcrt4.dll", "pe-x86_64::rpcss.dll", "pe-x86_64::rsaenh.dll",
        "pe-x86_64::rpcdiag.dll", "pe-x86_64::rstrtmgr.dll", "pe-x86_64::samlib.dll", "pe-x86_64::samsrv.dll", "pe-x86_64::scarddlg.dll",
        "pe-x86_64::sccbase.dll", "pe-x86_64::scecli.dll", "pe-x86_64::scesrv.dll", "pe-x86_64::schannel.dll", "pe-x86_64::sclgntfy.dll",
        "pe-x86_64::script.dll", "pe-x86_64::scrobj.dll", "pe-x86_64::seo.dll", "pe-x86_64::serialui.dll", "pe-x86_64::serwvdrv.dll",
        "pe-x86_64::setupapi.dll", "pe-x86_64::sfmapi.dll", "pe-x86_64::shdocvw.dll", "pe-x86_64::shell32.dll", "pe-x86_64::shfolder.dll",
        "pe-x86_64::shlwapi.dll", "pe-x86_64::shscrap.dll", "pe-x86_64::shsvcs.dll", "pe-x86_64::sigtab.dll", "pe-x86_64::skdll.dll",
        "pe-x86_64::slbcsp.dll", "pe-x86_64::slwga.dll", "pe-x86_64::smtpapi.dll", "pe-x86_64::smtpctrs.dll", "pe-x86_64::snapin.dll",
        "pe-x86_64::snmpmib.exe", "pe-x86_64::softpub.dll", "pe-x86_64::spoolss.dll", "pe-x86_64::sqlsrv32.dll", "pe-x86_64::sqlxmlx.dll",
        "pe-x86_64::srclient.dll", "pe-x86_64::srrstr.dll", "pe-x86_64::ssdpapi.dll", "pe-x86_64::ssinc.dll", "pe-x86_64::staxmem.dll",
        "pe-x86_64::sti.dll", "pe-x86_64::subauth.dll", "pe-x86_64::svcpack.dll", "pe-x86_64::synceng.dll", "pe-x86_64::syncui.dll",
        "pe-x86_64::sysmod.dll", "pe-x86_64::syssetup.dll", "pe-x86_64::scrrun.dll", "pe-x86_64::secur32.dll", "pe-x86_64::security.dll",
        "pe-x86_64::sens.dll", "pe-x86_64::sensapi.dll", "pe-x86_64::senscfg.dll", "pe-x86_64::shimeng.dll", "pe-x86_64::sspicli.dll",
        "pe-x86_64::sysinv.dll", "pe-x86_64::tapi32.dll", "pe-x86_64::tapiperf.dll", "pe-x86_64::tcpmib.dll", "pe-x86_64::traffic.dll",
        "pe-x86_64::tsappcmp.dll", "pe-x86_64::tsbyuv.dll", "pe-x86_64::ufat.dll", "pe-x86_64::umandlg.dll", "pe-x86_64::uniime.dll",
        "pe-x86_64::unimdmat.dll", "pe-x86_64::untfs.dll", "pe-x86_64::upnp.dll", "pe-x86_64::url.dll", "pe-x86_64::urlauth.dll",
        "pe-x86_64::usbcamd2.sys", "pe-x86_64::usbd.sys", "pe-x86_64::usbport.sys", "pe-x86_64::user32.dll", "pe-x86_64::userenv.dll",
        "pe-x86_64::usp10.dll", "pe-x86_64::utildll.dll", "pe-x86_64::uxtheme.dll", "pe-x86_64::verifier.dll", "pe-x86_64::version.dll",
        "pe-x86_64::vgx.dll", "pe-x86_64::vmx_mode.dll", "pe-x86_64::vssapi.dll", "pe-x86_64::virtdisk.dll", "pe-x86_64::w32topl.dll",
        "pe-x86_64::w3ctrs.dll", "pe-x86_64::w3tp.dll", "pe-x86_64::wab32.dll", "pe-x86_64::wabimp.dll", "pe-x86_64::wbemcore.dll",
        "pe-x86_64::wdmaud.drv", "pe-x86_64::wdsclientapi.dll", "pe-x86_64::wdscore.dll", "pe-x86_64::wdscsl.dll", "pe-x86_64::wdstptc.dll",
        "pe-x86_64::wdsutil.dll", "pe-x86_64::webhits.dll", "pe-x86_64::wevtfwd.dll", "pe-x86_64::wiadss.dll", "pe-x86_64::winfax.dll",
        "pe-x86_64::winhttp.dll", "pe-x86_64::wininet.dll", "pe-x86_64::winipsec.dll", "pe-x86_64::winmm.dll", "pe-x86_64::winrnr.dll",
        "pe-x86_64::winspool.drv", "pe-x86_64::winsta.dll", "pe-x86_64::wintrust.dll", "pe-x86_64::winusb.dll", "pe-x86_64::wldap32.dll",
        "pe-x86_64::wlstore.dll", "pe-x86_64::wmi.dll", "pe-x86_64::wmilib.sys", "pe-x86_64::wmisvc.dll", "pe-x86_64::ws2help.dll",
        "pe-x86_64::ws2_32.dll", "pe-x86_64::wscapi.dll", "pe-x86_64::wscsvc.dll", "pe-x86_64::wsock32.dll", "pe-x86_64::wtsapi32.dll",
        "pe-x86_64::wbemupgd.dll", "pe-x86_64::wdsclient.dll", "pe-x86_64::wdsimage.dll", "pe-x86_64::wdsupgcompl.dll", "pe-x86_64::webcheck.dll",
        "pe-x86_64::wecapi.dll", "pe-x86_64::winscard.dll", "pe-x86_64::windowscodecs.dll", "pe-x86_64::wlnotify.dll", "pe-x86_64::wmiaprpl.dll",
        "pe-x86_64::wmiprop.dll", "pe-x86_64::x3daudio1_2.dll", "pe-x86_64::x3daudio1_3.dll", "pe-x86_64::x3daudio1_4.dll",
        "pe-x86_64::x3daudio1_5.dll", "pe-x86_64::x3daudio1_6.dll", "pe-x86_64::x3daudio1_7.dll", "pe-x86_64::x3daudiod1_7.dll",
        "pe-x86_64::xapofx1_0.dll", "pe-x86_64::xapofx1_1.dll", "pe-x86_64::xapofx1_2.dll", "pe-x86_64::xapofx1_3.dll", "pe-x86_64::xapofx1_4.dll",
        "pe-x86_64::xapofx1_5.dll", "pe-x86_64::xapofxd1_5.dll", "pe-x86_64::xaudio2_8.dll", "pe-x86_64::xinput1_1.dll", "pe-x86_64::xinput1_2.dll",
        "pe-x86_64::xinput1_3.dll", "pe-x86_64::xinput1_4.dll", "pe-x86_64::xinput9_1_0.dll", "pe-x86_64::zoneoc.dll", "pe-x86_64::admparse.dll",
        "pe-x86_64::adptif.dll", "pe-x86_64::adsldpc.dll", "pe-x86_64::alrsvc.dll", "pe-x86_64::apcups.dll",
        "pe-x86_64::api-ms-win-core-winrt-l1-1-0.dll", "pe-x86_64::apphelp.dll", "pe-x86_64::aqueue.dll", "pe-x86_64::asp.dll",
        "pe-x86_64::aspperf.dll", "pe-x86_64::atkctrs.dll", "pe-x86_64::atrace.dll", "pe-x86_64::azroles.dll", "pe-x86_64::batt.dll",
        "pe-x86_64::bcrypt.dll", "pe-x86_64::browser.dll", "pe-x86_64::bthprops.cpl", "pe-x86_64::catsrv.dll", "pe-x86_64::catsrvut.dll",
        "pe-x86_64::certcli.dll", "pe-x86_64::cimwin32.dll", "pe-x86_64::clb.dll", "pe-x86_64::clfsw32.dll", "pe-x86_64::cmcfg32.dll",
        "pe-x86_64::cmdial32.dll", "pe-x86_64::cmpbk32.dll", "pe-x86_64::cmutil.dll", "pe-x86_64::colbact.dll", "pe-x86_64::comdlg32.dll",
        "pe-x86_64::comsetup.dll", "pe-x86_64::comsvcs.dll", "pe-x86_64::connect.dll", "pe-x86_64::corpol.dll", "pe-x86_64::credui.dll",
        "pe-x86_64::crtdll.dll", "pe-x86_64::cryptdll.dll", "pe-x86_64::d2d1.dll", "pe-x86_64::d3d11.dll", "pe-x86_64::d3d8thk.dll",
        "pe-x86_64::d3d9.dll", "pe-x86_64::d3dcsx_46.dll", "pe-x86_64::d3dcsxd_43.dll", "pe-x86_64::d3dx10_33.dll", "pe-x86_64::d3dx10_34.dll",
        "pe-x86_64::d3dx10_35.dll", "pe-x86_64::d3dx10_36.dll", "pe-x86_64::d3dx10_37.dll", "pe-x86_64::d3dx10_38.dll", "pe-x86_64::d3dx10_39.dll",
        "pe-x86_64::d3dx10_40.dll", "pe-x86_64::d3dx10_41.dll", "pe-x86_64::d3dx10_42.dll", "pe-x86_64::d3dx10_43.dll", "pe-x86_64::d3dx11_42.dll",
        "pe-x86_64::d3dx11_43.dll", "pe-x86_64::d3dx9_24.dll", "pe-x86_64::d3dx9_25.dll", "pe-x86_64::d3dx9_26.dll", "pe-x86_64::d3dx9_27.dll",
        "pe-x86_64::d3dx9_28.dll", "pe-x86_64::d3dx9_29.dll", "pe-x86_64::d3dx9_30.dll", "pe-x86_64::d3dx9_31.dll", "pe-x86_64::d3dx9_32.dll",
        "pe-x86_64::d3dx9_33.dll", "pe-x86_64::d3dx9_34.dll", "pe-x86_64::d3dx9_35.dll", "pe-x86_64::d3dx9_36.dll", "pe-x86_64::d3dx9_37.dll",
        "pe-x86_64::d3dx9_38.dll", "pe-x86_64::d3dx9_39.dll", "pe-x86_64::d3dx9_40.dll", "pe-x86_64::d3dx9_41.dll", "pe-x86_64::d3dx9_42.dll",
        "pe-x86_64::d3dx9_43.dll", "pe-x86_64::d3dxof.dll", "pe-x86_64::davclnt.dll", "pe-x86_64::dbgeng.dll", "pe-x86_64::dbghelp.dll",
        "pe-x86_64::dhcpcsvc.dll", "pe-x86_64::dhcpcsvc6.dll", "pe-x86_64::dimsntfy.dll", "pe-x86_64::dimsroam.dll", "pe-x86_64::dmconfig.dll",
        "pe-x86_64::dmivcitf.dll", "pe-x86_64::dmutil.dll", "pe-x86_64::dnsrslvr.dll", "pe-x86_64::dpnaddr.dll", "pe-x86_64::drprov.dll",
        "pe-x86_64::dsprop.dll", "pe-x86_64::dsquery.dll", "pe-x86_64::dsuiext.dll", "pe-x86_64::dwmapi.dll", "pe-x86_64::dxgi.dll",
        "pe-x86_64::dxva2.dll", "pe-x86_64::eappcfg.dll", "pe-x86_64::eapphost.dll", "pe-x86_64::eappprxy.dll", "pe-x86_64::eventlog.dll",
        "pe-x86_64::exports.dll", "pe-x86_64::exstrace.dll", "pe-x86_64::faultrep.dll", "pe-x86_64::fdeploy.dll", "pe-x86_64::framedyn.dll",
        "pe-x86_64::fwpuclnt.dll", "pe-x86_64::fxsui.dll", "pe-x86_64::gdiplus.dll", "pe-x86_64::hgfs.dll", "pe-x86_64::hlink.dll",
        "pe-x86_64::hostmib.dll", "pe-x86_64::hotplug.dll", "pe-x86_64::htui.dll", "pe-x86_64::iashlpr.dll", "pe-x86_64::iaspolcy.dll",
        "pe-x86_64::iassam.dll", "pe-x86_64::iassvcs.dll", "pe-x86_64::icmp.dll", "pe-x86_64::icwdl.dll", "pe-x86_64::icwphbk.dll",
        "pe-x86_64::iedkcs32.dll", "pe-x86_64::iesetup.dll", "pe-x86_64::iisui.dll", "pe-x86_64::imagehlp.dll", "pe-x86_64::imedic.dll",
        "pe-x86_64::imejpcus.dll", "pe-x86_64::imeshare.dll", "pe-x86_64::imeskdic.dll", "pe-x86_64::imjp81k.dll", "pe-x86_64::imjputyc.dll",
        "pe-x86_64::inetmib1.dll", "pe-x86_64::infosoft.dll", "pe-x86_64::inseng.dll", "pe-x86_64::iprtprio.dll", "pe-x86_64::iprtrmgr.dll",
        "pe-x86_64::ipxsap.dll", "pe-x86_64::isapitst.dll", "pe-x86_64::isignup2.dll", "pe-x86_64::kdcom.dll", "pe-x86_64::ks.sys",
        "pe-x86_64::ksuser.dll", "pe-x86_64::ktmw32.dll", "pe-x86_64::lmmib2.dll", "pe-x86_64::loadperf.dll", "pe-x86_64::mchgrcoi.dll",
        "pe-x86_64::mf3216.dll", "pe-x86_64::mgmtapi.dll", "pe-x86_64::mmutilse.dll", "pe-x86_64::mobsync.dll", "pe-x86_64::modemui.dll",
        "pe-x86_64::mofd.dll", "pe-x86_64::mqad.dll", "pe-x86_64::mqdscli.dll", "pe-x86_64::mqise.dll", "pe-x86_64::mqrt.dll", "pe-x86_64::mqsec.dll",
        "pe-x86_64::mqutil.dll", "pe-x86_64::mscms.dll", "pe-x86_64::msdadiag.dll", "pe-x86_64::msdmo.dll", "pe-x86_64::msdrm.dll",
        "pe-x86_64::msgr3en.dll", "pe-x86_64::msgsvc.dll", "pe-x86_64::msi.dll", "pe-x86_64::msimtf.dll", "pe-x86_64::msir3jp.dll",
        "pe-x86_64::msisip.dll", "pe-x86_64::msls31.dll", "pe-x86_64::msobmain.dll", "pe-x86_64::mspatcha.dll", "pe-x86_64::mstask.dll",
        "pe-x86_64::mstlsapi.dll", "pe-x86_64::msv1_0.dll", "pe-x86_64::msvcirt.dll", "pe-x86_64::msvcp120_app.dll", "pe-x86_64::msvcp60.dll",
        "pe-x86_64::msvcr100.dll", "pe-x86_64::msvcr120_app.dll", "pe-x86_64::msvcr80.dll", "pe-x86_64::msvcr90.dll", "pe-x86_64::msvcrt.dll",
        "pe-x86_64::mtxex.dll", "pe-x86_64::mydocs.dll", "pe-x86_64::ncrypt.dll", "pe-x86_64::netcfgx.dll", "pe-x86_64::netman.dll",
        "pe-x86_64::netoc.dll", "pe-x86_64::netshell.dll", "pe-x86_64::newdev.dll", "pe-x86_64::ntdll.dll", "pe-x86_64::ntdsbcli.dll",
        "pe-x86_64::ntdtcsetup.dll", "pe-x86_64::ntlsapi.dll", "pe-x86_64::ntoskrnl.exe", "pe-x86_64::ntshrui.dll", "pe-x86_64::ntvdm64.dll",
        "pe-x86_64::oakley.dll", "pe-x86_64::odbcbcp.dll", "pe-x86_64::odbcconf.dll", "pe-x86_64::ole32.dll", "pe-x86_64::oledlg.dll",
        "pe-x86_64::pcwum.dll", "pe-x86_64::pdh.dll", "pe-x86_64::perfctrs.dll", "pe-x86_64::ps5ui.dll", "pe-x86_64::pscript5.dll",
        "pe-x86_64::qmgr.dll", "pe-x86_64::qosname.dll", "pe-x86_64::query.dll", "pe-x86_64::qwave.dll", "pe-x86_64::rasadhlp.dll",
        "pe-x86_64::rasauto.dll", "pe-x86_64::rasctrs.dll", "pe-x86_64::rasman.dll", "pe-x86_64::rasmans.dll", "pe-x86_64::rasmxs.dll",
        "pe-x86_64::rasppp.dll", "pe-x86_64::rasser.dll", "pe-x86_64::rastapi.dll", "pe-x86_64::rastls.dll", "pe-x86_64::regsvc.dll",
        "pe-x86_64::rpchttp.dll", "pe-x86_64::rtm.dll", "pe-x86_64::rtutils.dll", "pe-x86_64::schedsvc.dll", "pe-x86_64::scredir.dll",
        "pe-x86_64::sdhcinst.dll", "pe-x86_64::seclogon.dll", "pe-x86_64::setupqry.dll", "pe-x86_64::sfc.dll", "pe-x86_64::sfc_os.dll",
        "pe-x86_64::sfcfiles.dll", "pe-x86_64::shimgvw.dll", "pe-x86_64::sisbkup.dll", "pe-x86_64::slc.dll", "pe-x86_64::slcext.dll",
        "pe-x86_64::snmpapi.dll", "pe-x86_64::snmpelea.dll", "pe-x86_64::srchctls.dll", "pe-x86_64::srvsvc.dll", "pe-x86_64::sti_ci.dll",
        "pe-x86_64::streamci.dll", "pe-x86_64::strmfilt.dll", "pe-x86_64::sxs.dll", "pe-x86_64::t2embed.dll", "pe-x86_64::tbs.dll",
        "pe-x86_64::tdh.dll", "pe-x86_64::tsd32.dll", "pe-x86_64::tsoc.dll", "pe-x86_64::txfw32.dll", "pe-x86_64::ucrtbase.dll",
        "pe-x86_64::umdmxfrm.dll", "pe-x86_64::umpnpmgr.dll", "pe-x86_64::unidrv.dll", "pe-x86_64::unidrvui.dll", "pe-x86_64::uniplat.dll",
        "pe-x86_64::upnpui.dll", "pe-x86_64::urlmon.dll", "pe-x86_64::vcruntime140_app.dll", "pe-x86_64::vdsutil.dll", "pe-x86_64::w32time.dll",
        "pe-x86_64::w3core.dll", "pe-x86_64::w3dt.dll", "pe-x86_64::w3isapi.dll", "pe-x86_64::w3ssl.dll", "pe-x86_64::wamreg.dll",
        "pe-x86_64::wdigest.dll", "pe-x86_64::webclnt.dll", "pe-x86_64::wer.dll", "pe-x86_64::wevtapi.dll", "pe-x86_64::wiarpc.dll",
        "pe-x86_64::wiaservc.dll", "pe-x86_64::wiashext.dll", "pe-x86_64::winsrv.dll", "pe-x86_64::wkssvc.dll", "pe-x86_64::wlanapi.dll",
        "pe-x86_64::wlanui.dll", "pe-x86_64::wlanutil.dll", "pe-x86_64::wmi2xml.dll", "pe-x86_64::wow64.dll", "pe-x86_64::wow64cpu.dll",
        "pe-x86_64::wow64mib.dll", "pe-x86_64::wow64win.dll", "pe-x86_64::wpd_ci.dll", "pe-x86_64::wsdapi.dll", "pe-x86_64::wshatm.dll",
        "pe-x86_64::wshbth.dll" };
    CPPUNIT_ASSERT_EQUAL(expectedDLLs, package->libprovides);
}
