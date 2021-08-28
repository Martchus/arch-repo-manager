#include "./json.h"

#include <c++utilities/conversion/stringbuilder.h>
#include <c++utilities/io/misc.h>

#include <reflective_rapidjson/json/errorformatting.h>

#include <rapidjson/filewritestream.h>
#include <rapidjson/prettywriter.h>

#include <cstring>
#include <filesystem>

using namespace CppUtilities;

namespace LibRepoMgr {

std::string formatJsonDeserializationError(std::string_view fileNameWithoutExtension, const ReflectiveRapidJSON::JsonDeserializationError &error)
{
    return "Unable to restore " % fileNameWithoutExtension % ".json: " + ReflectiveRapidJSON::formatJsonDeserializationError(error);
}

std::string determineJsonPath(
    std::string_view directoryPath, std::string_view fileNameWithoutExtension, DumpJsonExistingFileHandling existingFileHandling)
{
    std::filesystem::path jsonPath = directoryPath % '/' % fileNameWithoutExtension + ".json";
    if (!std::filesystem::exists(jsonPath)) {
        return jsonPath;
    }
    switch (existingFileHandling) {
    case DumpJsonExistingFileHandling::Backup:
        for (size_t counter = 0;; ++counter) {
            const auto backupPath = directoryPath % '/' % fileNameWithoutExtension % '-' % counter + ".json";
            if (std::filesystem::exists(backupPath)) {
                continue;
            }
            std::filesystem::rename(jsonPath, backupPath);
            break;
        }
        break;
    case DumpJsonExistingFileHandling::Skip:
        jsonPath.clear();
        break;
    default:;
    }
    return jsonPath;
}

std::string determineJsonPath(
    std::string_view directoryPath, std::string_view fileNameWithoutExtension, RestoreJsonExistingFileHandling existingFileHandling)
{
    auto jsonPath = directoryPath % '/' % fileNameWithoutExtension + ".json";
    if (std::filesystem::exists(jsonPath)) {
        return jsonPath;
    }
    switch (existingFileHandling) {
    case RestoreJsonExistingFileHandling::RequireExistingFile:
        throw std::runtime_error("file does not exist under \"" % jsonPath + '\"');
    case RestoreJsonExistingFileHandling::Skip:
        jsonPath.clear();
        break;
    }
    return jsonPath;
}

void writeJsonDocument(const RAPIDJSON_NAMESPACE::Document &document, std::string_view outputPath)
{
    FILE *const fileHandle = std::fopen(outputPath.data(), "wb");
    if (!fileHandle) {
        throw std::runtime_error("unable to open \"" % outputPath % "\": " + strerror(errno));
    }
    try {
        char writeBuffer[65536];
        RAPIDJSON_NAMESPACE::FileWriteStream fileStream(fileHandle, writeBuffer, sizeof(writeBuffer));
        RAPIDJSON_NAMESPACE::PrettyWriter<RAPIDJSON_NAMESPACE::FileWriteStream> writer(fileStream);
        document.Accept(writer);
        std::fflush(fileHandle);
        if (std::ferror(fileHandle)) {
            throw std::runtime_error("unable to write to \"" % outputPath % "\": " + strerror(errno));
        }
        std::fclose(fileHandle);
    } catch (...) {
        std::fclose(fileHandle);
        throw;
    }
}

std::filesystem::path handleOldJsonFile(const std::filesystem::path &jsonFilePath)
{
    std::filesystem::path oldFilePath;
    if (!std::filesystem::exists(jsonFilePath)) {
        return oldFilePath;
    }
    oldFilePath = argsToString(jsonFilePath, ".old");
    std::filesystem::rename(jsonFilePath, oldFilePath);
    return oldFilePath;
}

} // namespace LibRepoMgr
