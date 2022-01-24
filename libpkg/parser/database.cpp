#include "./database.h"
#include "./utils.h"

#include <c++utilities/conversion/stringbuilder.h>
#include <c++utilities/conversion/stringconversion.h>
#include <c++utilities/io/ansiescapecodes.h>

#include <cstring>
#include <iostream>
#include <map>

using namespace std;
using namespace CppUtilities;
using namespace CppUtilities::EscapeCodes;

namespace LibPkg {

bool Database::isFileRelevant(const char *filePath, const char *fileName, mode_t)
{
    CPP_UTILITIES_UNUSED(filePath)
    return !std::strcmp(fileName, "desc") || !std::strcmp(fileName, "depends") || !std::strcmp(fileName, "files");
}

void Database::loadPackagesFromConfiguredPaths(bool withFiles, bool force)
{
    const auto &dbPath = withFiles && !filesPath.empty() ? filesPath : path;
    if (dbPath.empty()) {
        throw runtime_error("local path not configured");
    }
    const auto lastFileUpdate = lastModified(dbPath);
    if (force || lastFileUpdate > lastUpdate) {
        loadPackages(extractFiles(dbPath, &isFileRelevant), lastFileUpdate);
    }
}

void LibPkg::Database::loadPackages(const string &databaseData, DateTime lastModified)
{
    loadPackages(extractFilesFromBuffer(databaseData, name + " db file", &isFileRelevant), lastModified);
}

void Database::loadPackages(FileMap &&databaseFiles, DateTime lastModified)
{
    lastUpdate = lastModified;
    auto updater = PackageUpdater(*this);
    for (auto &dir : databaseFiles) {
        if (dir.first.find('/') != std::string::npos) {
            cerr << Phrases::WarningMessage << "Database \"" << name << "\" contains unexpected sub directory: " << dir.first << Phrases::EndFlush;
            continue;
        }
        auto descriptionParts = std::vector<std::string>();
        descriptionParts.reserve(dir.second.size());
        for (auto &file : dir.second) {
            descriptionParts.emplace_back(std::move(file.content));
        }
        updater.update(Package::fromDescription(descriptionParts));
    }
    updater.commit();
}

} // namespace LibPkg
