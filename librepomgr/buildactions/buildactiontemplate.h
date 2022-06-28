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

struct BuildActionSequenceNode;
struct LIBREPOMGR_EXPORT BuildActionSequenceData : public ReflectiveRapidJSON::JsonSerializable<BuildActionSequenceData> {
    std::string name;
    bool concurrent = false;
};
struct LIBREPOMGR_EXPORT BuildActionSequenceNodes : public ReflectiveRapidJSON::JsonSerializable<BuildActionSequenceData> {
    std::vector<BuildActionSequenceNode> actions;
};
struct LIBREPOMGR_EXPORT BuildActionSequence : public BuildActionSequenceData, public BuildActionSequenceNodes {};
struct LIBREPOMGR_EXPORT BuildActionSequenceNode : public std::variant<std::string, BuildActionSequence> {};

struct LIBREPOMGR_EXPORT BuildTask : public BuildActionSequence, public ReflectiveRapidJSON::JsonSerializable<BuildTask> {
    std::string desc;
    std::string category;
    CppUtilities::TimeSpan frequency = CppUtilities::TimeSpan::infinity();
};

struct LIBREPOMGR_EXPORT BuildPresets : public ReflectiveRapidJSON::JsonSerializable<BuildPresets> {
    std::unordered_map<std::string, BuildActionTemplate> templates;
    std::unordered_map<std::string, BuildTask> tasks;
};

} // namespace LibRepoMgr

namespace ReflectiveRapidJSON {
namespace JsonReflector {

// declare custom (de)serialization for BuildActionSequence
template <>
LIBREPOMGR_EXPORT void push<LibRepoMgr::BuildActionSequence>(
    const LibRepoMgr::BuildActionSequence &reflectable, RAPIDJSON_NAMESPACE::Value &value, RAPIDJSON_NAMESPACE::Document::AllocatorType &allocator);
template <>
LIBREPOMGR_EXPORT void pull<LibRepoMgr::BuildActionSequence>(LibRepoMgr::BuildActionSequence &reflectable,
    const RAPIDJSON_NAMESPACE::GenericValue<RAPIDJSON_NAMESPACE::UTF8<char>> &value, JsonDeserializationErrors *errors);

// declare custom (de)serialization for BuildActionSequenceNode
template <>
LIBREPOMGR_EXPORT void push<LibRepoMgr::BuildActionSequenceNode>(const LibRepoMgr::BuildActionSequenceNode &reflectable,
    RAPIDJSON_NAMESPACE::Value &value, RAPIDJSON_NAMESPACE::Document::AllocatorType &allocator);
template <>
LIBREPOMGR_EXPORT void pull<LibRepoMgr::BuildActionSequenceNode>(LibRepoMgr::BuildActionSequenceNode &reflectable,
    const RAPIDJSON_NAMESPACE::GenericValue<RAPIDJSON_NAMESPACE::UTF8<char>> &value, JsonDeserializationErrors *errors);

} // namespace JsonReflector
} // namespace ReflectiveRapidJSON

#endif // LIBREPOMGR_BUILD_ACTION_TEMPLATE_H
