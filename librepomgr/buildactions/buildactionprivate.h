#ifndef LIBREPOMGR_BUILD_ACTION_PRIVATE_H
#define LIBREPOMGR_BUILD_ACTION_PRIVATE_H

#include "./buildaction.h"
#include "./subprocess.h"

#include "../webclient/aur.h"
#include "../webclient/database.h"

#include <c++utilities/chrono/datetime.h>
#include <c++utilities/io/ansiescapecodes.h>
#include <c++utilities/io/inifile.h>
#include <c++utilities/misc/flagenumclass.h>

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <sstream>
#include <unordered_map>

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/posix/stream_descriptor.hpp>
#include <boost/beast/core/file.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/process/extend.hpp>

#ifdef CPP_UTILITIES_DEBUG_BUILD
#include <boost/asio/deadline_timer.hpp>
#endif

namespace boost::process {
class child;
}

namespace LibRepoMgr {

enum class BuildActionAccess {
    ReadConfig,
    WriteConfig,
};

enum class RequiredDatabases {
    None = 0x0,
    OneSource = 0x1,
    OneDestination = 0x2,
    OneOrMoreSources = 0x4,
    OneOrMoreDestinations = 0x8,
    MaybeSource = 0x10,
    MaybeDestination = 0x20,
    AllowFromAur = 0x40,
    AllowToAur = 0x80,
};

enum class RequiredParameters {
    None = 0x0,
    Packages = 0x1,
    MaybePackages = 0x2,
};

} // namespace LibRepoMgr

CPP_UTILITIES_MARK_FLAG_ENUM_CLASS(LibRepoMgr, LibRepoMgr::RequiredDatabases)
CPP_UTILITIES_MARK_FLAG_ENUM_CLASS(LibRepoMgr, LibRepoMgr::RequiredParameters)

namespace LibRepoMgr {

/// \brief The BufferPool struct provides fixed-sized buffers used for BuildProcessSession's live-steaming.
template <typename StorageType> struct LIBREPOMGR_EXPORT BufferPool {
    explicit BufferPool(std::size_t bufferSize);
    using BufferType = std::shared_ptr<StorageType>;
    BufferType newBuffer();
    std::size_t bufferSize() const;
    std::size_t storedBuffers() const;

private:
    std::vector<BufferType> m_buffers;
    std::size_t m_bufferSize;
    std::mutex m_mutex;
};

template <typename StorageType>
inline BufferPool<StorageType>::BufferPool(std::size_t bufferSize)
    : m_bufferSize(bufferSize)
{
    m_buffers.reserve(16);
}

template <typename StorageType> inline typename BufferPool<StorageType>::BufferType BufferPool<StorageType>::newBuffer()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto &existingBuffer : m_buffers) {
        if (!existingBuffer) {
            return existingBuffer = std::make_shared<StorageType>();
        } else if (existingBuffer.use_count() == 1) {
            return existingBuffer;
        }
    }
    return m_buffers.emplace_back(std::make_shared<StorageType>());
}

template <typename StorageType> inline std::size_t BufferPool<StorageType>::bufferSize() const
{
    return m_bufferSize;
}

template <typename StorageType> inline std::size_t BufferPool<StorageType>::storedBuffers() const
{
    return m_buffers.size();
}

/// \brief The OutputBufferingForSession struct holds buffers used by the BuildAction live-streaming.
struct LIBREPOMGR_EXPORT OutputBufferingForSession {
    static constexpr std::size_t bufferSize = 4096;
    using StorageType = std::array<char, bufferSize>;
    using BufferPoolType = BufferPool<StorageType>;
    using BufferType = BufferPoolType::BufferType;
    using BufferPile = std::vector<std::pair<BufferType, std::size_t>>;
    using BufferRefs = std::vector<boost::asio::const_buffer>;
    BufferPile currentlySentBuffers;
    BufferPile outstandingBuffersToSend;
    BufferRefs currentlySentBufferRefs;
    std::atomic<std::size_t> bytesSent = 0;
    std::atomic<std::size_t> existingOutputSize = 0;
    std::atomic_bool existingOutputSent = false;
};

