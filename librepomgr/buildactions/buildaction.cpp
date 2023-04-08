
#include "./buildactionprivate.h"

#include "../webapi/session.h"

#include <passwordfile/io/passwordfile.h>

#include <reflective_rapidjson/binary/reflector-chronoutilities.h>
#include <reflective_rapidjson/json/reflector-chronoutilities.h>

#include "reflection/buildaction.h"

#include <boost/asio/post.hpp>

#ifdef LIBREPOMGR_DUMMY_BUILD_ACTION_ENABLED
#include <boost/date_time/posix_time/posix_time.hpp>
#include <c++utilities/io/misc.h>
#include <c++utilities/tests/testutils.h>
#endif

using namespace std;
using namespace CppUtilities;
using namespace CppUtilities::EscapeCodes;

namespace LibRepoMgr {

InternalBuildAction::InternalBuildAction(ServiceSetup &setup, const std::shared_ptr<BuildAction> &buildAction)
    : m_setup(setup)
    , m_buildAction(buildAction)
{
}

static bool isAur(const std::string &dbName)
{
    return dbName == "aur" || dbName == "AUR";
}

std::string InternalBuildAction::validateParameter(RequiredDatabases requiredDatabases, RequiredParameters requiredParameters)
{
    if (requiredDatabases & RequiredDatabases::OneOrMoreSources) {
        if (m_buildAction->sourceDbs.empty()) {
            return "no source databases specified";
        }
    } else if (requiredDatabases & RequiredDatabases::OneSource) {
        if (m_buildAction->sourceDbs.size() != 1) {
            return "not exactly one source database specified";
        }
    } else if (!(requiredDatabases & RequiredDatabases::MaybeSource)) {
        if (!m_buildAction->sourceDbs.empty()) {
            return "no source database must be specified";
        }
    }
    if (requiredDatabases & RequiredDatabases::OneOrMoreDestinations) {
        if (m_buildAction->destinationDbs.empty()) {
            return "no destination databases specified";
        }
    } else if (requiredDatabases & RequiredDatabases::OneDestination) {
        if (m_buildAction->destinationDbs.size() != 1) {
            return "not exactly one destination database specified";
        }
    } else if (!(requiredDatabases & RequiredDatabases::MaybeDestination)) {
        if (!m_buildAction->destinationDbs.empty()) {
            return "no destination database must be specified";
        }
    }
    for (const auto &sourceDb : m_buildAction->sourceDbs) {
        if (sourceDb.empty() || sourceDb == "none") {
            return "empty/invalid source database specified";
        }
        if (isAur(sourceDb)) {
            if (requiredDatabases & RequiredDatabases::AllowFromAur) {
                m_fromAur = true;
            } else {
                return "source database must not be AUR";
            }
        }
    }
    for (const auto &destinationDb : m_buildAction->destinationDbs) {
        if (destinationDb.empty() || destinationDb == "none") {
            return "empty/invalid destination database specified";
        }
        if (isAur(destinationDb)) {
            if (requiredDatabases & RequiredDatabases::AllowToAur) {
                m_toAur = true;
            } else {
                return "destination database must not be AUR";
            }
        }
    }
    if (requiredParameters & RequiredParameters::Packages) {
        if (m_buildAction->packageNames.empty()) {
            return "no packages specified";
        }
    } else if (!(requiredParameters & RequiredParameters::MaybePackages)) {
        if (!m_buildAction->packageNames.empty()) {
            return "no packages must be specified";
        }
    }
    return string();
}

std::string InternalBuildAction::findDatabases()
{
    m_sourceDbs.clear();
    m_destinationDbs.clear();
    for (const auto &sourceDb : m_buildAction->sourceDbs) {
        if (isAur(sourceDb)) {
            continue;
        }
        if (auto *const db = m_setup.config.findDatabaseFromDenotation(sourceDb)) {
            m_sourceDbs.emplace(db);
        } else {
            return "source database " % sourceDb + " does not exist";
        }
    }
    for (const auto &destinationDb : m_buildAction->destinationDbs) {
        if (isAur(destinationDb)) {
            continue;
        }
        if (auto *const db = m_setup.config.findDatabaseFromDenotation(destinationDb)) {
            m_destinationDbs.emplace(db);
        } else {
            return "destination database " % destinationDb + " does not exist";
        }
    }
    return string();
}

typename InternalBuildAction::InitReturnType InternalBuildAction::init(
    BuildActionAccess access, RequiredDatabases requiredDatabases, RequiredParameters requiredParameters)
{
    InitReturnType configLock;
    if (auto error = validateParameter(requiredDatabases, requiredParameters); !error.empty()) {
        reportError(std::move(error));
        return configLock;
    }
    switch (access) {
    case BuildActionAccess::ReadConfig:
        configLock = m_setup.config.lockToRead();
        break;
    case BuildActionAccess::WriteConfig:
        configLock = m_setup.config.lockToWrite();
        break;
    }
    if (auto error = findDatabases(); !error.empty()) {
        configLock = monostate();
        reportError(std::move(error));
        return configLock;
    }
    return configLock;
}

std::string InternalBuildAction::determineWorkingDirectory(std::string_view name)
{
    const auto workingDirectory = m_setup.building.workingDirectory % '/' % name % '/' + m_buildAction->directory;
    m_buildAction->appendOutput(Phrases::InfoMessage, "Working directory: " % workingDirectory + '\n');
    return workingDirectory;
}

const std::string &InternalBuildAction::findSetting(const std::string_view &setting) const
{
    if (const auto i = m_buildAction->settings.find(std::string(setting)); i != m_buildAction->settings.end()) {
        return i->second;
    } else {
        static const auto empty = std::string();
        return empty;
    }
}

void InternalBuildAction::reportError(std::string &&error)
{
    const auto buildActionLock = m_setup.building.lockToWrite();
    m_buildAction->resultData = std::move(error);
    m_buildAction->conclude(BuildActionResult::Failure);
}

void InternalBuildAction::reportError()
{
    m_buildAction->conclude(BuildActionResult::Failure);
}

void InternalBuildAction::reportSuccess()
{
    m_buildAction->conclude(BuildActionResult::Success);
}

void InternalBuildAction::reportResult(BuildActionResult result)
{
    m_buildAction->conclude(result);
}

bool InternalBuildAction::reportAbortedIfAborted()
{
    if (!m_buildAction->isAborted()) {
        return false;
    }
    const auto buildActionLock = m_setup.building.lockToWrite();
    m_buildAction->conclude(BuildActionResult::Aborted);
    return true;
}

BuildAction::BuildAction(IdType id, ServiceSetup *setup) noexcept
    : BuildActionBase(id)
    , m_log(this)
    , m_setup(setup)
    , m_stopHandler(std::bind(&BuildAction::terminateOngoingBuildProcesses, this))
{
}

BuildAction &BuildAction::operator=(BuildAction &&other)
{
    if (this == &other) {
        return *this;
    }
    id = other.id;
    taskName = std::move(other.taskName);
    templateName = std::move(other.templateName);
    directory = std::move(other.directory);
    packageNames = std::move(other.packageNames);
    sourceDbs = std::move(other.sourceDbs);
    destinationDbs = std::move(other.destinationDbs);
    settings = std::move(other.settings);
    flags = other.flags;
    type = other.type;
    status = other.status;
    result = other.result;
    resultData = std::move(other.resultData);
    logfiles = std::move(other.logfiles);
    artefacts = std::move(other.artefacts);
    created = other.created;
    started = other.started;
    finished = other.finished;
    startAfter = std::move(other.startAfter);
    m_log = LogContext(this);
    m_setup = other.m_setup;
    m_aborted = false;
    m_stopHandler = std::bind(&BuildAction::terminateOngoingBuildProcesses, this);
    m_concludeHandler = std::function<void(void)>();
    m_ongoingProcesses.clear();
    m_outputSession.reset();
    m_internalBuildAction = std::move(other.m_internalBuildAction);
    return *this;
}

BuildAction::~BuildAction()
{
}

bool BuildAction::haveSucceeded(const std::vector<std::shared_ptr<BuildAction>> &buildActions)
{
    for (const auto &buildAction : buildActions) {
        if (!buildAction->hasSucceeded()) {
            return false;
        }
    }
    return true;
}

/*!
 * \brief Starts the build action. The caller must acquire the lock to write build actions if
 *        the build action is setup-globally visible.
 * \returns Returns immediately. The real work is done in a build action thread.
 */
LibPkg::StorageID BuildAction::start(ServiceSetup &setup, std::unique_ptr<Io::PasswordFile> &&secrets)
{
    if (!isScheduled()) {
        return 0;
    }

    started = DateTime::gmtNow();
    status = BuildActionStatus::Running;
    m_setup = &setup;

    // grab secrets from session
    // note: That's done regardless of the type because we might need to pass the secrets to the next
    //       action in the chain (regardless of the current build action's type).
    if (secrets) {
        m_secrets = std::move(secrets);
    }

    switch (type) {
    case BuildActionType::Invalid:
        resultData = "type is invalid";
        return conclude(BuildActionResult::Failure);
    case BuildActionType::RemovePackages:
        post<RemovePackages>();
        break;
    case BuildActionType::MovePackages:
        post<MovePackages>();
        break;
    case BuildActionType::CheckForUpdates:
        post<UpdateCheck>();
        break;
    case BuildActionType::ReloadDatabase:
        post<ReloadDatabase>();
        break;
    case BuildActionType::ReloadLibraryDependencies:
        post<ReloadLibraryDependencies>();
        break;
    case BuildActionType::PrepareBuild:
        post<PrepareBuild>();
        break;
    case BuildActionType::ConductBuild:
        post<ConductBuild>();
        break;
    case BuildActionType::MakeLicenseInfo:
        post<MakeLicenseInfo>();
        break;
    case BuildActionType::ReloadConfiguration:
        post<ReloadConfiguration>();
        break;
    case BuildActionType::CheckForProblems:
        post<CheckForProblems>();
        break;
    case BuildActionType::CleanRepository:
        post<CleanRepository>();
        break;
#ifdef LIBREPOMGR_DUMMY_BUILD_ACTION_ENABLED
    case BuildActionType::DummyBuildAction:
        post<DummyBuildAction>();
        break;
#endif
    case BuildActionType::CustomCommand:
        post<CustomCommand>();
        break;
    case BuildActionType::BuildServiceCleanup:
        post<BuildServiceCleanup>();
        break;
    default:
        resultData = "not implemented yet or invalid type";
        return conclude(BuildActionResult::Failure);
    }

    // update in persistent storage and create entry in "running cache"
    return m_setup->building.storeBuildAction(shared_from_this());
}

void BuildAction::assignStartAfter(const std::vector<std::shared_ptr<BuildAction>> &startsAfterBuildActions)
{
    for (auto &previousBuildAction : startsAfterBuildActions) {
        startAfter.emplace_back(previousBuildAction->id);
    }
}

void BuildAction::abort()
{
    m_aborted.store(true);
    if (!m_setup) {
        return;
    }
    if (m_stopHandler) {
        boost::asio::post(m_setup->building.ioContext.get_executor(), m_stopHandler);
    }
    if (m_waitingOnAsyncLock) {
        const auto buildActionLock = m_setup->building.lockToWrite();
        if (isExecuting()) {
            conclude(BuildActionResult::Aborted);
        }
    }
}

void LibRepoMgr::BuildAction::acquireToWrite(std::string &&lockName, std::move_only_function<void(UniqueLoggingLock &&)> &&callback)
{
    // flag this action as "waiting for async lock" so the abort() function is allowed to conclude the action right away (and thus the
    // build action is not stuck in "running" until the lock is acquired)
    m_waitingOnAsyncLock.store(true);

    // handle abortion if aborted (instead of acquiring the lock)
    if (m_aborted) {
        auto buildActionLock = m_setup->building.lockToWrite();
        if (isExecuting()) {
            conclude(BuildActionResult::Aborted);
        }
        return;
    }

    // acquire the lock asynchronously
    m_setup->locks.acquireToWrite(
        log(), std::move(lockName), [t = shared_from_this(), callback = std::move(callback)](UniqueLoggingLock &&lock) mutable {
            // stop the abort() function from immediately concluding the build action again
            t->m_waitingOnAsyncLock.store(false);

            // execute the callback in another building thread to avoid interferances with the thread that invoked this callback (when releasing its own lock)
            boost::asio::post(t->m_setup->building.ioContext.get_executor(), [t = t, callback = std::move(callback), lock = std::move(lock)] mutable {
                // conclude the action as aborted if it has been aborted meanwhile
                auto buildActionLock = t->m_setup->building.lockToWrite();
                if (t->m_aborted && t->isExecuting()) {
                    t->conclude(BuildActionResult::Aborted);
                }

                // execute the callback only if the action hasn't been aborted
                if (!t->m_aborted) {
                    buildActionLock.unlock();
                    callback(std::move(lock));
                }
            });
        });
}

template <typename InternalBuildActionType> void BuildAction::post()
{
    assert(m_setup);
    m_internalBuildAction = make_unique<InternalBuildActionType>(*m_setup, shared_from_this());
    post(bind(&InternalBuildActionType::run, static_cast<InternalBuildActionType *>(m_internalBuildAction.get())));
}

template <typename Callback> void BuildAction::post(Callback &&codeToRun)
{
    assert(m_setup);
    boost::asio::post(m_setup->building.ioContext.get_executor(), std::forward<Callback>(codeToRun));
}

/*!
 * \brief Internally called to conclude the build action.
 */
LibPkg::StorageID BuildAction::conclude(BuildActionResult result)
{
    // set fields accordingly
    status = BuildActionStatus::Finished;
    this->result = result;
    finished = DateTime::gmtNow();

    // start globally visible follow-up actions if succeeded
    if (result == BuildActionResult::Success && m_setup) {
        const auto followUps = m_setup->building.followUpBuildActions(id);
        for (auto &followUpAction : followUps) {
            if (followUpAction->isScheduled() && BuildAction::haveSucceeded(m_setup->building.getBuildActions(followUpAction->startAfter))) {
                auto secrets = std::unique_ptr<Io::PasswordFile>();
                if (m_secrets) {
                    secrets = std::make_unique<Io::PasswordFile>();
                    secrets->setPath(m_secrets->path());
                    secrets->setPassword(m_secrets->password());
                }
                followUpAction->start(*m_setup, std::move(secrets));
            }
        }
        // note: Not cleaning up the follow-up actions here because at some point I might implement recursive restarting.
    }

    // detach build process sessions
    if (const auto lock = std::unique_lock(m_outputSessionMutex); m_outputSession) {
        m_outputSession->writeEnd(); // tell clients waiting for output that it's over
        m_outputSession.reset();
    }
    if (const auto lock = std::unique_lock(m_processesMutex)) {
        m_ongoingProcesses.clear();
    }

    // write build action to persistent storage
    // TODO: should this also be done in the middle of the execution to have some "save points"?
    auto id = LibPkg::StorageID();
    if (m_setup && m_setup->building.hasStorage()) {
        id = m_setup->building.storeBuildAction(shared_from_this());
    }

    if (m_concludeHandler) {
        m_concludeHandler();
    }
    return id;
}

BuildServiceCleanup::BuildServiceCleanup(ServiceSetup &setup, const std::shared_ptr<BuildAction> &buildAction)
    : InternalBuildAction(setup, buildAction)
    , m_dbCleanupConcluded(false)
    , m_cacheCleanupConcluded(false)
{
}

void BuildServiceCleanup::run()
{
    // validate parameter and read parameter/settings
    if (auto error = validateParameter(RequiredDatabases::None, RequiredParameters::None); !error.empty()) {
        reportError(std::move(error));
        return;
    }
    const auto flags = static_cast<BuildServiceCleanupFlags>(m_buildAction->flags);
    m_dryCacheCleanup = flags & BuildServiceCleanupFlags::DryPackageCacheCleanup;

    // get variables from setup
    auto setupLock = m_setup.lockToRead();
    m_paccachePath = findExecutable(m_setup.building.paccachePath);
    const auto packageCachePath = m_setup.building.packageCacheDir;
    setupLock.unlock();

    // find concrete cache dirs (packageCachePath does not contain arch subdir) and start invoking paccache
    m_concreteCacheDirs.reserve(8);
    try {
        for (auto i = boost::filesystem::directory_iterator(packageCachePath, boost::filesystem::directory_options::follow_directory_symlink);
             auto entry : i) {
            if (entry.path().filename_is_dot() || entry.path().filename_is_dot_dot()) {
                continue;
            }
            auto canonical = boost::filesystem::canonical(entry.path());
            if (boost::filesystem::is_directory(canonical)) {
                m_concreteCacheDirs.emplace_back(entry.path().filename().string(), std::move(canonical));
            }
        }
    } catch (const std::exception &e) {
        m_messages.errors.emplace_back(argsToString("unable to locate package cache directories: ", e.what()));
    }
    m_concreteCacheDirsIterator = m_concreteCacheDirs.begin();
    invokePaccache();

    // iterate though build actions and delete those that are unlikely to be relevant anymore
    auto count = std::size_t();
    constexpr auto stopAt = 150;
    m_setup.building.forEachBuildAction(
        [this, &count, twoWeeksAgo = DateTime::gmtNow() - TimeSpan::fromDays(14)](
            LibPkg::StorageID id, BuildAction &action, ServiceSetup::BuildSetup::VisitorBehavior &visitorBehavior) {
            if (count <= stopAt) {
                return true; // abort deletion if under 150 build actions anyways
            }
            if (m_buildAction->id == id || action.finished.isNull()) {
                return false; // avoid deleting cleanup action itself as well as any unfinished actions
            }
            if (action.result != BuildActionResult::Success || action.finished > twoWeeksAgo) {
                return false; // delete only successful actions that are at least two weeks old
            }
            visitorBehavior = ServiceSetup::BuildSetup::VisitorBehavior::Delete;
            return --count <= stopAt;
        },
        &count);
    m_messages.notes.emplace_back(argsToString("deleted ", count, " build actions"));
    auto lock = lockToWrite();
    m_dbCleanupConcluded = true;
    conclude(std::move(lock));
}

void BuildServiceCleanup::invokePaccache()
{
    if (m_concreteCacheDirsIterator == m_concreteCacheDirs.end()) {
        auto lock = lockToWrite();
        m_cacheCleanupConcluded = true;
        conclude(std::move(lock));
        return;
    }
    const auto &cacheDirArch = m_concreteCacheDirsIterator->first;
    const auto &cacheDirPath = m_concreteCacheDirsIterator->second;
    auto processSession = m_buildAction->makeBuildProcess(
        "paccache-" + cacheDirArch, "paccache-" % cacheDirArch + ".log", [this](boost::process::child &&child, ProcessResult &&result) {
            CPP_UTILITIES_UNUSED(child)
            if (result.errorCode) {
                const auto errorMessage = result.errorCode.message();
                m_messages.errors.emplace_back("unable to invoke paccache: " + errorMessage);
            } else if (result.exitCode != 0) {
                m_messages.errors.emplace_back(argsToString("paccache returned with exit code ", result.exitCode));
            }
            invokePaccache();
        });
    ++m_concreteCacheDirsIterator;
    processSession->launch(m_paccachePath, m_dryCacheCleanup ? "--dryrun" : "--remove", "--cachedir", cacheDirPath.string());
}

void BuildServiceCleanup::conclude(std::unique_lock<std::shared_mutex> &&lock)
{
    if (!m_dbCleanupConcluded || !m_cacheCleanupConcluded) {
        return;
    }
    lock.unlock();

    const auto res = m_messages.errors.empty() ? BuildActionResult::Success : BuildActionResult::Failure;
    const auto buildLock = m_setup.building.lockToWrite();
    m_buildAction->resultData = std::move(m_messages);
    reportResult(res);
}

#ifdef LIBREPOMGR_DUMMY_BUILD_ACTION_ENABLED
DummyBuildAction::DummyBuildAction(ServiceSetup &setup, const std::shared_ptr<BuildAction> &buildAction)
    : InternalBuildAction(setup, buildAction)
    , m_counter(0)
    , m_timer(setup.building.ioContext)
{
    m_buildAction->setStopHandler(std::bind(&DummyBuildAction::stop, this));
}

void DummyBuildAction::run()
{
    // validate parameter
    if (auto error = validateParameter(RequiredDatabases::None, RequiredParameters::None); !error.empty()) {
        reportError(std::move(error));
        return;
    }
    if (m_buildAction->directory.empty()) {
        reportError("Unable to find working directory: no directory name specified");
        return;
    }

    // find test files
    auto testApp = TestApplication();
    auto scriptPath = std::string();
    try {
        scriptPath = testFilePath("scripts/print_some_data.sh");
    } catch (const std::exception &e) {
        reportError(e.what());
        return;
    }

    // create working directory
    m_workingDirectory = "dummy/" + m_buildAction->directory;
    try {
        std::filesystem::create_directories(m_workingDirectory);
    } catch (const std::filesystem::filesystem_error &e) {
        reportError(argsToString("Unable to make working directory: ", e.what()));
        return;
    }

    // add an artefact
    auto buildActionsWriteLock = m_setup.building.lockToWrite();
    m_buildAction->artefacts.emplace_back(m_workingDirectory + "/some-artefact.txt");
    buildActionsWriteLock.unlock();
    try {
        writeFile(m_buildAction->artefacts.back(), "artefact contents\n");
    } catch (const std::ios_base::failure &e) {
        reportError(argsToString("Unable to make artefact: ", e.what()));
        return;
    }

    // launch subprocess producing a logfile
    m_logProcess
        = m_buildAction->makeBuildProcess("dummy", m_workingDirectory + "/foo.log", [this](boost::process::child &&child, ProcessResult &&result) {
              CPP_UTILITIES_UNUSED(child)
              m_logProcess = nullptr;
              m_buildAction->appendOutput("log process exited with code: ", result.exitCode, '\n');
              if (!result.error.empty()) {
                  m_buildAction->appendOutput("log process error: ", result.error, '\n');
              }
              if (!m_buildAction->isAborted()) {
                  stop();
              }
          });
    m_logProcess->launch(scriptPath, "1");

    continuePrinting();
}

void DummyBuildAction::continuePrinting()
{
    m_timer.expires_from_now(boost::posix_time::seconds(1));
    m_timer.async_wait(std::bind(&DummyBuildAction::printLine, this));
}

void DummyBuildAction::printLine()
{
    if (m_buildAction->isAborted()) {
        return;
    }
    m_buildAction->appendOutput("Output: ", ++m_counter, '\n');
    continuePrinting();
}

void DummyBuildAction::stop()
{
    m_buildAction->appendOutput("stopping"sv);
    boost::system::error_code timerCancelError;
    m_timer.cancel(timerCancelError);
    if (timerCancelError.failed()) {
        m_buildAction->appendOutput("failed to cancel timer: ", timerCancelError.message(), '\n');
    }
    std::error_code terminateError;
    if (m_logProcess) {
        m_logProcess->group.terminate(terminateError);
        if (terminateError) {
            m_buildAction->appendOutput("failed to terminate logging process: ", terminateError.message(), '\n');
        }
    }
    reportSuccess();
}
#endif // LIBREPOMGR_DUMMY_BUILD_ACTION_ENABLED

} // namespace LibRepoMgr
