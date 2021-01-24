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
 * \brief Returns the database with the specified \a name and \a architecture or creates a new one if it doesn't exist.
 * \remarks Resets the database's configuration. You'll end up with a blank database in any case.
 */
Database *Config::findOrCreateDatabase(std::string &&name, std::string_view architecture)
{
    auto *db = findDatabase(name, architecture);
    if (db) {
        db->resetConfiguration();
    } else {
        db = &databases.emplace_back(move(name));
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
        db = &databases.emplace_back(std::string(name));
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
 * \brief Runs \a processNextPackage for each package of each database using as many threads as CPU cores available.
 *
 * Databases and packages are iterated in order. \a processNextDatabase is called when "reaching" the next database.
 *
 * \a processNextDatabase and \a processNextPackage are supposed to return an empty string on success and an error message
 * on failure. If \a processNextDatabase fails, the whole database is skipped.
 *
 * \a processNextDatabase is not ran in parallel and therefore expected to be fast.
 *
 * \returns Returns the error messages returned by \a processNextDatabase and \a processNextPackage.
 * \remarks Not used anymore. Possibly still useful at some point?
 */
std::list<std::string> Config::forEachPackage(const std::function<std::string(Database *db)> &processNextDatabase,
    const std::function<std::string(Database *db, std::shared_ptr<Package> &pkg, std::mutex &dbMutex)> &processNextPackage)
{
    // define mutex to sync getting the next package
    std::mutex getNextPathMutex, submitFailureMutex, dbMutex;
    std::list<std::string> errorMessages;

    // define and initialize iterators
    auto dbIterator = databases.begin(), dbEnd = databases.end();
    auto error = processNextDatabase(&*dbIterator);
    while (dbIterator != dbEnd) {
        if (error.empty()) {
            break;
        }
        errorMessages.emplace_back(move(error));
        error = processNextDatabase(&*++dbIterator);
    }
    if (dbIterator == dbEnd) {
        return errorMessages;
    }
    auto pkgIterator = dbIterator->packages.begin(), pkgEnd = dbIterator->packages.end();

    // get the first database
    Database *currentDb = &*dbIterator;
    ++dbIterator;

    const auto recordError = [&](auto &&error) {
        lock_guard<mutex> lock(submitFailureMutex);
        cerr << Phrases::SubError << error << Phrases::End;
        errorMessages.emplace_back(error);
    };

    const auto processPackages = [&] {
        for (;;) {
            // get next package
            shared_ptr<Package> currentPackage;
            {
                lock_guard<mutex> lock(getNextPathMutex);
                for (;;) {
                    if (pkgIterator != pkgEnd) {
                        currentPackage = pkgIterator->second;
                        ++pkgIterator;
                        break;
                    } else if (dbIterator != dbEnd) {
                        // process next database
                        auto error = processNextDatabase(&*dbIterator);
                        if (!error.empty()) {
                            if (error != "skip") {
                                recordError(move(error));
                            }
                            ++dbIterator;
                            continue;
                        }
                        currentDb = &*dbIterator;
                        pkgIterator = dbIterator->packages.begin();
                        pkgEnd = dbIterator->packages.end();
                        ++dbIterator;
                        continue;
                    } else {
                        return;
                    }
                }
            }

            // process next package
            try {
                auto error = processNextPackage(currentDb, currentPackage, dbMutex);
                if (!error.empty()) {
                    recordError(move(error));
                }
            } catch (const runtime_error &e) {
                recordError(argsToString(currentPackage->name, ':', ' ', e.what()));
                continue;
            }
        }
    };

    vector<thread> threads(thread::hardware_concurrency() + 2); // FIXME: make this thread count configurable?
    for (thread &t : threads) {
        t = thread(processPackages);
    }
    processPackages();
    for (thread &t : threads) {
        t.join();
    }

    return errorMessages;
}

/*!
 * \brief Returns all packages with the specified database name, database architecture and package name.
 */
std::vector<PackageSearchResult> Config::findPackages(std::tuple<std::string_view, std::string_view, std::string_view> dbAndPackageName)
{
    vector<PackageSearchResult> pkgs;
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
        if (const auto i = db.packages.find(name); i != db.packages.end()) {
            pkgs.emplace_back(db, i->second);
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
    PackageSearchResult result;
    for (auto &db : databases) {
        for (auto range = db.providedDeps.equal_range(dependency.name); range.first != range.second; ++range.first) {
            const auto &providedDependency = range.first->second;
            if (!Dependency::matches(dependency.mode, dependency.version, providedDependency.version)) {
                continue;
            }
            const auto pkgs = providedDependency.relevantPackages;
            for (const auto &pkg : pkgs) {
                if (!result.pkg) {
                    result.db = &db;
                    result.pkg = pkg;
                }
                // prefer package where the name matches exactly; so if we found one no need to look further
                if (dependency.name == pkg->name) {
                    result.db = &db;
                    result.pkg = pkg;
                    return result;
                }
            }
        }
    }
    return result;
}

/*!
 * \brief Returns all packages satisfying \a dependency or - if \a reverse is true - all packages requiring \a dependency.
 */
std::vector<PackageSearchResult> Config::findPackages(const Dependency &dependency, bool reverse)
{
    const auto dependencySet = reverse ? &Database::requiredDeps : &Database::providedDeps;
    vector<PackageSearchResult> results;
    for (auto &db : databases) {
        for (auto range = (db.*dependencySet).equal_range(dependency.name); range.first != range.second; ++range.first) {
            const auto &providedDependency = range.first->second;
            if (!Dependency::matches(dependency.mode, dependency.version, providedDependency.version)) {
                continue;
            }
            for (const auto &pkg : providedDependency.relevantPackages) {
                if (std::find_if(results.begin(), results.end(), [&pkg](const auto &res) { return res.pkg == pkg; }) == results.end()) {
                    results.emplace_back(db, pkg);
                }
            }
        }
    }
    return results;
}

/*!
 * \brief Returns all packages providing \a library or - if \a reverse is true - all packages requiring \a library.
 */
std::vector<PackageSearchResult> Config::findPackagesProvidingLibrary(const string &library, bool reverse)
{
    const auto packagesByLibraryName = reverse ? &Database::requiredLibs : &Database::providedLibs;
    vector<PackageSearchResult> results;
    for (auto &db : databases) {
        for (auto range = (db.*packagesByLibraryName).equal_range(library); range.first != range.second; ++range.first) {
            for (const auto &pkg : range.first->second) {
                if (std::find_if(results.begin(), results.end(), [&pkg](const auto &res) { return res.pkg == pkg; }) == results.end()) {
                    results.emplace_back(db, pkg);
                }
            }
        }
    }
    return results;
}

/*!
 * \brief Returns all packages which names matches \a regex.
 */
std::vector<PackageSearchResult> Config::findPackages(const regex &regex)
{
    vector<PackageSearchResult> pkgs;
    for (auto &db : databases) {
        for (const auto &pkg : db.packages) {
            if (regex_match(pkg.second->name, regex)) {
                pkgs.emplace_back(db, pkg.second);
            }
        }
    }
    return pkgs;
}

/*!
 * \brief Returns all packages considered "the same" as \a package.
 * \remarks See Package::isSame().
 */
std::vector<PackageSearchResult> Config::findPackages(const Package &package)
{
    vector<PackageSearchResult> pkgs;
    for (auto &db : databases) {
        for (const auto &pkg : db.packages) {
            if (pkg.second->isSame(package)) {
                pkgs.emplace_back(db, pkg.second);
                break;
            }
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
    std::vector<PackageSearchResult> pkgs;
    for (auto &db : databases) {
        if (!databasePred(db)) {
            continue;
        }
        for (const auto &pkg : db.packages) {
            if (packagePred(db, *pkg.second)) {
                pkgs.emplace_back(db, pkg.second);
            }
        }
    }
    return pkgs;
}

/*!
 * \brief Returns all packages \a pred returns true for.
 */
std::vector<PackageSearchResult> Config::findPackages(const std::function<bool(const Database &, const Package &)> &pred)
{
    std::vector<PackageSearchResult> pkgs;
    for (auto &db : databases) {
        for (const auto &pkg : db.packages) {
            if (pred(db, *pkg.second)) {
                pkgs.emplace_back(db, pkg.second);
            }
        }
    }
    return pkgs;
}

} // namespace LibPkg
