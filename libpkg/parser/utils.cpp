#include "./utils.h"

#include "./data/package.h"

#include <c++utilities/conversion/stringbuilder.h>
#include <c++utilities/io/misc.h>

#include <archive.h>
#include <archive_entry.h>

#include <sys/time.h>

#include <chrono>
#include <exception>
#include <filesystem>
#include <regex>

using namespace std;
using namespace CppUtilities;

namespace LibPkg {

/*!
 * \brief Returns the first non-alphanumeric character \a str.
 * \remarks The \a end is returned if \a str only contains alphanumeric characters.
 * \todo Care about ä, ö, ü, ß in version numbers?
 */
const char *firstNonAlphanumericCharacter(const char *str, const char *end)
{
    for (; str != end; ++str) {
        const char c = *str;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) {
            return str;
        }
    }
    return str;
}

/*!
 * \brief Determines when the file with the specified \a path has been modified the last time.
 * \fixme Make no assumptions on the internal resolution so the code is portable (seems not possible with C++17).
 */
CppUtilities::DateTime lastModified(const string &path)
{
    try {
        return CppUtilities::DateTime::fromChronoTimePointGmt(
            chrono::time_point<chrono::system_clock, chrono::nanoseconds>{ filesystem::last_write_time(path).time_since_epoch() }
            + chrono::seconds{ 6437664000 });
    } catch (const runtime_error &) {
        return CppUtilities::DateTime();
    }
}

/*!
 * \brief Sets when the file with the specified \a path has been modified the last time.
 * \fixme Use the std::filesystem library once the time point can be constructed in a portable way.
 */
bool setLastModified(const string &path, DateTime lastModified)
{
    timeval tv[2];
    tv[0].tv_usec = UTIME_OMIT;
    tv[1].tv_sec = lastModified.toTimeStamp();
    tv[1].tv_usec = lastModified.nanosecond();
    return utimes(path.data(), tv) == 0;
}

/*!
 * \brief Override an overridden variable assignment (to ensure the configured default value is actually used and not overridden).
 */
static void overrideOverriddenVariableAssignment(string &pkgbuildContents, string_view variableName, const string *configuredDefaultValue)
{
    if (!configuredDefaultValue || pkgbuildContents.find(variableName) == string::npos) {
        return;
    }
    pkgbuildContents.append(argsToString(
        pkgbuildContents.empty() || pkgbuildContents.back() == '\n' ? "" : "\n", variableName, '=', '\'', *configuredDefaultValue, '\'', '\n'));
}

/*!
 * \brief Amends the PKGBUILD with the specified \a path.
 * \throws Throws std::ios_base::failure when an IO error occurs.
 */
AmendedVersions amendPkgbuild(const string &path, const PackageVersion &existingVersion, const PackageAmendment &amendment)
{
    auto amendedVersions = AmendedVersions();
    if (amendment.isEmpty()) {
        return amendedVersions;
    }

    auto pkgbuildContents = readFile(path, 0x10000);

    // set upstream version
    if (amendment.setUpstreamVersion) {
        static const auto pkgverRegex = regex{ "\npkgver=[^\n]*", regex::extended };
        pkgbuildContents = regex_replace(pkgbuildContents, pkgverRegex, "\npkgver=" + existingVersion.upstream);
        amendedVersions.newUpstreamVersion = existingVersion.upstream;
    }

    // bump downstream version
    switch (amendment.bumpDownstreamVersion) {
    case PackageAmendment::VersionBump::None:
        break;
    case PackageAmendment::VersionBump::Epoch:
        amendedVersions.newEpoch = numberToString(stringToNumber<unsigned int>(existingVersion.epoch) + 1);
        amendedVersions.newPkgRel = "1";
        break;
    case PackageAmendment::VersionBump::PackageVersion:
        amendedVersions.newPkgRel = numberToString(stringToNumber<double>(existingVersion.package) + 0.1);
        break;
    }
    if (!amendedVersions.newEpoch.empty()) {
        if (pkgbuildContents.find("\nepoch=") != string::npos) {
            static const auto epochRegex = regex{ "\nepoch=[^\n]*", regex::extended };
            pkgbuildContents = regex_replace(pkgbuildContents, epochRegex, "\nepoch=" + amendedVersions.newEpoch);
        } else {
            static const auto epochRegex = regex{ "\npkgver=", regex::extended };
            pkgbuildContents = regex_replace(pkgbuildContents, epochRegex, "\nepoch=" % amendedVersions.newEpoch + "\npkgver=");
        }
    }
    if (!amendedVersions.newPkgRel.empty()) {
        static const auto pkgrelRegex = regex{ "\npkgrel=[^\n]*", regex::extended };
        pkgbuildContents = regex_replace(pkgbuildContents, pkgrelRegex, "\npkgrel=" + amendedVersions.newPkgRel);
    }

    // override overrides for SRCEXT/PKGEXT to ensure defaults from makepkg.conf are used
    overrideOverriddenVariableAssignment(pkgbuildContents, "SRCEXT", amendment.ensureSourceExtension);
    overrideOverriddenVariableAssignment(pkgbuildContents, "PKGEXT", amendment.ensurePackageExtension);

    writeFile(path, pkgbuildContents);
    return amendedVersions;
}

} // namespace LibPkg
