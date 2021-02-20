#ifndef LIBREPOMGR_SERVER_SETUP_H
#define LIBREPOMGR_SERVER_SETUP_H

#include "./authentication.h"
#include "./buildactions/buildaction.h"
#include "./buildactions/buildactiontemplate.h"

#include "../libpkg/data/config.h"
#include "../libpkg/data/lockable.h"

#include <reflective_rapidjson/json/serializable.h>

#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/ssl/context.hpp>

#include <memory>
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

struct LIBREPOMGR_EXPORT ServiceSetup : public LibPkg::Lockable {
    // the overall configuration (databases, packages, ...) used at various places
    // -> acquire the config lock for these
    LibPkg::Config config;

    // service global configuration; only changed when (re)loading config
    // -> acquire the setup lock for these
    std::string configFilePath = "server.conf";
    std::string pacmanConfigFilePath = "/etc/pacman.conf";
    std::string workingDirectory = "workingdir";

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
        bool verifySslCertificates = true;
        bool logSslCertificateValidation = false;

        void applyConfig(const std::multimap<std::string, std::string> &multimap);
        void initSsl();
        static bool logCertificateValidation(bool preVerified, boost::asio::ssl::verify_context &context);
    } webServer;

    // variables relevant for build actions and web server routes dealing with them
    struct LIBREPOMGR_EXPORT BuildSetup : public LibPkg::Lockable {
        struct LIBREPOMGR_EXPORT Worker : private boost::asio::executor_work_guard<boost::asio::io_context::executor_type>, public ThreadPool {
            explicit Worker(BuildSetup &setup);
            ~Worker();
            BuildSetup &setup;
        };

        // read/written by build actions and routes
        // -> acquire the build lock for these
        std::vector<std::shared_ptr<BuildAction>> actions;
        std::unordered_set<std::size_t> invalidActions;

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
        std::string updatePkgSumsPath = "updpkgsums";
        std::string repoAddPath = "repo-add";
        std::string repoRemovePath = "repo-remove";
        std::string ccacheDir;
        std::string chrootDir;
        std::string chrootRootUser = "root";
        std::string chrootDefaultUser = "buildservice";
        std::string pacmanConfigFilePath; // FIXME: not useful after all?; using config-$arch directory within chrootDir instead
        std::string makepkgConfigFilePath; // FIXME: not useful after all?; using config-$arch directory within chrootDir instead
        std::vector<std::string> makechrootpkgFlags;
        std::vector<std::string> makepkgFlags;
        std::string packageCacheDir;
        std::string testFilesDir;
        BuildPresets presets;
        bool loadFilesDbs = false;

        // never changed after startup
        unsigned short threadCount = 4;
        boost::asio::io_context ioContext;

        void applyConfig(const std::multimap<std::string, std::string> &multimap);
        void readPresets(const std::string &configFilePath, const std::string &presetsFile);
        Worker allocateBuildWorker();
        BuildAction::IdType allocateBuildActionID();
        std::shared_ptr<BuildAction> getBuildAction(BuildAction::IdType id);
        std::vector<std::shared_ptr<BuildAction>> getBuildActions(const std::vector<BuildAction::IdType> &ids);
    } building;

    struct LIBREPOMGR_EXPORT Authentication : public LibPkg::Lockable {
        std::unordered_map<std::string, UserInfo> users;

        void applyConfig(const std::string &userName, const std::multimap<std::string, std::string> &multimap);
        UserPermissions authenticate(std::string_view authorizationHeader) const;
    } auth;

    struct LIBREPOMGR_EXPORT Locks {
        [[nodiscard]] std::shared_lock<std::shared_mutex> acquireToRead(const std::string &lockName);
        [[nodiscard]] std::unique_lock<std::shared_mutex> acquireToWrite(const std::string &lockName);
        [[nodiscard]] std::unique_lock<std::shared_mutex> acquireToWrite(std::shared_lock<std::shared_mutex> &readLock, const std::string &lockName);
        void clear();

    private:
        std::mutex m_mutex;
        std::unordered_map<std::string, LibPkg::Lockable> m_locksByName;
    } locks;

    void loadConfigFiles(bool restoreStateAndDiscardDatabases);
    void printDatabases();
    std::string_view cacheFilePath() const;
    RAPIDJSON_NAMESPACE::Document libraryDependenciesToJson() const;
    void restoreLibraryDependenciesFromJson(const std::string &json, ReflectiveRapidJSON::JsonDeserializationErrors *errors);
    std::size_t restoreState();
    std::size_t saveState();
    void run();
    ServiceStatus computeStatus() const;
};

inline std::shared_ptr<BuildAction> ServiceSetup::BuildSetup::getBuildAction(BuildAction::IdType id)
{
    return id < actions.size() ? actions[id] : nullptr;
}

inline std::shared_lock<std::shared_mutex> ServiceSetup::Locks::acquireToRead(const std::string &lockName)
{
    const auto lock = std::lock_guard(m_mutex);
    return m_locksByName[lockName].lockToRead();
}

inline std::unique_lock<std::shared_mutex> ServiceSetup::Locks::acquireToWrite(const std::string &lockName)
{
    const auto lock = std::lock_guard(m_mutex);
    return m_locksByName[lockName].lockToWrite();
}

inline std::unique_lock<std::shared_mutex> ServiceSetup::Locks::acquireToWrite(
    std::shared_lock<std::shared_mutex> &readLock, const std::string &lockName)
{
    readLock.unlock();
    return acquireToWrite(lockName);
}

struct LIBREPOMGR_EXPORT ServiceStatus : public ReflectiveRapidJSON::JsonSerializable<ServiceStatus> {
    ServiceStatus(const ServiceSetup &setup);

    const char *const version = nullptr;
    const LibPkg::Status config;
    const BuildActionMetaInfo &actions;
    const BuildPresets &presets;
};

inline ServiceStatus ServiceSetup::computeStatus() const
{
    return ServiceStatus(*this);
}

} // namespace LibRepoMgr

#endif // LIBREPOMGR_SERVER_SETUP_H
