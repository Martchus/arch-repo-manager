#ifndef LIBPKG_DATA_DATABASE_H
#define LIBPKG_DATA_DATABASE_H

#include "./package.h"
#include "./siglevel.h"
#include "./storagefwd.h"

#include "../global.h"

#include <c++utilities/chrono/datetime.h>
#include <c++utilities/misc/flagenumclass.h>

#include <atomic>
#include <filesystem>
#include <optional>
#include <unordered_set>

namespace LibPkg {

struct Config;
struct Database;

struct LIBPKG_EXPORT DatabaseInfo {
    std::string name;
    std::string arch;
};

struct LIBPKG_EXPORT PackageSearchResult {
    PackageSearchResult();
    PackageSearchResult(Database &database, const std::shared_ptr<Package> &package, StorageID id);
    PackageSearchResult(Database &database, std::shared_ptr<Package> &&package, StorageID id);
    PackageSearchResult(Database &database, Package &&package, StorageID id);
    bool operator==(const PackageSearchResult &other) const;

    /// \brief The related database.
    /// \remarks
    /// - The find functions always uses Database* and it is guaranteed to be never nullptr.
    /// - The deserialization functions always use DatabaseInfo and the values might be empty if the source was empty.
    /// - The serialization functions can cope with both alternatives.
    std::variant<Database *, DatabaseInfo> db;
    std::shared_ptr<Package> pkg;
    StorageID id;
};

/*!
 * \brief The DatabaseUsage enum specifies the usage of a database within pacman.
 */
enum class DatabaseUsage {
    None = 0,
    Sync = (1 << 0), /*! The database is used when synchronizing. */
    Search = (1 << 1), /*! The database is used when searching. */
    Install = (1 << 2), /*! The database is used to install packages. */
    Upgrade = (1 << 3), /*! The database is used to upgrade packages. */
    All = (1 << 4) - 1, /*! The database is used for everything. */
};

enum class UpdateCheckOptions {
    None = 0,
    ConsiderRegularPackage = (1 << 0),
};

} // namespace LibPkg

CPP_UTILITIES_MARK_FLAG_ENUM_CLASS(LibPkg, LibPkg::DatabaseUsage)
CPP_UTILITIES_MARK_FLAG_ENUM_CLASS(LibPkg, LibPkg::UpdateCheckOptions)

namespace LibPkg {

struct LIBPKG_EXPORT PackageUpdate : public ReflectiveRapidJSON::JsonSerializable<PackageUpdate>,
                                     public ReflectiveRapidJSON::BinarySerializable<PackageUpdate> {
    PackageUpdate(PackageSearchResult &&oldVersion = PackageSearchResult(), PackageSearchResult &&newVersion = PackageSearchResult())
        : oldVersion(oldVersion)
        , newVersion(newVersion)
    {
    }
    PackageSearchResult oldVersion;
    PackageSearchResult newVersion;
};

struct LIBPKG_EXPORT PackageUpdates : public ReflectiveRapidJSON::JsonSerializable<PackageUpdates>,
                                      public ReflectiveRapidJSON::BinarySerializable<PackageUpdates, 1> {
    std::vector<PackageUpdate> versionUpdates;
    std::vector<PackageUpdate> packageUpdates;
    std::vector<PackageUpdate> downgrades;
    std::vector<PackageSearchResult> orphans;
};

struct LIBPKG_EXPORT PackageLocation {
    std::filesystem::path pathWithinRepo;
    std::filesystem::path storageLocation;
    std::optional<std::filesystem::filesystem_error> error;
    bool exists = false;
};

struct LIBPKG_EXPORT UnresolvedDependencies : public ReflectiveRapidJSON::JsonSerializable<UnresolvedDependencies>,
                                              public ReflectiveRapidJSON::BinarySerializable<UnresolvedDependencies, 1> {
    std::vector<Dependency> deps;
    std::vector<std::string> libs;
};

struct PackageUpdaterPrivate;

struct LIBPKG_EXPORT PackageUpdater {
    explicit PackageUpdater(Database &database, bool clear = false);
    ~PackageUpdater();

