#include "./buildactiontemplate.h"

#include <reflective_rapidjson/json/reflector-chronoutilities.h>

#include "reflection/buildactiontemplate.h"

using namespace std;
using namespace CppUtilities;

namespace LibRepoMgr {

} // namespace LibRepoMgr

namespace ReflectiveRapidJSON {
namespace JsonReflector {

template <>
LIBREPOMGR_EXPORT void push<LibRepoMgr::BuildActionSequence>(
    const LibRepoMgr::BuildActionSequence &reflectable, RAPIDJSON_NAMESPACE::Value &value, RAPIDJSON_NAMESPACE::Document::AllocatorType &allocator)
{
    if (reflectable.name.empty() && !reflectable.concurrent) {
        push(reflectable.actions, value, allocator);
    } else {
        push(static_cast<const LibRepoMgr::BuildActionSequenceData &>(reflectable), value, allocator);
        push(static_cast<const LibRepoMgr::BuildActionSequenceNodes &>(reflectable), value, allocator);
    }
}

template <>
LIBREPOMGR_EXPORT void pull<LibRepoMgr::BuildActionSequence>(LibRepoMgr::BuildActionSequence &reflectable,
    const RAPIDJSON_NAMESPACE::GenericValue<RAPIDJSON_NAMESPACE::UTF8<char>> &value, JsonDeserializationErrors *errors)
{
    if (value.IsArray()) {
        reflectable.name.clear();
        reflectable.concurrent = false;
        pull(reflectable.actions, value, errors);
    } else if (value.IsObject()) {
        pull(static_cast<LibRepoMgr::BuildActionSequenceData &>(reflectable), value, errors);
        pull(static_cast<LibRepoMgr::BuildActionSequenceNodes &>(reflectable), value, errors);
    } else if (errors) {
        errors->reportTypeMismatch<LibRepoMgr::BuildActionSequence>(value.GetType());
    }
}

template <>
LIBREPOMGR_EXPORT void push<LibRepoMgr::BuildActionSequenceNode>(const LibRepoMgr::BuildActionSequenceNode &reflectable,
    RAPIDJSON_NAMESPACE::Value &value, RAPIDJSON_NAMESPACE::Document::AllocatorType &allocator)
{
    if (const auto *const name = std::get_if<std::string>(&reflectable)) {
        push(*name, value, allocator);
    } else if (const auto *const sequence = std::get_if<LibRepoMgr::BuildActionSequence>(&reflectable)) {
        push(*sequence, value, allocator);
    }
}

template <>
LIBREPOMGR_EXPORT void pull<LibRepoMgr::BuildActionSequenceNode>(LibRepoMgr::BuildActionSequenceNode &reflectable,
    const RAPIDJSON_NAMESPACE::GenericValue<RAPIDJSON_NAMESPACE::UTF8<char>> &value, JsonDeserializationErrors *errors)
{
    if (value.IsString()) {
        auto &name = reflectable.emplace<std::string>();
        pull(name, value, errors);
    } else {
        auto &sequence = reflectable.emplace<LibRepoMgr::BuildActionSequence>();
        pull(sequence, value, errors);
    }
}

} // namespace JsonReflector
} // namespace ReflectiveRapidJSON
