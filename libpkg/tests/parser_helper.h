#ifndef LIBPKG_TESTS_PARSER_HELPER
#define LIBPKG_TESTS_PARSER_HELPER

namespace LibPkg {
struct Package;
}

namespace TestHelper {
void checkHarfbuzzPackage(const LibPkg::Package &pkg);
void checkHarfbuzzPackagePeDependencies(const LibPkg::Package &pkg);
void checkAutoconfPackage(const LibPkg::Package &pkg);
void checkCmakePackageSoDependencies(const LibPkg::Package &pkg);
void checkSyncthingTrayPackageSoDependencies(const LibPkg::Package &pkg);
} // namespace TestHelper

#endif // LIBPKG_TESTS_PARSER_HELPER