/// \brief The BuildProcessSession class spawns a process assoicated with a build action.
/// The process output is make available as a logfile of the build action allowing live-steaming.
class LIBREPOMGR_EXPORT BuildProcessSession : public std::enable_shared_from_this<BuildProcessSession>, public BaseProcessSession {
public:
    static constexpr std::size_t bufferSize = 4096;
    using StorageType = std::array<char, bufferSize>;
    using BufferPoolType = BufferPool<StorageType>;
    using BufferType = BufferPoolType::BufferType;

    explicit BuildProcessSession(BuildAction *buildAction, boost::asio::io_context &ioContext, std::string &&displayName, std::string &&logFilePath,
        Handler &&handler, AssociatedLocks &&locks = AssociatedLocks());
    template <typename... ChildArgs> void launch(ChildArgs &&...childArgs);
    void registerWebSession(std::shared_ptr<WebAPI::Session> &&webSession);
    void registerNewDataHandler(std::function<void(BufferType, std::size_t)> &&handler);
    AssociatedLocks &locks();
    bool hasExited() const;

private:
    using BufferPile = std::vector<std::pair<BufferType, std::size_t>>;
    using BufferRefs = std::vector<boost::asio::const_buffer>;
    struct BuffersToWrite {
        BufferPile currentlySentBuffers;
        BufferPile outstandingBuffersToSend;
        BufferRefs currentlySentBufferRefs;
        bool error = false;
        void clear();
    };
    struct DataForWebSession : public BuffersToWrite {
        explicit DataForWebSession(BuildProcessSession &session);
        void streamFile(const std::string &filePath, std::shared_ptr<WebAPI::Session> &&session, std::unique_lock<std::mutex> &&lock);
        std::size_t bytesToSendFromFile() const;

    private:
        void writeFileData(const std::string &filePath, std::shared_ptr<WebAPI::Session> session, const boost::system::error_code &error,
            std::size_t bytesTransferred);

        BuildProcessSession &m_session;
        std::atomic<std::size_t> m_bytesToSendFromFile = 0;
        boost::beast::file m_file;
        BufferType m_fileBuffer;
        boost::asio::posix::stream_descriptor m_descriptor;
    };

    void prpareLogFile();
    void readMoreFromPipe();
    void writeDataFromPipe(boost::system::error_code ec, std::size_t bytesRead);
    void writeNextBufferToLogFile(const boost::system::error_code &error, std::size_t bytesTransferred);
    void writeNextBufferToWebSession(
        const boost::system::error_code &error, std::size_t bytesTransferred, WebAPI::Session &session, BuffersToWrite &sessionInfo);
    void conclude();

    std::weak_ptr<BuildAction> m_buildAction;
    boost::process::async_pipe m_pipe;
    BufferPoolType m_bufferPool;
    BufferType m_buffer;
    std::string m_displayName;
    std::string m_logFilePath;
    boost::beast::file m_logFile;
    boost::asio::posix::stream_descriptor m_logFileDescriptor;
    std::mutex m_mutex;
    BuffersToWrite m_logFileBuffers;
    std::unordered_map<std::shared_ptr<WebAPI::Session>, std::unique_ptr<DataForWebSession>> m_registeredWebSessions;
    std::function<void(BufferType, std::size_t)> m_newDataHandler;
    AssociatedLocks m_locks;
    std::atomic_bool m_exited = false;
};

inline BuildProcessSession::DataForWebSession::DataForWebSession(BuildProcessSession &session)
    : m_session(session)
    , m_descriptor(session.m_ioContext)
{
}

inline std::size_t BuildProcessSession::DataForWebSession::bytesToSendFromFile() const
{
    return m_bytesToSendFromFile.load();
}

