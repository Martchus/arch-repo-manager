#ifndef LIBREPOMGR_SERVER_SETUP_H
#define LIBREPOMGR_SERVER_SETUP_H

#include "./authentication.h"
#include "./buildactions/buildaction.h"
#include "./buildactions/buildactiontemplate.h"
#include "./globallock.h"
#include "./resourceusage.h"

#include "../libpkg/data/config.h"
#include "../libpkg/data/lockable.h"

#include <reflective_rapidjson/json/serializable.h>

#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/ssl/context.hpp>

#include <functional>
#include <memory>
#include <regex>
#include <thread>
#include <vector>

namespace LibRepoMgr {

struct LIBREPOMGR_EXPORT ThreadPool {
    explicit ThreadPool(const char *name, boost::asio::io_context &ioContext, unsigned short threadCount);
    ~ThreadPool();

    const char *const name;
    std::vector<std::thread> threads;
};

struct ServiceStatus;
struct Storage;

struct StringHash {
    using is_transparent = void;
    [[nodiscard]] size_t operator()(const char *txt) const
    {
        return std::hash<std::string_view>{}(txt);
    }
    [[nodiscard]] size_t operator()(std::string_view txt) const
    {
        return std::hash<std::string_view>{}(txt);
    }
    [[nodiscard]] size_t operator()(const std::string &txt) const
    {
        return std::hash<std::string>{}(txt);
    }
};

struct LIBREPOMGR_EXPORT ServiceSetup : public LibPkg::Lockable {
    // the overall configuration (databases, packages, ...) used at various places
    // -> acquire the config lock for these
    LibPkg::Config config;

    // service global configuration; only changed when (re)loading config
    // -> acquire the setup lock for these
    std::vector<std::string> configFilePaths = { "server.conf" };
    std::string pacmanConfigFilePath = "/etc/pacman.conf";
    std::filesystem::path initialWorkingDirectory;
    std::string workingDirectory = "workingdir";
    std::string defaultArch = "x86_64";
    std::string dbPath = "libpkg-1.db";
    std::uint32_t maxDbs = 0;
    std::size_t packageCacheLimit = 1000;

    void loadConfigFiles(bool doFirstTimeSetup);
    void printLimits();
    void printDatabases();
    void printIoUringUsage();
    std::string_view cacheFilePath() const;
    void restoreState();
    std::size_t saveState();
    void initStorage();
    int run();
    int fixDb();
    int dumpDb(std::string_view filterRegex);
    ServiceStatus computeStatus();

    // variables relevant for the web server; only changed when (re)loading config
    struct LIBREPOMGR_EXPORT WebServerSetup {
        // only read by build actions and routes; changed when (re)loading config
        // -> acquire the setup lock for these
        std::string staticFilesPath;

        // never changed after setup
        boost::asio::ip::address address = boost::asio::ip::make_address("127.0.0.1");
        unsigned short port = 8090;
        unsigned short threadCount = 1;
        boost::asio::io_context ioContext;
        boost::asio::ssl::context sslContext{ boost::asio::ssl::context::sslv23_client };
        std::atomic_size_t packageSearchResponseLimit = 20000; // sufficient to return a "full architecture"
        std::atomic_size_t buildActionsResponseLimit = 200;
        bool verifySslCertificates = true;
        bool logSslCertificateValidation = false;

        void applyConfig(const std::multimap<std::string, std::string> &multimap);
        void initSsl();
        static bool logCertificateValidation(bool preVerified, boost::asio::ssl::verify_context &context);
    } webServer;

    // variables relevant for build actions and web server routes dealing with them
    struct LIBREPOMGR_EXPORT BuildSetup : public LibPkg::Lockable {
        friend void ServiceSetup::restoreState();

        struct LIBREPOMGR_EXPORT Worker : private boost::asio::executor_work_guard<boost::asio::io_context::executor_type>, public ThreadPool {
            explicit Worker(BuildSetup &setup);
            ~Worker();
            BuildSetup &setup;
        };

        enum class VisitorBehavior {
            DiscardChanges,
            SaveChanges,
            Delete,
        };

