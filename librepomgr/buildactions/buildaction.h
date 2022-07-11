#ifndef LIBREPOMGR_BUILD_ACTION_H
#define LIBREPOMGR_BUILD_ACTION_H

#include "./buildactionfwd.h"
#include "./buildactionmeta.h"
#include "./subprocessfwd.h"

#include "../webapi/routes.h"

#include "../globallock.h"
#include "../logcontext.h"

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

namespace Io {
class PasswordFile;
}

namespace LibRepoMgr {

struct ServiceSetup;

namespace WebAPI {
struct Params;
class Session;
} // namespace WebAPI

struct InternalBuildAction;

using AssociatedLocks = std::vector<std::variant<SharedLoggingLock, UniqueLoggingLock>>;

struct LIBREPOMGR_EXPORT PackageBuildData : public ReflectiveRapidJSON::JsonSerializable<PackageBuildData>,
                                            public ReflectiveRapidJSON::BinarySerializable<PackageBuildData, 1> {
    std::string existingVersion;
    std::vector<std::shared_ptr<LibPkg::Package>> existingPackages;
    std::string sourceDirectory;
    std::string originalSourceDirectory;
    std::optional<LibPkg::SourceInfo> sourceInfo;
    std::vector<LibPkg::PackageSpec> packages;
    std::vector<std::string> warnings;
    std::string error;
    std::size_t specifiedIndex = std::numeric_limits<std::size_t>::max();
    bool hasSource = false;
};

struct LIBREPOMGR_EXPORT BuildPreparation : public ReflectiveRapidJSON::JsonSerializable<BuildPreparation>,
                                            public ReflectiveRapidJSON::BinarySerializable<BuildPreparation, 1> {
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
                                                public ReflectiveRapidJSON::BinarySerializable<PackageBuildProgress, 1> {
    bool hasBeenAnyProgressMade() const;
    void resetProgress();
    void resetChrootSettings();

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
                                       public ReflectiveRapidJSON::BinarySerializable<RebuildInfo, 1> {
    std::vector<LibPkg::Dependency> provides;
    std::vector<std::string> libprovides;

    void replace(const LibPkg::DependencySet &deps, const std::unordered_set<std::string> &libs);
    void add(const LibPkg::DependencySet &deps, const std::unordered_set<std::string> &libs);
};

using RebuildInfoByPackage = std::unordered_map<std::string, RebuildInfo>;
using RebuildInfoByDatabase = std::unordered_map<std::string, RebuildInfoByPackage>;

struct LIBREPOMGR_EXPORT BuildProgress : public ReflectiveRapidJSON::JsonSerializable<BuildProgress>,
                                         public ReflectiveRapidJSON::BinarySerializable<BuildProgress, 1> {
    std::unordered_map<std::string, PackageBuildProgress> progressByPackage;
    std::string targetDbFilePath;
    std::string targetRepoPath;
    std::string stagingDbFilePath;
    std::string stagingRepoPath;
    RebuildInfoByPackage producedProvides, removedProvides;
    RebuildInfoByDatabase rebuildList;
};

struct LIBREPOMGR_EXPORT PackageMovementResult : public ReflectiveRapidJSON::JsonSerializable<PackageMovementResult>,
                                                 public ReflectiveRapidJSON::BinarySerializable<PackageMovementResult, 1> {
    std::vector<std::pair<std::string, std::string>> failedPackages;
    std::vector<std::string> processedPackages;
    std::string errorMessage;
};

struct LIBREPOMGR_EXPORT RepositoryProblem : public ReflectiveRapidJSON::JsonSerializable<RepositoryProblem>,
                                             public ReflectiveRapidJSON::BinarySerializable<RepositoryProblem, 1> {
    std::variant<std::string, LibPkg::UnresolvedDependencies> desc;
    std::string pkg;
    bool critical = true;
};

struct LIBREPOMGR_EXPORT BuildActionMessages : public ReflectiveRapidJSON::JsonSerializable<BuildActionMessages>,
                                               public ReflectiveRapidJSON::BinarySerializable<BuildActionMessages, 1> {
    std::vector<std::string> notes;
    std::vector<std::string> warnings;
    std::vector<std::string> errors;
};

class BuildProcessSession;
struct ServiceSetup;

struct LIBREPOMGR_EXPORT BuildActionBase : public ReflectiveRapidJSON::JsonSerializable<BuildActionBase>,
                                           public ReflectiveRapidJSON::BinarySerializable<BuildActionBase, 1> {
    using IdType = BuildActionIdType;
    static constexpr IdType invalidId = std::numeric_limits<BuildActionBase::IdType>::max();
    explicit BuildActionBase(IdType id = invalidId);

    bool isScheduled() const;
    bool isExecuting() const;
    bool isDone() const;
    bool hasSucceeded() const;

    IdType id;
    BuildActionType type = BuildActionType::Invalid;
    std::string taskName;
    std::string templateName;
    BuildActionStatus status = BuildActionStatus::Created;
    BuildActionResult result = BuildActionResult::None;
    CppUtilities::DateTime created = CppUtilities::DateTime::gmtNow();
    CppUtilities::DateTime started;
    CppUtilities::DateTime finished;
    std::vector<IdType> startAfter;
    std::string directory;
    std::vector<std::string> sourceDbs, destinationDbs;
    std::vector<std::string> packageNames;
    BuildActionFlagType flags = noBuildActionFlags;
    std::unordered_map<std::string, std::string> settings;
};

inline BuildActionBase::BuildActionBase(IdType id)
    : id(id)
{
}

struct LIBREPOMGR_EXPORT BuildAction : public BuildActionBase,
                                       public std::enable_shared_from_this<BuildAction>,
                                       public ReflectiveRapidJSON::JsonSerializable<BuildAction>,
                                       public ReflectiveRapidJSON::BinarySerializable<BuildAction, 1> {
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
    explicit BuildAction(IdType id = invalidId, ServiceSetup *setup = nullptr) noexcept;
    BuildAction &operator=(BuildAction &&other);
    ~BuildAction();
    static bool haveSucceeded(const std::vector<std::shared_ptr<BuildAction>> &buildActions);
    bool isAborted() const;
    const std::atomic_bool &aborted() const;
    LibPkg::StorageID start(ServiceSetup &setup, std::unique_ptr<Io::PasswordFile> &&secrets);
    void assignStartAfter(const std::vector<std::shared_ptr<BuildAction>> &startsAfterBuildActions);
    void abort();
    void appendOutput(std::string_view output);
    void appendOutput(std::string &&output);
    template <typename... Args> void appendOutput(Args &&...args);
    template <typename... Args> void appendOutput(CppUtilities::EscapeCodes::Phrases phrase, Args &&...args);
    LogContext &log();
    void setStopHandler(std::function<void(void)> &&stopHandler);
    void setConcludeHandler(std::function<void(void)> &&concludeHandler);
    std::shared_ptr<BuildProcessSession> findBuildProcess(const std::string &filePath);
    std::shared_ptr<BuildProcessSession> makeBuildProcess(
        std::string &&displayName, std::string &&logFilePath, ProcessHandler &&handler, AssociatedLocks &&locks = AssociatedLocks());
    void terminateOngoingBuildProcesses();
    void streamFile(const WebAPI::Params &params, const std::string &filePath, boost::beast::string_view fileMimeType,
        boost::beast::string_view contentDisposition = boost::beast::string_view());
    ServiceSetup *setup();
    Io::PasswordFile *secrets();
    using ReflectiveRapidJSON::JsonSerializable<BuildAction>::fromJson;
    using ReflectiveRapidJSON::JsonSerializable<BuildAction>::toJson;
    using ReflectiveRapidJSON::JsonSerializable<BuildAction>::toJsonDocument;
    using ReflectiveRapidJSON::BinarySerializable<BuildAction, 1>::toBinary;
    using ReflectiveRapidJSON::BinarySerializable<BuildAction, 1>::restoreFromBinary;
    using ReflectiveRapidJSON::BinarySerializable<BuildAction, 1>::fromBinary;

protected:
private:
    template <typename InternalBuildActionType> void post();
    template <typename Callback> void post(Callback &&codeToRun);
    LibPkg::StorageID conclude(BuildActionResult result);

public:
    std::vector<std::string> logfiles;
    std::vector<std::string> artefacts;
    std::variant<std::string, std::vector<std::string>, LibPkg::LicenseResult, LibPkg::PackageUpdates, BuildPreparation, BuildProgress,
        PackageMovementResult, std::unordered_map<std::string, std::vector<RepositoryProblem>>, BuildActionMessages>
        resultData;

private:
    LogContext m_log;
    ServiceSetup *m_setup = nullptr;
    std::atomic_bool m_aborted = false;
    std::function<void(void)> m_stopHandler;
    std::function<void(void)> m_concludeHandler;
    std::mutex m_processesMutex;
    std::unordered_map<std::string, std::shared_ptr<BuildProcessSession>> m_ongoingProcesses;
    std::mutex m_outputSessionMutex;
    std::shared_ptr<BuildProcessSession> m_outputSession;
    std::unique_ptr<InternalBuildAction> m_internalBuildAction;
    std::unique_ptr<Io::PasswordFile> m_secrets;
};

inline bool BuildActionBase::isScheduled() const
{
    return status == BuildActionStatus::Created || status == BuildActionStatus::AwaitingConfirmation;
}

inline bool BuildActionBase::isExecuting() const
{
    return status == BuildActionStatus::Enqueued || status == BuildActionStatus::Running;
}

inline bool BuildActionBase::isDone() const
{
    return status == BuildActionStatus::Finished;
}

inline bool BuildActionBase::hasSucceeded() const
{
    return isDone() && result == BuildActionResult::Success;
}

inline bool BuildAction::isAborted() const
{
    return m_aborted.load();
}

inline const std::atomic_bool &BuildAction::aborted() const
{
    return m_aborted;
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

inline ServiceSetup *BuildAction::setup()
{
    return m_setup;
}

inline Io::PasswordFile *BuildAction::secrets()
{
    return m_secrets.get();
}

/*!
 * \brief Appends the specified arguments to the build action's log but *not* to the overall service log.
 */
template <typename... Args> inline void BuildAction::appendOutput(Args &&...args)
{
    appendOutput(CppUtilities::argsToString(std::forward<Args>(args)...));
}

/*!
 * \brief Append output (overload needed to prevent endless recursion).
 */
inline void BuildAction::appendOutput(std::string &&output)
{
    appendOutput(std::string_view(output));
}

/*!
 * \brief Appends the specified arguments to the build action's log and to the overall service log.
 */
template <typename... Args> inline void BuildAction::appendOutput(CppUtilities::EscapeCodes::Phrases phrase, Args &&...args)
{
    appendOutput(std::move(CppUtilities::argsToString(CppUtilities::EscapeCodes::formattedPhraseString(phrase), std::forward<Args>(args)...)));
}

} // namespace LibRepoMgr

#endif // LIBREPOMGR_BUILD_ACTION_H

// avoid making LogContext available without also defining overloads for operator() which would possibly lead to linker errors
#include "../logging.h"
