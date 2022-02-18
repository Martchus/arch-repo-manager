#include "./database.h"
#include "./config.h"
#include "./storageprivate.h"

#include "reflection/database.h"

#include <c++utilities/conversion/stringbuilder.h>

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

    explicit PackageUpdaterPrivate(DatabaseStorage &storage);
    void update(const PackageCache::StoreResult &res, const std::shared_ptr<Package> &package);
    void update(const StorageID packageID, bool removed, const std::shared_ptr<Package> &package);
    void submit(const std::string &dependencyName, AffectedDeps::mapped_type &affected, DependencyStorage::RWTransaction &txn);
    void submit(const std::string &libraryName, AffectedLibs::mapped_type &affected, LibraryDependencyStorage::RWTransaction &txn);

    PackageStorage::RWTransaction packagesTxn;
    AffectedDeps affectedProvidedDeps;
    AffectedDeps affectedRequiredDeps;
    AffectedLibs affectedProvidedLibs;
    AffectedLibs affectedRequiredLibs;

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

void Database::resetConfiguration()
{
    path.clear();
    filesPath.clear();
    mirrors.clear();
    usage = DatabaseUsage::None;
    signatureLevel = SignatureLevel::Default;
    arch = "x86_64";
    dependencies.clear();
    localPkgDir.clear();
    localDbDir.clear();
    syncFromMirror = false;
}