        explicit BuildSetup();
        BuildSetup(BuildSetup &&) = delete;
        ~BuildSetup();

        // fields which have their own locking
        // -> acquire the object's lock
        BuildActionMetaInfo metaInfo; // only static data so far but maybe extended to allow defining custom build actions

        // only read by build actions and routes; changed when (re)loading config
        // -> acquire the setup lock for these
        std::string workingDirectory = "building";
        std::vector<std::string> pkgbuildsDirs;
        std::regex ignoreLocalPkgbuildsRegex;
        std::string makePkgPath = "makepkg";
        std::string makeChrootPkgPath = "makechrootpkg";
        std::string makeContainerPkgPath = "makecontainerpkg";
        std::string updatePkgSumsPath = "updpkgsums";
        std::string conversionScriptPath;
        std::string repoAddPath = "repo-add";
        std::string repoRemovePath = "repo-remove";
        std::string pacmanPath = "pacman";
        std::string paccachePath = "paccache";
        std::string pkgctlPath = "pkgctl";
        std::string gpgPath = "gpg";
        std::string ccacheDir;
        std::string chrootDir;
        std::string chrootRootUser = "root";
        std::string chrootDefaultUser = "buildservice";
        std::string defaultGpgKey;
        std::string pacmanConfigFilePath; // FIXME: not useful after all?; using config-$arch directory within chrootDir instead
        std::string makepkgConfigFilePath; // FIXME: not useful after all?; using config-$arch directory within chrootDir instead
        std::vector<std::string> makechrootpkgFlags;
        std::vector<std::string> makepkgFlags;
        std::string packageCacheDir; // cache dir *without* architecture sub-folder (which is automatically added as needed)
        std::uint64_t packageDownloadSizeLimit = 500 * 1024 * 1024;
        std::string testFilesDir;
        BuildPresets presets;
        std::unordered_map<std::string, std::vector<std::string>, StringHash, std::equal_to<>> complementaryVariants;
        CppUtilities::TimeSpan buildActionRetention = CppUtilities::TimeSpan::fromDays(14);
        bool loadFilesDbs = false;
        bool forceLoadingDbs = false;

        // never changed after startup
        unsigned short threadCount = 4;
        boost::asio::io_context ioContext;
        std::string dbPath = "librepomgr-1.db";

        void initStorage(const char *path);
        bool hasStorage() const;
        void applyConfig(const std::multimap<std::string, std::string> &multimap);
        void readComplementaryVariants(const std::multimap<std::string, std::string> &multimap);
        void readPresets(const std::string &configFilePath, const std::string &presetsFile);
        Worker allocateBuildWorker();
        LibPkg::StorageID allocateBuildActionID();
        std::shared_ptr<BuildAction> getBuildAction(BuildActionIdType id);
        std::vector<std::shared_ptr<BuildAction>> getBuildActions(const std::vector<BuildActionIdType> &ids);
        LibPkg::StorageID storeBuildAction(const std::shared_ptr<BuildAction> &buildAction);
        void deleteBuildAction(const std::vector<std::shared_ptr<BuildAction>> &actions);
        std::size_t buildActionCount();
        std::size_t runningBuildActionCount() const;
        void rebuildDb();
        using BuildActionVisitorBase = std::function<bool(LibPkg::StorageID, BuildActionBase &&)>;
        void forEachBuildAction(std::function<void(std::size_t)> count, BuildActionVisitorBase &&func, std::size_t limit, std::size_t start);
        using BuildActionVisitorWriteable = std::function<bool(LibPkg::StorageID, BuildAction &, VisitorBehavior &)>;
        void forEachBuildAction(BuildActionVisitorWriteable &&func, std::size_t *count = nullptr);
        std::vector<std::shared_ptr<BuildAction>> followUpBuildActions(BuildActionIdType forId);

    private:
        std::unordered_map<BuildActionIdType, std::shared_ptr<BuildAction>> m_runningActions;
        std::unordered_map<BuildActionIdType, std::unordered_set<BuildActionIdType>> m_followUpActions;
        std::unique_ptr<Storage> m_storage;
    } building;

