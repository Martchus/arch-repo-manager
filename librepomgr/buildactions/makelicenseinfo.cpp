#include "./buildactionprivate.h"

#include "../serversetup.h"

namespace LibRepoMgr {

MakeLicenseInfo::MakeLicenseInfo(ServiceSetup &setup, const std::shared_ptr<BuildAction> &buildAction)
    : InternalBuildAction(setup, buildAction)
{
}

void MakeLicenseInfo::run()
{
    auto configReadLock = init(BuildActionAccess::ReadConfig, RequiredDatabases::None, RequiredParameters::Packages);
    if (std::holds_alternative<std::monostate>(configReadLock)) {
        return;
    }

    auto result = m_setup.config.computeLicenseInfo(m_buildAction->packageNames);
    std::get<std::shared_lock<std::shared_mutex>>(configReadLock).unlock();

    const auto buildActionWriteLock = m_setup.building.lockToWrite();
    m_buildAction->outputMimeType = "application/json";
    m_buildAction->resultData = std::move(result);
    reportResult(result.success ? BuildActionResult::Success : BuildActionResult::Failure);
}

} // namespace LibRepoMgr
