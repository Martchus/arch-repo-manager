#include "./database.h"
#include "./config.h"
#include "./storageprivate.h"

#include "reflection/database.h"

#include <c++utilities/conversion/stringbuilder.h>

#include <iostream>

using namespace std;
using namespace CppUtilities;

namespace LibPkg {

struct AffectedPackages {
    std::unordered_set<StorageID> newPackages;
    std::unordered_set<StorageID> removedPackages;
};

struct AffectedPackagesWithDependencyDetail : public AffectedPackages {
    std::string version;
    DependencyMode mode = DependencyMode::Any;
};

struct PackageUpdaterPrivate {
    using AffectedDeps = std::unordered_multimap<std::string, AffectedPackagesWithDependencyDetail>;
    using AffectedLibs = std::unordered_map<std::string, AffectedPackages>;

    explicit PackageUpdaterPrivate(DatabaseStorage &storage, bool clear);
    void update(const PackageCache::StoreResult &res, const std::shared_ptr<Package> &package);
    void update(const StorageID packageID, bool removed, const std::shared_ptr<Package> &package);
    void submit(const std::string &dependencyName, AffectedDeps::mapped_type &affected, DependencyStorage::RWTransaction &txn);
    void submit(const std::string &libraryName, AffectedLibs::mapped_type &affected, LibraryDependencyStorage::RWTransaction &txn);

