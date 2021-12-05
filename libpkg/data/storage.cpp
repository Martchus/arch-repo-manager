#include "./storageprivate.h"

#include <c++utilities/conversion/stringbuilder.h>

using namespace CppUtilities;

namespace LibPkg {

StorageDistribution::StorageDistribution(const char *path, std::uint32_t maxDbs)
{
    m_env = getMDBEnv(path, MDB_NOSUBDIR, 0600, maxDbs);
}

PackageSpec PackageCache::retrieve(DatabaseStorage &databaseStorage, const std::string &packageName)
{
    // check for package in cache
    const auto ref = PackageCacheRef(databaseStorage, packageName);
    const auto lock = std::unique_lock(m_mutex);
    auto &cacheEntry = m_packages.findOrCreate(ref);
    if (cacheEntry.package) {
        return PackageSpec(cacheEntry.id, cacheEntry.package);
    }
    // check for package in storage, populate cache entry
    cacheEntry.package = std::make_unique<Package>();
    auto txn = databaseStorage.packages.getROTransaction();
    if ((cacheEntry.id = txn.get<0>(packageName, *cacheEntry.package))) {
        cacheEntry.ref.packageName = &cacheEntry.package->name;
        return PackageSpec(cacheEntry.id, cacheEntry.package);
    }
    m_packages.undo();
    return PackageSpec(0, std::shared_ptr<Package>());
}

PackageCache::StoreResult PackageCache::store(DatabaseStorage &databaseStorage, const std::shared_ptr<Package> &package, bool force)
{
    // check for package in cache
    const auto ref = PackageCacheRef(databaseStorage, package->name);
    auto res = PackageCache::StoreResult();
    auto lock = std::unique_lock(m_mutex);
    auto &cacheEntry = m_packages.findOrCreate(ref);
    if (cacheEntry.package == package && !force) {
        // do nothing if cached package is the same as specified one
        res.id = cacheEntry.id;
        return res;
    } else if (cacheEntry.package) {
        // retain certain information obtained from package contents if this is actually the same package as before
        package->addDepsAndProvidesFromOtherPackage(*(res.oldPackage = cacheEntry.package));
    } else {
        cacheEntry.package = std::make_shared<Package>();
    }
    // check for package in storage
    auto txn = databaseStorage.packages.getRWTransaction();
    if (!res.oldPackage && (cacheEntry.id = txn.get<0>(package->name, *cacheEntry.package))) {
        package->addDepsAndProvidesFromOtherPackage(*(res.oldPackage = cacheEntry.package));
    }
    // update cache entry
    cacheEntry.ref.packageName = &package->name;
    cacheEntry.package = package;
    // update package in storage
    cacheEntry.id = txn.put(*package, cacheEntry.id);
    txn.commit();
    res.id = cacheEntry.id;
    res.updated = true;
    return res;
}

PackageCache::StoreResult PackageCache::store(
    DatabaseStorage &databaseStorage, PackageStorage::RWTransaction &txn, const std::shared_ptr<Package> &package)
{
    // check for package in cache
    const auto ref = PackageCacheRef(databaseStorage, package->name);
    auto res = PackageCache::StoreResult();
    auto lock = std::unique_lock(m_mutex);
    auto &cacheEntry = m_packages.findOrCreate(ref);
    if (cacheEntry.package) {
        // retain certain information obtained from package contents if this is actually the same package as before
        res.id = cacheEntry.id;
        package->addDepsAndProvidesFromOtherPackage(*(res.oldPackage = cacheEntry.package));
    } else {
        // check for package in storage
        cacheEntry.package = std::make_shared<Package>();
        if ((cacheEntry.id = txn.get<0>(package->name, *cacheEntry.package))) {
            package->addDepsAndProvidesFromOtherPackage(*(res.oldPackage = cacheEntry.package));
        }
    }
    // update cache entry
    cacheEntry.ref.packageName = &package->name;
    cacheEntry.package = package;
    // update package in storage
    res.id = cacheEntry.id = txn.put(*package, cacheEntry.id);
    res.updated = true;
    return res;
}

bool PackageCache::invalidate(DatabaseStorage &databaseStorage, const std::string &packageName)
{
    // remove package from cache
    const auto ref = PackageCacheRef(databaseStorage, packageName);
    auto lock = std::unique_lock(m_mutex);
    m_packages.erase(ref);
    lock.unlock();
    // remove package from storage
    auto txn = databaseStorage.packages.getRWTransaction();
    if (auto i = txn.find<0>(packageName); i != txn.end()) {
        i.del();
        txn.commit();
        return true;
    }
    return false;
}

void PackageCache::clear(DatabaseStorage &databaseStorage)
{
    clearCacheOnly(databaseStorage);
    auto packagesTxn = databaseStorage.packages.getRWTransaction();
    packagesTxn.clear();
    packagesTxn.commit();
    auto providedDepsTxn = databaseStorage.providedDeps.getRWTransaction();
    providedDepsTxn.clear();
    providedDepsTxn.commit();
    auto requiredDepsTxn = databaseStorage.requiredDeps.getRWTransaction();
    requiredDepsTxn.clear();
    requiredDepsTxn.commit();
    auto providedLibsTxn = databaseStorage.providedLibs.getRWTransaction();
    providedLibsTxn.clear();
    providedLibsTxn.commit();
    auto requiredLibsTxn = databaseStorage.requiredLibs.getRWTransaction();
    requiredLibsTxn.clear();
    requiredLibsTxn.commit();
}

void PackageCache::clearCacheOnly(DatabaseStorage &databaseStorage)
{
    const auto lock = std::unique_lock(m_mutex);
    m_packages.clear(databaseStorage);
}

DatabaseStorage::DatabaseStorage(const std::shared_ptr<MDBEnv> &env, PackageCache &packageCache, std::string_view uniqueDatabaseName)
    : packageCache(packageCache)
    , packages(env, argsToString(uniqueDatabaseName, "_packages"))
    , providedDeps(env, argsToString(uniqueDatabaseName, "_provides"))
    , requiredDeps(env, argsToString(uniqueDatabaseName, "_requires"))
    , providedLibs(env, argsToString(uniqueDatabaseName, "_libprovides"))
    , requiredLibs(env, argsToString(uniqueDatabaseName, "_librequires"))
    , m_env(env)
{
}

std::size_t hash_value(const PackageCacheRef &ref)
{
    const auto hasher1 = boost::hash<const LibPkg::DatabaseStorage *>();
    const auto hasher2 = boost::hash<std::string>();
    return ((hasher1(ref.databaseStorage) ^ (hasher2(*ref.packageName) << 1)) >> 1);
}

PackageCacheEntry &RecentlyUsedPackages::findOrCreate(const PackageCacheRef &ref)
{
    const auto &index = m_packages.get<PackageCacheRef>();
    if (auto i = index.find(ref); i != index.end()) {
        m_packages.relocate(m_packages.begin(), m_packages.project<0>(i));
        return i.get_node()->value();
    }
    const auto [i, newItem] = m_packages.emplace_front(ref);
    if (!newItem) {
        m_packages.relocate(m_packages.begin(), i);
    } else if (m_packages.size() > m_limit) {
        m_packages.pop_back();
    }
    return i.get_node()->value();
}

std::size_t RecentlyUsedPackages::clear(const DatabaseStorage &databaseStorage)
{
    auto count = std::size_t();
    for (auto i = m_packages.begin(); i != m_packages.end();) {
        if (i->ref.databaseStorage == &databaseStorage) {
            i = m_packages.erase(i);
            ++count;
        } else {
            ++i;
        }
    }
    return count;
}

} // namespace LibPkg
