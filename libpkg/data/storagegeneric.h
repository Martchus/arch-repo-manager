#ifndef LIBPKG_DATA_STORAGE_GENERIC_H
#define LIBPKG_DATA_STORAGE_GENERIC_H

#include "../lmdb-safe/lmdb-reflective.hh"
#include "../lmdb-safe/lmdb-safe.hh"
#include "../lmdb-safe/lmdb-typed.hh"

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>

#include <memory>
#include <mutex>

namespace LibPkg {

using StorageID = std::uint32_t;

template <typename StorageType, typename EntryType> struct StorageCacheRef {
    using Storage = StorageType;
    explicit StorageCacheRef(const StorageType &relatedStorage, const std::shared_ptr<EntryType> &entry);
    explicit StorageCacheRef(const StorageType &relatedStorage, const std::string &entryName);
    bool operator==(const StorageCacheRef &other) const;
    const StorageType *relatedStorage = nullptr;
    const std::string *entryName;
};

template <typename StorageType, typename EntryType>
inline StorageCacheRef<StorageType, EntryType>::StorageCacheRef(const StorageType &relatedStorage, const std::shared_ptr<EntryType> &entry)
    : relatedStorage(&relatedStorage)
    , entryName(&entry->name)
{
}

template <typename StorageType, typename EntryType>
inline StorageCacheRef<StorageType, EntryType>::StorageCacheRef(const StorageType &relatedStorage, const std::string &entryName)
    : relatedStorage(&relatedStorage)
    , entryName(&entryName)
{
}

template <typename StorageType, typename EntryType>
inline bool StorageCacheRef<StorageType, EntryType>::operator==(const StorageCacheRef<StorageType, EntryType> &other) const
{
    return relatedStorage == other.relatedStorage && *entryName == *other.entryName;
}

template <typename StorageRefType, typename EntryType> struct StorageCacheEntry {
    using Ref = StorageRefType;
    using Entry = EntryType;
    using Storage = typename Ref::Storage;
    explicit StorageCacheEntry(const StorageRefType &ref, StorageID id);
    StorageRefType ref;
    StorageID id;
    std::shared_ptr<EntryType> entry;
};

template <typename StorageRefType, typename EntryType>
inline StorageCacheEntry<StorageRefType, EntryType>::StorageCacheEntry(const StorageRefType &ref, StorageID id)
    : ref(ref)
    , id(id)
{
}

template <typename StorageEntryType> struct StorageEntryByID {
    struct result_type {
        StorageID id = 0;
        const typename StorageEntryType::Storage *storage = nullptr;

        bool operator==(const result_type &other) const
        {
            return id == other.id && storage == other.storage;
        }
    };

    result_type operator()(const StorageEntryType &storageEntry) const
    {
        return result_type{ storageEntry.id, storageEntry.ref.relatedStorage };
    }
};

template <typename StorageEntryType> class StorageCacheEntries {
public:
    using Ref = typename StorageEntryType::Ref;
    using Entry = typename StorageEntryType::Entry;
    using Storage = typename StorageEntryType::Storage;
    using StorageEntry = StorageEntryType;
    using ByID = StorageEntryByID<StorageEntry>;
    using EntryList = boost::multi_index::multi_index_container<StorageEntry,
        boost::multi_index::indexed_by<boost::multi_index::sequenced<>,
            boost::multi_index::hashed_unique<boost::multi_index::tag<typename ByID::result_type>, ByID>,
            boost::multi_index::hashed_unique<boost::multi_index::tag<Ref>, BOOST_MULTI_INDEX_MEMBER(StorageEntryType, Ref, ref)>>>;
    using iterator = typename EntryList::iterator;

    explicit StorageCacheEntries(std::size_t limit = 1000);

    template <typename IndexType> StorageEntry *find(const IndexType &ref);
    StorageEntry &insert(StorageEntry &&entry);
    std::size_t erase(const Ref &ref);
    std::size_t clear(const Storage &storage);
    iterator begin();
    iterator end();
    void setLimit(std::size_t limit);
    std::size_t size() const;

private:
    EntryList m_entries;
    std::size_t m_limit;
};

template <typename StorageEntryType>
inline StorageCacheEntries<StorageEntryType>::StorageCacheEntries(std::size_t limit)
    : m_limit(limit)
{
}

template <typename StorageEntryType> inline std::size_t StorageCacheEntries<StorageEntryType>::erase(const Ref &ref)
{
    return m_entries.template get<typename StorageEntryType::Ref>().erase(ref);
}

template <typename StorageEntryType> inline auto StorageCacheEntries<StorageEntryType>::begin() -> iterator
{
    return m_entries.begin();
}

template <typename StorageEntryType> inline auto StorageCacheEntries<StorageEntryType>::end() -> iterator
{
    return m_entries.end();
}

template <typename StorageEntryType> inline std::size_t StorageCacheEntries<StorageEntryType>::size() const
{
    return m_entries.size();
}

template <typename StorageEntriesType, typename StorageType, typename SpecType> struct StorageCache {
    using Entries = StorageEntriesType;
    using Entry = typename Entries::Entry;
    using ROTxn = typename StorageType::ROTransaction;
    using RWTxn = typename StorageType::RWTransaction;
    using Storage = typename Entries::Storage;
    struct StoreResult {
        StorageID id = 0;
        bool updated = false;
        std::shared_ptr<typename Entries::Entry> oldEntry;
    };

    SpecType retrieve(Storage &storage, ROTxn *, StorageID storageID);
    SpecType retrieve(Storage &storage, StorageID storageID);
    SpecType retrieve(Storage &storage, RWTxn *, const std::string &entryName);
    SpecType retrieve(Storage &storage, const std::string &entryName);
    StoreResult store(Storage &storage, RWTxn &txn, const std::shared_ptr<Entry> &entry, bool force);
    bool invalidate(Storage &storage, const std::string &entryName);
    void clear(Storage &storage);
    void clearCacheOnly(Storage &storage);
    void setLimit(std::size_t limit);
    std::size_t size();

private:
    Entries m_entries;
    std::mutex m_mutex;
};

template <typename StorageEntriesType, typename StorageType, typename SpecType>
std::size_t StorageCache<StorageEntriesType, StorageType, SpecType>::size()
{
    return m_entries.size();
}

} // namespace LibPkg

#endif // LIBPKG_DATA_STORAGE_GENERIC_H