    PackageSpec findPackageWithID(const std::string &packageName);
    StorageID update(const std::shared_ptr<Package> &package);
    void commit();

private:
    Database &m_database;
    std::unique_ptr<PackageUpdaterPrivate> m_d;
};

struct AtomicDateTime : public std::atomic<CppUtilities::DateTime> {
    AtomicDateTime(CppUtilities::DateTime value = CppUtilities::DateTime())
        : std::atomic<CppUtilities::DateTime>(value)
    {
    }
    AtomicDateTime(AtomicDateTime &&other)
        : std::atomic<CppUtilities::DateTime>(other.load())
    {
    }
    AtomicDateTime &operator=(AtomicDateTime &&other)
    {
        store(other.load());
        return *this;
    }
};

struct LIBPKG_EXPORT Database : public ReflectiveRapidJSON::JsonSerializable<Database>, public ReflectiveRapidJSON::BinarySerializable<Database> {
    using PackageVisitorMove = std::function<bool(StorageID, std::shared_ptr<Package> &&)>; // package is invalidated/reused unless moved from!!!
    using PackageVisitorConst = std::function<bool(StorageID, const std::shared_ptr<Package> &)>;
    using PackageVisitorByName = std::function<bool(std::string_view, const std::function<PackageSpec(void)> &)>;

    friend struct PackageUpdater;

    explicit Database(const std::string &name = std::string(), const std::string &path = std::string());
    explicit Database(std::string &&name, std::string &&path);
    Database(Database &&other);
    ~Database();
    Database &operator=(Database &&rhs) = default;

    void initStorage(StorageDistribution &storage);
    void deducePathsFromLocalDirs();
    void resetConfiguration(bool keepLocalPaths = false);
    void clearPackages();
    void loadPackagesFromConfiguredPaths(bool withFiles = false, bool force = false);
    void loadPackages(const std::string &databaseData, CppUtilities::DateTime lastModified);
    void loadPackages(FileMap &&databaseFiles, CppUtilities::DateTime lastModified);
    static bool isFileRelevant(const char *filePath, const char *fileName, mode_t);
    std::vector<std::shared_ptr<Package>> findPackages(const std::function<bool(const Database &, const Package &)> &pred);
    void allPackages(const PackageVisitorMove &visitor);
    void allPackagesByName(const PackageVisitorByName &visitor);
    std::size_t packageCount() const;
    void providingPackages(const Dependency &dependency, bool reverse, const PackageVisitorConst &visitor);
    void providingPackages(const std::string &libraryName, bool reverse, const PackageVisitorConst &visitor);
    bool provides(const Dependency &dependency, bool reverse = false) const;
    bool provides(const std::string &libraryName, bool reverse = false) const;
    std::shared_ptr<Package> findPackage(StorageID packageID);
    std::shared_ptr<Package> findPackage(const std::string &packageName);
    PackageSpec findPackageWithID(const std::string &packageName);
    void removePackage(const std::string &packageName);
    StorageID updatePackage(const std::shared_ptr<Package> &package);
    StorageID forceUpdatePackage(const std::shared_ptr<Package> &package);
    void replacePackages(const std::vector<std::shared_ptr<Package>> &newPackages, CppUtilities::DateTime lastModified);
    std::unordered_map<PackageSpec, UnresolvedDependencies> detectUnresolvedPackages(Config &config,
        const std::vector<std::shared_ptr<Package>> &newPackages, const DependencySet &removedPackages,
        const std::unordered_set<std::string_view> &depsToIgnore = std::unordered_set<std::string_view>(),
        const std::unordered_set<std::string_view> &libsToIgnore = std::unordered_set<std::string_view>());
    PackageUpdates checkForUpdates(const std::vector<Database *> &updateSources, UpdateCheckOptions options = UpdateCheckOptions::None);
    PackageLocation locatePackage(const std::string &packageName) const;
    std::string filesPathFromRegularPath() const;

private:
    void removePackageDependencies(StorageID packageID, const std::shared_ptr<Package> &package);
    void addPackageDependencies(StorageID packageID, const std::shared_ptr<Package> &package);

public:
    std::string name;
    std::string path;
    std::string filesPath;
    std::vector<std::string> mirrors;
    DatabaseUsage usage = DatabaseUsage::None;
    SignatureLevel signatureLevel = SignatureLevel::Default;
    std::string arch = "x86_64";
    std::vector<std::string> dependencies;
    std::string localPkgDir;
    std::string localDbDir;
    AtomicDateTime lastUpdate;
    bool syncFromMirror = false;
    bool toBeDiscarded = false;

private:
    std::unique_ptr<DatabaseStorage> m_storage;
};

inline PackageSearchResult::PackageSearchResult()
    : db(nullptr)
    , id(0)
{
}

inline PackageSearchResult::PackageSearchResult(Database &database, const std::shared_ptr<Package> &package, StorageID id)
    : db(&database)
    , pkg(package)
    , id(id)
{
}

inline PackageSearchResult::PackageSearchResult(Database &database, std::shared_ptr<Package> &&package, StorageID id)
    : db(&database)
    , pkg(std::move(package))
    , id(id)
{
}

inline PackageSearchResult::PackageSearchResult(Database &database, Package &&package, StorageID id)
    : db(&database)
    , pkg(std::make_shared<Package>(std::move(package)))
    , id(id)
{
}

inline bool PackageSearchResult::operator==(const PackageSearchResult &other) const
{
    const auto *const *const db1 = std::get_if<Database *>(&db);
    const auto *const *const db2 = std::get_if<Database *>(&other.db);
    if (!db1 || !db2) {
        return false;
    }
    return ((!*db1 && !*db2) || (*db1 && *db2 && (**db1).name == (**db2).name)) && pkg == other.pkg;
}

} // namespace LibPkg

