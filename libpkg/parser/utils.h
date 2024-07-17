#ifndef LIBPKG_PARSER_UTILS_H
#define LIBPKG_PARSER_UTILS_H

#include "../global.h"

#include <c++utilities/chrono/datetime.h>

#include <string>

namespace LibPkg {

/*
 * PKGBUILD amendment
 */

struct PackageVersion;

struct LIBPKG_EXPORT PackageAmendment {
    enum class VersionBump {
        None, /*!< Don't increment the version. */
        PackageVersion, /*!< Increments the package version of the specified existing version by 0.1 and sets that as pkgrel within the PKGBUILD. */
        Epoch, /*!< Increments the epoch of the specified existing version by 1 and sets that as epoch within the PKGBUILD. */
    };
    /// \brief Ensures that "PKGEXT" is *not* overridden. If it appears to be overridden, the specified "PKGEXT" is enforced by appending an
    ///        explicit assignment.
    const std::string *ensurePackageExtension = nullptr;
    /// \brief Ensures that "SRCEXT" is *not* overridden. If it appears to be overridden, the specified "SRCEXT" is enforced by appending an
    ///        explicit assignment.
    const std::string *ensureSourceExtension = nullptr;
    /// \brief Increments epoch/pkgrel with respect to the specified existing version.
    VersionBump bumpDownstreamVersion = VersionBump::None;
    /// \brief Sets pkgver to the existing version.
    bool setUpstreamVersion = false;

    bool isEmpty() const;
};

struct LIBPKG_EXPORT AmendedVersions {
    std::string newUpstreamVersion;
    std::string newPkgRel;
    std::string newEpoch;
};

inline bool PackageAmendment::isEmpty() const
{
    return bumpDownstreamVersion == PackageAmendment::VersionBump::None && !setUpstreamVersion;
}

LIBPKG_EXPORT AmendedVersions amendPkgbuild(const std::string &path, const PackageVersion &existingVersion, const PackageAmendment &amendment);

/*
 * Misc helper
 */

LIBPKG_EXPORT const char *firstNonAlphanumericCharacter(const char *str, const char *end);
LIBPKG_EXPORT CppUtilities::DateTime lastModified(const std::string &path);
LIBPKG_EXPORT bool setLastModified(const std::string &path, CppUtilities::DateTime lastModified);

} // namespace LibPkg

#endif // LIBPKG_PARSER_UTILS_H
