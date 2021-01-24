#include "./config.h"

#include <c++utilities/conversion/stringbuilder.h>
#include <c++utilities/io/ansiescapecodes.h>
#include <c++utilities/io/inifile.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <unordered_map>

#include <sys/utsname.h> // for uname

using namespace std;
using namespace CppUtilities;
using namespace CppUtilities::EscapeCodes;

namespace LibPkg {

static void moveLastValue(string &target, multimap<string, string> &multimap, const string &key)
{
    const auto i = find_if(multimap.rbegin(), multimap.rend(), [&key](const pair<string, string> &i) { return i.first == key; });
    if (i != multimap.rend()) {
        target = move(i->second);
    }
}

static void moveValues(vector<string> &target, multimap<string, string> &multimap, const string &key)
{
    for (auto range = multimap.equal_range(key); range.first != range.second; ++range.first) {
        target.emplace_back(move(range.first->second));
    }
}

void Config::loadPacmanConfig(const char *pacmanConfigPath)
{
    // open and parse ini
    IniFile configIni;
    unordered_map<string, IniFile> includedInis;
    {
        ifstream configFile;
        configFile.exceptions(ios_base::failbit | ios_base::badbit);
        configFile.open(pacmanConfigPath, ios_base::in);
        configIni.parse(configFile);
    }
    auto &configData = configIni.data();
    auto architecture = std::string_view{};

    // read options and create Database object for each db
    for (auto &scope : configData) {
        if (scope.first == "options") {
            // read global options or assume defaults
            auto &options = scope.second;
            for (auto range = options.equal_range("Architecture"); range.first != range.second; ++range.first) {
                if (range.first->second != "auto") {
                    architecture = *architectures.emplace(move(range.first->second)).first;
                } else {
                    struct utsname un;
                    uname(&un);
                    architecture = *architectures.emplace(un.machine).first;
                }
            }
            moveLastValue(pacmanDatabasePath, options, "DBPath");
            if (pacmanDatabasePath.empty()) {
                pacmanDatabasePath = "/var/lib/pacman/";
            }
            moveValues(packageCacheDirs, options, "CacheDir");
            if (packageCacheDirs.empty()) {
                packageCacheDirs.emplace_back("/var/cache/pacman/pkg/");
            }
            string sigLevel;
            moveLastValue(sigLevel, options, "SigLevel");
            signatureLevel = SignatureLevelConfig::fromString(sigLevel);
            if (!signatureLevel.isValid()) {
                signatureLevel = SignatureLevelConfig();
                cerr << Phrases::WarningMessage << "The global/default signature level \"" << sigLevel << "\" is invalid and will be ignored."
                     << Phrases::End << Phrases::SubWarning << "Assuming default \"" << signatureLevel.toString() << "\" instead"
                     << Phrases::EndFlush;
            }
        } else {
            // read sync database
            auto *const db = findOrCreateDatabase(move(scope.first), architecture);
            // read sig level
            string sigLevel;
            moveLastValue(sigLevel, scope.second, "SigLevel");
            const auto dbSpecificSignatureLevelConfig = SignatureLevelConfig::fromString(sigLevel);
            if (dbSpecificSignatureLevelConfig.databaseScope != SignatureLevel::Invalid) {
                db->signatureLevel = dbSpecificSignatureLevelConfig.databaseScope;
            } else {
                cerr << Phrases::WarningMessage << "The signature level \"" << sigLevel << "\" specified for DB \"" << db->name
                     << "\" is invalid and will be ignored." << Phrases::End << Phrases::SubWarning << "Assuming global default \""
                     << signatureLevelToString(signatureLevel.databaseScope) << "\" instead" << Phrases::EndFlush;
                db->signatureLevel = signatureLevel.databaseScope;
            }
            // add mirrors
            for (auto range = scope.second.equal_range("Server"); range.first != range.second; ++range.first) {
                for (const auto &arch : architectures) {
                    string url = range.first->second;
                    findAndReplace<string>(url, "$repo", db->name);
                    findAndReplace<string>(url, "$arch", arch);
                    db->mirrors.emplace_back(move(url));
                }
            }
            // add included mirrors
            for (auto range = scope.second.equal_range("Include"); range.first != range.second; ++range.first) {
                const auto &path = range.first->second;
                auto &includedIni = includedInis[path];
                if (includedIni.data().empty()) {
                    try {
                        ifstream includedFile;
                        includedFile.exceptions(ios_base::failbit | ios_base::badbit);
                        includedFile.open(path, ios_base::in);
                        includedIni.parse(includedFile);
                    } catch (const ios_base::failure &) {
                        cerr << Phrases::WarningMessage << "An IO error occured when parsing the included file \"" << path << "\"."
                             << Phrases::EndFlush;
                    }
                }
                for (auto &nestedScope : includedIni.data()) {
                    if (!nestedScope.first.empty()) {
                        continue;
                    }
                    for (auto range = nestedScope.second.equal_range("Server"); range.first != range.second; ++range.first) {
                        for (const auto &arch : architectures) {
                            string url = range.first->second;
                            findAndReplace<string>(url, "$repo", db->name);
                            findAndReplace<string>(url, "$arch", arch);
                            db->mirrors.emplace_back(move(url));
                        }
                    }
                }
            }
            // set database file paths
            if (db->localDbDir.empty()) {
                db->localDbDir = pacmanDatabasePath + "sync";
            }
            if (db->localPkgDir.empty()) {
                db->localPkgDir = packageCacheDirs.front();
            }
            // ensure the database is not being discarded
            db->toBeDiscarded = false;
        }
    }
}

void Config::loadAllPackages(bool withFiles)
{
    for (Database &db : databases) {
        try {
            db.loadPackages(withFiles);
        } catch (const runtime_error &e) {
            cerr << Phrases::ErrorMessage << "Unable to load database \"" << db.name << "\": " << e.what() << Phrases::EndFlush;
        }
    }
}

std::uint64_t Config::restoreFromCache()
{
    fstream cacheFile;
    cacheFile.exceptions(ios_base::failbit | ios_base::badbit);
    cacheFile.open("cache.bin", ios_base::in | ios_base::binary);
    restoreFromBinary(cacheFile);
    return static_cast<std::uint64_t>(cacheFile.tellg());
}

std::uint64_t Config::dumpCacheFile()
{
    fstream cacheFile;
    cacheFile.exceptions(ios_base::failbit | ios_base::badbit);
    cacheFile.open("cache.bin", ios_base::out | ios_base::trunc | ios_base::binary);
    toBinary(cacheFile);
    const auto size = static_cast<std::uint64_t>(cacheFile.tellp());
    cacheFile.close();
    return size;
}

std::pair<std::string_view, std::string_view> Config::parseDatabaseDenotation(std::string_view databaseDenotation)
{
    const auto archStart = databaseDenotation.rfind('@');
    if (archStart == std::string_view::npos) {
        return std::make_pair(databaseDenotation, "x86_64");
    } else {
        return std::make_pair(databaseDenotation.substr(0, archStart), databaseDenotation.substr(archStart + 1));
    }
}

std::tuple<std::string_view, std::string_view, std::string_view> Config::parsePackageDenotation(std::string_view packageDenotation)
{
    const char *const end = packageDenotation.data() + packageDenotation.size();
    const char *packageName = packageDenotation.data();
    for (; packageName != end && *packageName != '/'; ++packageName)
        ;
    if (packageName == end) {
        return std::make_tuple(std::string_view(), std::string_view(), packageDenotation);
    } else {
        const auto &[dbName, dbArch]
            = parseDatabaseDenotation(std::string_view(packageDenotation.data(), static_cast<std::size_t>(packageName - packageDenotation.data())));
        return std::make_tuple(dbName, dbArch, std::string_view(packageName + 1, static_cast<std::size_t>(end - packageName - 1)));
    }
}

} // namespace LibPkg

#include "reflection/config.h"
