#include "./resourceusage.h"
#include "./serversetup.h"

#if defined(PLATFORM_WINDOWS)
#include <psapi.h>
#include <windows.h>
#elif defined(PLATFORM_LINUX) || defined(PLATFORM_UNIX)
#include <cstdio>
#include <sys/resource.h>
#include <unistd.h>
#endif

#include <filesystem>

namespace LibRepoMgr {

MemoryUsage::MemoryUsage()
{
#if defined(PLATFORM_WINDOWS)
    auto info = PROCESS_MEMORY_COUNTERS();
    GetProcessMemoryInfo(GetCurrentProcess(), &info, sizeof(info));
    physicalMemory = static_cast<std::size_t>(info.WorkingSetSize);
    physicalMemoryPeak = static_cast<std::size_t>(info.PeakWorkingSetSize);
#elif defined(PLATFORM_LINUX) || defined(PLATFORM_UNIX)
    if (auto *const statm = std::fopen("/proc/self/statm", "r")) {
        auto pages = 0l, pagesInRealMemory = 0l, pagesShared = 0l;
        if (std::fscanf(statm, "%ld%ld%ld", &pages, &pagesInRealMemory, &pagesShared) == 3) {
            const auto pageSize = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
            virtualMemory = static_cast<std::size_t>(pages) * pageSize;
            residentSetSize = static_cast<std::size_t>(pagesInRealMemory) * pageSize;
            sharedResidentSetSize = static_cast<std::size_t>(pagesShared) * pageSize;
        }
        std::fclose(statm);
    }
    struct rusage rusage;
    getrusage(RUSAGE_SELF, &rusage);
    peakResidentSetSize = static_cast<std::size_t>(rusage.ru_maxrss) * 1024u;
#endif
}

ResourceUsage::ResourceUsage(ServiceSetup &setup)
{
    auto ec = std::error_code();
    packageDbSize = std::filesystem::file_size(setup.dbPath, ec);
    actionsDbSize = std::filesystem::file_size(setup.building.dbPath, ec);
    cachedPackages = setup.config.cachedPackages();
    actionsCount = setup.building.buildActionCount();
    runningActionsCount = setup.building.runningBuildActionCount();
}

} // namespace LibRepoMgr

#include "reflection/resourceusage.h"