inline BuildProcessSession::BuildProcessSession(BuildAction *buildAction, boost::asio::io_context &ioContext, std::string &&displayName,
    std::string &&logFilePath, BaseProcessSession::Handler &&handler, AssociatedLocks &&locks)
    : BaseProcessSession(ioContext, std::move(handler))
    , m_buildAction(buildAction ? buildAction->weak_from_this() : std::weak_ptr<BuildAction>())
    , m_pipe(ioContext)
    , m_bufferPool(bufferSize)
    , m_displayName(std::move(displayName))
    , m_logFilePath(std::move(logFilePath))
    , m_logFileDescriptor(ioContext)
    , m_locks(std::move(locks))
{
}

inline AssociatedLocks &BuildProcessSession::locks()
{
    return m_locks;
}

inline bool BuildProcessSession::hasExited() const
{
    return m_exited.load();
}

template <typename... ChildArgs> void BuildProcessSession::launch(ChildArgs &&...childArgs)
{
    prpareLogFile();
    if (result.errorCode) {
        conclude();
        return;
    }
    try {
        child = boost::process::child(
            m_ioContext, group, std::forward<ChildArgs>(childArgs)..., (boost::process::std_out & boost::process::std_err) > m_pipe,
            boost::process::extend::on_success =
                [session = shared_from_this()](auto &executor) {
                    if (const auto buildAction = session->m_buildAction.lock()) {
                        buildAction->appendOutput(
                            CppUtilities::EscapeCodes::Phrases::InfoMessage, "Launched \"", session->m_displayName, "\", PID: ", executor.pid, '\n');
                    }
                },
            boost::process::on_exit =
                [session = shared_from_this()](int exitCode, const std::error_code &errorCode) {
                    session->result.exitCode = exitCode;
                    session->result.errorCode = errorCode;
                    session->conclude();
                },
            boost::process::extend::on_error =
                [session = shared_from_this()](auto &, const std::error_code &errorCode) {
                    session->result.errorCode = errorCode;
                    session->conclude();
                });
    } catch (const boost::process::process_error &e) {
        result.errorCode = e.code();
        result.error = CppUtilities::argsToString("unable to launch: ", e.what());
        conclude();
        return;
    }
    readMoreFromPipe();
}

/// \brief The BufferSearch struct invokes a callback if an initially given search term occurs in consecutively specified buffers.
struct LIBREPOMGR_EXPORT BufferSearch {
    BufferSearch(
        std::string_view searchTerm, std::string_view terminationChars, std::string_view giveUpTerm, std::function<void(std::string &&)> &&callback);
    void operator()(const BuildProcessSession::BufferType &buffer, std::size_t bufferSize);

private:
    const std::string_view m_searchTerm;
    const std::string_view m_terminationChars;
    const std::string_view m_giveUpTerm;
    const std::function<void(std::string &&)> m_callback;
    std::string_view::const_iterator m_searchTermIterator;
    std::string_view::const_iterator m_giveUpTermIterator;
    std::string m_result;
    bool m_hasResult;
};

inline BufferSearch::BufferSearch(
    std::string_view searchTerm, std::string_view terminationChars, std::string_view giveUpTerm, std::function<void(std::string &&)> &&callback)
    : m_searchTerm(searchTerm)
    , m_terminationChars(terminationChars)
    , m_giveUpTerm(giveUpTerm)
    , m_callback(std::move(callback))
    , m_searchTermIterator(m_searchTerm.begin())
    , m_giveUpTermIterator(m_giveUpTerm.begin())
    , m_hasResult(false)
{
}

struct ProcessResult;

/// \brief The InternalBuildAction struct contains internal details (which are not serialized / accessible via the web API) and helpers.
/// \remarks This struct is mean to be inherited from when creating a new type build action like it is down by the classes below.
struct LIBREPOMGR_EXPORT InternalBuildAction {
    InternalBuildAction(ServiceSetup &setup, const std::shared_ptr<BuildAction> &buildAction);

protected:
    std::string validateParameter(RequiredDatabases requiredDatabases, RequiredParameters requiredParameters);
    std::string findDatabases();
    using InitReturnType = std::variant<std::monostate, std::shared_lock<std::shared_mutex>, std::unique_lock<std::shared_mutex>>;
    InitReturnType init(BuildActionAccess access, RequiredDatabases requiredDatabases, RequiredParameters requiredParameters);
    std::string determineWorkingDirectory(std::string_view name);
    const std::string &findSetting(const std::string_view &setting) const;
    void reportError(std::string &&error);
    void reportError();
    void reportSuccess();
    void reportResult(BuildActionResult result);
    bool reportAbortedIfAborted();