    bool clear = false;
    std::unique_lock<std::mutex> lock;
    PackageStorage::RWTransaction packagesTxn;
    std::unordered_set<StorageID> handledIds;
    AffectedDeps affectedProvidedDeps;
    AffectedDeps affectedRequiredDeps;
    AffectedLibs affectedProvidedLibs;
    AffectedLibs affectedRequiredLibs;
    std::size_t packageCountBeforeCommit = 0;

private:
    static AffectedDeps::iterator findDependency(const Dependency &dependency, AffectedDeps &affected);
    static void addDependency(StorageID packageID, const Dependency &dependency, bool removed, AffectedDeps &affected);
    static void addLibrary(StorageID packageID, const std::string &libraryName, bool removed, AffectedLibs &affected);
};

Database::Database(const std::string &name, const std::string &path)
    : name(name)
    , path(path)
{
}

Database::Database(std::string &&name, std::string &&path)
    : name(std::move(name))
    , path(std::move(path))
{
}

Database::Database(Database &&other) = default;

Database::~Database()
{
}

void Database::initStorage(StorageDistribution &storage)
{
    m_storage = storage.forDatabase(name % '@' + arch);
}

void LibPkg::Database::rebuildDb()
{
    std::cerr << "Rebuilding package database \"" << name << "\"\n";
    auto txn = m_storage->packages.getRWTransaction();
    auto processed = std::size_t();
    auto ok = std::size_t();
    auto lastOk = false;
    txn.rebuild([count = txn.size(), &processed, &ok, &lastOk](StorageID id, Package *package) mutable {
        std::cerr << "Processing package " << ++processed << " / " << count << "          ";
        if (!package) {
            std::cerr << "\nDeleting package " << id << ": unable to deserialize\n";
            return lastOk = false;
        }
        if (package->name.empty()) {
            std::cerr << "\nDeleting package " << id << ": name is empty\n";
            return lastOk = false;
        }
        std::cerr << '\r';
        ++ok;
        return lastOk = true;
    });
    if (lastOk) {
        std::cerr << '\n';
    }
    if (ok < processed) {
        std::cerr << "Discarding " << (processed - ok) << " invalid packages from \"" << name << "\".\n";
    } else {
        std::cerr << "All " << ok << " packages from \"" << name << "\" are valid.\n";
    }
    std::cerr << "Committing changes to package database \"" << name << "\".\n";
    txn.commit();
}

void Database::dumpDb(const std::optional<std::regex> &filterRegex)
{
    std::cout << "db: " << name << '@' << arch << '\n';
    auto txn = m_storage->packages.getROTransaction();
    auto end = txn.end();
    std::cout << "packages (" << txn.size() << "):\n";
    for (auto i = txn.begin(); i != end; ++i) {
        if (const auto &value = i.value(); !filterRegex.has_value() || std::regex_match(value.name, filterRegex.value())) {
            const auto key = i.getKey().get<LMDBSafe::IDType>();
            std::cout << key << ':' << ' ' << value.name << '\n';
        }
    }
    std::cout << "index (" << txn.size<0>() << "):\n";
    for (auto i = txn.begin<0>(); i != end; ++i) {
        if (const auto key = i.getKey().get<std::string_view>();
            !filterRegex.has_value() || std::regex_match(key.cbegin(), key.cend(), filterRegex.value())) {
            const auto value = i.getID();
            std::cout << key << ':' << ' ' << value << '\n';
        }
    }
    std::cout << '\n';
}

void LibPkg::Database::deducePathsFromLocalDirs()
{
    if (localDbDir.empty()) {
        return;
    }
    if (path.empty()) {
        path = localDbDir % '/' % name + ".db";
    }
    if (filesPath.empty()) {
        filesPath = localDbDir % '/' % name + ".files";
    }
}

void Database::resetConfiguration(bool keepLocalPaths)
{
    path.clear();
    filesPath.clear();
    mirrors.clear();
    usage = DatabaseUsage::None;
    signatureLevel = SignatureLevel::Default;
    arch = "x86_64";
    dependencies.clear();
    if (!keepLocalPaths) {
        localPkgDir.clear();
        localDbDir.clear();
    }
    syncFromMirror = false;
}

void Database::clearPackages()
{
    if (!m_storage) {
        return;
    }
    const auto lock = std::unique_lock(m_storage->updateMutex);
    m_storage->packageCache.clear(*m_storage);
    lastUpdate = DateTime();
}

std::vector<std::shared_ptr<Package>> Database::findPackages(const std::function<bool(const Database &, const Package &)> &pred)
{
    // TODO: use cache here
    auto pkgs = std::vector<std::shared_ptr<Package>>();
    auto txn = m_storage->packages.getROTransaction();
    for (auto i = txn.begin<std::shared_ptr>(); i != txn.end(); ++i) {
        if (pred(*this, *i)) {
            pkgs.emplace_back(std::move(i.getPointer()));
        }
    }
    return pkgs;
}

static void removeDependency(DependencyStorage::RWTransaction &txn, StorageID packageID, const std::string &dependencyName)
{
    for (auto [i, end] = txn.equal_range<0>(dependencyName); i != end; ++i) {
        auto &dependency = i.value();
        dependency.relevantPackages.erase(packageID);
        if (dependency.relevantPackages.empty()) {
            i.del();
        } else {
            txn.put(dependency, i.getID());
        }
    }
}

static void removeLibDependency(LibraryDependencyStorage::RWTransaction &txn, StorageID packageID, const std::string &dependencyName)
{
    for (auto [i, end] = txn.equal_range<0>(dependencyName); i != end; ++i) {
        auto &dependency = i.value();
        dependency.relevantPackages.erase(packageID);
        if (dependency.relevantPackages.empty()) {
            i.del();
        } else {
            txn.put(dependency, i.getID());
        }
    }
}

static void removePackageDependencies(DatabaseStorage &storage, const std::shared_ptr<LMDBSafe::MDBRWTransaction> &txnHandle, StorageID packageID,
    const std::shared_ptr<Package> &package)
{
    {
        auto txn = storage.providedDeps.getRWTransaction(txnHandle);
        removeDependency(txn, packageID, package->name);
        for (const auto &dep : package->provides) {
            removeDependency(txn, packageID, dep.name);
        }
    }
    {
        auto txn = storage.requiredDeps.getRWTransaction(txnHandle);
        for (const auto &dep : package->dependencies) {
            removeDependency(txn, packageID, dep.name);
        }
        for (const auto &dep : package->optionalDependencies) {
            removeDependency(txn, packageID, dep.name);
        }
    }
    {
        auto txn = storage.providedLibs.getRWTransaction(txnHandle);
        for (const auto &lib : package->libprovides) {
            removeLibDependency(txn, packageID, lib);
        }
    }
    {
        auto txn = storage.requiredLibs.getRWTransaction(txnHandle);
        for (const auto &lib : package->libdepends) {
            removeLibDependency(txn, packageID, lib);
        }
    }
}

static void addDependency(DependencyStorage::RWTransaction &txn, StorageID packageID, const std::string &dependencyName,
    const std::string &dependencyVersion, DependencyMode dependencyMode = DependencyMode::Any)
{
    if (dependencyName.empty()) {
        return;
    }
    for (auto [i, end] = txn.equal_range<0>(dependencyName); i != end; ++i) {
        auto &existingDependency = i.value();
        if (static_cast<const Dependency &>(existingDependency).version != dependencyVersion) {
            continue;
        }
        const auto [i2, newID] = existingDependency.relevantPackages.emplace(packageID);
        if (newID) {
            txn.put(existingDependency, i.getID());
        }
        return;
    }
    auto newDependency = DatabaseDependency(dependencyName, dependencyVersion, dependencyMode);
    newDependency.relevantPackages.emplace(packageID);
    txn.put(newDependency);
}

static void addLibDependency(LibraryDependencyStorage::RWTransaction &txn, StorageID packageID, const std::string &dependencyName)
{
    if (dependencyName.empty()) {
        return;
    }
    for (auto [i, end] = txn.equal_range<0>(dependencyName); i != end; ++i) {
        auto &existingDependency = i.value();
        const auto [i2, newID] = existingDependency.relevantPackages.emplace(packageID);
        if (newID) {
            txn.put(existingDependency, i.getID());
        }
        return;
    }
    auto newDependency = DatabaseLibraryDependency(dependencyName);
    newDependency.relevantPackages.emplace(packageID);
    txn.put(newDependency);
}

static void addPackageDependencies(DatabaseStorage &storage, const std::shared_ptr<LMDBSafe::MDBRWTransaction> &txnHandle, StorageID packageID,
    const std::shared_ptr<Package> &package)
{
    {
        auto txn = storage.providedDeps.getRWTransaction(txnHandle);
        addDependency(txn, packageID, package->name, package->version);
        for (const auto &dep : package->provides) {
            addDependency(txn, packageID, dep.name, dep.version, dep.mode);
        }
    }
    {
        auto txn = storage.requiredDeps.getRWTransaction(txnHandle);
        for (const auto &dep : package->dependencies) {
            addDependency(txn, packageID, dep.name, dep.version, dep.mode);
        }
        for (const auto &dep : package->optionalDependencies) {
            addDependency(txn, packageID, dep.name, dep.version, dep.mode);
        }
    }
    {
        auto txn = storage.providedLibs.getRWTransaction(txnHandle);
        for (const auto &lib : package->libprovides) {
            addLibDependency(txn, packageID, lib);
        }
    }
    {
        auto txn = storage.requiredLibs.getRWTransaction(txnHandle);
        for (const auto &lib : package->libdepends) {
            addLibDependency(txn, packageID, lib);
        }
    }
}

void Database::allPackages(const PackageVisitorMove &visitor)
{
    auto txn = m_storage->packages.getROTransaction();
    for (auto i = txn.begin<std::shared_ptr>(); i != txn.end(); ++i) {
        if (visitor(i.getID(), std::move(i.getPointer()))) {
            return;
        }
    }
}

void Database::allPackagesBase(const PackageVisitorBase &visitor)
{
    auto txn = m_storage->packages.getROTransaction();
    for (auto i = txn.begin<std::shared_ptr, PackageBase>(); i != txn.end(); ++i) {
        if (visitor(i.getID(), std::move(i.getPointer()))) {
            return;
        }
    }
}

void LibPkg::Database::allPackagesByName(const PackageVisitorByName &visitor)
{
    auto txn = m_storage->packages.getROTransaction();
    for (auto i = txn.begin_idx<0, std::shared_ptr>(); i != txn.end(); ++i) {
        const auto packageName = i.getKey().get<string_view>();
        if (visitor(packageName, [this, &txn, &i] { return m_storage->packageCache.retrieve(*m_storage, &txn, i.value()); })) {
            return;
        }
    }
}

void LibPkg::Database::allPackagesByName(const PackageVisitorByNameBase &visitor)
{
    auto txn = m_storage->packages.getROTransaction();
    for (auto i = txn.begin_idx<0, std::shared_ptr>(); i != txn.end(); ++i) {
        const auto packageName = i.getKey().get<string_view>();
        if (visitor(packageName, [&txn, &i](PackageBase &pkg) { return txn.get(i.value(), pkg); })) {
            return;
        }
    }
}

std::size_t Database::packageCount() const
{
    return m_storage->packages.getROTransaction().size();
}

void Database::providingPackages(const Dependency &dependency, bool reverse, const PackageVisitorConst &visitor)
{
    if (dependency.name.empty()) {
        return;
    }
    auto providesTxn = (reverse ? m_storage->requiredDeps : m_storage->providedDeps).getROTransaction();
    auto packagesTxn = m_storage->packages.getROTransaction();
    for (auto [i, end] = providesTxn.equal_range<0>(dependency.name); i != end; ++i) {
        const auto &providedDependency = i.value();
        const auto &asDependency = static_cast<const Dependency &>(providedDependency);
        if (!Dependency::matches(dependency.mode, dependency.version, asDependency.version)) {
            continue;
        }
        for (const auto packageID : providedDependency.relevantPackages) {
            auto res = m_storage->packageCache.retrieve(*m_storage, &packagesTxn, packageID);
            if (res.pkg && visitor(packageID, res.pkg)) {
                return;
            }
        }
    }
}

void Database::providingPackagesBase(const Dependency &dependency, bool reverse, const PackageVisitorBase &visitor)
{
    if (dependency.name.empty()) {
        return;
    }
    auto providesTxn = (reverse ? m_storage->requiredDeps : m_storage->providedDeps).getROTransaction();
    auto packagesTxn = m_storage->packages.getROTransaction();
    auto package = std::shared_ptr<PackageBase>();
    for (auto [i, end] = providesTxn.equal_range<0>(dependency.name); i != end; ++i) {
        const auto &providedDependency = i.value();
        const auto &asDependency = static_cast<const Dependency &>(providedDependency);
        if (!Dependency::matches(dependency.mode, dependency.version, asDependency.version)) {
            continue;
        }
        for (const auto packageID : providedDependency.relevantPackages) {
            if (!package) {
                package = std::make_shared<PackageBase>();
            } else {
                package->clear();
            }
            if (packagesTxn.get<PackageBase>(packageID, *package) && visitor(packageID, std::move(package))) {
                return;
            }
        }
    }
}

void Database::providingPackages(const std::string &libraryName, bool reverse, const PackageVisitorConst &visitor)
{
    if (libraryName.empty()) {
        return;
    }
    auto providesTxn = (reverse ? m_storage->requiredLibs : m_storage->providedLibs).getROTransaction();
    auto packagesTxn = m_storage->packages.getROTransaction();
    for (auto [i, end] = providesTxn.equal_range<0>(libraryName); i != end; ++i) {
        for (const auto packageID : i->relevantPackages) {
            auto res = m_storage->packageCache.retrieve(*m_storage, &packagesTxn, packageID);
            if (res.pkg && visitor(packageID, res.pkg)) {
                return;
            }
        }
    }
}

void Database::providingPackagesBase(const std::string &libraryName, bool reverse, const PackageVisitorBase &visitor)
{
    if (libraryName.empty()) {
        return;
    }
    auto providesTxn = (reverse ? m_storage->requiredLibs : m_storage->providedLibs).getROTransaction();
    auto packagesTxn = m_storage->packages.getROTransaction();
    auto package = std::shared_ptr<PackageBase>();
    for (auto [i, end] = providesTxn.equal_range<0>(libraryName); i != end; ++i) {
        for (const auto packageID : i->relevantPackages) {
            if (!package) {
                package = std::make_shared<PackageBase>();
            } else {
                package->clear();
            }
            if (packagesTxn.get<PackageBase>(packageID, *package) && visitor(packageID, std::move(package))) {
                return;
            }
        }
    }
}

bool Database::provides(const Dependency &dependency, bool reverse) const
{
    if (dependency.name.empty()) {
        return false;
    }
    auto providesTxn = (reverse ? m_storage->requiredDeps : m_storage->providedDeps).getROTransaction();
    for (auto [i, end] = providesTxn.equal_range<0>(dependency.name); i != end; ++i) {
        const Dependency &providedDependency = i.value();
        if (Dependency::matches(dependency.mode, dependency.version, providedDependency.version)) {
            return true;
        }
    }
    return false;
}

bool Database::provides(const std::string &libraryName, bool reverse) const
{
    if (libraryName.empty()) {
        return false;
    }
    auto providesTxn = (reverse ? m_storage->requiredLibs : m_storage->providedLibs).getROTransaction();
    return providesTxn.find<0>(libraryName) != providesTxn.end();
}

std::shared_ptr<Package> Database::findPackage(StorageID packageID)
{
    return m_storage->packageCache.retrieve(*m_storage, packageID).pkg;
}

std::shared_ptr<Package> Database::findPackage(const std::string &packageName)
{
    return m_storage->packageCache.retrieve(*m_storage, packageName).pkg;
}

PackageSpec Database::findPackageWithID(const std::string &packageName)
{
    return m_storage->packageCache.retrieve(*m_storage, packageName);
}

StorageID Database::findBasePackageWithID(const std::string &packageName, PackageBase &basePackage)
{
    auto txn = m_storage->packages.getROTransaction();
    return txn.get<0, PackageBase>(packageName, basePackage);
}

void Database::removePackage(const std::string &packageName)
{
    if (packageName.empty()) {
        return;
    }
    const auto lock = std::unique_lock(m_storage->updateMutex);
    auto txn = m_storage->packages.getRWTransaction();
    const auto [packageID, package] = m_storage->packageCache.retrieve(*m_storage, &txn, packageName);
    if (package) {
        removePackageDependencies(*m_storage, txn.getTransactionHandle(), packageID, package);
        txn.commit();
        m_storage->packageCache.invalidate(*m_storage, packageName);
    }
}

StorageID Database::updatePackage(const std::shared_ptr<Package> &package)
{
    if (package->name.empty()) {
        return 0;
    }
    const auto lock = std::unique_lock(m_storage->updateMutex);
    auto txn = m_storage->packages.getRWTransaction();
    const auto res = m_storage->packageCache.store(*m_storage, txn, package);
    if (res.oldEntry) {
        removePackageDependencies(*m_storage, txn.getTransactionHandle(), res.id, res.oldEntry);
    }
    addPackageDependencies(*m_storage, txn.getTransactionHandle(), res.id, package);
    txn.commit();
    return res.id;
}

/*!
 * \brief Determines which packages are unresolvable assuming new packages are added to the database and certain provides are removed.
 * \param config The configuration supposed to contain database dependencies.
 * \param newPackages Packages which are assumed to be added to the database.
 * \param removedProvides Provides which are assumed to be removed from the database.
 * \param depsToIgnore Specifies dependencies to be ignored if missing (version constraints not supported).
 * \param libsToIgnore Specifies libraries to be ignored if missing.
 * \remarks "Resolvable" means here (so far) just that all dependencies are present. It does not mean a package is "installable" because
 *          conflicts between dependencies might still prevent that.
 */
std::unordered_map<PackageSpec, UnresolvedDependencies> Database::detectUnresolvedPackages(Config &config,
    const std::vector<std::shared_ptr<Package>> &newPackages, const DependencySet &removedProvides,
    const std::unordered_set<std::string_view> &depsToIgnore, const std::unordered_set<std::string_view> &libsToIgnore)
{
    auto unresolvedPackages = std::unordered_map<PackageSpec, UnresolvedDependencies>();

    // determine new provides
    auto newProvides = DependencySet();
    auto newLibProvides = std::set<std::string>();
    for (const auto &newPackage : newPackages) {
        newProvides.add(Dependency(newPackage->name, newPackage->version), newPackage);
        for (const auto &newProvide : newPackage->provides) {
            newProvides.add(newProvide, newPackage);
        }
        for (const auto &newLibProvide : newPackage->libprovides) {
            newLibProvides.emplace(newLibProvide);
        }
    }

    // determine dependencies and "protected" database
    auto deps = std::vector<Database *>();
    const auto depOrder = config.computeDatabaseDependencyOrder(*this, false);
    if (auto *const dbs = std::get_if<std::vector<Database *>>(&depOrder)) {
        deps = std::move(*dbs);
    }
    if (auto *const protectedDb = config.findDatabase(protectedName(), arch)) {
        const auto protectedDepOrder = config.computeDatabaseDependencyOrder(*protectedDb, true);
        if (auto *const dbs = std::get_if<std::vector<Database *>>(&protectedDepOrder)) {
            for (auto *const db : *dbs) {
                if (std::find(deps.begin(), deps.end(), db) == deps.end()) {
                    deps.emplace_back(db);
                }
            }
        }
    }

    // check testing and base databases as well if this is a staging/testing database
    auto dbsToCheck = std::vector<Database *>();
    auto forStaging = false, forTesting = false;
    dbsToCheck.reserve(3);
    dbsToCheck.reserve(2);
    dbsToCheck.emplace_back(this);
    Database *stagingDb = nullptr, *testingDb = nullptr, *baseDb = nullptr;
    if (isStaging()) {
        stagingDb = this;
        forStaging = true;
        forTesting = true;
        testingDb = config.findDatabase(testingName(), arch);
        dbsToCheck.emplace_back(testingDb);
    } else if (isTesting()) {
        testingDb = this;
        forTesting = true;
    }
    if (testingDb && (baseDb = config.findDatabase(baseName(), arch))) {
        dbsToCheck.emplace_back(baseDb);
    }

    // compute list of libraries that are going to be removed by packages in staging/testing database
    auto removedLibs = std::set<std::string>();
    auto ri = deps.rbegin(), rend = deps.rend();
    while (stagingDb) {
        stagingDb->allPackages([&] (LibPkg::StorageID, std::shared_ptr<LibPkg::Package> &&package) {
            for (auto i = deps.rbegin(), end = deps.rend(); i != end; ++i) {
                auto *const db = *i;
                if (db->isStaging()) {
                    continue;
                }
                if (db->findPackage(package->name)) {
                    removedLibs.insert(package->libprovides.begin(), package->libprovides.end());
                    break;
                }
            }
            return false;
        });
        stagingDb = nullptr;
        for (; ri != rend; ++ri) {
            if ((*ri)->isStaging()) {
                stagingDb = *ri;
                break;
            }
        }
    }
    ri = deps.rbegin(), rend = deps.rend();
    while (testingDb) {
        testingDb->allPackages([&] (LibPkg::StorageID, std::shared_ptr<LibPkg::Package> &&package) {
            for (auto i = deps.rbegin(), end = deps.rend(); i != end; ++i) {
                auto *const db = *i;
                if (db->isStaging() || db->isTesting()) {
                    continue;
                }
                if (db->findPackage(package->name)) {
                    removedLibs.insert(package->libprovides.begin(), package->libprovides.end());
                    break;
                }
            }
            return false;
        });
        testingDb = nullptr;
        for (; ri != rend; ++ri) {
            if ((*ri)->isTesting()) {
                testingDb = *ri;
                break;
            }
        }
    }

    // check all relevant databases
    for (auto *dbToCheck : dbsToCheck) {
        dbToCheck->detectUnresolvedPackages(deps, newProvides, newLibProvides, removedProvides, removedLibs, depsToIgnore, libsToIgnore, forStaging, forTesting, unresolvedPackages);
    }
    return unresolvedPackages;
}

void Database::detectUnresolvedPackages(const std::vector<Database *> &deps, const DependencySet &newProvides, const std::set<std::string> &newLibProvides, const DependencySet &removedProvides, const std::set<string> &removedLibs, const std::unordered_set<std::string_view> &depsToIgnore, const std::unordered_set<std::string_view> &libsToIgnore, bool forStaging, bool forTesting, std::unordered_map<PackageSpec, UnresolvedDependencies> &unresolvedPackages)
{
    // check whether all required dependencies are still provided
    for (auto txn = m_storage->requiredDeps.getROTransaction(); const auto &requiredDep : txn) {
        // skip dependencies to ignore
        if (depsToIgnore.find(requiredDep.name) != depsToIgnore.end()) {
            continue;
        }

        // skip if new packages provide dependency
        if (newProvides.provides(requiredDep)) {
            continue;
        }

        // skip if db provides dependency
        if (!removedProvides.provides(requiredDep) && provides(requiredDep)) {
            continue;
        }

        // skip if dependency is provided by a database this database depends on or the protected version of this db
        auto providedByAnotherDb = false;
        for (auto i = deps.rbegin(), end = deps.rend(); i != end; ++i) {
            auto *const db = *i;
            if ((providedByAnotherDb = db->provides(requiredDep))) {
                break;
            }
            // skip checking further databases if the package is present but doesn't satisfy the dependencies
            if (db->findPackage(requiredDep.name)) {
                break;
            }
        }
        if (providedByAnotherDb) {
            continue;
        }

        // add packages to list of unresolved packages
        for (const auto &affectedPackageID : requiredDep.relevantPackages) {
            const auto affectedPackage = findPackage(affectedPackageID);
            unresolvedPackages[GenericPackageSpec(affectedPackageID, affectedPackage)].deps.emplace_back(requiredDep);
        }
    }

    // check whether all required libraries are still provided
    for (auto txn = m_storage->requiredLibs.getROTransaction(); const auto &requiredLib : txn) {

        // skip libs to ignore
        if (libsToIgnore.find(requiredLib.name) != libsToIgnore.end()) {
            continue;
        }

        // skip if new packages provide dependency
        if (newLibProvides.find(requiredLib.name) != newLibProvides.end()) {
            continue;
        }

        // skip if db provides dependency
        if (provides(requiredLib.name)) {
            continue;
        }

        // skip if dependency is provided by a database this database depends on or the protected version of this db
        const auto removed = removedLibs.find(requiredLib.name) == removedLibs.end();
        auto providedByAnotherDb = false;
        for (auto i = deps.rbegin(), end = deps.rend(); i != end; ++i) {
            auto *const db = *i;
            if (((forTesting && (!db->isStaging() && !db->isTesting())) || (forStaging && (!db->isStaging()))) && removed) {
                continue;
            }
            if ((providedByAnotherDb = db->provides(requiredLib.name))) {
                break;
            }
        }
        if (providedByAnotherDb) {
            continue;
        }

        // skip DLLs known to be provided by Windows (but can not be detected as provides of mingw-w64-crt)
        if (requiredLib.name.find("api-ms-win") != std::string::npos && requiredLib.name.ends_with(".dll")) {
            continue;
        }

        // add packages to list of unresolved packages
        for (const auto &affectedPackageID : requiredLib.relevantPackages) {
            const auto affectedPackage = findPackage(affectedPackageID);
            unresolvedPackages[GenericPackageSpec(affectedPackageID, affectedPackage)].libs.emplace_back(requiredLib.name);
        }
    }
}

LibPkg::PackageUpdates LibPkg::Database::checkForUpdates(const std::vector<LibPkg::Database *> &updateSources, UpdateCheckOptions options)
{
    auto results = PackageUpdates();
    allPackages([&](StorageID myPackageID, std::shared_ptr<Package> &&package) {
        auto regularName = std::string();
        auto used = false;
        if (options & UpdateCheckOptions::ConsiderRegularPackage) {
            const auto decomposedName = package->decomposeName();
            if ((!decomposedName.targetPrefix.empty() || !decomposedName.vcsSuffix.empty()) && !decomposedName.isVcsPackage()) {
                regularName = decomposedName.actualName;
            }
        }
        auto foundPackage = false;
        for (auto *const updateSource : updateSources) {
            const auto [updatePackageID, updatePackage] = updateSource->findPackageWithID(package->name);
            if (!updatePackage) {
                continue;
            }
            foundPackage = true;
            const auto versionDiff = package->compareVersion(*updatePackage);
            std::vector<PackageUpdate> *list = nullptr;
            switch (versionDiff) {
            case PackageVersionComparison::SoftwareUpgrade:
                list = &results.versionUpdates;
                break;
            case PackageVersionComparison::PackageUpgradeOnly:
                list = &results.packageUpdates;
                break;
            case PackageVersionComparison::NewerThanSyncVersion:
                list = &results.downgrades;
                break;
            default:;
            }
            if (list) {
                list->emplace_back(
                    PackageSearchResult(*this, package, myPackageID), PackageSearchResult(*updateSource, updatePackage, updatePackageID));
                used = true;
            }
        }
        if (!foundPackage) {
            results.orphans.emplace_back(PackageSearchResult(*this, package, myPackageID));
            used = true;
        }
        if (!regularName.empty()) {
            for (auto *const updateSource : updateSources) {
                const auto [updatePackageID, updatePackage] = updateSource->findPackageWithID(regularName);
                if (!updatePackage) {
                    continue;
                }
                const auto versionDiff = package->compareVersion(*updatePackage);
                std::vector<PackageUpdate> *list = nullptr;
                switch (versionDiff) {
                case PackageVersionComparison::SoftwareUpgrade:
                    list = &results.versionUpdates;
                    break;
                case PackageVersionComparison::PackageUpgradeOnly:
                    list = &results.packageUpdates;
                    break;
                case PackageVersionComparison::NewerThanSyncVersion:
                    list = &results.downgrades;
                    break;
                default:;
                }
                if (list) {
                    list->emplace_back(
                        PackageSearchResult(*this, package, myPackageID), PackageSearchResult(*updateSource, updatePackage, updatePackageID));
                    used = true;
                }
            }
        }
        if (used) {
            package.reset();
        }
        return false;
    });
    return results;
}

PackageLocation Database::locatePackage(const string &packageName) const
{
    PackageLocation res;
    if (packageName.empty()) {
        return res;
    }
    res.pathWithinRepo = localPkgDir % '/' + packageName;
    try {
        switch (std::filesystem::symlink_status(res.pathWithinRepo).type()) {
        case std::filesystem::file_type::regular:
            res.exists = true;
            break;
        case std::filesystem::file_type::symlink:
            res.storageLocation = std::filesystem::read_symlink(res.pathWithinRepo);
            if (res.storageLocation.is_absolute()) {
                res.exists = std::filesystem::is_regular_file(res.storageLocation);
                break;
            }
            res.storageLocation = argsToString(localPkgDir, '/', res.storageLocation);
            if ((res.exists = std::filesystem::is_regular_file(res.storageLocation))) {
                res.storageLocation = std::filesystem::canonical(res.storageLocation);
            }
            break;
        default:
            break;
        }
    } catch (std::filesystem::filesystem_error &e) {
        res.error = std::move(e);
    }
    return res;
}

std::string Database::filesPathFromRegularPath() const
{
    if (path.empty()) {
        return std::string();
    }
    const auto ext = path.rfind(".db");
    return ext == std::string::npos ? path : argsToString(std::string_view(path.data(), ext), ".files");
}

bool Database::isStaging(std::string_view name)
{
    return name.ends_with("-staging");
}

bool Database::isTesting(std::string_view name)
{
    return name.ends_with("-testing");
}

bool Database::isDebug(std::string_view name)
{
    return name.ends_with("-debug");
}

std::string_view Database::specialSuffix(std::string_view name)
{
    static constexpr auto suffixLength = std::string_view("-staging").size();
    return isStaging(name) || isTesting(name) ? std::string_view(name.data() + name.size() - suffixLength) : std::string_view();
}

std::string_view Database::nameWithoutDebugSuffix() const
{
    return isDebug(name) ? std::string_view(name.data(), name.size() - 6) : std::string_view(name);
}

std::pair<string_view, string_view> Database::baseNameAndSuffix() const
{
    const auto name = nameWithoutDebugSuffix();
    const auto suffix = specialSuffix(name);
    return std::pair(std::string_view(name.data(), name.size() - suffix.size()), suffix);
}

string_view Database::baseName() const
{
    return baseNameAndSuffix().first;
}

std::string Database::stagingName() const
{
    return argsToString(baseName(), "-staging");
}

std::string Database::testingName() const
{
    return argsToString(baseName(), "-testing");
}

std::string Database::protectedName() const
{
    const auto [baseName, suffix] = baseNameAndSuffix();
    return argsToString(baseName, "-protected", suffix);
}

std::string Database::debugName() const
{
    return name + "-debug";
}

PackageUpdaterPrivate::PackageUpdaterPrivate(DatabaseStorage &storage, bool clear)
    : clear(clear)
    , lock(storage.updateMutex)
    , packagesTxn(storage.packages.getRWTransaction())
{
}

void PackageUpdaterPrivate::update(const PackageCache::StoreResult &res, const std::shared_ptr<Package> &package)
{
    if (!res.id) {
        return;
    }
    handledIds.emplace(res.id);
    update(res.id, false, package);
    if (!clear && res.oldEntry) {
        update(res.id, true, res.oldEntry);
    }
}

void PackageUpdaterPrivate::update(const StorageID packageID, bool removed, const std::shared_ptr<Package> &package)
{
    addDependency(packageID, Dependency(package->name, package->version), removed, affectedProvidedDeps);
    for (const auto &dependency : package->provides) {
        addDependency(packageID, dependency, removed, affectedProvidedDeps);
    }
    for (const auto &lib : package->libprovides) {
        addLibrary(packageID, lib, removed, affectedProvidedLibs);
    }
    for (const auto &dependency : package->dependencies) {
        addDependency(packageID, dependency, removed, affectedRequiredDeps);
    }
    for (const auto &dependency : package->optionalDependencies) {
        addDependency(packageID, dependency, removed, affectedRequiredDeps);
    }
    for (const auto &lib : package->libdepends) {
        addLibrary(packageID, lib, removed, affectedRequiredLibs);
    }
}

template <typename Dependency, typename MappedType, typename Txn>
static void submitExistingDependency(StorageID id, Dependency &existingDependency, MappedType &affected, Txn &txn)
{
    auto &pkgs = existingDependency.relevantPackages;
    auto change = false;
    for (auto &toRemove : affected.removedPackages) {
        change = pkgs.erase(toRemove) || change;
    }
    auto size = pkgs.size();
    pkgs.merge(affected.newPackages);
    change = change || pkgs.size() != size;
    if (change) {
        txn.put(existingDependency, id);
    }
}

void PackageUpdaterPrivate::submit(const std::string &dependencyName, AffectedDeps::mapped_type &affected, DependencyStorage::RWTransaction &txn)
{
    for (auto [i, end] = txn.equal_range<0>(dependencyName); i != end; ++i) {
        auto &existingDependency = i.value();
        if (static_cast<const Dependency &>(existingDependency).version != affected.version) {
            continue;
        }
        submitExistingDependency(i.getID(), existingDependency, affected, txn);
        return;
    }
    auto newDependency = DatabaseDependency(dependencyName, affected.version, affected.mode);
    newDependency.relevantPackages.swap(affected.newPackages);
    txn.put(newDependency);
}

void PackageUpdaterPrivate::submit(const std::string &libraryName, AffectedLibs::mapped_type &affected, LibraryDependencyStorage::RWTransaction &txn)
{
    for (auto [i, end] = txn.equal_range<0>(libraryName); i != end; ++i) {
        submitExistingDependency(i.getID(), i.value(), affected, txn);
        return;
    }
    auto newDependency = DatabaseLibraryDependency(libraryName);
    newDependency.relevantPackages.swap(affected.newPackages);
    txn.put(newDependency);
}
PackageUpdaterPrivate::AffectedDeps::iterator PackageUpdaterPrivate::findDependency(const Dependency &dependency, AffectedDeps &affected)
{
    for (auto range = affected.equal_range(dependency.name); range.first != range.second; ++range.first) {
        if (dependency.version == range.first->second.version) {
            return range.first;
        }
    }
    return affected.end();
}

void PackageUpdaterPrivate::addDependency(StorageID packageID, const Dependency &dependency, bool removed, AffectedDeps &affected)
{
    if (dependency.name.empty()) {
        return;
    }
    auto iterator = findDependency(dependency, affected);
    if (iterator == affected.end()) {
        iterator = affected.insert(AffectedDeps::value_type(dependency.name, AffectedDeps::mapped_type()));
        iterator->second.version = dependency.version;
        iterator->second.mode = dependency.mode;
    }
    if (!removed) {
        iterator->second.newPackages.emplace(packageID);
    } else {
        iterator->second.removedPackages.emplace(packageID);
    }
}

void PackageUpdaterPrivate::addLibrary(StorageID packageID, const std::string &libraryName, bool removed, AffectedLibs &affected)
{
    if (libraryName.empty()) {
        return;
    }
    if (auto &affectedPackages = affected[libraryName]; !removed) {
        affectedPackages.newPackages.emplace(packageID);
    } else {
        affectedPackages.removedPackages.emplace(packageID);
    }
}

PackageUpdater::PackageUpdater(Database &database, bool clear)
    : m_database(database)
    , m_d(std::make_unique<PackageUpdaterPrivate>(*m_database.m_storage, clear))
{
}

PackageUpdater::~PackageUpdater()
{
}

PackageSpec LibPkg::PackageUpdater::findPackageWithID(const std::string &packageName)
{
    return m_database.m_storage->packageCache.retrieve(*m_database.m_storage, &m_d->packagesTxn, packageName);
}

/*!
 * \brief Begins updating the existing \a package with the specified \a packageID.
 * \remarks
 * - Do not use this function when PackageUpdate has been constructed with clear=true.
 * - Call this method before modifying \a package. Then modify the package. Then call endUpdate().
 */
void PackageUpdater::beginUpdate(StorageID packageID, const std::shared_ptr<Package> &package)
{
    m_d->update(packageID, true, package);
}

/*!
 * \brief Ends updating the existing \a package with the specified \a packageID.
 * \remarks
 * - Do not use this function when PackageUpdate has been constructed with clear=true.
 * - Call this method after callsing beginUpdate() and modifying \a package.
 */
void PackageUpdater::endUpdate(StorageID packageID, const std::shared_ptr<Package> &package)
{
    const auto &storage = m_database.m_storage;
    storage->packageCache.store(*m_database.m_storage, m_d->packagesTxn, packageID, package);
    m_d->update(packageID, false, package);
}

/*!
 * \brief Updates the specified \a package. The \a package may or may not exist.
 * \remarks If the package exists then provides/deps from the existing package are taken over if appropriate.
 */
StorageID PackageUpdater::update(const std::shared_ptr<Package> &package)
{
    const auto &storage = m_database.m_storage;
    const auto res = storage->packageCache.store(*m_database.m_storage, m_d->packagesTxn, package);
    m_d->update(res, package);
    return res.id;
}

/*!
 * \brief Returns the package IDs handled so far.
 * \remarks Those IDs might not have been committed.
 */
const std::unordered_set<StorageID> &PackageUpdater::handledIDs() const
{
    return m_d->handledIds;
}

/*!
 * \brief Returns the number of packages that was present just before committing changes.
 */
std::size_t PackageUpdater::packageCount() const
{
    return m_d->packageCountBeforeCommit;
}

bool PackageUpdater::insertFromDatabaseFile(const std::string &databaseFilePath)
{
    LibPkg::Package::fromDatabaseFile(databaseFilePath, [this](const std::shared_ptr<LibPkg::Package> &package) {
        if (const auto [id, existingPackage] = findPackageWithID(package->name); existingPackage) {
            package->addDepsAndProvidesFromOtherPackage(*existingPackage);
        }
        update(package);
        return false;
    });
    return false;
}

void PackageUpdater::commit()
{
    auto &storage = *m_database.m_storage;
    auto &pkgTxn = m_d->packagesTxn;
    auto txnHandle = pkgTxn.getTransactionHandle();
    if (m_d->clear) {
        const auto &toPreserve = m_d->handledIds;
        const auto end = pkgTxn.end();
        for (auto i = pkgTxn.begin(); i != end; ++i) {
            if (!toPreserve.contains(i.getID())) {
                storage.packageCache.invalidateCacheOnly(storage, i.value().name);
                i.del();
            }
        }
    }
    {
        auto txn = storage.providedDeps.getRWTransaction(txnHandle);
        if (m_d->clear) {
            txn.clear();
        }
        for (auto &[dependencyName, affected] : m_d->affectedProvidedDeps) {
            m_d->submit(dependencyName, affected, txn);
        }
    }
    {
        auto txn = storage.requiredDeps.getRWTransaction(txnHandle);
        if (m_d->clear) {
            txn.clear();
        }
        for (auto &[dependencyName, affected] : m_d->affectedRequiredDeps) {
            m_d->submit(dependencyName, affected, txn);
        }
    }
    {
        auto txn = storage.providedLibs.getRWTransaction(txnHandle);
        if (m_d->clear) {
            txn.clear();
        }
        for (auto &[libraryName, affected] : m_d->affectedProvidedLibs) {
            m_d->submit(libraryName, affected, txn);
        }
    }
    {
        auto txn = storage.requiredLibs.getRWTransaction(txnHandle);
        if (m_d->clear) {
            txn.clear();
        }
        for (auto &[libraryName, affected] : m_d->affectedRequiredLibs) {
            m_d->submit(libraryName, affected, txn);
        }
    }
    m_d->packageCountBeforeCommit = pkgTxn.size();
    pkgTxn.commit();
    m_d->lock.unlock();
}

} // namespace LibPkg