namespace std {

template <> struct hash<LibPkg::PackageSearchResult> {
    std::size_t operator()(const LibPkg::PackageSearchResult &res) const
    {
        using std::hash;
        const std::string *dbName = nullptr;
        if (const auto *const dbInfo = std::get_if<LibPkg::DatabaseInfo>(&res.db)) {
            dbName = &dbInfo->name;
        } else if (const auto *const db = std::get<LibPkg::Database *>(res.db)) {
            dbName = &db->name;
        }
        return ((hash<string>()(dbName ? *dbName : string()) ^ (hash<std::shared_ptr<LibPkg::Package>>()(res.pkg) << 1)) >> 1);
    }
};

} // namespace std

namespace ReflectiveRapidJSON {

namespace JsonReflector {

// declare custom (de)serialization for PackageSearchResult
template <>
LIBPKG_EXPORT void push<LibPkg::PackageSearchResult>(
    const LibPkg::PackageSearchResult &reflectable, RAPIDJSON_NAMESPACE::Value &value, RAPIDJSON_NAMESPACE::Document::AllocatorType &allocator);
template <>
LIBPKG_EXPORT void pull<LibPkg::PackageSearchResult>(LibPkg::PackageSearchResult &reflectable,
    const RAPIDJSON_NAMESPACE::GenericValue<RAPIDJSON_NAMESPACE::UTF8<char>> &value, JsonDeserializationErrors *errors);

// declare custom (de)serialization for AtomicDateTime
template <>
LIBPKG_EXPORT void push<LibPkg::AtomicDateTime>(
    const LibPkg::AtomicDateTime &reflectable, RAPIDJSON_NAMESPACE::Value &value, RAPIDJSON_NAMESPACE::Document::AllocatorType &allocator);
template <>
LIBPKG_EXPORT void pull<LibPkg::AtomicDateTime>(LibPkg::AtomicDateTime &reflectable,
    const RAPIDJSON_NAMESPACE::GenericValue<RAPIDJSON_NAMESPACE::UTF8<char>> &value, JsonDeserializationErrors *errors);

} // namespace JsonReflector

namespace BinaryReflector {

template <>
LIBPKG_EXPORT void writeCustomType<LibPkg::PackageSearchResult>(
    BinarySerializer &serializer, const LibPkg::PackageSearchResult &packageSearchResult, BinaryVersion version);
template <>
LIBPKG_EXPORT BinaryVersion readCustomType<LibPkg::PackageSearchResult>(
    BinaryDeserializer &deserializer, LibPkg::PackageSearchResult &packageSearchResult, BinaryVersion version);

template <>
LIBPKG_EXPORT void writeCustomType<LibPkg::AtomicDateTime>(
    BinarySerializer &serializer, const LibPkg::AtomicDateTime &packageSearchResult, BinaryVersion version);
template <>
LIBPKG_EXPORT BinaryVersion readCustomType<LibPkg::AtomicDateTime>(
    BinaryDeserializer &deserializer, LibPkg::AtomicDateTime &packageSearchResult, BinaryVersion version);

} // namespace BinaryReflector

} // namespace ReflectiveRapidJSON

#endif // LIBPKG_DATA_DATABASE_H
