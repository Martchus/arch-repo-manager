#include "./buildactionprivate.h"

#include "../serversetup.h"

namespace LibRepoMgr {

ReloadConfiguration::ReloadConfiguration(ServiceSetup &setup, const std::shared_ptr<BuildAction> &buildAction)
    : InternalBuildAction(setup, buildAction)
{
}

void ReloadConfiguration::run()
{
    auto configLock = init(BuildActionAccess::WriteConfig, RequiredDatabases::None, RequiredParameters::None);
    if (std::holds_alternative<std::monostate>(configLock)) {
        return;
    }

    m_setup.config.markAllDatabasesToBeDiscarded();
    auto setupLock = m_setup.lockToWrite();
    m_setup.auth.users.clear();
    m_setup.loadConfigFiles(false);
    setupLock.unlock();
    m_setup.config.discardDatabases();
    std::get<std::unique_lock<std::shared_mutex>>(configLock).unlock();

    {
        const auto buildActionLock = m_setup.building.lockToWrite();
        reportSuccess();
    }
    {
        const auto configReadLock = m_setup.config.lockToRead();
        m_setup.saveState();
        m_setup.printDatabases();
    }
}

} // namespace LibRepoMgr
