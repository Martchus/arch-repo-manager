#include "./buildactionprivate.h"

#include "../webapi/session.h"

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
        reportError(move(error));
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
        reportError(move(error));
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
    m_buildAction->resultData = move(error);
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
    : id(id)
    , m_log(this)
    , m_setup(setup)
    , m_stopHandler(std::bind(&BuildAction::terminateOngoingBuildProcesses, this))
{
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
void BuildAction::start(ServiceSetup &setup)
{
    if (!isScheduled()) {
        return;
    }

    started = DateTime::gmtNow();
    status = BuildActionStatus::Running;
    m_setup = &setup;

    switch (type) {
    case BuildActionType::Invalid:
        resultData = "type is invalid";
        conclude(BuildActionResult::Failure);
        break;
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
    default:
        resultData = "not implemented yet or invalid type";
        conclude(BuildActionResult::Failure);
    }
}

void BuildAction::startAfterOtherBuildActions(ServiceSetup &setup, const std::vector<std::shared_ptr<BuildAction>> &startsAfterBuildActions)
{
    auto allSucceeded = true;
    for (auto &previousBuildAction : startsAfterBuildActions) {
        if (!previousBuildAction->hasSucceeded()) {
            previousBuildAction->m_followUpActions.emplace_back(weak_from_this());
            allSucceeded = false;
        }
    }
    if (allSucceeded) {
        start(setup);
    }
}

void BuildAction::assignStartAfter(const std::vector<std::shared_ptr<BuildAction>> &startsAfterBuildActions)
{
    for (auto &previousBuildAction : startsAfterBuildActions) {
        startAfter.emplace_back(previousBuildAction->id);
        if (!previousBuildAction->hasSucceeded()) {
            previousBuildAction->m_followUpActions.emplace_back(weak_from_this());
        }
    }
}

void BuildAction::abort()
{
    m_aborted.store(true);
    if (m_setup && m_stopHandler) {
        boost::asio::post(m_setup->building.ioContext.get_executor(), m_stopHandler);
    }
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
    boost::asio::post(m_setup->building.ioContext.get_executor(), forward<Callback>(codeToRun));
}

/*!
 * \brief Internally called to conclude the build action.
 */
void BuildAction::conclude(BuildActionResult result)
{
    // set fields accordingly
    status = BuildActionStatus::Finished;
    this->result = result;
    finished = DateTime::gmtNow();

    // tell clients waiting for output that it's over
    const auto outputStreamingLock = std::unique_lock<std::mutex>(m_outputStreamingMutex);
    for (auto i = m_bufferingForSession.begin(); i != m_bufferingForSession.end();) {
        if (!i->second->currentlySentBuffers.empty() || !i->second->outstandingBuffersToSend.empty()) {
            ++i;
            continue;
        }
        boost::beast::net::async_write(i->first->socket(), boost::beast::http::make_chunk_last(),
            std::bind(&WebAPI::Session::responded, i->first, std::placeholders::_1, std::placeholders::_2, true));
        i = m_bufferingForSession.erase(i);
    }

    // start globally visible follow-up actions if succeeded
    if (result == BuildActionResult::Success && m_setup) {
        for (auto &maybeStillValidFollowUpAction : m_followUpActions) {
            auto followUpAction = maybeStillValidFollowUpAction.lock();
            if (followUpAction && followUpAction->isScheduled()
                && BuildAction::haveSucceeded(m_setup->building.getBuildActions(followUpAction->startAfter))) {
                followUpAction->start(*m_setup);
            }
        }
        // note: Not cleaning up the follow-up actions here because at some point I might implement recursive restarting.
    }

    if (m_concludeHandler) {
        m_concludeHandler();
    }
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
        reportError(move(error));
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