    ServiceSetup &m_setup;
    std::shared_ptr<BuildAction> m_buildAction;
    std::set<LibPkg::Database *> m_sourceDbs; // ordering important to prevent deadlocks when acquiring locks for DBs
    std::set<LibPkg::Database *> m_destinationDbs; // ordering important to prevent deadlocks when acquiring locks for DBs
    bool m_fromAur = false;
    bool m_toAur = false;
    bool m_hasError = false;

    static constexpr std::string_view buildDataWorkingDirectory = "build-data";
    static constexpr std::string_view buildPreparationFileName = "build-preparation";
    static constexpr std::string_view buildProgressFileName = "build-progress";
    static constexpr std::string_view repoManagementWorkingDirectory = "repo-management";
    static constexpr std::string_view customCommandsWorkingDirectory = "custom-commands";
};

/// \brief The PackageMovementAction struct contains data and helpers for RemovePackages and MovePackages.
struct LIBREPOMGR_EXPORT PackageMovementAction : public InternalBuildAction {
    PackageMovementAction(ServiceSetup &setup, const std::shared_ptr<BuildAction> &buildAction);

protected:
    bool prepareRepoAction(RequiredDatabases requiredDatabases);
    void reportResultWithData(BuildActionResult result);

private:
    void initWorkingDirectory();
    void locatePackages();

protected:
    std::string m_sourceRepoDirectory;
    std::string m_sourceDatabaseFile;
    std::string m_sourceDatabaseLockName;
    std::string m_destinationRepoDirectory;
    std::string m_destinationDatabaseFile;
    std::string m_destinationDatabaseLockName;
    std::string m_workingDirectory;
    std::vector<std::string> m_fileNames;
    PackageMovementResult m_result;
    std::vector<std::tuple<std::string_view, LibPkg::PackageLocation, bool>> m_packageLocations;
    boost::filesystem::path m_repoRemovePath;
    boost::filesystem::path m_repoAddPath;
};

struct LIBREPOMGR_EXPORT RemovePackages : public PackageMovementAction {
    RemovePackages(ServiceSetup &setup, const std::shared_ptr<BuildAction> &buildAction);
    void run();

private:
    void handleRepoRemoveResult(boost::process::child &&child, ProcessResult &&result);
    void movePackagesToArchive();
};

struct LIBREPOMGR_EXPORT MovePackages : public PackageMovementAction {
    MovePackages(ServiceSetup &setup, const std::shared_ptr<BuildAction> &buildAction);
    void run();

private:
    void handleRepoRemoveResult(MultiSession<void>::SharedPointerType processSession, boost::process::child &&child, ProcessResult &&result);
    void handleRepoAddResult(MultiSession<void>::SharedPointerType processSession, boost::process::child &&child, ProcessResult &&result);
    void conclude();

    std::string m_addErrorMessage;
};

struct LIBREPOMGR_EXPORT UpdateCheck : public InternalBuildAction {
    UpdateCheck(ServiceSetup &setup, const std::shared_ptr<BuildAction> &buildAction);
    void run();

private:
    LibPkg::PackageUpdates checkForUpdates();

    LibPkg::UpdateCheckOptions m_options = LibPkg::UpdateCheckOptions::None;
    bool m_packageLookupDone = false;
};

struct LIBREPOMGR_EXPORT CustomCommand : public InternalBuildAction {
    CustomCommand(ServiceSetup &setup, const std::shared_ptr<BuildAction> &buildAction);
    void run();

private:
    std::string m_workingDirectory;
};

