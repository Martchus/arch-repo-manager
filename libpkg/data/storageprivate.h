#ifndef LIBPKG_DATA_STORAGE_PRIVATE_H
#define LIBPKG_DATA_STORAGE_PRIVATE_H

#include "./package.h"

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
using PackageStorage = TypedDBI<Package, index_on<Package, std::string, &Package::name>>;
using DependencyStorage = TypedDBI<DatabaseDependency, index_on<Dependency, std::string, &DatabaseDependency::name>>;
using LibraryDependencyStorage
    = TypedDBI<DatabaseLibraryDependency, index_on<DatabaseLibraryDependency, std::string, &DatabaseLibraryDependency::name>>;

struct PackageCache;

struct DatabaseStorage {
    explicit DatabaseStorage(const std::shared_ptr<MDBEnv> &env, PackageCache &packageCache, std::string_view uniqueDatabaseName);
    PackageCache &packageCache;
    PackageStorage packages;
    DependencyStorage providedDeps;
    DependencyStorage requiredDeps;
    LibraryDependencyStorage providedLibs;
    LibraryDependencyStorage requiredLibs;

private:
    std::shared_ptr<MDBEnv> m_env;
};

struct PackageCacheRef {
    explicit PackageCacheRef(const DatabaseStorage &databaseStorage, const std::shared_ptr<Package> &package);
    explicit PackageCacheRef(const DatabaseStorage &databaseStorage, const std::string &packageName);
    bool operator==(const PackageCacheRef &other) const;
    const DatabaseStorage *databaseStorage = nullptr;
    const std::string *packageName;
};

inline PackageCacheRef::PackageCacheRef(const DatabaseStorage &databaseStorage, const std::shared_ptr<Package> &package)
    : databaseStorage(&databaseStorage)
    , packageName(&package->name)
{
}

inline PackageCacheRef::PackageCacheRef(const DatabaseStorage &databaseStorage, const std::string &packageName)
    : databaseStorage(&databaseStorage)
    , packageName(&packageName)
{
}

inline bool PackageCacheRef::operator==(const PackageCacheRef &other) const
{
    return databaseStorage == other.databaseStorage && *packageName == *other.packageName;
}

std::size_t hash_value(const PackageCacheRef &ref);

struct PackageCacheEntry {
    explicit PackageCacheEntry(const PackageCacheRef &ref);
    PackageCacheRef ref;
    StorageID id;
    std::shared_ptr<Package> package;
};

inline PackageCacheEntry::PackageCacheEntry(const PackageCacheRef &ref)
    : ref(ref)
    , id(0)
{
}

class RecentlyUsedPackages {
    using PackageList = boost::multi_index::multi_index_container<PackageCacheEntry,
        boost::multi_index::indexed_by<boost::multi_index::sequenced<>,
            boost::multi_index::hashed_unique<boost::multi_index::tag<PackageCacheRef>,
                BOOST_MULTI_INDEX_MEMBER(PackageCacheEntry, PackageCacheRef, ref)>>>;
    using iterator = PackageList::iterator;

public:
    explicit RecentlyUsedPackages(std::size_t limit = 1000);

    PackageCacheEntry &findOrCreate(const PackageCacheRef &ref);
    void undo();
    std::size_t erase(const PackageCacheRef &ref);
    std::size_t clear(const DatabaseStorage &databaseStorage);
    iterator begin();
    iterator end();

private:
    PackageList m_packages;
    std::size_t m_limit;
};

inline RecentlyUsedPackages::RecentlyUsedPackages(std::size_t limit)
    : m_limit(limit)
{
}

inline void RecentlyUsedPackages::undo()
{
    m_packages.pop_front();
}

inline std::size_t RecentlyUsedPackages::erase(const PackageCacheRef &ref)
{
    return m_packages.get<PackageCacheRef>().erase(ref);
}

inline RecentlyUsedPackages::iterator RecentlyUsedPackages::begin()
{
    return m_packages.begin();
}

inline RecentlyUsedPackages::iterator RecentlyUsedPackages::end()
{
    return m_packages.end();
}

struct PackageCache {
    struct StoreResult {
        StorageID id = 0;
        bool updated = false;
        std::shared_ptr<Package> oldPackage;
    };

    PackageSpec retrieve(DatabaseStorage &databaseStorage, const std::string &packageName);
    StoreResult store(DatabaseStorage &databaseStorage, const std::shared_ptr<Package> &package, bool force);
    StoreResult store(DatabaseStorage &databaseStorage, PackageStorage::RWTransaction &txn, const std::shared_ptr<Package> &package);
    bool invalidate(DatabaseStorage &databaseStorage, const std::string &packageName);
    void clear(DatabaseStorage &databaseStorage);
    void clearCacheOnly(DatabaseStorage &databaseStorage);

private:
    RecentlyUsedPackages m_packages;
    std::mutex m_mutex;
};

struct StorageDistribution {
    explicit StorageDistribution(const char *path, std::uint32_t maxDbs);

    std::unique_ptr<DatabaseStorage> forDatabase(std::string_view uniqueDatabaseName);

private:
    std::shared_ptr<MDBEnv> m_env;
    PackageCache m_packageCache;
};

inline std::unique_ptr<DatabaseStorage> StorageDistribution::forDatabase(std::string_view uniqueDatabaseName)
{
    return std::make_unique<DatabaseStorage>(m_env, m_packageCache, uniqueDatabaseName);
}

} // namespace LibPkg

#endif // LIBPKG_DATA_STORAGE_PRIVATE_H
