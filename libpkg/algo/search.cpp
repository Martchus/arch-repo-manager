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
Database *Config::createDatabase(std::string &&name, std::string &&architecture)
{
    auto *const db = &databases.emplace_back(std::move(name));
    db->arch = std::move(architecture);
    if (storage()) {
        db->initStorage(*storage());
    }
    return db;
}

/*!
 * \brief Returns the database with the specified \a name and \a architecture or creates a new one if it doesn't exist.
 * \remarks Resets the database's configuration. You'll end up with a blank database in any case.
 */
Database *Config::findOrCreateDatabase(std::string &&name, std::string_view architecture, bool keepLocalPaths)
{
    auto *db = findDatabase(name, architecture);
    if (db) {
        db->resetConfiguration(keepLocalPaths);
        if (!architecture.empty()) {
            db->arch = architecture;
        }
    } else {
        db = createDatabase(std::move(name), std::string(architecture));
    }
    return db;
}

/*!
 * \brief Returns the database with the specified \a name and \a architecture or creates a new one if it doesn't exist.
 * \remarks Resets the database's configuration. You'll end up with a blank database in any case.
 */
Database *Config::findOrCreateDatabase(std::string_view name, std::string_view architecture, bool keepLocalPaths)
{
    auto *db = findDatabase(name, architecture);
    if (db) {
        db->resetConfiguration(keepLocalPaths);
        if (!architecture.empty()) {
            db->arch = architecture;
        }
    } else {
        db = createDatabase(std::string(name), std::string(architecture));
    }
    return db;
}

/*!
 * \brief Returns the database with the specified \a databaseDenotation or creates a new one if it doesn't exist.
 * \remarks Resets the database's configuration. You'll end up with a blank database in any case.
 * \sa parseDatabaseDenotation() for the format of \a databaseDenotation
 */
Database *Config::findOrCreateDatabaseFromDenotation(std::string_view databaseDenotation, bool keepLocalPaths)
{
    const auto dbInfo = parseDatabaseDenotation(databaseDenotation);
    return findOrCreateDatabase(dbInfo.first, dbInfo.second, keepLocalPaths);
}

/*!
 * \brief Returns all packages with the specified database name, database architecture and package name.
 */