struct LIBREPOMGR_EXPORT ReloadDatabase : public InternalBuildAction {
    ReloadDatabase(ServiceSetup &setup, const std::shared_ptr<BuildAction> &buildAction);
    void run();

private:
    std::vector<std::string> m_preparationFailures;
};

struct LIBREPOMGR_EXPORT ReloadLibraryDependencies : public InternalBuildAction {
    ReloadLibraryDependencies(ServiceSetup &setup, const std::shared_ptr<BuildAction> &buildAction);
    void run();

private:
    struct PackageToConsider {
        std::string path;
        std::string url;
        CppUtilities::DateTime lastModified;
        LibPkg::Package info;
    };
    struct DatabaseToConsider {
        std::string name;
        std::string arch;
        std::vector<PackageToConsider> packages;
    };

    void downloadPackagesFromMirror();
    void loadPackageInfoFromContents();
    void conclude();

    BuildActionMessages m_messages;
    std::stringstream m_skippingNote;
    std::vector<DatabaseToConsider> m_relevantPackagesByDatabase;
    std::atomic_size_t m_remainingPackages;
    WebClient::PackageCachingDataForSession m_cachingData;
};

struct LIBREPOMGR_EXPORT CheckForProblems : public InternalBuildAction {
    CheckForProblems(ServiceSetup &setup, const std::shared_ptr<BuildAction> &buildAction);
    void run();
};

struct LIBREPOMGR_EXPORT CleanRepository : public InternalBuildAction {
    CleanRepository(ServiceSetup &setup, const std::shared_ptr<BuildAction> &buildAction);
    void run();

private:
    void handleFatalError(InternalBuildAction::InitReturnType &init);

    BuildActionMessages m_messages;
    bool m_dryRun = false;
};

struct LIBREPOMGR_EXPORT ReloadConfiguration : public InternalBuildAction {
    ReloadConfiguration(ServiceSetup &setup, const std::shared_ptr<BuildAction> &buildAction);
    void run();
};

struct LIBREPOMGR_EXPORT MakeLicenseInfo : public InternalBuildAction {
    MakeLicenseInfo(ServiceSetup &setup, const std::shared_ptr<BuildAction> &buildAction);
    void run();
};

struct BatchItem;
using Batch = std::vector<BatchItem *>;
using BatchList = std::vector<Batch>;
using BatchMap = std::unordered_map<std::string, BatchItem>;

struct LIBREPOMGR_EXPORT PrepareBuild : public InternalBuildAction {
    PrepareBuild(ServiceSetup &setup, const std::shared_ptr<BuildAction> &buildAction);
    void run();

private:
    void populateDbConfig(const std::vector<LibPkg::Database *> &dbOrder, bool forStaging = false);
    bool isExistingPackageRelevant(const std::string &dependencyName, LibPkg::PackageSearchResult &package, PackageBuildData &packageBuildData,
        const LibPkg::Database &destinationDb);
    void makeSrcInfo(
        std::shared_ptr<WebClient::AurSnapshotQuerySession> &multiSession, const std::string &sourceDirectory, const std::string &packageName);
    static void processSrcInfo(WebClient::AurSnapshotQuerySession &multiSession, const std::string &sourceDirectory, const std::string &packageName,
        boost::process::child &&child, ProcessResult &&result);
    static void addResultFromSrcInfo(WebClient::AurSnapshotQuerySession &multiSession, const std::string &packageName, const std::string &srcInfo);
    static void addPackageToLogLine(std::string &logLine, const std::string &packageName);
    void fetchMissingBuildData();
    bool pullFurtherDependencies(const std::vector<LibPkg::Dependency> &dependencies);
    void bumpVersions();
    void computeDependencies(WebClient::AurSnapshotQuerySession::ContainerType &&responses);
    std::unordered_map<std::string, BatchItem> prepareBatches();
    void deduceBatchesFromSpecifiedOrder();
    void computeBatches();
    void addBatchesToResult(BatchList &&batches, Batch &&cyclicLeftovers);
    BuildPreparation makeResultData(std::string &&error = std::string());

