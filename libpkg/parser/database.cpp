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
        loadPackages(dbPath, lastFileUpdate);
    }
}

void Database::loadPackages(const std::string &databaseFilePath, DateTime lastModified)
{
    auto updater = PackageUpdater(*this, true);
    updater.insertFromDatabaseFile(databaseFilePath);
    updater.commit();
    lastUpdate = lastModified;
}

} // namespace LibPkg
