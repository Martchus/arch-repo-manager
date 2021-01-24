#ifndef LIBREPOMGR_JSON_H
#define LIBREPOMGR_JSON_H

#include "./global.h"

#include <c++utilities/conversion/stringbuilder.h>
#include <c++utilities/io/misc.h>
#include <c++utilities/misc/traits.h>

#include <reflective_rapidjson/json/reflector.h>

#include <rapidjson/document.h>
#include <rapidjson/error/en.h>

#include <filesystem>
#include <functional>
#include <stdexcept>

namespace LibRepoMgr {

enum class DumpJsonExistingFileHandling {
    Backup,
    Skip,
    Override,
};

enum class RestoreJsonExistingFileHandling {
    RequireExistingFile,
    Skip,
};

LIBREPOMGR_EXPORT std::string formatJsonDeserializationError(
    std::string_view fileNameWithoutExtension, const ReflectiveRapidJSON::JsonDeserializationError &error);
LIBREPOMGR_EXPORT std::string determineJsonPath(
    std::string_view directoryPath, std::string_view fileNameWithoutExtension, DumpJsonExistingFileHandling existingFileHandling);
LIBREPOMGR_EXPORT std::string determineJsonPath(
    std::string_view directoryPath, std::string_view fileNameWithoutExtension, RestoreJsonExistingFileHandling existingFileHandling);
LIBREPOMGR_EXPORT void writeJsonDocument(const RAPIDJSON_NAMESPACE::Document &document, std::string_view outputPath);
LIBREPOMGR_EXPORT std::filesystem::path handleOldJsonFile(const std::filesystem::path &jsonFilePath);

template <typename DocumentGenerator,
    CppUtilities::Traits::EnableIf<std::is_invocable_r<RAPIDJSON_NAMESPACE::Document, DocumentGenerator>> * = nullptr>
std::string dumpJsonDocument(DocumentGenerator &&makeDocument, std::string_view directoryPath, std::string_view fileNameWithoutExtension,
    DumpJsonExistingFileHandling existingFileHandling)
{
    try {
        const auto jsonPath = determineJsonPath(directoryPath, fileNameWithoutExtension, existingFileHandling);
        if (jsonPath.empty()) {
            return jsonPath; // skipping
        }
        const auto jsonDocument = makeDocument();
        const auto oldFilePath = handleOldJsonFile(jsonPath);
        writeJsonDocument(jsonDocument, jsonPath);
        if (!oldFilePath.empty()) {
            std::filesystem::remove(oldFilePath);
        }
        return jsonPath;
    } catch (const std::runtime_error &e) {
        throw std::runtime_error(CppUtilities::argsToString("Unable to make ", fileNameWithoutExtension, ".json: ", e.what()));
    }
}

template <typename ObjectType>
std::string dumpJsonObject(const ObjectType &obj, std::string_view directoryPath, std::string_view fileNameWithoutExtension,
    DumpJsonExistingFileHandling existingFileHandling)
{
    return dumpJsonDocument([&obj] { return ReflectiveRapidJSON::JsonReflector::toJsonDocument<ObjectType>(static_cast<const ObjectType &>(obj)); },
        directoryPath, fileNameWithoutExtension, existingFileHandling);
}

template <typename RestoreFunction,
    CppUtilities::Traits::EnableIf<std::is_invocable<RestoreFunction, const std::string &, ReflectiveRapidJSON::JsonDeserializationErrors *>>
        * = nullptr>
std::string restoreJsonObject(RestoreFunction &&restoreFunction, std::string_view directoryPath, std::string_view fileNameWithoutExtension,
    RestoreJsonExistingFileHandling existingFileHandling)
{
    try {
        const auto jsonPath = determineJsonPath(directoryPath, fileNameWithoutExtension, existingFileHandling);
        if (jsonPath.empty()) {
            return jsonPath; // skipping
        }
        ReflectiveRapidJSON::JsonDeserializationErrors errors{};
        errors.throwOn = ReflectiveRapidJSON::JsonDeserializationErrors::ThrowOn::All;
        restoreFunction(CppUtilities::readFile(jsonPath), &errors);
        return jsonPath;
    } catch (const ReflectiveRapidJSON::JsonDeserializationError &e) {
        throw std::runtime_error(formatJsonDeserializationError(fileNameWithoutExtension, e));
    } catch (const RAPIDJSON_NAMESPACE::ParseResult &e) {
        throw std::runtime_error(CppUtilities::argsToString("Unable to restore ", fileNameWithoutExtension, ".json: ", "parse error at ", e.Offset(),
            ": ", RAPIDJSON_NAMESPACE::GetParseError_En(e.Code())));
    } catch (const std::runtime_error &e) {
        throw std::runtime_error(CppUtilities::argsToString("Unable to restore ", fileNameWithoutExtension, ".json: ", e.what()));
    }
}

template <typename ObjectType,
    CppUtilities::Traits::DisableIf<std::is_invocable<ObjectType, const std::string &, ReflectiveRapidJSON::JsonDeserializationErrors *>> * = nullptr>
std::string restoreJsonObject(
    ObjectType &obj, std::string_view directoryPath, std::string_view fileNameWithoutExtension, RestoreJsonExistingFileHandling existingFileHandling)
{
    return restoreJsonObject(
        [&obj](const std::string &json, ReflectiveRapidJSON::JsonDeserializationErrors *errors) {
            obj = ReflectiveRapidJSON::JsonReflector::fromJson<ObjectType>(json, errors);
        },
        directoryPath, fileNameWithoutExtension, existingFileHandling);
}

inline auto serializeParseError(const RAPIDJSON_NAMESPACE::ParseResult &e)
{
    return std::make_tuple("parse error at ", e.Offset(), ": ", RAPIDJSON_NAMESPACE::GetParseError_En(e.Code()));
}

inline auto serializeParseResult(const RAPIDJSON_NAMESPACE::ParseResult &e)
{
    return e.IsError() ? CppUtilities::tupleToString(serializeParseError(e)) : std::string("ok");
}

} // namespace LibRepoMgr

#endif // LIBREPOMGR_JSON_H