    std::mutex m_mutex;
    std::string m_workingDirectory;
    boost::filesystem::path m_makePkgPath;
    std::vector<std::string> m_pkgbuildsDirs;
    std::regex m_ignoreLocalPkgbuildsRegex;
    std::unordered_map<std::string, PackageBuildData> m_buildDataByPackage;
    CppUtilities::IniFile::ScopeList m_dbConfig;
    CppUtilities::IniFile::ScopeList m_stagingDbConfig;
    std::unordered_set<std::string> m_baseDbs;
    std::unordered_set<std::string> m_requiredDbs;
    std::string m_targetDbName, m_targetArch, m_stagingDbName;
    std::vector<std::vector<std::string>> m_batches;
    std::vector<std::string> m_cyclicLeftovers;
    std::vector<std::string> m_warnings;
    bool m_forceBumpPackageVersion = false;
    bool m_cleanSourceDirectory = false;
    bool m_keepOrder = false;
    bool m_keepPkgRelAndEpoch = false;
};

struct LIBREPOMGR_EXPORT BatchProcessingSession : public MultiSession<std::string> {
    using SharedPointerType = std::shared_ptr<BatchProcessingSession>;

    explicit BatchProcessingSession(const std::unordered_set<std::string_view> &relevantPackages, std::vector<std::vector<std::string>> &batches,
        boost::asio::io_context &ioContext, HandlerType &&handler, bool skipBatchesAfterFailure = false);
    bool isValid() const;
    bool isStagingEnabled() const;
    bool hasFailuresInPreviousBatches() const;
    const std::string &currentPackageName() const;
    void selectNextPackage();
    const std::string *getCurrentPackageNameIfValidAndRelevantAndSelectNext();
    void enableStagingInNextBatch();

private:
    const std::unordered_set<std::string_view> &m_relevantPackages;
    std::mutex m_mutex;
    std::vector<std::vector<std::string>>::iterator m_batchBegin, m_batchIterator, m_batchEnd;
    std::vector<std::string>::iterator m_packageIterator, m_packageEnd;
    bool m_skipBatchesAfterFailure;
    bool m_hasFailuresInPreviousBatches;
    std::atomic_bool m_enableStagingInNextBatch;
    std::atomic_bool m_stagingEnabled;
};

struct BinaryPackageInfo {
    const std::string *const name;
    const std::string *const fileName;
    std::filesystem::path path;
    const bool isAny = false;
    bool artefactAlreadyPresent = false;
};

enum class InvocationResult {
    Ok,
    Skipped,
    Error,
};

