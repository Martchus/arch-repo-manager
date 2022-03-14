#include "../data/config.h"

#include <c++utilities/conversion/stringbuilder.h>

#include <fstream>
#include <sstream>

#include <iostream>

using namespace std;
using namespace CppUtilities;

namespace LibPkg {

bool Config::addLicenseInfo(LicenseResult &result, const Dependency &dependency)
{
    // find the referenced package
    auto searchResult = findPackage(dependency);
    const auto &package = searchResult.pkg;
    if (!package) {
        result.success = false;
        result.notes.emplace_back("Unable to locate " + dependency.toString());
        return false;
    }

    auto packageID = addLicenseInfo(result, searchResult, package);
    if (packageID.empty()) {
        result.ignoredPackages.emplace_back(package->name % '-' + package->version);
        return false;
    }

    result.consideredPackages.emplace_back(package->name % '-' + package->version);
    if (result.mainProject.empty()) {
        result.mainProject = move(packageID);
    } else {
        result.dependendProjects.emplace(move(packageID));
    }
    return true;
}

std::string Config::addLicenseInfo(LicenseResult &result, PackageSearchResult &searchResult, const std::shared_ptr<Package> &package)
{
    // make up some identifier to refer to the package in the license summary
    //  * use the "regular" package name (e.g. gcc instead of mingw-w64-gcc)
    //  * include only the upstream version (but not epoch and pkgrel)
    //  * the licensing files for the whole MinGW-w64 project is contained by the mingw-w64-headers package
    //  * the licensing files for GCC is contained by the gcc-libs package
    //  * the licensing files for all Qt modules are contained within qt5-base; don't distinguish the modules for our purposes
    //  * use the real project name if known
    const auto upstreamVersion = PackageVersion::fromString(package->version).upstream;
    auto regularPackageName = package->computeRegularPackageName();
    auto packageID = regularPackageName.empty() ? package->name : regularPackageName;
    static const auto displayNames = unordered_map<string, string>{
        { "mingw-w64-headers", "MinGW-w64" },
        { "gcc", "GCC" },
        { "gcc-libs", "GCC" },
        { "freetype2", "FreeType" },
        { "harfbuzz", "HarfBuzz" },
        { "graphite", "Graphite" },
        { "openssl", "OpenSSL" },
        { "pcre", "PCRE" },
        { "pcre2", "PCRE2" },
        { "glib2", "GLib" },
        { "numix-icon-theme", "Numix icon theme" },
        { "breeze-icons", "Breeze icons (from KDE)" },
        { "go", "Go" },
        { "syncthing", "Syncthing" },
        { "syncthingtray", "Syncthing Tray" },
        { "tageditor", "Tag Editor" },
        { "passwordmanager", "Password Manager" },
    };
    if (endsWith(packageID, "-git") || endsWith(packageID, "-svn")) {
        packageID.resize(packageID.size() - 4);
    } else if (endsWith(packageID, "-hg")) {
        packageID.resize(packageID.size() - 3);
    }
    if (const auto displayName = displayNames.find(packageID); displayName != displayNames.cend()) {
        packageID = displayName->second;
    } else if (startsWith(packageID, "qt5-")) {
        packageID = "Qt 5";
        regularPackageName = "qt5-base";
    }

    // skip package if custom license has already been added
    if (const auto customLicenses = result.customLicences.find(packageID);
        customLicenses != result.customLicences.end() && !customLicenses->second.empty()) {
        return packageID;
    }

    // check whether the package has a standard license and/or a custom license
    bool hasCustomLicense = package->licenses.empty();
    if (packageID == "Qt 5") {
        // consider Qt's licenses custom as it has special variants of the standard licenses FDL, GPL and LGPL
        hasCustomLicense = true;
    } else {
        // read the package's license field (see https://wiki.archlinux.org/index.php/PKGBUILD#license)
        for (const auto &license : package->licenses) {
            // check for custom licenses and licenses which contain project specific references and are therefore considered custom as well
            if (startsWith(license, "custom") || license == "BSD" || license == "ISC" || license == "MIT" || license == "ZLIB"
                || license == "Python") {
                hasCustomLicense = true;
                continue;
            }

            // map Arch Linux generic way to say e.g. "GPL2 and above" to a concrete license e.g. "GPL2"
            auto concreteLicense = license;
            if (license == "GPL") {
                concreteLicense = "GPL2";
            } else if (license == "LGPL" || license == "LGPL2") {
                concreteLicense = "LGPL2.1";
            } else if (license == "FDL") {
                concreteLicense = "FDL1.2";
            } else if (license == "AGPL") {
                concreteLicense = "AGPL3";
            } else if (license == "APACHE") {
                concreteLicense = "Apache";
            }

            result.commonLicenses[concreteLicense].relevantPackages.emplace(packageID);
        }
    }

    if (!hasCustomLicense) {
        return packageID;
    }

    // read custom license
    if (!regularPackageName.empty()) {
        const auto regularPackageSearchResult = findPackage(Dependency(regularPackageName, package->version));
        if (regularPackageSearchResult.pkg) {
            const auto regularUpstreamVersion = PackageVersion::fromString(regularPackageSearchResult.pkg->version).upstream;
            if (upstreamVersion != regularUpstreamVersion) {
                result.success = false; // likely the license hasn't change; let's continue but don't consider it a successful run
                result.notes.emplace_back(
                    "Regular package for " % searchResult.pkg->name % '-' % searchResult.pkg->version % " has different upstream version "
                    + regularUpstreamVersion);
            }
            searchResult.db = regularPackageSearchResult.db;
            searchResult.pkg = regularPackageSearchResult.pkg;
        }
    }
    // -> locate package
    const auto *const db = std::get<Database *>(searchResult.db);
    if (!db) {
        packageID.clear();
        return packageID;
    }
    const auto &packageInfo = searchResult.pkg->packageInfo;
    if (!packageInfo) {
        packageID.clear();
        return packageID;
    }
    const auto path = db->localPkgDir % '/' + packageInfo->fileName;
    try {
        auto directories = extractFiles(path, &Package::isLicense);
        auto &licenses = result.customLicences[packageID];
        for (auto &dir : directories) {
            for (auto &file : dir.second) {
                if (file.type != ArchiveFileType::Regular) {
                    // skip anything but regular files (symlinks might point to other packages so resolving them is tricky)
                    continue;
                }
                licenses.emplace_back(dir.first % '/' + file.name, move(file.content));
            }
        }
        if (licenses.empty()) {
            const auto &packageName = searchResult.pkg->name;
            string fallback;
            if (startsWith(packageName, "qt5-")) {
                fallback = "qt5-base"; // the qt5-base package contains licenses for the other qt5-* packages
            } else if (packageName == "gcc") {
                fallback = "gcc-libs"; // the gcc-libs packages contains licenses for the gcc package
            }
            if (!fallback.empty() && fallback != packageName) {
                result.notes.emplace_back("No license file contained in " % path % ", falling back to " + fallback);
                if (!addLicenseInfo(result, Dependency(fallback, searchResult.pkg->version))) {
                    packageID.clear();
                }
                return packageID;
            }

            result.success = false;
            result.notes.emplace_back("No license file contained in " + path);
            packageID.clear();
            return packageID;
        }
    } catch (const runtime_error &e) {
        result.success = false;
        result.notes.emplace_back(e.what());
        packageID.clear();
        return packageID;
    }

    return packageID;
}

static void printQuoted(ostream &os, const string &str)
{
    bool needQuote = true;
    for (auto c : str) {
        if (needQuote) {
            os << "> ";
            needQuote = false;
        }
        os << c;
        if (c == '\n') {
            needQuote = true;
        }
    }
}

LicenseResult Config::computeLicenseInfo(const std::vector<string> &dependencyDenotations)
{
    LicenseResult result;

    // find "licenses" package containing common licenses
    const auto commonLicensePackageSearch = findPackage(Dependency("licenses"));
    const auto &licensesPackage = commonLicensePackageSearch.pkg;
    const auto *const db = std::get<Database *>(commonLicensePackageSearch.db);
    if (!db || !licensesPackage || !licensesPackage->packageInfo) {
        result.success = false;
        result.notes.emplace_back("Unable to find licenses package.");
        return result;
    }

    // extract common licenses
    if (db->localPkgDir.empty()) {
        result.success = false;
        result.notes.emplace_back("No local package dir for database \"" % db->name + "\" (containing \"licenses\" package) configured.");
        return result;
    }
    const auto path = db->localPkgDir % '/' + licensesPackage->packageInfo->fileName;
    decltype(extractFiles(path, &Package::isLicense)) licensesDirs;
    try {
        licensesDirs = extractFiles(path, &Package::isLicense);
        if (licensesDirs.empty()) {
            result.success = false;
            result.notes.emplace_back("No relevant files found in licenses package.");
            return result;
        }
    } catch (const runtime_error &e) {
        result.success = false;
        result.notes.emplace_back(e.what());
        return result;
    }

    // add required common licenses and custom licenses
    for (const auto &dependencyDenotation : dependencyDenotations) {
        addLicenseInfo(result, Dependency(dependencyDenotation.data(), dependencyDenotation.size()));
    }

    // find relevant common licenses in licenses package
    for (auto &commonLicense : result.commonLicenses) {
        const auto &licenseName = commonLicense.first;
        auto &license = commonLicense.second;
        const auto dir = licensesDirs.find("usr/share/licenses/common/" + licenseName);
        if (dir == licensesDirs.end() || dir->second.empty()) {
            result.success = false;
            result.notes.emplace_back("Unable to find license dir for common license " + licenseName);
            continue;
        }
        for (auto &file : dir->second) {
            license.files.emplace_back(move(file.name), move(file.content));
        }
    }

    // move "unique" common licenses to project specific licenses
    for (auto i = result.commonLicenses.begin(); i != result.commonLicenses.end();) {
        auto &license = i->second;
        if (license.relevantPackages.size() != 1) {
            ++i;
            continue;
        }
        auto &customLicenses = result.customLicences[*license.relevantPackages.cbegin()];
        customLicenses.insert(customLicenses.begin(), license.files.begin(), license.files.end());
        result.commonLicenses.erase(i++);
    }

    if (result.dependendProjects.empty()) {
        return result;
    }

    // start summary
    stringstream summary;
    summary << "This file contains licensing information for `" << result.mainProject << "` and libraries distributed with it:\n";
    for (const auto &dependency : result.dependendProjects) {
        summary << " * `" << dependency << "`\n";
    }

    // add common licenses to summary
    for (const auto &commonLicense : result.commonLicenses) {
        const auto &licenseName = commonLicense.first;
        auto &license = commonLicense.second;
        if (summary.tellp()) {
            summary << "\n---\n\n";
        }
        summary << "License";
        if (license.files.size() != 1) {
            summary << 's';
        }
        summary << " `" << licenseName << "` of " << joinStrings(license.relevantPackages, ", ", false, "`", "`") << ":\n\n";
        for (const auto &licenseFile : license.files) {
            if (license.files.size() > 1) {
                summary << '`' << licenseFile.filename << "`:  \n";
            }
            printQuoted(summary, licenseFile.content);
        }
    }

    // add custom licenses to summary
    for (const auto &customLicense : result.customLicences) {
        if (customLicense.second.empty()) {
            continue;
        }
        if (summary.tellp()) {
            summary << "\n---\n\n";
        }
        summary << "License";
        if (customLicense.second.size() != 1) {
            summary << 's';
        }
        summary << " of `" << customLicense.first << "`:\n\n";
        for (const auto &license : customLicense.second) {
            if (customLicense.second.size() > 1) {
                summary << '`' << license.filename << "`:  \n";
            }
            printQuoted(summary, license.content);
        }
    }

    result.licenseSummary = summary.str();

    // print the summary for debugging (FIXME: remove debug code)
    fstream("license-summary.md", ios_base::out | ios_base::trunc) << result.licenseSummary;

    // add project specific licenses
    return result;
}

} // namespace LibPkg
