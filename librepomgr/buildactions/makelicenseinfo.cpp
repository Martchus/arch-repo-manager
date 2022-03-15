#include "./buildactionprivate.h"

#include "../serversetup.h"

#include <c++utilities/conversion/stringbuilder.h>
#include <c++utilities/io/misc.h>
#include <c++utilities/io/path.h>

using namespace CppUtilities;

namespace LibRepoMgr {

MakeLicenseInfo::MakeLicenseInfo(ServiceSetup &setup, const std::shared_ptr<BuildAction> &buildAction)
    : InternalBuildAction(setup, buildAction)
{
}

void MakeLicenseInfo::run()
{
    // determine output file path
    auto &metaInfo = m_setup.building.metaInfo;
    auto metaInfoLock = metaInfo.lockToRead();
    const auto &typeInfo = metaInfo.typeInfoForId(BuildActionType::MakeLicenseInfo);
    const auto outputFilePathSetting = typeInfo.settings[static_cast<std::size_t>(MakeLicenseInfoSettings::OutputFilePath)].param;
    metaInfoLock.unlock();
    auto outputFilePath = findSetting(outputFilePathSetting);
    auto outputDir = std::string();
    if (!outputFilePath.empty()) {
        outputDir = directory(outputFilePath);
    } else if (!m_buildAction->directory.empty()) {
        const auto setupReadLock = m_setup.lockToRead();
        outputDir = m_setup.building.workingDirectory % "/license-info/" + m_buildAction->directory;
    } else {
        reportError("Unable to create working directory: no directory name or output file path specified");
        return;
    }

    // validate params and acquire read lock
    auto configReadLock = init(BuildActionAccess::ReadConfig, RequiredDatabases::None, RequiredParameters::Packages);
    if (std::holds_alternative<std::monostate>(configReadLock)) {
        return;
    }

    auto result = m_setup.config.computeLicenseInfo(m_buildAction->packageNames);
    std::get<std::shared_lock<std::shared_mutex>>(configReadLock).unlock();
    auto wroteOutputFile = false;
    if (outputFilePath.empty()) {
        outputFilePath = outputDir % '/' % m_buildAction->id + "-summary.md";
    }
    try {
        std::filesystem::create_directories(outputDir);
        writeFile(outputFilePath, result.licenseSummary);
        result.licenseSummary = std::move(outputFilePath);
        wroteOutputFile = true;
    } catch (const std::exception &e) {
        result.notes.emplace_back("Unable to write output file \"" % outputFilePath % "\": " + e.what());
        result.success = false;
    }

    const auto buildActionWriteLock = m_setup.building.lockToWrite();
    if (wroteOutputFile) {
        m_buildAction->artefacts.emplace_back(result.licenseSummary);
    }
    m_buildAction->resultData = std::move(result);
    reportResult(result.success ? BuildActionResult::Success : BuildActionResult::Failure);
}

} // namespace LibRepoMgr