struct LIBREPOMGR_EXPORT ConductBuild
    : public InternalBuildAction,
      private LibPkg::Lockable { // lockable is used for values of m_buildProgress.buildData in code paths where concurrency is allowed
    friend BuildActionsTests;

    ConductBuild(ServiceSetup &setup, const std::shared_ptr<BuildAction> &buildAction);
    void run();

private:
    void makeMakepkgConfigFile(const std::filesystem::path &makepkgConfigPath);
    void makePacmanConfigFile(
        const std::filesystem::path &pacmanConfigPath, const std::vector<std::pair<std::string, std::multimap<std::string, std::string>>> &dbConfig);
    void makeConfigFiles();
    void downloadSourcesAndContinueBuilding();
    void enqueueDownloads(const BatchProcessingSession::SharedPointerType &downloadsSession, std::size_t maxParallelDownloads);
    void enqueueMakechrootpkg(const BatchProcessingSession::SharedPointerType &makepkgchrootSession, std::size_t maxParallelInvocations);
    bool checkForFailedDependency(
        const std::string &packageNameToCheck, const std::vector<const std::vector<LibPkg::Dependency> *> &dependencies) const;
    InvocationResult invokeUpdatePkgSums(const BatchProcessingSession::SharedPointerType &downloadsSession, const std::string &packageName,
        PackageBuildProgress &packageProgress, const std::string &buildDirectory);
    InvocationResult invokeMakepkgToMakeSourcePackage(const BatchProcessingSession::SharedPointerType &downloadsSession,
        const std::string &packageName, PackageBuildProgress &packageProgress, const std::string &buildDirectory);
    InvocationResult invokeMakechrootpkg(
        const BatchProcessingSession::SharedPointerType &makepkgchrootSession, const std::string &packageName, bool hasFailuresInPreviousBatches);
    void addPackageToRepo(
        const BatchProcessingSession::SharedPointerType &makepkgchrootSession, const std::string &packageName, PackageBuildProgress &packageProgress);
    void checkDownloadErrorsAndMakePackages(BatchProcessingSession::ContainerType &&failedPackages);
    void handleMakechrootpkgErrorsAndAddPackageToRepo(const BatchProcessingSession::SharedPointerType &makepkgchrootSession,
        const std::string &packageName, PackageBuildProgress &packageProgress, boost::process::child &&child, ProcessResult &&result);
    void handleRepoAddErrorsAndMakeNextPackage(const BatchProcessingSession::SharedPointerType &makepkgchrootSession, const std::string &packageName,
        PackageBuildProgress &packageProgress, boost::process::child &&child, ProcessResult &&result);
    void checkBuildErrors(BatchProcessingSession::ContainerType &&failedPackages);
    void dumpBuildProgress();
    void addLogFile(std::string &&logFilePath);
    void assignNewVersion(const std::string &packageName, PackageBuildProgress &packageProgress, std::string &&updatedVersionInfo);
    void copyPkgbuildToOriginalSourceDirectory(
        const std::string &packageName, PackageBuildProgress &packageProgress, const std::string &buildDirectory);
    PackageStagingNeeded checkWhetherStagingIsNeededAndPopulateRebuildList(
        const std::string &packageName, const PackageBuildData &buildData, const std::vector<BinaryPackageInfo> &builtPackages);

    std::string m_workingDirectory;
    BuildPreparation m_buildPreparation;
    BuildProgress m_buildProgress;
    std::string m_globalPacmanConfigPath;
    std::string m_globalMakepkgConfigFilePath;
    std::string m_globalCcacheDir;
    std::string m_globalPackageCacheDir;
    std::string m_globalTestFilesDir;
    std::string m_chrootRootUser;
    boost::filesystem::path m_makePkgPath;
    boost::filesystem::path m_makeChrootPkgPath;
    boost::filesystem::path m_updatePkgSumsPath;
    boost::filesystem::path m_repoAddPath;
    std::filesystem::path m_makepkgConfigPath;
    std::filesystem::path m_pacmanConfigPath;
    std::filesystem::path m_pacmanStagingConfigPath;
    std::filesystem::path m_buildPreparationFilePath;
    std::string m_binaryPackageExtension;
    std::string m_sourcePackageExtension;
    std::unordered_set<std::string_view> m_relevantPackages;
    std::mutex m_rebuildListMutex;
    bool m_buildAsFarAsPossible;
    bool m_saveChrootDirsOfFailures;
    bool m_updateChecksums;
    bool m_autoStaging;
};

#ifdef LIBREPOMGR_DUMMY_BUILD_ACTION_ENABLED
struct LIBREPOMGR_EXPORT DummyBuildAction : public InternalBuildAction {
    DummyBuildAction(ServiceSetup &setup, const std::shared_ptr<BuildAction> &buildAction);
    void run();
    void continuePrinting();

private:
    void printLine();
    void stop();

    std::uint64_t m_counter;
    std::string m_workingDirectory;
    boost::asio::deadline_timer m_timer;
    std::shared_ptr<BuildProcessSession> m_logProcess;
};
#endif // LIBREPOMGR_DUMMY_BUILD_ACTION_ENABLED

} // namespace LibRepoMgr

#endif // LIBREPOMGR_BUILD_ACTION_PRIVATE_H
