#include "./storageprivate.h"

#include <c++utilities/conversion/stringbuilder.h>

using namespace CppUtilities;

namespace LibPkg {

template <typename StorageEntryType> auto StorageCacheEntries<StorageEntryType>::findOrCreate(const Ref &ref) -> StorageEntry &
{
    const auto &index = m_entries.template get<Ref>();
    if (auto i = index.find(ref); i != index.end()) {
        m_entries.relocate(m_entries.begin(), m_entries.template project<0>(i));
        return i.get_node()->value();
    }
    const auto [i, newItem] = m_entries.emplace_front(ref);
    if (!newItem) {
        m_entries.relocate(m_entries.begin(), i);
    } else if (m_entries.size() > m_limit) {
        m_entries.pop_back();
    }
    return i.get_node()->value();
}

template <typename StorageEntryType> std::size_t StorageCacheEntries<StorageEntryType>::clear(const Storage &storage)
{
    auto count = std::size_t();
    for (auto i = m_entries.begin(); i != m_entries.end();) {
        if (i->ref.relatedStorage == &storage) {
            i = m_entries.erase(i);
            ++count;
        } else {
            ++i;
        }
    }
    return count;
}

template <typename StorageEntriesType, typename TransactionType, typename SpecType>
auto StorageCache<StorageEntriesType, TransactionType, SpecType>::retrieve(Storage &storage, const std::string &entryName) -> SpecType
{
    // check for package in cache
    const auto ref = typename Entries::Ref(storage, entryName);
    const auto lock = std::unique_lock(m_mutex);
    auto &cacheEntry = m_entries.findOrCreate(ref);
    if (cacheEntry.entry) {
        return PackageSpec(cacheEntry.id, cacheEntry.entry);
    }
    // check for package in storage, populate cache entry
    cacheEntry.entry = std::make_shared<Entry>();
    auto txn = storage.packages.getROTransaction();
    if ((cacheEntry.id = txn.template get<0>(entryName, *cacheEntry.entry))) {
        cacheEntry.ref.entryName = &cacheEntry.entry->name;
        return PackageSpec(cacheEntry.id, cacheEntry.entry);
    }
    m_entries.undo();
    return PackageSpec(0, std::shared_ptr<Entry>());
}

template <typename StorageEntriesType, typename TransactionType, typename SpecType>
auto StorageCache<StorageEntriesType, TransactionType, SpecType>::store(Storage &storage, const std::shared_ptr<Entry> &entry, bool force)
    -> StoreResult
{
    // check for package in cache
    const auto ref = typename Entries::Ref(storage, entry->name);
    auto res = StorageCache::StoreResult();
    auto lock = std::unique_lock(m_mutex);
    auto &cacheEntry = m_entries.findOrCreate(ref);
    if (cacheEntry.entry == entry && !force) {
        // do nothing if cached package is the same as specified one
        res.id = cacheEntry.id;
        return res;
    } else if (cacheEntry.entry) {
        // retain certain information obtained from package contents if this is actually the same package as before
        entry->addDepsAndProvidesFromOtherPackage(*(res.oldEntry = cacheEntry.entry));
    } else {
        cacheEntry.entry = std::make_shared<Entry>();
    }
    // check for package in storage
    auto txn = storage.packages.getRWTransaction();
    if (!res.oldEntry && (cacheEntry.id = txn.template get<0>(entry->name, *cacheEntry.entry))) {
        entry->addDepsAndProvidesFromOtherPackage(*(res.oldEntry = cacheEntry.entry));
    }
    // update cache entry
    cacheEntry.ref.entryName = &entry->name;
    cacheEntry.entry = entry;
    // update package in storage
    cacheEntry.id = txn.put(*entry, cacheEntry.id);
    txn.commit();
    res.id = cacheEntry.id;
    res.updated = true;
    return res;
}

template <typename StorageEntriesType, typename TransactionType, typename SpecType>
auto StorageCache<StorageEntriesType, TransactionType, SpecType>::store(Storage &storage, Txn &txn, const std::shared_ptr<Entry> &entry)
    -> StoreResult
{
    // check for package in cache
    const auto ref = typename Entries::Ref(storage, entry->name);
    auto res = StorageCache::StoreResult();
    auto lock = std::unique_lock(m_mutex);
    auto &cacheEntry = m_entries.findOrCreate(ref);
    if (cacheEntry.entry) {
        // retain certain information obtained from package contents if this is actually the same package as before
        res.id = cacheEntry.id;
        entry->addDepsAndProvidesFromOtherPackage(*(res.oldEntry = cacheEntry.entry));
    } else {
        // check for package in storage
        cacheEntry.entry = std::make_shared<Entry>();
        if ((cacheEntry.id = txn.template get<0>(entry->name, *cacheEntry.entry))) {
            entry->addDepsAndProvidesFromOtherPackage(*(res.oldEntry = cacheEntry.entry));
        }
    }
    // update cache entry
    cacheEntry.ref.entryName = &entry->name;
    cacheEntry.entry = entry;
    // update package in storage
    res.id = cacheEntry.id = txn.put(*entry, cacheEntry.id);
    res.updated = true;
    return res;
}

template <typename StorageEntriesType, typename TransactionType, typename SpecType>
bool StorageCache<StorageEntriesType, TransactionType, SpecType>::invalidate(Storage &storage, const std::string &entryName)
{
    // remove package from cache
    const auto ref = typename Entries::Ref(storage, entryName);
    auto lock = std::unique_lock(m_mutex);
    m_entries.erase(ref);
    lock.unlock();
    // remove package from storage
    auto txn = storage.packages.getRWTransaction();
    if (auto i = txn.template find<0>(entryName); i != txn.end()) {
        i.del();
        txn.commit();
        return true;
    }
    return false;
}

template <typename StorageEntriesType, typename TransactionType, typename SpecType>
void StorageCache<StorageEntriesType, TransactionType, SpecType>::clear(Storage &storage)
{
    clearCacheOnly(storage);
    auto packagesTxn = storage.packages.getRWTransaction();
    packagesTxn.clear();
    packagesTxn.commit();
    auto providedDepsTxn = storage.providedDeps.getRWTransaction();
    providedDepsTxn.clear();
    providedDepsTxn.commit();
    auto requiredDepsTxn = storage.requiredDeps.getRWTransaction();
    requiredDepsTxn.clear();
    requiredDepsTxn.commit();
    auto providedLibsTxn = storage.providedLibs.getRWTransaction();
    providedLibsTxn.clear();
    providedLibsTxn.commit();
    auto requiredLibsTxn = storage.requiredLibs.getRWTransaction();
    requiredLibsTxn.clear();
    requiredLibsTxn.commit();
}

template <typename StorageEntriesType, typename TransactionType, typename SpecType>
void StorageCache<StorageEntriesType, TransactionType, SpecType>::clearCacheOnly(Storage &storage)
{
    const auto lock = std::unique_lock(m_mutex);
    m_entries.clear(storage);
}

template struct StorageCacheRef<DatabaseStorage, Package>;
template struct StorageCacheEntry<PackageCacheRef, Package>;
template class StorageCacheEntries<PackageCacheEntry>;
template struct StorageCache<PackageCacheEntries, PackageStorage::RWTransaction, PackageSpec>;

StorageDistribution::StorageDistribution(const char *path, std::uint32_t maxDbs)
{
    m_env = LMDBSafe::getMDBEnv(path, MDB_NOSUBDIR, 0600, maxDbs);
}

DatabaseStorage::DatabaseStorage(const std::shared_ptr<LMDBSafe::MDBEnv> &env, PackageCache &packageCache, std::string_view uniqueDatabaseName)
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
    return ((hasher1(ref.relatedStorage) ^ (hasher2(*ref.entryName) << 1)) >> 1);
}

} // namespace LibPkg
