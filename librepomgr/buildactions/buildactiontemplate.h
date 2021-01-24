#ifndef LIBREPOMGR_BUILD_ACTION_TEMPLATE_H
#define LIBREPOMGR_BUILD_ACTION_TEMPLATE_H

#include "./buildactionmeta.h"

#include <reflective_rapidjson/json/serializable.h>

#include <c++utilities/chrono/timespan.h>

#include <unordered_map>

namespace LibRepoMgr {

struct LIBREPOMGR_EXPORT BuildActionTemplate : public ReflectiveRapidJSON::JsonSerializable<BuildActionTemplate> {
    std::string directory;
    std::vector<std::string> packageNames;
    std::vector<std::string> sourceDbs, destinationDbs;
    std::unordered_map<std::string, std::string> settings;
    BuildActionFlagType flags = noBuildActionFlags;
    BuildActionType type = BuildActionType::Invalid;
    CppUtilities::TimeSpan maxFrequency = CppUtilities::TimeSpan::infinity();
};

struct LIBREPOMGR_EXPORT BuildTask : public ReflectiveRapidJSON::JsonSerializable<BuildTask> {
    std::string name;
    std::string desc;
    std::string category;
    std::vector<std::string> actions;
    CppUtilities::TimeSpan frequency = CppUtilities::TimeSpan::infinity();
};

struct LIBREPOMGR_EXPORT BuildPresets : public ReflectiveRapidJSON::JsonSerializable<BuildPresets> {
    std::unordered_map<std::string, BuildActionTemplate> templates;
    std::unordered_map<std::string, BuildTask> tasks;
};

} // namespace LibRepoMgr

#endif // LIBREPOMGR_BUILD_ACTION_TEMPLATE_H