namespace ReflectiveRapidJSON {

namespace JsonReflector {

template <>
void push<LibPkg::PackageSearchResult>(
    const LibPkg::PackageSearchResult &reflectable, RAPIDJSON_NAMESPACE::Value &value, RAPIDJSON_NAMESPACE::Document::AllocatorType &allocator)
{
    // customize serialization of PackageSearchResult to render as if it was pkg itself with an additional db property
    value.SetObject();
    auto obj = value.GetObject();
    auto &pkg = reflectable.pkg;
    push(pkg->name, "name", obj, allocator);
    push(pkg->origin, "origin", obj, allocator);
    push(pkg->timestamp, "timestamp", obj, allocator);
    push(pkg->version, "version", obj, allocator);
    push(pkg->description, "description", obj, allocator);
    if (!pkg->arch.empty()) {
        push(pkg->arch, "arch", obj, allocator);
    }
    if (!pkg->buildDate.isNull()) {
        push(pkg->buildDate, "buildDate", obj, allocator);
    }
    if (!pkg->archs.empty()) {
        push(pkg->archs, "archs", obj, allocator);
    }
    if (const auto *const dbInfo = std::get_if<LibPkg::DatabaseInfo>(&reflectable.db)) {
        if (!dbInfo->name.empty()) {
            push(dbInfo->name, "db", obj, allocator);
        }
        if (!dbInfo->arch.empty()) {
            push(dbInfo->arch, "dbArch", obj, allocator);
        }
    } else if (const auto *const db = std::get<LibPkg::Database *>(reflectable.db)) {
        push(db->name, "db", obj, allocator);
        if (!db->arch.empty()) {
            push(db->arch, "dbArch", obj, allocator);
        }
    }
}

template <>
void pull<LibPkg::PackageSearchResult>(LibPkg::PackageSearchResult &reflectable,
    const RAPIDJSON_NAMESPACE::GenericValue<RAPIDJSON_NAMESPACE::UTF8<char>> &value, JsonDeserializationErrors *errors)
{
    if (!value.IsObject()) {
        if (errors) {
            errors->reportTypeMismatch<LibPkg::PackageSearchResult>(value.GetType());
        }
        return;
    }

    auto obj = value.GetObject();
    auto &pkg = reflectable.pkg;
    if (!pkg) {
        pkg = make_shared<LibPkg::Package>();
    }
    ReflectiveRapidJSON::JsonReflector::pull(pkg->name, "name", obj, errors);
    ReflectiveRapidJSON::JsonReflector::pull(pkg->origin, "origin", obj, errors);
    ReflectiveRapidJSON::JsonReflector::pull(pkg->timestamp, "timestamp", obj, errors);
    ReflectiveRapidJSON::JsonReflector::pull(pkg->version, "version", obj, errors);
    ReflectiveRapidJSON::JsonReflector::pull(pkg->description, "description", obj, errors);
    ReflectiveRapidJSON::JsonReflector::pull(pkg->arch, "arch", obj, errors);
    ReflectiveRapidJSON::JsonReflector::pull(pkg->buildDate, "buildDate", obj, errors);
    ReflectiveRapidJSON::JsonReflector::pull(pkg->archs, "archs", obj, errors);
    auto &dbInfo = reflectable.db.emplace<LibPkg::DatabaseInfo>();
    ReflectiveRapidJSON::JsonReflector::pull(dbInfo.name, "db", obj, errors);
    ReflectiveRapidJSON::JsonReflector::pull(dbInfo.arch, "dbArch", obj, errors);
}

template <>
void push<LibPkg::PackageBaseSearchResult>(
    const LibPkg::PackageBaseSearchResult &reflectable, RAPIDJSON_NAMESPACE::Value &value, RAPIDJSON_NAMESPACE::Document::AllocatorType &allocator)
{
    // serialize PackageBaseSearchResult object in accordance with PackageSearchResult
    value.SetObject();
    auto obj = value.GetObject();
    if (auto &pkg = reflectable.pkg) {
        push(*pkg, obj, allocator);
    }
    if (const auto &db = reflectable.db) {
        push(db->name, "db", obj, allocator);
        if (!db->arch.empty()) {
            push(db->arch, "dbArch", obj, allocator);
        }
    }
}

template <>
void pull<LibPkg::PackageBaseSearchResult>(LibPkg::PackageBaseSearchResult &reflectable,
    const RAPIDJSON_NAMESPACE::GenericValue<RAPIDJSON_NAMESPACE::UTF8<char>> &value, JsonDeserializationErrors *errors)
{
    CPP_UTILITIES_UNUSED(reflectable)
    CPP_UTILITIES_UNUSED(value)
    CPP_UTILITIES_UNUSED(errors)
    throw std::logic_error("Attempt to deserialize LibPkg::PackageBaseSearchResult");
}

template <>
void push<LibPkg::AtomicDateTime>(
    const LibPkg::AtomicDateTime &reflectable, RAPIDJSON_NAMESPACE::Value &value, RAPIDJSON_NAMESPACE::Document::AllocatorType &allocator)
{
    push<CppUtilities::DateTime>(reflectable.load(), value, allocator);
}

template <>
void pull<LibPkg::AtomicDateTime>(LibPkg::AtomicDateTime &reflectable,
    const RAPIDJSON_NAMESPACE::GenericValue<RAPIDJSON_NAMESPACE::UTF8<char>> &value, JsonDeserializationErrors *errors)
{
    auto d = CppUtilities::DateTime();
    pull<CppUtilities::DateTime>(d, value, errors);
    reflectable.store(d);
}

} // namespace JsonReflector

namespace BinaryReflector {

template <>
void writeCustomType<LibPkg::PackageSearchResult>(
    BinarySerializer &serializer, const LibPkg::PackageSearchResult &packageSearchResult, BinaryVersion version)
{
    if (const auto *const dbInfo = std::get_if<LibPkg::DatabaseInfo>(&packageSearchResult.db)) {
        serializer.write(dbInfo->name, version);
    } else if (const auto *const db = std::get<LibPkg::Database *>(packageSearchResult.db)) {
        serializer.write(db->name, version);
    } else {
        serializer.write(std::string(), version);
    }
    serializer.write(packageSearchResult.pkg, version);
}

template <>
BinaryVersion readCustomType<LibPkg::PackageSearchResult>(
    BinaryDeserializer &deserializer, LibPkg::PackageSearchResult &packageSearchResult, BinaryVersion version)
{
    deserializer.read(packageSearchResult.db.emplace<LibPkg::DatabaseInfo>().name, version);
    deserializer.read(packageSearchResult.pkg, version);
    return 0;
}

template <> void writeCustomType<LibPkg::AtomicDateTime>(BinarySerializer &serializer, const LibPkg::AtomicDateTime &dateTime, BinaryVersion version)
{
    writeCustomType<CppUtilities::DateTime>(serializer, dateTime.load(), version);
}

template <>
BinaryVersion readCustomType<LibPkg::AtomicDateTime>(BinaryDeserializer &deserializer, LibPkg::AtomicDateTime &dateTime, BinaryVersion version)
{
    auto d = CppUtilities::DateTime();
    auto v = readCustomType<CppUtilities::DateTime>(deserializer, d, version);
    dateTime.store(d);
    return v;
}

} // namespace BinaryReflector

} // namespace ReflectiveRapidJSON
