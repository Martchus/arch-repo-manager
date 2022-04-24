#ifndef LIBREPOMGR_RESOURCE_USAGE_H
#define LIBREPOMGR_RESOURCE_USAGE_H

#include "./global.h"

#include <reflective_rapidjson/json/serializable.h>

namespace LibRepoMgr {

struct ServiceSetup;

struct LIBREPOMGR_EXPORT MemoryUsage : public ReflectiveRapidJSON::JsonSerializable<MemoryUsage> {
    explicit MemoryUsage();

    std::size_t virtualMemory = 0;
    std::size_t residentSetSize = 0;
    std::size_t sharedResidentSetSize = 0;
    std::size_t peakResidentSetSize = 0;
};

struct LIBREPOMGR_EXPORT ResourceUsage : public MemoryUsage, public ReflectiveRapidJSON::JsonSerializable<ResourceUsage> {
    explicit ResourceUsage(ServiceSetup &setup);

    std::size_t packageDbSize = 0;
    std::size_t actionsDbSize = 0;
    std::size_t cachedPackages = 0;
    std::size_t actionsCount = 0;
    std::size_t runningActionsCount = 0;
};

} // namespace LibRepoMgr

#endif // LIBREPOMGR_RESOURCE_USAGE_H
