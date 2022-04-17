#ifndef LIBPKG_DATA_CONFIG_H
#define LIBPKG_DATA_CONFIG_H

#include "./database.h"
#include "./lockable.h"
#include "./siglevel.h"

#include "../global.h"

#include <reflective_rapidjson/binary/serializable.h>
#include <reflective_rapidjson/json/serializable.h>

#include <cstring>
#include <memory>
#include <mutex>
#include <regex>
#include <set>

namespace LibPkg {

/*!
 * \brief The SigStatus enum specifies PGP signature verification status return codes.
 */
enum class SignatureStatus { Valid, KeyExpired, SigExpired, KeyUnknown, KeyDisabled, InvalidId };

struct Config;
struct Database;
struct StorageDistribution;

struct LIBPKG_EXPORT DatabaseStatistics : public ReflectiveRapidJSON::JsonSerializable<Config> {
    DatabaseStatistics(const Database &config);

    const std::string &name;
    const std::size_t packageCount;
    const std::string &arch;
    const CppUtilities::DateTime lastUpdate;
    const std::string &localPkgDir;
    const std::string &mainMirror;
    const bool syncFromMirror;
};

struct LIBPKG_EXPORT Status : public ReflectiveRapidJSON::JsonSerializable<Status> {
    Status(const Config &config);

    std::vector<DatabaseStatistics> dbStats;
    const std::set<std::string> &architectures;
    const std::string &pacmanDatabasePath;
    const std::vector<std::string> &packageCacheDirs;
};

struct TopoSortItem;

struct LIBPKG_EXPORT BuildOrderResult : public ReflectiveRapidJSON::JsonSerializable<BuildOrderResult> {
    std::vector<PackageSearchResult> order;
    std::vector<PackageSearchResult> cycle;
    std::vector<std::string> ignored;
    bool success = false;
};

enum BuildOrderOptions {
    None = 0x0, /**< none of the other options enabled */
    IncludeSourceOnlyDependencies = 0x2, /**< whether source-only dependencies should be added the list of resulting packages */
    IncludeAllDependencies
    = 0x3, /**< whether *all* dependencies should be added the list of resulting packages (implies IncludeSourceOnlyDependencies) */
    ConsiderBuildDependencies = 0x4, /**< whether build dependencies should be taken into account for the topo sort */
};

struct LicenseFile : public ReflectiveRapidJSON::JsonSerializable<LicenseFile>, public ReflectiveRapidJSON::BinarySerializable<LicenseFile> {
    LicenseFile() = default;
    LicenseFile(std::string &&filename, std::string &&content)
        : filename(filename)
        , content(content)
    {
    }
    std::string filename;
    std::string content;
};

struct LIBPKG_EXPORT CommonLicense : public ReflectiveRapidJSON::JsonSerializable<CommonLicense>,
                                     public ReflectiveRapidJSON::BinarySerializable<CommonLicense> {
    std::set<std::string> relevantPackages;
    std::vector<LicenseFile> files;
};

struct LIBPKG_EXPORT LicenseResult : public ReflectiveRapidJSON::JsonSerializable<LicenseResult>,
                                     public ReflectiveRapidJSON::BinarySerializable<LicenseResult, 1> {
    std::map<std::string, CommonLicense> commonLicenses;
    std::map<std::string, std::vector<LicenseFile>> customLicences;
    std::vector<std::string> consideredPackages;
    std::vector<std::string> ignoredPackages;
    std::vector<std::string> notes;
    std::string mainProject;
    std::set<std::string> dependendProjects;
    std::string licenseSummary;
    bool success = true;
};

constexpr BuildOrderOptions operator|(BuildOrderOptions lhs, BuildOrderOptions rhs)
{
    return static_cast<BuildOrderOptions>(static_cast<int>(lhs) | static_cast<int>(rhs));
}

constexpr bool operator&(BuildOrderOptions lhs, BuildOrderOptions rhs)
{
    return (static_cast<int>(lhs) & static_cast<int>(rhs)) != 0;
}

struct LIBPKG_EXPORT Config : public Lockable, public ReflectiveRapidJSON::BinarySerializable<Config> {
    using DatabaseVisitor = std::function<bool(Database &)>;
    using PackageVisitorMove
        = std::function<bool(Database &, StorageID, std::shared_ptr<Package> &&)>; // package is invalidated/reused unless moved from!!!
    using PackageVisitorConst = std::function<bool(Database &, StorageID, const std::shared_ptr<Package> &)>;
    using PackageVisitorByName = std::function<bool(Database &, std::string_view, const std::function<PackageSpec(void)> &)>;

    explicit Config();
    ~Config();

    // load config and packages
    void loadPacmanConfig(const char *pacmanConfigPath);
    void loadAllPackages(bool withFiles, bool force);