    struct LIBREPOMGR_EXPORT Authentication : public LibPkg::Lockable {
        std::unordered_map<std::string, UserInfo> users;

        void applyConfig(const std::string &userName, const std::multimap<std::string, std::string> &multimap);
        UserAuth authenticate(std::string_view authorizationHeader) const;
    } auth;

    struct LIBREPOMGR_EXPORT Locks {
        using LockTable = std::unordered_map<std::string, GlobalLockable>;

        [[nodiscard]] GlobalLockable &namedLock(const std::string &lockName);
        [[nodiscard]] SharedLoggingLock acquireToRead(LogContext &log, std::string &&lockName);
        [[nodiscard]] UniqueLoggingLock acquireToWrite(LogContext &log, std::string &&lockName);
        [[nodiscard]] std::pair<LockTable *, std::unique_lock<std::shared_mutex>> acquireLockTable();
        void acquireToRead(LogContext &log, std::string &&name, std::move_only_function<void(SharedLoggingLock &&lock)> &&callback);
        void acquireToWrite(LogContext &log, std::string &&name, std::move_only_function<void(UniqueLoggingLock &&lock)> &&callback);
        void clear();
        static std::string forDatabase(std::string_view dbName, std::string_view dbArch);
        static std::string forDatabase(const LibPkg::Database &db);

    private:
        std::mutex m_accessMutex;
        std::shared_mutex m_cleanupMutex;
        LockTable m_locksByName;
    } locks;
};

inline bool ServiceSetup::BuildSetup::hasStorage() const
{
    return m_storage != nullptr;
}

inline std::size_t ServiceSetup::BuildSetup::runningBuildActionCount() const
{
    return m_runningActions.size();
}

inline GlobalLockable &ServiceSetup::Locks::namedLock(const std::string &lockName)
{
    const auto locktableLock = std::unique_lock(m_accessMutex);
    return m_locksByName[lockName];
}

inline SharedLoggingLock ServiceSetup::Locks::acquireToRead(LogContext &log, std::string &&lockName)
{
    const auto locktableLock = std::shared_lock(m_cleanupMutex);
    return namedLock(lockName).lockToRead(log, std::move(lockName));
}

inline UniqueLoggingLock ServiceSetup::Locks::acquireToWrite(LogContext &log, std::string &&lockName)
{
    const auto locktableLock = std::shared_lock(m_cleanupMutex);
    return namedLock(lockName).lockToWrite(log, std::move(lockName));
}

inline void ServiceSetup::Locks::acquireToRead(
    LogContext &log, std::string &&lockName, std::move_only_function<void(SharedLoggingLock &&lock)> &&callback)
{
    const auto locktableLock = std::shared_lock(m_cleanupMutex);
    namedLock(lockName).lockToRead(log, std::move(lockName), std::move(callback));
}

inline void ServiceSetup::Locks::acquireToWrite(
    LogContext &log, std::string &&lockName, std::move_only_function<void(UniqueLoggingLock &&lock)> &&callback)
{
    const auto locktableLock = std::shared_lock(m_cleanupMutex);
    namedLock(lockName).lockToWrite(log, std::move(lockName), std::move(callback));
}

inline std::pair<ServiceSetup::Locks::LockTable *, std::unique_lock<std::shared_mutex>> ServiceSetup::Locks::acquireLockTable()
{
    return std::make_pair(&m_locksByName, std::unique_lock(m_cleanupMutex));
}

struct LIBREPOMGR_EXPORT ServiceStatus : public ReflectiveRapidJSON::JsonSerializable<ServiceStatus> {
    explicit ServiceStatus(ServiceSetup &setup);

    const char *const version = nullptr;
    const char *const url = nullptr;
    const LibPkg::Status config;
    const BuildActionMetaInfo &actions;
    const BuildPresets &presets;
    const std::string &defaultArch;
    const ResourceUsage resourceUsage;
};

inline ServiceStatus ServiceSetup::computeStatus()
{
    return ServiceStatus(*this);
}

} // namespace LibRepoMgr

#endif // LIBREPOMGR_SERVER_SETUP_H
