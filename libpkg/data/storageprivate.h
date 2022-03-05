#ifndef LIBPKG_DATA_STORAGE_PRIVATE_H
#define LIBPKG_DATA_STORAGE_PRIVATE_H

#include "./package.h"
#include "./storagegeneric.h"

#include <mutex>

namespace LibPkg {

using PackageStorage = LMDBSafe::TypedDBI<Package, LMDBSafe::index_on<Package, std::string, &Package::name>>;
using DependencyStorage = LMDBSafe::TypedDBI<DatabaseDependency, LMDBSafe::index_on<Dependency, std::string, &DatabaseDependency::name>>;
using LibraryDependencyStorage
    = LMDBSafe::TypedDBI<DatabaseLibraryDependency, LMDBSafe::index_on<DatabaseLibraryDependency, std::string, &DatabaseLibraryDependency::name>>;
using PackageCacheRef = StorageCacheRef<DatabaseStorage, Package>;
using PackageCacheEntry = StorageCacheEntry<PackageCacheRef, Package>;
using PackageCacheEntries = StorageCacheEntries<PackageCacheEntry>;
using PackageCacheEntryByID = typename PackageCacheEntries::ByID::result_type;
using PackageCache = StorageCache<PackageCacheEntries, PackageStorage, PackageSpec>;

extern template struct StorageCacheRef<DatabaseStorage, Package>;
extern template struct StorageCacheEntry<PackageCacheRef, Package>;
extern template class StorageCacheEntries<PackageCacheEntry>;
extern template struct StorageCache<PackageCacheEntries, PackageStorage, PackageSpec>;

struct StorageDistribution {
    explicit StorageDistribution(const char *path, std::uint32_t maxDbs);

    std::unique_ptr<DatabaseStorage> forDatabase(std::string_view uniqueDatabaseName);
    PackageCache &packageCache();

private:
    std::shared_ptr<LMDBSafe::MDBEnv> m_env;
    PackageCache m_packageCache;
};

inline std::unique_ptr<DatabaseStorage> StorageDistribution::forDatabase(std::string_view uniqueDatabaseName)
{
    return std::make_unique<DatabaseStorage>(m_env, m_packageCache, uniqueDatabaseName);
}

inline PackageCache &StorageDistribution::packageCache()
{
    return m_packageCache;
}

struct DatabaseStorage {
    explicit DatabaseStorage(const std::shared_ptr<LMDBSafe::MDBEnv> &env, PackageCache &packageCache, std::string_view uniqueDatabaseName);
    PackageCache &packageCache;
    PackageStorage packages;
    DependencyStorage providedDeps;
    DependencyStorage requiredDeps;
    LibraryDependencyStorage providedLibs;
    LibraryDependencyStorage requiredLibs;
    std::mutex updateMutex; // must be acquired to update packages, concurrent reads should still be possible

private:
    std::shared_ptr<LMDBSafe::MDBEnv> m_env;
};

std::size_t hash_value(const PackageCacheRef &ref);
std::size_t hash_value(const PackageCacheEntryByID &entryByID);

} // namespace LibPkg

#endif // LIBPKG_DATA_STORAGE_PRIVATE_H