    // storage and caching
    void initStorage(const char *path = "libpkg.db", std::uint32_t maxDbs = 0);
    void setPackageCacheLimit(std::size_t limit);
    std::unique_ptr<StorageDistribution> &storage();
    std::uint64_t restoreFromCache();
    std::uint64_t dumpCacheFile();
    void markAllDatabasesToBeDiscarded();
    void discardDatabases();

    // computions
    Status computeStatus() const;
    BuildOrderResult computeBuildOrder(const std::vector<std::string> &dependencyDenotations, BuildOrderOptions options);
    LicenseResult computeLicenseInfo(const std::vector<std::string> &dependencyDenotations);
    std::variant<std::vector<Database *>, std::string> computeDatabaseDependencyOrder(Database &database, bool addSelf = true);
    std::vector<Database *> computeDatabasesRequiringDatabase(Database &database);
    void pullDependentPackages(const std::shared_ptr<Package> &package, const std::unordered_set<LibPkg::Database *> &relevantDbs,
        std::unordered_map<LibPkg::StorageID, std::shared_ptr<LibPkg::Package>> &runtimeDependencies, DependencySet &missingDependencies,
        std::unordered_set<StorageID> &visited);

    // database search/creation
    Database *findDatabase(std::string_view name, std::string_view architecture);
    Database *findDatabaseFromDenotation(std::string_view databaseDenotation);
    Database *findOrCreateDatabase(std::string &&name, std::string_view architecture, bool keepLocalPaths = false);
    Database *findOrCreateDatabase(std::string_view name, std::string_view architecture, bool keepLocalPaths = false);
    Database *findOrCreateDatabaseFromDenotation(std::string_view databaseDenotation, bool keepLocalPaths = false);

    // packages search
    static std::pair<std::string_view, std::string_view> parseDatabaseDenotation(std::string_view databaseDenotation);
    static std::tuple<std::string_view, std::string_view, std::string_view> parsePackageDenotation(std::string_view packageDenotation);
    std::vector<PackageSearchResult> findPackages(std::string_view packageDenotation, std::size_t limit = std::numeric_limits<std::size_t>::max());
    std::vector<PackageSearchResult> findPackages(
        std::string_view dbName, std::string_view dbArch, std::string_view packageName, std::size_t limit = std::numeric_limits<std::size_t>::max());
    std::vector<PackageSearchResult> findPackages(std::tuple<std::string_view, std::string_view, std::string_view> dbAndPackageName,
        std::size_t limit = std::numeric_limits<std::size_t>::max());
    PackageSearchResult findPackage(const Dependency &dependency);
    std::vector<PackageSearchResult> findPackages(
        const Dependency &dependency, bool reverse = false, std::size_t limit = std::numeric_limits<std::size_t>::max());
    std::vector<PackageSearchResult> findPackagesProvidingLibrary(
        const std::string &library, bool reverse, std::size_t limit = std::numeric_limits<std::size_t>::max());

    // package iteration
    void packages(std::string_view dbName, std::string_view dbArch, const std::string &packageName, const DatabaseVisitor &databaseVisitor,
        const PackageVisitorConst &visitor);
    void packagesByName(const DatabaseVisitor &databaseVisitor, const PackageVisitorByName &visitor);
    void providingPackages(const Dependency &dependency, bool reverse, const DatabaseVisitor &databaseVisitor, const PackageVisitorConst &visitor);
    void providingPackages(const std::string &libraryName, bool reverse, const DatabaseVisitor &databaseVisitor, const PackageVisitorConst &visitor);

    std::vector<Database> databases;
    Database aur = Database("aur");
    std::set<std::string> architectures;
    std::string pacmanDatabasePath;
    std::vector<std::string> packageCacheDirs;
    SignatureLevelConfig signatureLevel;

private:
    Database *createDatabase(std::string &&name);
    bool addDepsRecursivelyInTopoOrder(std::vector<std::unique_ptr<TopoSortItem>> &allItems, std::vector<TopoSortItem *> &items,
        std::vector<std::string> &ignored, std::vector<PackageSearchResult> &cycleTracking, const Dependency &dependency, BuildOrderOptions options,
        bool onlyDependency);
    bool addLicenseInfo(LicenseResult &result, const Dependency &dependency);
    std::string addLicenseInfo(LicenseResult &result, PackageSearchResult &searchResult, const std::shared_ptr<Package> &package);

    std::unique_ptr<StorageDistribution> m_storage;
};

inline std::unique_ptr<StorageDistribution> &Config::storage()
{
    return m_storage;
}

inline Status Config::computeStatus() const
{
    return Status(*this);
}

inline std::vector<PackageSearchResult> Config::findPackages(
    std::string_view dbName, std::string_view dbArch, std::string_view packageName, std::size_t limit)
{
    return findPackages(std::make_tuple(dbName, dbArch, packageName), limit);
}

inline std::vector<PackageSearchResult> Config::findPackages(std::string_view packageDenotation, std::size_t limit)
{
    return findPackages(parsePackageDenotation(packageDenotation), limit);
}

} // namespace LibPkg

#endif // LIBPKG_DATA_CONFIG_H
