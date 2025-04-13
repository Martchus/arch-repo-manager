#include "./buildactionprivate.h"
#include "./subprocess.h"

#include "../serversetup.h"

#include <c++utilities/io/ansiescapecodes.h>

#include <boost/process/v1/search_path.hpp>
#include <boost/process/v1/start_dir.hpp>

using namespace std;
using namespace CppUtilities;
using namespace CppUtilities::EscapeCodes;

namespace LibRepoMgr {

CustomCommand::CustomCommand(ServiceSetup &setup, const std::shared_ptr<BuildAction> &buildAction)
    : InternalBuildAction(setup, buildAction)
    , m_command(nullptr)
{
}

void CustomCommand::run()
{
    // validate and read parameter/settings
    if (auto error = validateParameter(RequiredDatabases::None, RequiredParameters::None); !error.empty()) {
        reportError(move(error));
        return;
    }
    if (m_buildAction->directory.empty()) {
        reportError("No directory specified.");
        return;
    }
    auto &metaInfo = m_setup.building.metaInfo;
    auto metaInfoLock = metaInfo.lockToRead();
    const auto &typeInfo = metaInfo.typeInfoForId(BuildActionType::CustomCommand);
    const auto commandSetting = typeInfo.settings[static_cast<std::size_t>(CustomCommandSettings::Command)].param;
    const auto sharedLocksSetting = typeInfo.settings[static_cast<std::size_t>(CustomCommandSettings::SharedLocks)].param;
    const auto exclusiveLocksSetting = typeInfo.settings[static_cast<std::size_t>(CustomCommandSettings::ExclusiveLocks)].param;
    metaInfoLock.unlock();
    const auto &command = findSetting(commandSetting);
    if (command.empty()) {
        reportError("No command specified.");
        return;
    }

    // prepare working dir
    try {
        m_workingDirectory = determineWorkingDirectory(customCommandsWorkingDirectory);
        if (!std::filesystem::is_directory(m_workingDirectory)) {
            std::filesystem::create_directories(m_workingDirectory);
        }
    } catch (const std::filesystem::filesystem_error &e) {
        reportError(argsToString("Unable to create working directory: ", e.what()));
        return;
    }

    m_buildAction->appendOutput(Phrases::InfoMessage, "Running custom command: ", command, '\n');

    // prepare process and finish handler
    m_command = &command;
    m_process = m_buildAction->makeBuildProcess("command", m_workingDirectory + "/the.log", [this](boost::process::v1::child &&, ProcessResult &&result) {
        if (result.errorCode) {
            m_buildAction->appendOutput(Phrases::InfoMessage, "Unable to invoke command: ", result.errorCode.message());
            reportError(result.errorCode.message());
            return;
        }
        m_buildAction->appendOutput(
            result.exitCode == 0 ? Phrases::InfoMessage : Phrases::ErrorMessage, "Command exited with return code ", result.exitCode);
        if (result.exitCode != 0) {
            reportError(argsToString("non-zero exit code ", result.exitCode));
            return;
        }
        const auto buildLock = m_setup.building.lockToWrite();
        reportSuccess();
    });
    if (!m_process) {
        return;
    }

    // acquire locks
    // note: Using an std::set here (instead of a std::vector) to ensure we don't attempt to acquire the same lock twice and to ensure
    //       locks are always acquired in the same order (to prevent deadlocks).
    m_sharedLockNames = splitStringSimple<std::set<std::string_view>>(findSetting(sharedLocksSetting), ",");
    m_sharedLockNamesIterator = m_sharedLockNames.begin();
    m_exclusiveLockNames = splitStringSimple<std::set<std::string_view>>(findSetting(exclusiveLocksSetting), ",");
    m_exclusiveLockNamesIterator = m_exclusiveLockNames.begin();
    m_process->locks().reserve(m_sharedLockNames.size() + m_exclusiveLockNames.size());
    acquireNextSharedLock();
}

void LibRepoMgr::CustomCommand::acquireNextSharedLock()
{
    if (m_sharedLockNamesIterator == m_sharedLockNames.end()) {
        // continue acquiring exclusive locks once all shared locks are acquired
        acquireNextExclusiveLock();
        return;
    }
    if (m_sharedLockNamesIterator->empty()) {
        ++m_sharedLockNamesIterator;
        acquireNextSharedLock();
        return;
    }
    m_setup.locks.acquireToRead(
        m_buildAction->log(), std::string(*m_sharedLockNamesIterator), [this, buildAction = m_buildAction](SharedLoggingLock &&lock) {
            m_process->locks().emplace_back(std::move(lock));
            ++m_sharedLockNamesIterator;
            acquireNextSharedLock();
        });
}

void LibRepoMgr::CustomCommand::acquireNextExclusiveLock()
{
    if (m_exclusiveLockNamesIterator == m_exclusiveLockNames.end()) {
        // execute process once all locks are acquired
        m_process->launch(boost::process::v1::start_dir(m_workingDirectory), boost::process::v1::search_path("bash"), "-ec", *m_command);
        m_process.reset();
        return;
    }
    if (m_exclusiveLockNamesIterator->empty()) {
        ++m_exclusiveLockNamesIterator;
        acquireNextExclusiveLock();
        return;
    }
    m_setup.locks.acquireToWrite(
        m_buildAction->log(), std::string(*m_exclusiveLockNamesIterator), [this, buildAction = m_buildAction](UniqueLoggingLock &&lock) {
            m_process->locks().emplace_back(std::move(lock));
            ++m_exclusiveLockNamesIterator;
            acquireNextExclusiveLock();
        });
}

} // namespace LibRepoMgr
