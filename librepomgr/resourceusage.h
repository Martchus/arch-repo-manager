#ifndef LIBREPOMGR_RESOURCE_USAGE_H
#define LIBREPOMGR_RESOURCE_USAGE_H

#include "./global.h"

#include <reflective_rapidjson/json/serializable.h>

namespace LibRepoMgr {

struct LIBREPOMGR_EXPORT ResourceUsage : public ReflectiveRapidJSON::JsonSerializable<ResourceUsage> {
    explicit ResourceUsage();

    std::size_t virtualMemory = 0;
    std::size_t residentSetSize = 0;
    std::size_t sharedResidentSetSize = 0;
    std::size_t peakResidentSetSize = 0;
    std::size_t packageDbSize = 0;
    std::size_t actionsDbSize = 0;
    std::size_t cachedPackages = 0;
    std::size_t actionsCount = 0;
    std::size_t runningActionsCount = 0;
};

} // namespace LibRepoMgr

#endif // LIBREPOMGR_RESOURCE_USAGE_H
