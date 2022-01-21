#include "./config.h"
#include "./storageprivate.h"

#include <reflective_rapidjson/json/reflector.h>

#include <c++utilities/conversion/stringbuilder.h>

using namespace std;
using namespace CppUtilities;

namespace LibPkg {

Status::Status(const Config &config)
    : architectures(config.architectures)
    , pacmanDatabasePath(config.pacmanDatabasePath)
    , packageCacheDirs(config.packageCacheDirs)
{
    dbStats.reserve(config.databases.size() + 1);
    for (const auto &db : config.databases) {
        dbStats.emplace_back(db);
    }
    dbStats.emplace_back(config.aur);
}

static const std::string &firstNonLocalMirror(const std::vector<std::string> &mirrors)
{
    for (const auto &mirror : mirrors) {
        if (!mirror.empty() && !startsWith(mirror, "file:")) {
            return mirror;
        }
    }
    static const auto emptyMirror = std::string();
    return emptyMirror;
}

DatabaseStatistics::DatabaseStatistics(const Database &db)
    : name(db.name)
    , packageCount(db.packageCount())
    , arch(db.arch)
    , lastUpdate(db.lastUpdate)
    , localPkgDir(db.localPkgDir)
    , mainMirror(firstNonLocalMirror(db.mirrors))
    , syncFromMirror(db.syncFromMirror)
{
}

Config::Config()
{
}

Config::~Config()
{
}

void Config::initStorage(const char *path, std::uint32_t maxDbs)
{
    assert(m_storage == nullptr); // only allow initializing storage once
    m_storage = std::make_unique<StorageDistribution>(path, maxDbs ? maxDbs : std::max<std::size_t>(databases.size() * 10u + 15u, 60u));
    for (auto &db : databases) {
        db.initStorage(*m_storage);
    }
    aur.initStorage(*m_storage);
}

static std::string addDatabaseDependencies(
    Config &config, Database &database, std::vector<Database *> &result, std::unordered_map<Database *, bool> &visited, bool addSelf)
{
    // abort if ...
    const auto insertion = visited.emplace(make_pair(&database, false));
    if (insertion.first->second) {
        return string(); // ... the database is already dealt with
    }
    if (!insertion.second) {
        return argsToString("cycle at ", database.name); // ... a cycle has been detected
    }

    // add the dependencies first
    for (const auto &dependency : database.dependencies) {
        auto *const requiredDb = config.findDatabase(dependency, database.arch);
        if (!requiredDb) {
            return argsToString(
                "database \"", dependency, "\" required by \"", database.name, "\" does not exist (architecture ", database.arch, ')');
        }
        if (auto error = addDatabaseDependencies(config, *requiredDb, result, visited, true); !error.empty()) {
            return error;
        }
    }

    // add database itself
    if (addSelf) {
        result.emplace_back(&database);
    }

    // consider this db done; if something else depends on it is o.k. and not a cycle
    visited[&database] = true;

    return string();
}

std::variant<std::vector<Database *>, std::string> Config::computeDatabaseDependencyOrder(Database &database, bool addSelf)
{
    std::vector<Database *> result;
    std::unordered_map<Database *, bool> visited;
    result.reserve(database.dependencies.size());
    auto error = addDatabaseDependencies(*this, database, result, visited, addSelf);
    if (!error.empty()) {
        return std::variant<std::vector<Database *>, std::string>(std::move(error));
    }
    return std::variant<std::vector<Database *>, std::string>(std::move(result));
}

static void addDatabasesRequiringDatabase(
    Config &config, Database &currentDatabase, std::vector<Database *> &result, std::unordered_set<Database *> &visited)
{
    // abort if the database is already dealt with or being processed (not caring about cycles here)
    if (!visited.emplace(&currentDatabase).second) {
        return;
    }

    // add the current database
    result.emplace_back(&currentDatabase);

    // add all configured databases depending on the current database
    for (auto &configuredDb : config.databases) {
        if (&configuredDb == &currentDatabase || configuredDb.arch != currentDatabase.arch) {
            continue;
        }
        if (std::find(configuredDb.dependencies.begin(), configuredDb.dependencies.end(), currentDatabase.name) != configuredDb.dependencies.end()) {
            addDatabasesRequiringDatabase(config, configuredDb, result, visited);
        }
    }
}

std::vector<Database *> Config::computeDatabasesRequiringDatabase(Database &database)
{
    std::vector<Database *> result;
    std::unordered_set<Database *> visited;
    result.reserve(databases.size());
    addDatabasesRequiringDatabase(*this, database, result, visited);
    return result;
}

void Config::pullDependentPackages(const std::vector<Dependency> &dependencies, const std::shared_ptr<Package> &relevantPackage,
    const std::unordered_set<LibPkg::Database *> &relevantDbs,
    std::unordered_map<LibPkg::StorageID, std::shared_ptr<LibPkg::Package>> &runtimeDependencies, DependencySet &missingDependencies,
    std::unordered_set<StorageID> &visited)
{
    auto found = false;
    for (const auto &dependency : dependencies) {
        for (auto &db : databases) {
            if (relevantDbs.find(&db) == relevantDbs.end()) {
                continue;
            }
            db.providingPackages(dependency, false, [&](StorageID packageID, const std::shared_ptr<Package> &package) {
                found = true;
                if (visited.emplace(packageID).second) {
                    const auto &[i, inserted] = runtimeDependencies.try_emplace(packageID);
                    if (inserted) {
                        i->second = package;
                    }
                    pullDependentPackages(i->second, relevantDbs, runtimeDependencies, missingDependencies, visited);
                }
                return false;
            });
        }
        if (!found) {
            missingDependencies.add(dependency, relevantPackage);
        }
    }
}

void Config::pullDependentPackages(const std::shared_ptr<Package> &package, const std::unordered_set<LibPkg::Database *> &relevantDbs,
    std::unordered_map<LibPkg::StorageID, std::shared_ptr<LibPkg::Package>> &runtimeDependencies, DependencySet &missingDependencies,
    std::unordered_set<StorageID> &visited)
{
    pullDependentPackages(package->dependencies, package, relevantDbs, runtimeDependencies, missingDependencies, visited);
    pullDependentPackages(package->optionalDependencies, package, relevantDbs, runtimeDependencies, missingDependencies, visited);
}

void Config::markAllDatabasesToBeDiscarded()
{
    for_each(databases.begin(), databases.end(), [](auto &db) { db.toBeDiscarded = true; });
}

void Config::discardDatabases()
{
    databases.erase(remove_if(databases.begin(), databases.end(), [](const auto &db) { return db.toBeDiscarded; }), databases.end());
}

} // namespace LibPkg
