#include "../data/config.h"

#include <c++utilities/conversion/stringbuilder.h>
#include <c++utilities/io/ansiescapecodes.h>

#include <iostream>
#include <thread>

using namespace std;
using namespace CppUtilities;
using namespace CppUtilities::EscapeCodes;

namespace LibPkg {

/*!
 * \brief Returns the database with the specified \a name and \a architecture or nullptr if it doesn't exist.
 */
Database *Config::findDatabase(std::string_view name, std::string_view architecture)
{
    for (auto &db : databases) {
        if (db.name == name && (architecture.empty() || db.arch == architecture)) {
            return &db;
        }
    }
    return nullptr;
}

/*!
 * \brief Returns the database with the specified \a databaseDenotation or nullptr if it doesn't exist.
 * \sa parseDatabaseDenotation() for the format of \a databaseDenotation
 */
Database *Config::findDatabaseFromDenotation(std::string_view databaseDenotation)
{
    const auto dbInfo = parseDatabaseDenotation(databaseDenotation);
    return findDatabase(dbInfo.first, dbInfo.second);
}

/*!
 * \brief Creates a database with the specified \a name and appends it to the configuration.
 */
Database *Config::createDatabase(std::string &&name)
{
    auto *const db = &databases.emplace_back(std::string(name));
    if (storage()) {
        db->initStorage(*storage());
    }
    return db;
}

/*!
 * \brief Returns the database with the specified \a name and \a architecture or creates a new one if it doesn't exist.
 * \remarks Resets the database's configuration. You'll end up with a blank database in any case.
 */
Database *Config::findOrCreateDatabase(std::string &&name, std::string_view architecture)
{
    auto *db = findDatabase(name, architecture);
    if (db) {
        db->resetConfiguration();
    } else {
        db = createDatabase(std::move(name));
    }
    if (!architecture.empty()) {
        db->arch = architecture;
    }
    return db;
}

/*!
 * \brief Returns the database with the specified \a name and \a architecture or creates a new one if it doesn't exist.
 * \remarks Resets the database's configuration. You'll end up with a blank database in any case.
 */
Database *Config::findOrCreateDatabase(std::string_view name, std::string_view architecture)
{
    auto *db = findDatabase(name, architecture);
    if (db) {
        db->resetConfiguration();
    } else {
        db = createDatabase(std::string(name));
    }
    if (!architecture.empty()) {
        db->arch = architecture;
    }
    return db;
}

/*!
 * \brief Returns the database with the specified \a databaseDenotation or creates a new one if it doesn't exist.
 * \remarks Resets the database's configuration. You'll end up with a blank database in any case.
 * \sa parseDatabaseDenotation() for the format of \a databaseDenotation
 */
Database *Config::findOrCreateDatabaseFromDenotation(std::string_view databaseDenotation)
{
    const auto dbInfo = parseDatabaseDenotation(databaseDenotation);
    return findOrCreateDatabase(dbInfo.first, dbInfo.second);
}

/*!
 * \brief Returns all packages with the specified database name, database architecture and package name.
 */
std::vector<PackageSearchResult> Config::findPackages(std::tuple<std::string_view, std::string_view, std::string_view> dbAndPackageName)
{
    auto pkgs = std::vector<PackageSearchResult>();
    const auto &[dbName, dbArch, packageName] = dbAndPackageName;

    // don't allow to get a list of all packages
    if (packageName.empty()) {
        return pkgs;
    }

    const auto name = std::string(packageName);
    for (auto &db : databases) {
        if ((!dbName.empty() && dbName != db.name) || (!dbArch.empty() && dbArch != db.arch)) {
            continue;
        }
        if (const auto [id, package] = db.findPackageWithID(name); package) {
            pkgs.emplace_back(db, package, id);
        }
    }
    return pkgs;
}

/*!
 * \brief Returns the first package satisfying \a dependency.
 * \remarks Packages where the name itself matches are preferred.
 */
PackageSearchResult Config::findPackage(const Dependency &dependency)
{
    auto result = PackageSearchResult();
    auto exactMatch = false;
    for (auto &db : databases) {
        db.providingPackages(dependency, false, [&](StorageID id, const std::shared_ptr<Package> &package) {
            exactMatch = dependency.name == package->name;
            result.db = &db;
            result.pkg = package;
            result.id = id;
            // prefer package where the name matches exactly; so if we found one no need to look further
            return exactMatch;
        });
        if (exactMatch) {
            break;
        }
    }
    return result;
}

/*!
 * \brief Returns all packages satisfying \a dependency or - if \a reverse is true - all packages requiring \a dependency.
 */
std::vector<PackageSearchResult> Config::findPackages(const Dependency &dependency, bool reverse)
{
    auto results = std::vector<PackageSearchResult>();
    for (auto &db : databases) {
        auto visited = std::unordered_set<StorageID>();
        db.providingPackages(dependency, reverse, [&](StorageID packageID, const std::shared_ptr<Package> &package) {
            if (visited.emplace(packageID).second) {
                results.emplace_back(db, package, packageID);
            }
            return false;
        });
    }
    return results;
}

/*!
 * \brief Returns all packages providing \a library or - if \a reverse is true - all packages requiring \a library.
 */
std::vector<PackageSearchResult> Config::findPackagesProvidingLibrary(const std::string &library, bool reverse)
{
    auto results = std::vector<PackageSearchResult>();
    auto visited = std::unordered_set<StorageID>();
    for (auto &db : databases) {
        db.providingPackages(library, reverse, [&](StorageID packageID, const std::shared_ptr<Package> &package) {
            if (visited.emplace(packageID).second) {
                results.emplace_back(db, package, packageID);
            }
            return false;
        });
    }
    return results;
}

/*!
 * \brief Returns all packages which names matches \a regex.
 */
std::vector<PackageSearchResult> Config::findPackages(const std::regex &regex)
{
    auto pkgs = std::vector<PackageSearchResult>();
    for (auto &db : databases) {
        db.allPackages([&](StorageID packageID, const std::shared_ptr<Package> &package) {
            if (std::regex_match(package->name, regex)) {
                pkgs.emplace_back(db, package, packageID);
            }
            return false;
        });
    }
    return pkgs;
}

/*!
 * \brief Returns all packages considered "the same" as \a package.
 * \remarks See Package::isSame().
 */
std::vector<PackageSearchResult> Config::findPackages(const Package &package)
{
    auto pkgs = std::vector<PackageSearchResult>();
    for (auto &db : databases) {
        if (const auto [id, pkg] = db.findPackageWithID(package.name); pkg && pkg->isSame(package)) {
            pkgs.emplace_back(db, pkg, id);
        }
    }
    return pkgs;
}

/*!
 * \brief Returns all packages \a packagePred returns true for from all databases \a databasePred returns true for.
 */
std::vector<PackageSearchResult> Config::findPackages(
    const std::function<bool(const Database &)> &databasePred, const std::function<bool(const Database &, const Package &)> &packagePred)
{
    auto pkgs = std::vector<PackageSearchResult>();
    for (auto &db : databases) {
        if (!databasePred(db)) {
            continue;
        }
        db.allPackages([&](StorageID packageID, const std::shared_ptr<Package> &package) {
            if (packagePred(db, *package)) {
                pkgs.emplace_back(db, package, packageID);
            }
            return false;
        });
    }
    return pkgs;
}

/*!
 * \brief Returns all packages \a pred returns true for.
 */
std::vector<PackageSearchResult> Config::findPackages(const std::function<bool(const Database &, const Package &)> &pred)
{
    auto pkgs = std::vector<PackageSearchResult>();
    for (auto &db : databases) {
        db.allPackages([&](StorageID packageID, const std::shared_ptr<Package> &package) {
            if (pred(db, *package)) {
                pkgs.emplace_back(db, package, packageID);
            }
            return false;
        });
    }
    return pkgs;
}

} // namespace LibPkg
