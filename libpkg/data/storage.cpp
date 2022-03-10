// ignore warning about "return result_type{ storageEntry.id, storageEntry.ref.relatedStorage };"
#pragma GCC diagnostic ignored "-Wnull-dereference"

#include "./storageprivate.h"

#include <c++utilities/conversion/stringbuilder.h>

using namespace CppUtilities;

namespace LibPkg {

template <typename StorageEntryType>
template <typename IndexType>
auto StorageCacheEntries<StorageEntryType>::find(const IndexType &ref) -> StorageEntry *
{
    const auto &index = m_entries.template get<IndexType>();
    if (auto i = index.find(ref); i != index.end()) {
        m_entries.relocate(m_entries.begin(), m_entries.template project<0>(i));
        return &i.get_node()->value();
    }
    return nullptr;
}

template <typename StorageEntryType> auto StorageCacheEntries<StorageEntryType>::insert(StorageEntry &&entry) -> StorageEntry &
{
    const auto [i, newItem] = m_entries.emplace_front(entry);
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

template <typename StorageEntryType> void StorageCacheEntries<StorageEntryType>::setLimit(std::size_t limit)
{
    m_limit = limit;
    while (m_entries.size() > limit) {
        m_entries.pop_back();
    }
}

template <typename StorageEntriesType, typename StorageType, typename SpecType>
auto StorageCache<StorageEntriesType, StorageType, SpecType>::retrieve(Storage &storage, ROTxn *txn, StorageID storageID) -> SpecType
{
    // check for package in cache, should be ok even if the db is being updated
    const auto ref = typename StorageEntryByID<typename Entries::StorageEntry>::result_type{ storageID, &storage };
    auto lock = std::unique_lock(m_mutex);
    if (auto *const existingCacheEntry = m_entries.find(ref)) {
        return SpecType(existingCacheEntry->id, existingCacheEntry->entry);
    }
    // check for package in storage, populate cache entry
    lock.unlock();
    auto entry = std::make_shared<Entry>();
    if (auto id = txn ? txn->get(storageID, *entry) : storage.packages.getROTransaction().get(storageID, *entry)) {
        // try to acquire update lock to avoid update existing cache entries while db is being updated
        if (const auto updateLock = std::unique_lock(storage.updateMutex, std::try_to_lock)) {
            using CacheEntry = typename Entries::StorageEntry;
            using CacheRef = typename Entries::Ref;
            auto newCacheEntry = CacheEntry(CacheRef(storage, entry), id);
            newCacheEntry.entry = entry;
            lock = std::unique_lock(m_mutex);
            m_entries.insert(std::move(newCacheEntry));
            lock.unlock();
        }
        return SpecType(id, entry);
    }
    return SpecType(0, std::shared_ptr<Entry>());
}

template <typename StorageEntriesType, typename StorageType, typename SpecType>
auto StorageCache<StorageEntriesType, StorageType, SpecType>::retrieve(Storage &storage, StorageID storageID) -> SpecType
{
    return retrieve(storage, nullptr, storageID);
}

template <typename StorageEntriesType, typename StorageType, typename SpecType>
auto StorageCache<StorageEntriesType, StorageType, SpecType>::retrieve(Storage &storage, RWTxn *txn, const std::string &entryName) -> SpecType
{
    // do not attempt to fetch empty entries (apparently can lead to "Getting data: MDB_BAD_VALSIZE: Unsupported size of key/DB name/data, or wrong DUPFIXED size")
    if (entryName.empty()) {
        return SpecType(0, std::shared_ptr<Entry>());
    }
    // check for package in cache, should be ok even if the db is being updated
    using CacheRef = typename Entries::Ref;
    const auto ref = CacheRef(storage, entryName);
    auto lock = std::unique_lock(m_mutex);
    if (auto *const existingCacheEntry = m_entries.find(ref)) {
        return SpecType(existingCacheEntry->id, existingCacheEntry->entry);
    }
    lock.unlock();
    // check for package in storage, populate cache entry
    auto entry = std::make_shared<Entry>();
    if (auto id = txn ? txn->template get<0>(entryName, *entry) : storage.packages.getROTransaction().template get<0>(entryName, *entry)) {
        // try to acquire update lock to avoid update existing cache entries while db is being updated
        if (const auto updateLock = std::unique_lock(storage.updateMutex, std::try_to_lock)) {
            using CacheEntry = typename Entries::StorageEntry;
            auto newCacheEntry = CacheEntry(CacheRef(storage, entry), id);
            newCacheEntry.entry = entry;
            lock = std::unique_lock(m_mutex);
            m_entries.insert(std::move(newCacheEntry));
            lock.unlock();
        }
        return SpecType(id, entry);
    }
    return SpecType(0, std::shared_ptr<Entry>());
}

template <typename StorageEntriesType, typename StorageType, typename SpecType>
auto StorageCache<StorageEntriesType, StorageType, SpecType>::retrieve(Storage &storage, const std::string &entryName) -> SpecType
{
    return retrieve(storage, nullptr, entryName);
}

template <typename StorageEntriesType, typename StorageType, typename SpecType>
auto StorageCache<StorageEntriesType, StorageType, SpecType>::store(Storage &storage, const std::shared_ptr<Entry> &entry, bool force) -> StoreResult
{
    // check for package in cache
    using CacheEntry = typename Entries::StorageEntry;
    using CacheRef = typename Entries::Ref;
    const auto ref = CacheRef(storage, entry);
    auto res = StorageCache::StoreResult();
    auto lock = std::unique_lock(m_mutex);
    auto *cacheEntry = m_entries.find(ref);
    if (cacheEntry) {
        res.id = cacheEntry->id;
        res.oldEntry = cacheEntry->entry;
        if (cacheEntry->entry == entry && !force) {
            // do nothing if cached package is the same as specified one
            return res;
        } else {
            // retain certain information obtained from package contents if this is actually the same package as before
            entry->addDepsAndProvidesFromOtherPackage(*cacheEntry->entry);
        }
    }
    lock.unlock();
    // check for package in storage
    auto txn = storage.packages.getRWTransaction();
    if (!res.oldEntry) {
        res.oldEntry = std::make_shared<Entry>();
        if ((res.id = txn.template get<0>(entry->name, *res.oldEntry))) {
            entry->addDepsAndProvidesFromOtherPackage(*res.oldEntry);
        } else {
            res.oldEntry.reset();
        }
    }
    // update package in storage
    res.id = txn.put(*entry, res.id);
    // update cache entry
    lock = std::unique_lock(m_mutex);
    if (cacheEntry) {
        cacheEntry->ref.entryName = &entry->name;
    } else {
        cacheEntry = &m_entries.insert(CacheEntry(ref, res.id));
    }
    cacheEntry->entry = entry;
    lock.unlock();
    txn.commit();
    res.updated = true;
    return res;
}

template <typename StorageEntriesType, typename StorageType, typename SpecType>
auto StorageCache<StorageEntriesType, StorageType, SpecType>::store(Storage &storage, RWTxn &txn, const std::shared_ptr<Entry> &entry) -> StoreResult
{
    // check for package in cache
    using CacheEntry = typename Entries::StorageEntry;
    using CacheRef = typename Entries::Ref;
    auto res = StorageCache::StoreResult();
    if (entry->name.empty()) {
        return res;
    }
    const auto ref = CacheRef(storage, entry);
    auto lock = std::unique_lock(m_mutex);
    auto *cacheEntry = m_entries.find(ref);
    if (cacheEntry) {
        // retain certain information obtained from package contents if this is actually the same package as before
        res.id = cacheEntry->id;
        entry->addDepsAndProvidesFromOtherPackage(*(res.oldEntry = cacheEntry->entry));
    }
    lock.unlock();
    // check for package in storage
    if (!res.oldEntry) {
        res.oldEntry = std::make_shared<Entry>();
        if ((res.id = txn.template get<0>(entry->name, *res.oldEntry))) {
            entry->addDepsAndProvidesFromOtherPackage(*res.oldEntry);
        } else {
            res.oldEntry.reset();
        }
    }
    // update package in storage
    res.id = txn.put(*entry, res.id);
    // update cache entry
    lock = std::unique_lock(m_mutex);
    if (cacheEntry) {
        cacheEntry->ref.entryName = &entry->name;
    } else {
        cacheEntry = &m_entries.insert(CacheEntry(ref, res.id));
    }
    cacheEntry->entry = entry;
    lock.unlock();
    res.updated = true;
    return res;
}

template <typename StorageEntriesType, typename StorageType, typename SpecType>
bool StorageCache<StorageEntriesType, StorageType, SpecType>::invalidate(Storage &storage, const std::string &entryName)
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

template <typename StorageEntriesType, typename StorageType, typename SpecType>
void StorageCache<StorageEntriesType, StorageType, SpecType>::clear(Storage &storage)
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

template <typename StorageEntriesType, typename StorageType, typename SpecType>
void StorageCache<StorageEntriesType, StorageType, SpecType>::clearCacheOnly(Storage &storage)
{
    const auto lock = std::unique_lock(m_mutex);
    m_entries.clear(storage);
}

template <typename StorageEntriesType, typename StorageType, typename SpecType>
void StorageCache<StorageEntriesType, StorageType, SpecType>::setLimit(std::size_t limit)
{
    const auto lock = std::unique_lock(m_mutex);
    m_entries.setLimit(limit);
}

template struct StorageCacheRef<DatabaseStorage, Package>;
template struct StorageCacheEntry<PackageCacheRef, Package>;
template class StorageCacheEntries<PackageCacheEntry>;
template struct StorageCache<PackageCacheEntries, PackageStorage, PackageSpec>;

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

std::size_t hash_value(const PackageCacheEntryByID &entryByID)
{
    const auto hasher1 = boost::hash<StorageID>();
    const auto hasher2 = boost::hash<const LibPkg::DatabaseStorage *>();
    return ((hasher1(entryByID.id) ^ (hasher2(entryByID.storage) << 1)) >> 1);
}

} // namespace LibPkg