void Database::clearPackages()
{
    lastUpdate = DateTime();
    if (m_storage) {
        m_storage->packageCache.clear(*m_storage);
    }
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

void Database::removePackageDependencies(StorageID packageID, const std::shared_ptr<Package> &package)
{
    {
        auto txn = m_storage->providedDeps.getRWTransaction();
        removeDependency(txn, packageID, package->name);
        for (const auto &dep : package->provides) {
            removeDependency(txn, packageID, dep.name);
        }
        txn.commit();
    }
    {
        auto txn = m_storage->requiredDeps.getRWTransaction();
        for (const auto &dep : package->dependencies) {
            removeDependency(txn, packageID, dep.name);
        }
        for (const auto &dep : package->optionalDependencies) {
            removeDependency(txn, packageID, dep.name);
        }
        txn.commit();
    }
    {
        auto txn = m_storage->providedLibs.getRWTransaction();
        for (const auto &lib : package->libprovides) {
            removeLibDependency(txn, packageID, lib);
        }
        txn.commit();
    }
    {
        auto txn = m_storage->requiredLibs.getRWTransaction();
        for (const auto &lib : package->libdepends) {
            removeLibDependency(txn, packageID, lib);
        }
        txn.commit();
    }
}

static void addDependency(DependencyStorage::RWTransaction &txn, StorageID packageID, const std::string &dependencyName,
    const std::string &dependencyVersion, DependencyMode dependencyMode = DependencyMode::Any)
{
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

void Database::addPackageDependencies(StorageID packageID, const std::shared_ptr<Package> &package)
{
    {
        auto txn = m_storage->providedDeps.getRWTransaction();
        addDependency(txn, packageID, package->name, package->version);
        for (const auto &dep : package->provides) {
            addDependency(txn, packageID, dep.name, dep.version, dep.mode);
        }
        txn.commit();
    }
    {
        auto txn = m_storage->requiredDeps.getRWTransaction();
        for (const auto &dep : package->dependencies) {
            addDependency(txn, packageID, dep.name, dep.version, dep.mode);
        }
        for (const auto &dep : package->optionalDependencies) {
            addDependency(txn, packageID, dep.name, dep.version, dep.mode);
        }
        txn.commit();
    }
    {
        auto txn = m_storage->providedLibs.getRWTransaction();
        for (const auto &lib : package->libprovides) {
            addLibDependency(txn, packageID, lib);
        }
        txn.commit();
    }
    {
        auto txn = m_storage->requiredLibs.getRWTransaction();
        for (const auto &lib : package->libdepends) {
            addLibDependency(txn, packageID, lib);
        }
        txn.commit();
    }
}

void Database::allPackages(const PackageVisitor &visitor)
{
    auto txn = m_storage->packages.getROTransaction();
    for (auto i = txn.begin<std::shared_ptr>(); i != txn.end(); ++i) {
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
        if (visitor(packageName, [this, &txn, &i]() { return m_storage->packageCache.retrieve(*m_storage, &txn, i.value()); })) {
            return;
        }
    }
}

std::size_t Database::packageCount() const
{
    return m_storage->packages.getROTransaction().size();
}

void Database::providingPackages(const Dependency &dependency, bool reverse, const PackageVisitor &visitor)
{
    auto providesTxn = (reverse ? m_storage->requiredDeps : m_storage->providedDeps).getROTransaction();
    auto packagesTxn = m_storage->packages.getROTransaction();
    for (auto [i, end] = providesTxn.equal_range<0>(dependency.name); i != end; ++i) {
        const auto &providedDependency = i.value();
        const auto &asDependency = static_cast<const Dependency &>(providedDependency);
        if (!Dependency::matches(dependency.mode, dependency.version, asDependency.version)) {
            continue;
        }
        for (const auto packageID : providedDependency.relevantPackages) {
            const auto res = m_storage->packageCache.retrieve(*m_storage, &packagesTxn, packageID);
            if (res.pkg && visitor(packageID, res.pkg)) {
                return;
            }
        }
    }
}

void Database::providingPackages(const std::string &libraryName, bool reverse, const PackageVisitor &visitor)
{
    auto providesTxn = (reverse ? m_storage->requiredLibs : m_storage->providedLibs).getROTransaction();
    auto packagesTxn = m_storage->packages.getROTransaction();
    for (auto [i, end] = providesTxn.equal_range<0>(libraryName); i != end; ++i) {
        for (const auto packageID : i->relevantPackages) {
            const auto res = m_storage->packageCache.retrieve(*m_storage, &packagesTxn, packageID);
            if (res.pkg && visitor(packageID, res.pkg)) {
                return;
            }
        }
    }
}

bool Database::provides(const Dependency &dependency, bool reverse) const
{
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

void Database::removePackage(const std::string &packageName)
{
    const auto [packageID, package] = m_storage->packageCache.retrieve(*m_storage, packageName);
    if (package) {
        removePackageDependencies(packageID, package);
        m_storage->packageCache.invalidate(*m_storage, packageName);
    }
}

StorageID Database::updatePackage(const std::shared_ptr<Package> &package)
{
    const auto res = m_storage->packageCache.store(*m_storage, package, false);
    if (!res.updated) {
        return res.id;
    }
    if (res.oldEntry) {
        removePackageDependencies(res.id, res.oldEntry);
    }
    addPackageDependencies(res.id, package);
    return res.id;
}

StorageID Database::forceUpdatePackage(const std::shared_ptr<Package> &package)
{
    const auto res = m_storage->packageCache.store(*m_storage, package, true);
    if (res.oldEntry) {
        removePackageDependencies(res.id, res.oldEntry);
    }
    addPackageDependencies(res.id, package);
    return res.id;
}

void Database::replacePackages(const std::vector<std::shared_ptr<Package>> &newPackages, DateTime lastModified)
{
    for (const auto &package : newPackages) {
        if (const auto existingPackage = findPackage(package->name)) {
            package->addDepsAndProvidesFromOtherPackage(*existingPackage);
        }
    }
    clearPackages();
    auto updater = PackageUpdater(*this);
    for (const auto &package : newPackages) {
        updater.update(package);
    }
    updater.commit();
    lastUpdate = lastModified;
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
    if (auto *const protectedDb = config.findDatabase(name + "-protected", arch)) {
        deps.emplace_back(protectedDb);
    }

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
        for (const auto *db : deps) {
            if ((providedByAnotherDb = db->provides(requiredDep))) {
                break;
            }
        }
        if (providedByAnotherDb) {
            continue;
        }

        // add packages to list of unresolved packages
        for (const auto &affectedPackageID : requiredDep.relevantPackages) {
            const auto affectedPackage = findPackage(affectedPackageID);
            unresolvedPackages[PackageSpec(affectedPackageID, affectedPackage)].deps.emplace_back(requiredDep);
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
        auto providedByAnotherDb = false;
        for (const auto *db : deps) {
            if ((providedByAnotherDb = db->provides(requiredLib.name))) {
                break;
            }
        }
        if (providedByAnotherDb) {
            continue;
        }

        // add packages to list of unresolved packages
        for (const auto &affectedPackageID : requiredLib.relevantPackages) {
            const auto affectedPackage = findPackage(affectedPackageID);
            unresolvedPackages[PackageSpec(affectedPackageID, affectedPackage)].libs.emplace_back(requiredLib.name);
        }
    }

    return unresolvedPackages;
}

LibPkg::PackageUpdates LibPkg::Database::checkForUpdates(const std::vector<LibPkg::Database *> &updateSources, UpdateCheckOptions options)
{
    auto results = PackageUpdates();
    allPackages([&](StorageID myPackageID, const std::shared_ptr<Package> &package) {
        auto regularName = std::string();
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
            }
        }
        if (!foundPackage) {
            results.orphans.emplace_back(PackageSearchResult(*this, package, myPackageID));
        }
        if (regularName.empty()) {
            return false;
        }
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
            }
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
        res.error = move(e);
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

PackageUpdaterPrivate::PackageUpdaterPrivate(DatabaseStorage &storage)
    : packagesTxn(storage.packages.getRWTransaction())
{
}

void PackageUpdaterPrivate::update(const PackageCache::StoreResult &res, const std::shared_ptr<Package> &package)
{
    update(res.id, false, package);
    if (res.oldEntry) {
        update(res.id, true, res.oldEntry);
    }
}

void PackageUpdaterPrivate::update(const StorageID packageID, bool removed, const std::shared_ptr<Package> &package)
{
    addDependency(packageID, Dependency(package->name, package->version), removed, affectedProvidedDeps);
    for (const auto &dependeny : package->provides) {
        addDependency(packageID, dependeny, removed, affectedProvidedDeps);
    }
    for (const auto &lib : package->libprovides) {
        addLibrary(packageID, lib, removed, affectedProvidedLibs);
    }
    for (const auto &dependeny : package->dependencies) {
        addDependency(packageID, dependeny, removed, affectedRequiredDeps);
    }
    for (const auto &dependeny : package->optionalDependencies) {
        addDependency(packageID, dependeny, removed, affectedRequiredDeps);
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
    if (auto &affectedPackages = affected[libraryName]; !removed) {
        affectedPackages.newPackages.emplace(packageID);
    } else {
        affectedPackages.removedPackages.emplace(packageID);
    }
}

PackageUpdater::PackageUpdater(Database &database)
    : m_database(database)
    , m_d(std::make_unique<PackageUpdaterPrivate>(*m_database.m_storage))
{
}

PackageUpdater::~PackageUpdater()
{
}

LibPkg::PackageSpec LibPkg::PackageUpdater::findPackageWithID(const std::string &packageName)
{
    return m_database.m_storage->packageCache.retrieve(*m_database.m_storage, &m_d->packagesTxn, packageName);
}

StorageID PackageUpdater::update(const std::shared_ptr<Package> &package)
{
    const auto res = m_database.m_storage->packageCache.store(*m_database.m_storage, m_d->packagesTxn, package);
    m_d->update(res, package);
    return res.id;
}

void PackageUpdater::commit()
{
    m_d->packagesTxn.commit();
    {
        auto txn = m_database.m_storage->providedDeps.getRWTransaction();
        for (auto &[dependencyName, affected] : m_d->affectedProvidedDeps) {
            m_d->submit(dependencyName, affected, txn);
        }
        txn.commit();
    }
    {
        auto txn = m_database.m_storage->requiredDeps.getRWTransaction();
        for (auto &[dependencyName, affected] : m_d->affectedRequiredDeps) {
            m_d->submit(dependencyName, affected, txn);
        }
        txn.commit();
    }
    {
        auto txn = m_database.m_storage->providedLibs.getRWTransaction();
        for (auto &[libraryName, affected] : m_d->affectedProvidedLibs) {
            m_d->submit(libraryName, affected, txn);
        }
        txn.commit();
    }
    {
        auto txn = m_database.m_storage->requiredLibs.getRWTransaction();
        for (auto &[libraryName, affected] : m_d->affectedRequiredLibs) {
            m_d->submit(libraryName, affected, txn);
        }
        txn.commit();
    }
}

} // namespace LibPkg

namespace ReflectiveRapidJSON {

namespace JsonReflector {

template <>
LIBPKG_EXPORT void push<LibPkg::PackageSearchResult>(
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
    if (const auto &pkgInfo = pkg->packageInfo) {
        push(pkgInfo->arch, "arch", obj, allocator);
        push(pkgInfo->buildDate, "buildDate", obj, allocator);
    }
    if (!pkg->archs.empty()) {
        push(pkg->archs, "archs", obj, allocator);
    } else if (const auto &srcInfo = pkg->sourceInfo) {
        push(srcInfo->archs, "archs", obj, allocator);
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
LIBPKG_EXPORT void pull<LibPkg::PackageSearchResult>(LibPkg::PackageSearchResult &reflectable,
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
    auto &pkgInfo = pkg->packageInfo;
    if (!pkgInfo) {
        pkgInfo = make_unique<LibPkg::PackageInfo>();
    }
    ReflectiveRapidJSON::JsonReflector::pull(pkgInfo->arch, "arch", obj, errors);
    ReflectiveRapidJSON::JsonReflector::pull(pkgInfo->buildDate, "buildDate", obj, errors);
    ReflectiveRapidJSON::JsonReflector::pull(pkg->archs, "archs", obj, errors);
    auto &dbInfo = reflectable.db.emplace<LibPkg::DatabaseInfo>();
    ReflectiveRapidJSON::JsonReflector::pull(dbInfo.name, "db", obj, errors);
    ReflectiveRapidJSON::JsonReflector::pull(dbInfo.arch, "dbArch", obj, errors);
}

} // namespace JsonReflector

namespace BinaryReflector {

template <>
LIBPKG_EXPORT void writeCustomType<LibPkg::PackageSearchResult>(
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
LIBPKG_EXPORT BinaryVersion readCustomType<LibPkg::PackageSearchResult>(
    BinaryDeserializer &deserializer, LibPkg::PackageSearchResult &packageSearchResult, BinaryVersion version)
{
    deserializer.read(packageSearchResult.db.emplace<LibPkg::DatabaseInfo>().name, version);
    deserializer.read(packageSearchResult.pkg, version);
    return 0;
}

} // namespace BinaryReflector

} // namespace ReflectiveRapidJSON