std::vector<PackageSearchResult> Config::findPackages(
    std::tuple<std::string_view, std::string_view, std::string_view> dbAndPackageName, std::size_t limit)
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
        if (pkgs.size() >= limit) {
            return pkgs;
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
std::vector<PackageSearchResult> Config::findPackages(const Dependency &dependency, bool reverse, std::size_t limit)
{
    auto results = std::vector<PackageSearchResult>();
    for (auto &db : databases) {
        auto visited = std::unordered_set<StorageID>();
        db.providingPackages(dependency, reverse, [&](StorageID packageID, const std::shared_ptr<Package> &package) {
            if (visited.emplace(packageID).second) {
                results.emplace_back(db, package, packageID);
            }
            return results.size() >= limit;
        });
    }
    return results;
}

/*!
 * \brief Returns all packages providing \a library or - if \a reverse is true - all packages requiring \a library.
 */
std::vector<PackageSearchResult> Config::findPackagesProvidingLibrary(const std::string &library, bool reverse, std::size_t limit)
{
    auto results = std::vector<PackageSearchResult>();
    auto visited = std::unordered_set<StorageID>();
    for (auto &db : databases) {
        db.providingPackages(library, reverse, [&](StorageID packageID, const std::shared_ptr<Package> &package) {
            if (visited.emplace(packageID).second) {
                results.emplace_back(db, package, packageID);
            }
            return results.size() >= limit;
        });
    }
    return results;
}

void Config::packages(std::string_view dbName, std::string_view dbArch, const std::string &packageName, const DatabaseVisitor &databaseVisitor,
    const PackageVisitorConst &visitor)
{
    // don't allow to iterate though all packages
    if (packageName.empty()) {
        return;
    }
    for (auto &db : databases) {
        if ((!dbName.empty() && dbName != db.name) || (!dbArch.empty() && dbArch != db.arch) || (databaseVisitor && databaseVisitor(db))) {
            continue;
        }
        if (const auto [id, package] = db.findPackageWithID(packageName); package) {
            visitor(db, id, package);
        }
    }
}

void Config::packages(std::string_view dbName, std::string_view dbArch, const std::string &packageName, const DatabaseVisitor &databaseVisitor,
    const PackageVisitorBase &visitor)
{
    // don't allow to iterate though all packages
    if (packageName.empty()) {
        return;
    }
    auto basePackage = std::make_shared<PackageBase>();
    for (auto &db : databases) {
        if ((!dbName.empty() && dbName != db.name) || (!dbArch.empty() && dbArch != db.arch) || (databaseVisitor && databaseVisitor(db))) {
            continue;
        }
        if (!basePackage) {
            basePackage = std::make_shared<PackageBase>();
        } else {
            basePackage->clear();
        }
        if (const auto id = db.findBasePackageWithID(packageName, *basePackage)) {
            visitor(db, id, std::move(basePackage));
        }
    }
}

void Config::packagesByName(const DatabaseVisitor &databaseVisitor, const PackageVisitorByName &visitor)
{
    for (auto &db : databases) {
        if (databaseVisitor && databaseVisitor(db)) {
            continue;
        }
        db.allPackagesByName(
            [&](std::string_view packageName, const std::function<PackageSpec(void)> &getPackage) { return visitor(db, packageName, getPackage); });
    }
}

void Config::packagesByName(const DatabaseVisitor &databaseVisitor, const PackageVisitorByNameBase &visitor)
{
    for (auto &db : databases) {
        if (databaseVisitor && databaseVisitor(db)) {
            continue;
        }
        db.allPackagesByName([&](std::string_view packageName, const std::function<StorageID(PackageBase &)> &getPackage) {
            return visitor(db, packageName, getPackage);
        });
    }
}

void Config::providingPackages(const Dependency &dependency, bool reverse, const DatabaseVisitor &databaseVisitor, const PackageVisitorConst &visitor)
{
    for (auto &db : databases) {
        if (databaseVisitor && databaseVisitor(db)) {
            continue;
        }
        auto visited = std::unordered_set<LibPkg::StorageID>();
        db.providingPackages(dependency, reverse, [&](StorageID packageID, const std::shared_ptr<Package> &package) {
            return visited.emplace(packageID).second ? visitor(db, packageID, package) : false;
        });
    }
}

void Config::providingPackagesBase(
    const Dependency &dependency, bool reverse, const DatabaseVisitor &databaseVisitor, const PackageVisitorBase &visitor)
{
    for (auto &db : databases) {
        if (databaseVisitor && databaseVisitor(db)) {
            continue;
        }
        auto visited = std::unordered_set<LibPkg::StorageID>();
        db.providingPackagesBase(dependency, reverse, [&](StorageID packageID, std::shared_ptr<PackageBase> &&package) {
            return visited.emplace(packageID).second ? visitor(db, packageID, std::move(package)) : false;
        });
    }
}

void Config::providingPackages(
    const std::string &libraryName, bool reverse, const DatabaseVisitor &databaseVisitor, const PackageVisitorConst &visitor)
{
    for (auto &db : databases) {
        if (databaseVisitor && databaseVisitor(db)) {
            continue;
        }
        auto visited = std::unordered_set<LibPkg::StorageID>();
        db.providingPackages(libraryName, reverse, [&](StorageID packageID, const std::shared_ptr<Package> &package) {
            return visited.emplace(packageID).second ? visitor(db, packageID, package) : false;
        });
    }
}

void Config::providingPackagesBase(
    const std::string &libraryName, bool reverse, const DatabaseVisitor &databaseVisitor, const PackageVisitorBase &visitor)
{
    for (auto &db : databases) {
        if (databaseVisitor && databaseVisitor(db)) {
            continue;
        }
        auto visited = std::unordered_set<LibPkg::StorageID>();
        db.providingPackagesBase(libraryName, reverse, [&](StorageID packageID, std::shared_ptr<PackageBase> &&package) {
            return visited.emplace(packageID).second ? visitor(db, packageID, std::move(package)) : false;
        });
    }
}

} // namespace LibPkg
