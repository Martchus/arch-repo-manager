#define RAPIDJSON_HAS_STDSTRING 1

#include "./serversetup.h"

#include "./helper.h"
#include "./json.h"

#include "./webapi/server.h"

#include "reflection/serversetup.h"
#include "resources/config.h"

#include <reflective_rapidjson/binary/serializable.h>
#include <reflective_rapidjson/json/errorformatting.h>

#include <c++utilities/application/argumentparser.h>
#include <c++utilities/conversion/stringbuilder.h>
#include <c++utilities/conversion/stringconversion.h>
#include <c++utilities/io/ansiescapecodes.h>
#include <c++utilities/io/inifile.h>
#include <c++utilities/io/misc.h>

#include <boost/asio/ssl/error.hpp>
#include <boost/asio/ssl/stream.hpp>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <unordered_set>

using namespace std;
using namespace CppUtilities;
using namespace CppUtilities::EscapeCodes;
using namespace LibPkg;

namespace LibRepoMgr {

static void deduplicateVector(std::vector<std::string> &vector)
{
    std::unordered_set<std::string_view> visited;
    vector.erase(
        std::remove_if(vector.begin(), vector.end(), [&visited](const std::string &value) { return !visited.emplace(value).second; }), vector.end());
}

ThreadPool::ThreadPool(const char *name, boost::asio::io_context &ioContext, unsigned short threadCount)
    : name(name)
{
    threads.reserve(threadCount);
    for (auto i = threadCount; i > 0; --i) {
        threads.emplace_back([&ioContext, name] {
            ioContext.run();
            std::cout << argsToString(formattedPhraseString(Phrases::SubMessage), name, " terminates", formattedPhraseString(Phrases::End));
        });
    }
}

ThreadPool::~ThreadPool()
{
    for (auto &thread : threads) {
        thread.join();
    }
}

void ServiceSetup::WebServerSetup::applyConfig(const std::multimap<std::string, std::string> &multimap)
{
    convertValue(multimap, "address", address);
    convertValue(multimap, "port", port);
    convertValue(multimap, "threads", threadCount);
    convertValue(multimap, "static_files", staticFilesPath);
    convertValue(multimap, "verify_ssl_certificates", verifySslCertificates);
    convertValue(multimap, "log_ssl_certificate_validation", logSslCertificateValidation);

    // allow the path for static files to be comma-separated
    // note: We're simpliy picking the first directory which actually exists at startup time. It makes no
    //       sense to go through all directories when looking up particular files because this would just
    //       lead to serving inconsistent files.
    if (staticFilesPath.empty()) {
        return;
    }
    const auto staticFilesPaths = splitStringSimple<std::vector<std::string_view>>(staticFilesPath, ":");
    auto foundStaticFilesPath = false;
    for (const auto &path : staticFilesPaths) {
        std::error_code ec;
        if ((foundStaticFilesPath = std::filesystem::is_directory(path, ec))) {
            staticFilesPath = path;
            break;
        }
    }
    if (foundStaticFilesPath) {
        cout << Phrases::InfoMessage << "Directory for static files: " << staticFilesPath << Phrases::EndFlush;
    } else {
        cerr << Phrases::WarningMessage << "None of the path specified in \"static_files\" is a local directory." << Phrases::EndFlush;
    }
}

void ServiceSetup::BuildSetup::applyConfig(const std::multimap<std::string, std::string> &multimap)
{
    convertValue(multimap, "threads", threadCount);
    convertValue(multimap, "working_directory", workingDirectory);
    convertValue(multimap, "local_pkgbuilds_dir", pkgbuildsDirs);
    convertValue(multimap, "ignore_local_pkgbuilds_regex", ignoreLocalPkgbuildsRegex);
    convertValue(multimap, "makepkg_path", makePkgPath);
    convertValue(multimap, "makechrootpkg_path", makeChrootPkgPath);
    convertValue(multimap, "updpkgsums_path", updatePkgSumsPath);
    convertValue(multimap, "repo_add_path", repoAddPath);
    convertValue(multimap, "repo_remove_path", repoRemovePath);
    convertValue(multimap, "ccache_dir", ccacheDir);
    convertValue(multimap, "chroot_dir", chrootDir);
    convertValue(multimap, "chroot_root_user", chrootRootUser);
    convertValue(multimap, "chroot_default_user", chrootDefaultUser);
    convertValue(multimap, "pacman_config_file_path", pacmanConfigFilePath);
    convertValue(multimap, "makepkg_config_file_path", makepkgConfigFilePath);
    convertValue(multimap, "makechrootpkg_flags", makechrootpkgFlags);
    convertValue(multimap, "makepkg_flags", makepkgFlags);
    convertValue(multimap, "package_cache_dir", packageCacheDir);
    convertValue(multimap, "test_files_dir", testFilesDir);
    convertValue(multimap, "load_files_dbs", loadFilesDbs);
}

void ServiceSetup::BuildSetup::readPresets(const std::string &configFilePath, const std::string &presetsFileRelativePath)
{
    if (presetsFileRelativePath.empty()) {
        return;
    }
    auto presetsFilePath = presetsFileRelativePath;
    try {
        if (presetsFilePath[0] != '/' && !configFilePath.empty()) {
            presetsFilePath = std::filesystem::canonical(configFilePath).parent_path() / presetsFileRelativePath;
        }
        ReflectiveRapidJSON::JsonDeserializationErrors errors{};
        errors.throwOn = ReflectiveRapidJSON::JsonDeserializationErrors::ThrowOn::All;
        presets = BuildPresets::fromJson(readFile(presetsFilePath), &errors);
    } catch (const ReflectiveRapidJSON::JsonDeserializationError &e) {
        cerr << Phrases::ErrorMessage << "Unable to deserialize presets file " << presetsFilePath << Phrases::SubMessage
             << ReflectiveRapidJSON::formatJsonDeserializationError(e) << Phrases::End;
    } catch (const RAPIDJSON_NAMESPACE::ParseResult &e) {
        cerr << Phrases::ErrorMessage << "Unable to parse presets file " << presetsFilePath << Phrases::SubMessage << "parse error at " << e.Offset()
             << ": " << RAPIDJSON_NAMESPACE::GetParseError_En(e.Code()) << Phrases::End;
    } catch (const std::runtime_error &e) {
        cerr << Phrases::ErrorMessage << "Unable to read presets file " << presetsFilePath << Phrases::SubMessage << e.what() << Phrases::End;
    }
}

void ServiceSetup::WebServerSetup::initSsl()
{
    if (!verifySslCertificates) {
        std::cerr << Phrases::SubWarning << "Certificate validation disabled!" << std::endl;
        return;
    }
    sslContext.set_verify_mode(boost::asio::ssl::verify_peer);
    if (logSslCertificateValidation) {
        sslContext.set_verify_callback(&WebServerSetup::logCertificateValidation);
    }
    sslContext.set_default_verify_paths();
}

bool ServiceSetup::WebServerSetup::logCertificateValidation(bool preVerified, boost::asio::ssl::verify_context &context)
{
    constexpr auto subjectNameLength = 1024;
    char subjectName[subjectNameLength];
    X509 *const cert = X509_STORE_CTX_get_current_cert(context.native_handle());
    X509_NAME_oneline(X509_get_subject_name(cert), subjectName, subjectNameLength);
    std::cerr << Phrases::InfoMessage << "Verifying SSL certificate: " << subjectName << Phrases::End;
    if (!preVerified) {
        std::cerr << Phrases::SubError << "verification failed" << endl;
    }
    return preVerified;
}

ServiceSetup::BuildSetup::Worker::Worker(ServiceSetup::BuildSetup &setup)
    : boost::asio::executor_work_guard<boost::asio::io_context::executor_type>(boost::asio::make_work_guard(setup.ioContext))
    , ThreadPool("Worker thread", setup.ioContext, setup.threadCount)
    , setup(setup)
{
}

ServiceSetup::BuildSetup::Worker::~Worker()
{
    cout << Phrases::SuccessMessage << "Stopping worker threads" << Phrases::End;
    setup.ioContext.stop();
}

ServiceSetup::BuildSetup::Worker ServiceSetup::BuildSetup::allocateBuildWorker()
{
    return Worker(*this);
}

BuildAction::IdType ServiceSetup::BuildSetup::allocateBuildActionID()
{
    if (!invalidActions.empty()) {
        const auto i = invalidActions.begin();
        const auto id = *i;
        invalidActions.erase(i);
        return id;
    }
    const auto id = actions.size();
    actions.emplace_back();
    return id;
}

std::vector<std::shared_ptr<BuildAction>> ServiceSetup::BuildSetup::getBuildActions(const std::vector<BuildAction::IdType> &ids)
{
    auto buildActions = std::vector<std::shared_ptr<BuildAction>>();
    buildActions.reserve(ids.size());
    for (const auto id : ids) {
        if (id < actions.size()) {
            if (auto &buildAction = actions[id]) {
                buildActions.emplace_back(buildAction);
            }
        }
    }
    return buildActions;
}

void ServiceSetup::loadConfigFiles(bool restoreStateAndDiscardDatabases)
{
    // read config file
    cout << Phrases::InfoMessage << "Reading config file: " << configFilePath << Phrases::EndFlush;
    IniFile configIni;
    try {
        // parse ini
        ifstream configFile;
        configFile.exceptions(fstream::badbit | fstream::failbit);
        configFile.open(configFilePath, fstream::in);
        configIni.parse(configFile);
        // read basic configuration values (not cached)
        for (const auto &iniEntry : configIni.data()) {
            if (iniEntry.first.empty()) {
                convertValue(iniEntry.second, "pacman_config_file_path", pacmanConfigFilePath);
                convertValue(iniEntry.second, "working_directory", workingDirectory);
            }
        }
        // apply working directory
        if (!workingDirectory.empty()) {
            try {
                workingDirectory = std::filesystem::absolute(workingDirectory);
            } catch (const std::filesystem::filesystem_error &e) {
                cerr << Phrases::WarningMessage << "Unable to determine absolute path of specified working directory: " << e.what()
                     << Phrases::EndFlush;
            }
            if (chdir(workingDirectory.c_str()) != 0) {
                cerr << Phrases::WarningMessage << "Unable to change the working directory to \"" << workingDirectory
                     << "\": " << std::strerror(errno) << Phrases::EndFlush;
            }
        }
        // restore state/cache and discard databases
        if (restoreStateAndDiscardDatabases) {
            restoreState();
            config.markAllDatabasesToBeDiscarded();
            restoreStateAndDiscardDatabases = false;
        }
        // read webserver, build and user configuration (partially cached so read it after the cache has been restored to override cached values)
        for (const auto &iniEntry : configIni.data()) {
            if (iniEntry.first == "webserver") {
                webServer.applyConfig(iniEntry.second);
            } else if (iniEntry.first == "building") {
                building.applyConfig(iniEntry.second);
                std::string presetsFile;
                convertValue(iniEntry.second, "presets", presetsFile);
                building.readPresets(configFilePath, presetsFile);
            } else if (startsWith(iniEntry.first, "user/")) {
                auth.applyConfig(iniEntry.first.substr(5), iniEntry.second);
            }
        }
    } catch (const ios_base::failure &) {
        cerr << Phrases::WarningMessage << "An IO error occured when parsing \"" << configFilePath << "\", using defaults" << Phrases::EndFlush;
    }

    // restore state/cache and discard databases if not done yet
    if (restoreStateAndDiscardDatabases) {
        restoreState();
        config.markAllDatabasesToBeDiscarded();
    }

    // read pacman config
    try {
        config.loadPacmanConfig(pacmanConfigFilePath.data());
    } catch (const ios_base::failure &e) {
        cerr << Phrases::ErrorMessage << "An IO error occured when loading pacman config: " << e.what() << Phrases::EndFlush;
    } catch (const runtime_error &e) {
        cerr << Phrases::ErrorMessage << "An error occured when loading pacman config: " << e.what() << Phrases::EndFlush;
    }

    // add databases declared in config
    std::unordered_map<std::string, std::string> globalDefinitions, dbDefinitions;
    for (auto &iniEntry : configIni.data()) {
        const auto &iniSection = iniEntry.first;
        if (iniSection == "definitions") {
            globalDefinitions.reserve(globalDefinitions.size() + iniEntry.second.size());
            for (auto &definition : iniEntry.second) {
                globalDefinitions['$' + definition.first] = move(definition.second);
            }
            continue;
        }
        if (!startsWith(iniSection, "database/")) {
            continue;
        }
        // find existing database or create a new one; clear mirrors and other data from existing DBs
        auto *const db = config.findOrCreateDatabaseFromDenotation(std::string_view(iniSection.data() + 9, iniSection.size() - 9));
        db->toBeDiscarded = false;
        dbDefinitions.clear();
        dbDefinitions["$repo"] = db->name;
        dbDefinitions["$arch"] = db->arch;
        for (auto &dirEntry : iniEntry.second) {
            const auto &key(dirEntry.first);
            auto &value(dirEntry.second);
            for (auto i = 0; i != 2; ++i) {
                for (const auto &definitions : { dbDefinitions, globalDefinitions }) {
                    for (const auto &definition : definitions) {
                        findAndReplace(value, definition.first, definition.second);
                    }
                }
            }
            if (key == "arch") {
                db->arch = move(value);
                dbDefinitions["$arch"] = db->arch;
            } else if (key == "depends") {
                db->dependencies = splitString<vector<string>>(value, " ", EmptyPartsTreat::Omit);
            } else if (key == "pkgdir") {
                db->localPkgDir = move(value);
            } else if (key == "dbdir") {
                db->localDbDir = move(value);
            } else if (key == "path") {
                db->path = move(value);
            } else if (key == "filespath") {
                db->filesPath = move(value);
            } else if (key == "sync_from_mirror") {
                db->syncFromMirror = value == "on";
            } else if (key == "mirror") {
                db->mirrors.emplace_back(move(value));
            } else {
                dbDefinitions["$" + key] = move(value);
            }
        }
    }

    // deduce database paths from local database dirs; remove duplicated mirrors
    for (auto &db : config.databases) {
        db.deducePathsFromLocalDirs();
        deduplicateVector(db.mirrors);
    }

    // log the most important config values
    cerr << Phrases::InfoMessage << "Working directory: " << workingDirectory << Phrases::EndFlush;
    cerr << Phrases::InfoMessage << "Package cache directory: " << building.packageCacheDir << Phrases::EndFlush;
    cerr << Phrases::InfoMessage << "Chroot directory: " << building.chrootDir << Phrases::EndFlush;
    cerr << Phrases::InfoMessage << "Chroot root user: " << building.chrootRootUser << Phrases::EndFlush;
    cerr << Phrases::InfoMessage << "Chroot default user: " << building.chrootDefaultUser << Phrases::EndFlush;
    cerr << Phrases::InfoMessage << "Ccache directory: " << building.ccacheDir << Phrases::EndFlush;
}

void ServiceSetup::printDatabases()
{
    cerr << Phrases::SuccessMessage << "Found " << config.databases.size() << " databases:" << Phrases::End;
    for (const auto &db : config.databases) {
        cerr << Phrases::SubMessage << db.name << "@" << db.arch << ": " << db.packages.size() << " packages, last updated on "
             << db.lastUpdate.toString(DateTimeOutputFormat::DateAndTime) << Phrases::End << "     - path: " << db.path
             << "\n     - local db dir: " << db.localDbDir << "\n     - local package dir: " << db.localPkgDir << '\n';
    }
    cerr << Phrases::SubMessage << "AUR (" << config.aur.packages.size() << " packages cached)" << Phrases::End;
}

std::string_view ServiceSetup::cacheFilePath() const
{
    return "cache-v" LIBREPOMGR_CACHE_VERSION ".bin";
}

RAPIDJSON_NAMESPACE::Document ServiceSetup::libraryDependenciesToJson() const
{
    namespace JR = ReflectiveRapidJSON::JsonReflector;
    auto document = RAPIDJSON_NAMESPACE::Document(RAPIDJSON_NAMESPACE::kObjectType);
    auto &alloc = document.GetAllocator();
    for (const auto &db : config.databases) {
        auto dbValue = RAPIDJSON_NAMESPACE::Value(RAPIDJSON_NAMESPACE::Type::kObjectType);
        for (const auto &[pkgName, pkg] : db.packages) {
            if (!pkg->packageInfo) {
                continue;
            }
            if (pkg->libdepends.empty() && pkg->libprovides.empty()) {
                auto hasVersionedPythonOrPerlDep = false;
                for (const auto &dependency : pkg->dependencies) {
                    if (dependency.mode == DependencyMode::Any || dependency.version.empty()
                        || (dependency.name != "python" && dependency.name != "python2" && dependency.name != "perl")) {
                        continue;
                    }
                    hasVersionedPythonOrPerlDep = true;
                    break;
                }
                if (!hasVersionedPythonOrPerlDep) {
                    continue;
                }
            }
            auto pkgValue = RAPIDJSON_NAMESPACE::Value(RAPIDJSON_NAMESPACE::Type::kObjectType);
            auto pkgObj = pkgValue.GetObject();
            JR::push(pkg->version, "v", pkgObj, alloc);
            JR::push(pkg->packageInfo->buildDate, "t", pkgObj, alloc);
            JR::push(pkg->dependencies, "d", pkgObj, alloc); // for versioned Python/Perl deps
            JR::push(pkg->libdepends, "ld", pkgObj, alloc);
            JR::push(pkg->libprovides, "lp", pkgObj, alloc);
            dbValue.AddMember(RAPIDJSON_NAMESPACE::StringRef(pkgName.data(), JR::rapidJsonSize(pkgName.size())), pkgValue, alloc);
        }
        document.AddMember(RAPIDJSON_NAMESPACE::Value(db.name % '@' + db.arch, alloc), dbValue, alloc);
    }
    return document;
}

void ServiceSetup::restoreLibraryDependenciesFromJson(const string &json, ReflectiveRapidJSON::JsonDeserializationErrors *errors)
{
    namespace JR = ReflectiveRapidJSON::JsonReflector;
    const auto document = JR::parseJsonDocFromString(json.data(), json.size());
    if (!document.IsObject()) {
        errors->reportTypeMismatch<std::map<std::string, LibPkg::Database>>(document.GetType());
        return;
    }
    // FIXME: be more error resilient here, e.g. set the following line and print list of errors instead of aborting on first error
    // errors->throwOn = ReflectiveRapidJSON::JsonDeserializationErrors::ThrowOn::None;
    const auto dbObj = document.GetObject();
    for (const auto &dbEntry : dbObj) {
        if (!dbEntry.value.IsObject()) {
            errors->reportTypeMismatch<decltype(LibPkg::Database::packages)>(document.GetType());
            continue;
        }
        auto *const db = config.findOrCreateDatabaseFromDenotation(std::string_view(dbEntry.name.GetString()));
        const auto pkgsObj = dbEntry.value.GetObject();
        for (const auto &pkgEntry : pkgsObj) {
            if (!pkgEntry.value.IsObject()) {
                errors->reportTypeMismatch<LibPkg::Package>(document.GetType());
                continue;
            }
            const auto pkgObj = pkgEntry.value.GetObject();
            auto name = std::string(pkgEntry.name.GetString());
            auto &pkg = db->packages[name];
            if (pkg) {
                // do not mess with already existing packages; this restoring stuff is supposed to be done before loading packages from DBs
                continue;
            }
            pkg = std::make_shared<LibPkg::Package>();
            pkg->name = std::move(name);
            pkg->origin = PackageOrigin::CustomSource;
            pkg->packageInfo = std::make_unique<LibPkg::PackageInfo>();
            JR::pull(pkg->version, "v", pkgObj, errors);
            JR::pull(pkg->packageInfo->buildDate, "t", pkgObj, errors);
            JR::pull(pkg->dependencies, "d", pkgObj, errors); // for versioned Python/Perl deps
            JR::pull(pkg->libdepends, "ld", pkgObj, errors);
            JR::pull(pkg->libprovides, "lp", pkgObj, errors);
            db->addPackageDependencies(pkg);
        }
    }
}

std::size_t ServiceSetup::restoreState()
{
    // clear old build actions before (There must not be any ongoing build actions when calling this function!)
    building.actions.clear();
    building.invalidActions.clear();

    // restore configuration and maybe build actions from JSON file
    const auto cacheFilePath = this->cacheFilePath();
    std::size_t size = 0;
    bool hasConfig = false, hasBuildActions = false;
    try {
        fstream cacheFile;
        cacheFile.exceptions(ios_base::failbit | ios_base::badbit);
        cacheFile.open(cacheFilePath.data(), ios_base::in | ios_base::binary);
        ReflectiveRapidJSON::BinaryReflector::BinaryDeserializer deserializer(&cacheFile);
        deserializer.read(config);
        hasConfig = true;
        if (!hasBuildActions) {
            deserializer.read(building.actions);
            hasBuildActions = true;
        }
        size = static_cast<std::uint64_t>(cacheFile.tellg());
        cacheFile.close();
        cerr << Phrases::SuccessMessage << "Restored cache file \"" << cacheFilePath << "\", " << dataSizeToString(size) << Phrases::EndFlush;
        if (hasBuildActions) {
            cerr << Phrases::SubMessage << "Restored build actions from cache file 😌" << Phrases::EndFlush;
        }
    } catch (const ConversionException &) {
        cerr << Phrases::WarningMessage << "A conversion error occured when restoring cache file \"" << cacheFilePath << "\"." << Phrases::EndFlush;
    } catch (const ios_base::failure &) {
        cerr << Phrases::WarningMessage << "An IO error occured when restoring cache file \"" << cacheFilePath << "\"." << Phrases::EndFlush;
    }

    // restore build actions from JSON file
    if (!hasBuildActions) {
        try {
            if (!restoreJsonObject(building.actions, workingDirectory, "build-actions-v" LIBREPOMGR_BUILD_ACTIONS_JSON_VERSION,
                    RestoreJsonExistingFileHandling::Skip)
                     .empty()) {
                cerr << Phrases::SuccessMessage << "Restored build actions from JSON file 😒" << Phrases::EndFlush;
            }
        } catch (const std::runtime_error &e) {
            cerr << Phrases::ErrorMessage << e.what() << Phrases::EndFlush;
        }
    }

    // restore provided/required libraries from JSON file
    if (!hasConfig) {
        try {
            if (!restoreJsonObject(std::bind(&ServiceSetup::restoreLibraryDependenciesFromJson, this, std::placeholders::_1, std::placeholders::_2),
                    workingDirectory, "library-dependencies-v" LIBREPOMGR_LIBRARY_DEPENDENCIES_JSON_VERSION, RestoreJsonExistingFileHandling::Skip)
                     .empty()) {
                cerr << Phrases::SuccessMessage << "Restored library dependencies from JSON file 😒" << Phrases::EndFlush;
            }
        } catch (const std::runtime_error &e) {
            cerr << Phrases::ErrorMessage << e.what() << Phrases::EndFlush;
        }
    }

    // determine invalid build actions
    if (building.actions.empty()) {
        return size;
    }
    auto newActionsSize = building.actions.size();
    for (auto id = newActionsSize - 1;; --id) {
        if (building.actions[id]) {
            break;
        }
        newActionsSize = id;
        if (!newActionsSize) {
            break;
        }
    }
    building.actions.resize(newActionsSize);
    for (std::size_t buildActionId = 0, size = building.actions.size(); buildActionId != size; ++buildActionId) {
        if (!building.actions[buildActionId]) {
            building.invalidActions.emplace(buildActionId);
        }
    }

    // ensure no build actions are considered running anymore and populate follow up actions
    for (auto &action : building.actions) {
        if (!action) {
            continue;
        }
        for (const auto previousBuildActionID : action->startAfter) {
            if (auto previousBuildAction = building.getBuildAction(previousBuildActionID)) {
                previousBuildAction->m_followUpActions.emplace_back(action->weak_from_this());
            }
        }
        if (action->isExecuting()) {
            action->status = BuildActionStatus::Finished;
            action->result = BuildActionResult::Failure;
            action->resultData = "service crashed while exectuing";
        }
    }

    return size;
}

std::size_t ServiceSetup::saveState()
{
    // write cache file to be able to restore the service state when restarting the service efficiently
    const auto cacheFilePath = this->cacheFilePath();
    std::size_t size = 0;
    try {
        fstream cacheFile;
        cacheFile.exceptions(ios_base::failbit | ios_base::badbit);
        cacheFile.open(cacheFilePath.data(), ios_base::out | ios_base::trunc | ios_base::binary);
        ReflectiveRapidJSON::BinaryReflector::BinarySerializer serializer(&cacheFile);
        serializer.write(config);
        serializer.write(building.actions);
        size = static_cast<std::uint64_t>(cacheFile.tellp());
        cacheFile.close();
        cerr << Phrases::SuccessMessage << "Wrote cache file \"" << cacheFilePath << "\", " << dataSizeToString(size) << Phrases::EndFlush;
    } catch (const ios_base::failure &) {
        cerr << Phrases::WarningMessage << "An IO error occured when dumping the cache file \"" << cacheFilePath << "\"." << Phrases::EndFlush;
    }

    // write build actions to a JSON file to be able to restore build actions even if the cache file can not be used due to version mismatch
    // note: The JSON file's format is hopefully more stable.
    try {
        if (!dumpJsonObject(
                building.actions, workingDirectory, "build-actions-v" LIBREPOMGR_BUILD_ACTIONS_JSON_VERSION, DumpJsonExistingFileHandling::Override)
                 .empty()) {
            cerr << Phrases::SuccessMessage << "Wrote build actions to JSON file." << Phrases::EndFlush;
        }
    } catch (const std::runtime_error &e) {
        cerr << Phrases::ErrorMessage << e.what() << Phrases::EndFlush;
    }

    // write provided/required libraries to a JSON file to be able to restore this information even if the cache file can not be used due to version
    // mismatch
    try {
        if (!dumpJsonDocument(std::bind(&ServiceSetup::libraryDependenciesToJson, this), workingDirectory,
                "library-dependencies-v" LIBREPOMGR_LIBRARY_DEPENDENCIES_JSON_VERSION, DumpJsonExistingFileHandling::Override)
                 .empty()) {
            cerr << Phrases::SuccessMessage << "Wrote library dependencies to JSON file." << Phrases::EndFlush;
        }
    } catch (const std::runtime_error &e) {
        cerr << Phrases::ErrorMessage << e.what() << Phrases::EndFlush;
    }

    return size;
}

void ServiceSetup::run()
{
#ifndef CPP_UTILITIES_DEBUG_BUILD
    try {
#endif
        loadConfigFiles(true);
        config.discardDatabases();
        config.loadAllPackages(building.loadFilesDbs);
#ifndef CPP_UTILITIES_DEBUG_BUILD
    } catch (const std::exception &e) {
        cerr << Phrases::SubError << e.what() << endl;
    } catch (...) {
        cerr << Phrases::SubError << "An unknown error occurred." << endl;
    }
#endif

    printDatabases();

    cout << Phrases::SuccessMessage << "Initializing SSL" << Phrases::End;
    webServer.initSsl();

    {
        cout << Phrases::SuccessMessage << "Allocating worker thread pool (thread count: " << building.threadCount << ")" << Phrases::End;
        const auto buildWorker = building.allocateBuildWorker();

#ifndef CPP_UTILITIES_DEBUG_BUILD
        try {
#endif
            cout << Phrases::SuccessMessage << "Starting web server (thread count: " << webServer.threadCount << "):" << TextAttribute::Reset
                 << " http://" << webServer.address << ':' << webServer.port << endl;
            WebAPI::Server::serve(*this);
            cout << Phrases::SuccessMessage << "Web server stopped." << Phrases::EndFlush;
#ifndef CPP_UTILITIES_DEBUG_BUILD
        } catch (const std::exception &e) {
            cerr << Phrases::ErrorMessage << "Server terminated due to exception: " << Phrases::End << "  " << e.what() << Phrases::EndFlush;
        } catch (...) {
            cerr << Phrases::ErrorMessage << "Server terminated due to an unknown error." << Phrases::EndFlush;
        }
#endif
    }

    for (auto &buildAction : building.actions) {
        if (buildAction && buildAction->isExecuting()) {
            buildAction->status = BuildActionStatus::Finished;
            buildAction->result = BuildActionResult::Aborted;
        }
    }

    saveState();
}

ServiceStatus::ServiceStatus(const ServiceSetup &setup)
    : version(applicationInfo.version)
    , config(setup.config.computeStatus())
    , actions(setup.building.metaInfo)
    , presets(setup.building.presets)
{
}

} // namespace LibRepoMgr
