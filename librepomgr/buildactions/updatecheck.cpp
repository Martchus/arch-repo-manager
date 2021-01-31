#include "./buildactionprivate.h"

#include "../serversetup.h"

#include "../webclient/aur.h"

using namespace std;
using namespace CppUtilities;

namespace LibRepoMgr {

UpdateCheck::UpdateCheck(ServiceSetup &setup, const std::shared_ptr<BuildAction> &buildAction)
    : InternalBuildAction(setup, buildAction)
{
}

void UpdateCheck::run()
{
    const auto flags = static_cast<CheckForUpdatesFlags>(m_buildAction->flags);
    m_options = static_cast<LibPkg::UpdateCheckOptions>(flags);

    auto configReadLock = init(BuildActionAccess::ReadConfig,
        RequiredDatabases::OneOrMoreSources | RequiredDatabases::OneDestination | RequiredDatabases::AllowFromAur, RequiredParameters::None);
    if (holds_alternative<monostate>(configReadLock)) {
        return;
    }

    if (m_fromAur && !m_packageLookupDone
        && WebClient::queryAurPackagesForDatabase(m_buildAction->log(), m_setup, m_setup.building.ioContext,
            &get<shared_lock<shared_mutex>>(configReadLock), **m_destinationDbs.begin(), [this](std::vector<std::shared_ptr<LibPkg::Package>> &&) {
                m_packageLookupDone = true;
                run();
            })) {
        return; // wait for async operation to complete
    }

    auto result = checkForUpdates();
    get<shared_lock<shared_mutex>>(configReadLock).unlock();

    auto buildActionWriteLock = m_setup.building.lockToWrite();
    m_buildAction->resultData = move(result);
    reportSuccess();
}

LibPkg::PackageUpdates UpdateCheck::checkForUpdates()
{
    vector<LibPkg::Database *> sourceDbs;
    sourceDbs.reserve(m_fromAur ? m_sourceDbs.size() + 1 : m_sourceDbs.size());
    sourceDbs.insert(sourceDbs.begin(), m_sourceDbs.cbegin(), m_sourceDbs.cend());
    if (m_fromAur) {
        sourceDbs.emplace_back(&m_setup.config.aur);
    }
    return (**m_destinationDbs.begin()).checkForUpdates(sourceDbs, m_options);
}

} // namespace LibRepoMgr
