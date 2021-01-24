#ifndef LIBREPOMGR_BUILD_ACTION_H
#define LIBREPOMGR_BUILD_ACTION_H

#include "./buildactionfwd.h"
#include "./buildactionmeta.h"
#include "./subprocessfwd.h"

#include "../webapi/routes.h"

#include "../../libpkg/data/config.h"
#include "../../libpkg/data/lockable.h"

#include <reflective_rapidjson/binary/serializable.h>
#include <reflective_rapidjson/json/serializable.h>

#include <c++utilities/chrono/datetime.h>
#include <c++utilities/conversion/stringbuilder.h>
#include <c++utilities/io/ansiescapecodes.h>
#include <c++utilities/misc/traits.h>

#include <boost/asio/ssl/context.hpp>

#include <atomic>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <shared_mutex>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

class BuildActionsTests;

namespace LibRepoMgr {

struct LogContext {
    explicit LogContext(BuildAction *buildAction = nullptr);
    LogContext &operator=(const LogContext &) = delete;
    template <typename... Args> LogContext &operator()(CppUtilities::EscapeCodes::Phrases phrase, Args &&...args);
    template <typename... Args> LogContext &operator()(Args &&...args);
    template <typename... Args> LogContext &operator()(std::string &&msg);

private:
    BuildAction *const m_buildAction;
};

inline LogContext::LogContext(BuildAction *buildAction)
    : m_buildAction(buildAction)
{
}

struct ServiceSetup;

namespace WebAPI {
struct Params;
class Session;
} // namespace WebAPI

struct InternalBuildAction;

struct LIBREPOMGR_EXPORT PackageBuildData : public ReflectiveRapidJSON::JsonSerializable<PackageBuildData>,
                                            public ReflectiveRapidJSON::BinarySerializable<PackageBuildData> {
    std::string existingVersion;
    std::vector<std::shared_ptr<LibPkg::Package>> existingPackages;
    std::string sourceDirectory;
    std::string originalSourceDirectory;
    std::shared_ptr<LibPkg::SourceInfo> sourceInfo;
    std::vector<std::shared_ptr<LibPkg::Package>> packages;
    std::vector<std::string> warnings;
    std::string error;
    std::size_t specifiedIndex = std::numeric_limits<std::size_t>::max();
    bool hasSource = false;
};

struct LIBREPOMGR_EXPORT BuildPreparation : public ReflectiveRapidJSON::JsonSerializable<BuildPreparation>,
                                            public ReflectiveRapidJSON::BinarySerializable<BuildPreparation> {
    std::unordered_map<std::string, PackageBuildData> buildData;
    std::vector<std::pair<std::string, std::multimap<std::string, std::string>>> dbConfig, stagingDbConfig;
    std::string targetDb, targetArch, stagingDb;
    std::vector<std::vector<std::string>> batches;
    std::vector<std::string> cyclicLeftovers;
    std::vector<std::string> warnings;
    std::string error;
    bool manuallyOrdered = false;
};

enum class PackageStagingNeeded {
    Undetermined,
    Yes,
    No,
};

struct LIBREPOMGR_EXPORT PackageBuildProgress : public ReflectiveRapidJSON::JsonSerializable<PackageBuildProgress>,
                                                public ReflectiveRapidJSON::BinarySerializable<PackageBuildProgress> {
    bool hasBeenAnyProgressMade() const;
    void reset();

    CppUtilities::DateTime started;
    CppUtilities::DateTime finished;
    std::string buildDirectory;
    std::string chrootDirectory;
    std::string chrootUser;
    std::vector<std::string> makechrootpkgFlags;
    std::vector<std::string> makepkgFlags;
    std::string packageExtension;
    std::vector<std::string> warnings;
    std::string error;
    std::string updatedVersion;
    PackageStagingNeeded stagingNeeded = PackageStagingNeeded::Undetermined;
    bool skipChrootUpgrade = false;
    bool skipChrootCleanup = false;
    bool keepPreviousSourceTree = false;
    bool checksumsUpdated = false;
    bool hasSources = false;
    bool addedToRepo = false;
};

struct LIBREPOMGR_EXPORT RebuildInfo : public ReflectiveRapidJSON::JsonSerializable<RebuildInfo>,
                                       public ReflectiveRapidJSON::BinarySerializable<RebuildInfo> {
    std::vector<LibPkg::Dependency> provides;
    std::vector<std::string> libprovides;

    void add(const LibPkg::DependencySet &deps, const std::unordered_set<std::string> &libs);
};

using RebuildInfoByPackage = std::unordered_map<std::string, RebuildInfo>;
using RebuildInfoByDatabase = std::unordered_map<std::string, RebuildInfoByPackage>;

struct LIBREPOMGR_EXPORT BuildProgress : public ReflectiveRapidJSON::JsonSerializable<BuildProgress>,
                                         public ReflectiveRapidJSON::BinarySerializable<BuildProgress> {
    std::unordered_map<std::string, PackageBuildProgress> progressByPackage;
    std::string targetDbFilePath;
    std::string targetRepoPath;
    std::string stagingDbFilePath;
    std::string stagingRepoPath;
    RebuildInfoByPackage producedProvides, removedProvides;
    RebuildInfoByDatabase rebuildList;
};

struct LIBREPOMGR_EXPORT PackageMovementResult : public ReflectiveRapidJSON::JsonSerializable<PackageMovementResult>,
                                                 public ReflectiveRapidJSON::BinarySerializable<PackageMovementResult> {
    std::vector<std::pair<std::string, std::string>> failedPackages;
    std::vector<std::string> processedPackages;
    std::string errorMessage;
};

struct LIBREPOMGR_EXPORT RepositoryProblem : public ReflectiveRapidJSON::JsonSerializable<RepositoryProblem>,
                                             public ReflectiveRapidJSON::BinarySerializable<RepositoryProblem> {
    std::variant<std::string, LibPkg::UnresolvedDependencies> desc;
    std::string pkg;
    bool critical = true;
};

struct LIBREPOMGR_EXPORT BuildActionMessages : public ReflectiveRapidJSON::JsonSerializable<BuildActionMessages>,
                                               public ReflectiveRapidJSON::BinarySerializable<BuildActionMessages> {
    std::vector<std::string> notes;
    std::vector<std::string> warnings;
    std::vector<std::string> errors;
};

class BuildProcessSession;
struct OutputBufferingForSession;
struct ServiceSetup;

struct LIBREPOMGR_EXPORT BuildAction : public std::enable_shared_from_this<BuildAction>,
                                       public ReflectiveRapidJSON::JsonSerializable<BuildAction>,
                                       public ReflectiveRapidJSON::BinarySerializable<BuildAction> {
    friend InternalBuildAction;
    friend ServiceSetup;
    friend BuildProcessSession;
    friend BuildActionsTests;
    friend void WebAPI::Routes::postBuildAction(const WebAPI::Params &params, WebAPI::ResponseHandler &&handler);
    friend void WebAPI::Routes::postBuildActionsFromTask(const WebAPI::Params &params, WebAPI::ResponseHandler &&handler, const std::string &taskName,
        const std::string &directory, const std::vector<BuildActionIdType> &startAfterIds, bool startImmediately);
    friend void WebAPI::Routes::deleteBuildActions(const WebAPI::Params &params, WebAPI::ResponseHandler &&handler);
    friend void WebAPI::Routes::postCloneBuildActions(const WebAPI::Params &params, WebAPI::ResponseHandler &&handler);

public:
    using IdType = BuildActionIdType;
    static constexpr IdType invalidId = std::numeric_limits<BuildAction::IdType>::max();

    explicit BuildAction(IdType id = invalidId, ServiceSetup *setup = nullptr) noexcept;
    ~BuildAction();
    bool isScheduled() const;
    bool isExecuting() const;
    bool isDone() const;
    bool hasSucceeded() const;
    static bool haveSucceeded(const std::vector<std::shared_ptr<BuildAction>> &buildActions);
    bool isAborted() const;
    void start(ServiceSetup &setup);
    void startAfterOtherBuildActions(ServiceSetup &setup, const std::vector<std::shared_ptr<BuildAction>> &startsAfterBuildActions);
    void abort();
    void appendOutput(std::string &&output);
    void appendOutput(std::string_view output);
    template <typename... Args> void appendOutput(Args &&...args);
    template <typename... Args> void appendOutput(CppUtilities::EscapeCodes::Phrases phrase, Args &&...args);
    LogContext &log();
    void setStopHandler(std::function<void(void)> &&stopHandler);
    void setConcludeHandler(std::function<void(void)> &&concludeHandler);
    std::shared_ptr<BuildProcessSession> findBuildProcess(const std::string &filePath);
    std::shared_ptr<BuildProcessSession> makeBuildProcess(std::string &&logFilePath, ProcessHandler &&handler);
    void terminateOngoingBuildProcesses();
    void streamFile(const WebAPI::Params &params, const std::string &filePath, std::string_view fileMimeType);
    void streamOutput(const WebAPI::Params &params, std::size_t offset = 0);

protected:
private:
    template <typename InternalBuildActionType> void post();
    template <typename Callback> void post(Callback &&codeToRun);
    void conclude(BuildActionResult result);
    void continueStreamingExistingOutputToSession(std::shared_ptr<WebAPI::Session> session, OutputBufferingForSession &buffering,
        const boost::system::error_code &error, std::size_t bytesTransferred);
    void continueStreamingNewOutputToSession(std::shared_ptr<WebAPI::Session> session, OutputBufferingForSession &buffering,
        const boost::system::error_code &error, std::size_t bytesTransferred);
    template <typename OutputType> void appendOutput(OutputType &&output);

public:
    IdType id;
    std::string taskName;
    std::string templateName;
    std::string directory;
    std::vector<std::string> packageNames;
    std::vector<std::string> sourceDbs, destinationDbs;
    std::vector<std::string> extraParams; // deprecated; remove at some point
    std::unordered_map<std::string, std::string> settings;
    BuildActionFlagType flags = noBuildActionFlags;
    BuildActionType type = BuildActionType::Invalid;

    // only the following member variables are supposed to change after the build action has been added
    // to the overall list of build actions
    BuildActionStatus status = BuildActionStatus::Created;
    BuildActionResult result = BuildActionResult::None;
    std::variant<std::string, std::vector<std::string>, LibPkg::LicenseResult, LibPkg::PackageUpdates, BuildPreparation, BuildProgress,
        PackageMovementResult, std::unordered_map<std::string, std::vector<RepositoryProblem>>, BuildActionMessages>
        resultData;
    std::string output;
    std::string outputMimeType = "text/plain";
    std::vector<std::string> logfiles;
    std::vector<std::string> artefacts;
    CppUtilities::DateTime created = CppUtilities::DateTime::gmtNow();
    CppUtilities::DateTime started;
    CppUtilities::DateTime finished;
    std::vector<IdType> startAfter;

private:
    LogContext m_log;
    ServiceSetup *m_setup = nullptr;
    std::atomic_bool m_aborted = false;
    std::function<void(void)> m_stopHandler;
    std::function<void(void)> m_concludeHandler;
    std::mutex m_processesMutex;
    std::unordered_map<std::string, std::shared_ptr<BuildProcessSession>> m_ongoingProcesses;
    std::mutex m_outputStreamingMutex;
    std::unordered_map<std::shared_ptr<WebAPI::Session>, std::unique_ptr<OutputBufferingForSession>> m_bufferingForSession;
    std::unique_ptr<InternalBuildAction> m_internalBuildAction;
    std::vector<std::weak_ptr<BuildAction>> m_followUpActions;
};

inline bool BuildAction::isScheduled() const
{
    return status == BuildActionStatus::Created || status == BuildActionStatus::AwaitingConfirmation;
}

inline bool BuildAction::isExecuting() const
{
    return status == BuildActionStatus::Enqueued || status == BuildActionStatus::Running;
}

inline bool BuildAction::isDone() const
{
    return status == BuildActionStatus::Finished;
}

inline bool BuildAction::hasSucceeded() const
{
    return isDone() && result == BuildActionResult::Success;
}

inline bool BuildAction::isAborted() const
{
    return m_aborted.load();
}

inline LogContext &BuildAction::log()
{
    return m_log;
}

inline void BuildAction::setStopHandler(std::function<void()> &&stopHandler)
{
    m_stopHandler = std::move(stopHandler);
}

inline void BuildAction::setConcludeHandler(std::function<void()> &&concludeHandler)
{
    m_concludeHandler = std::move(concludeHandler);
}

inline std::shared_ptr<BuildProcessSession> BuildAction::findBuildProcess(const std::string &filePath)
{
    const auto i = m_ongoingProcesses.find(filePath);
    return i != m_ongoingProcesses.cend() ? i->second : nullptr;
}

/*!
 * \brief Appends the specified arguments to the build action's log but *not* to the overall service log.
 */
template <typename... Args> inline void BuildAction::appendOutput(Args &&...args)
{
    appendOutput(CppUtilities::argsToString(std::forward<Args>(args)...));
}

/*!
 * \brief Appends the specified arguments to the build action's log and to the overall service log.
 */
template <typename... Args> inline void BuildAction::appendOutput(CppUtilities::EscapeCodes::Phrases phrase, Args &&...args)
{
    auto msg = CppUtilities::argsToString(CppUtilities::EscapeCodes::formattedPhraseString(phrase), std::forward<Args>(args)...);
    std::cerr << msg;
    appendOutput(std::move(msg));
}

struct LIBREPOMGR_EXPORT BuildActionBasicInfo : public ReflectiveRapidJSON::JsonSerializable<BuildActionBasicInfo> {
    explicit BuildActionBasicInfo(const BuildAction &buildAction)
        : id(buildAction.id)
        , taskName(buildAction.taskName)
        , directory(buildAction.directory)
        , packageNames(buildAction.packageNames)
        , sourceDbs(buildAction.sourceDbs)
        , destinationDbs(buildAction.destinationDbs)
        , startAfter(buildAction.startAfter)
        , settings(buildAction.settings)
        , flags(buildAction.flags)
        , type(buildAction.type)
        , status(buildAction.status)
        , result(buildAction.result)
        , created(buildAction.created)
        , started(buildAction.started)
        , finished(buildAction.finished)
    {
    }

    const BuildAction::IdType id;
    const std::string &taskName;
    const std::string &directory;
    const std::vector<std::string> &packageNames;
    const std::vector<std::string> &sourceDbs, &destinationDbs;
    const std::vector<BuildAction::IdType> &startAfter;
    const std::unordered_map<std::string, std::string> settings;
    const BuildActionFlagType flags = noBuildActionFlags;
    const BuildActionType type;
    const BuildActionStatus status;
    const BuildActionResult result;
    const CppUtilities::DateTime created;
    const CppUtilities::DateTime started;
    const CppUtilities::DateTime finished;
};

} // namespace LibRepoMgr

#endif // LIBREPOMGR_BUILD_ACTION_H
