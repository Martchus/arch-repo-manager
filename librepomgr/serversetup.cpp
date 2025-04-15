#define RAPIDJSON_HAS_STDSTRING 1

#include "./serversetup.h"

#include "./helper.h"
#include "./json.h"

#include "./webapi/server.h"

#include "../libpkg/data/storagegeneric.h"

#include "reflection/serversetup.h"
#include "resources/config.h"

#include <reflective_rapidjson/binary/serializable.h>
#include <reflective_rapidjson/json/errorformatting.h>

#include <passwordfile/io/passwordfile.h>

#include <c++utilities/application/argumentparser.h>
#include <c++utilities/conversion/stringbuilder.h>
#include <c++utilities/conversion/stringconversion.h>
#include <c++utilities/io/ansiescapecodes.h>
#include <c++utilities/io/inifile.h>
#include <c++utilities/io/misc.h>

#include <boost/asio/ssl/error.hpp>
#include <boost/asio/ssl/stream.hpp>

#include <boost/exception/diagnostic_information.hpp>
#include <boost/exception/exception.hpp>

#ifdef PLATFORM_LINUX
#include <pthread.h>
#include <sys/resource.h>
#endif

#ifdef USE_LIBSYSTEMD
#include <systemd/sd-daemon.h>
#endif

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <regex>
#include <string_view>
#include <unordered_set>

using namespace std;
using namespace CppUtilities;
using namespace CppUtilities::EscapeCodes;
using namespace LibPkg;

namespace LibRepoMgr {

struct Storage {
    using BuildActionStorage = LMDBSafe::TypedDBI<BuildAction>;

    explicit Storage(const char *path);

private:
    std::shared_ptr<LMDBSafe::MDBEnv> m_env;

public:
    BuildActionStorage buildActions;
};

Storage::Storage(const char *path)
    : m_env(LMDBSafe::getMDBEnv(path, MDB_NOSUBDIR, 0600, 2))
    , buildActions(m_env, "buildactions")
{
}

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
#ifdef PLATFORM_LINUX
        pthread_setname_np(
#endif
            threads
                .emplace_back([&ioContext, name] {
#ifndef CPP_UTILITIES_DEBUG_BUILD
                    for (;;) {
                        try {
#endif
                            ioContext.run();
                            std::cout << argsToString(
                                formattedPhraseString(Phrases::SubMessage), name, " thread terminates", formattedPhraseString(Phrases::End));
#ifndef CPP_UTILITIES_DEBUG_BUILD
                            return;
                        } catch (const boost::exception &e) {
                            std::cerr << Phrases::ErrorMessage << "Exception occurred in \"" << name << "\" thread: " << Phrases::End << "    "
                                      << boost::diagnostic_information(e) << Phrases::EndFlush;
                        } catch (const std::exception &e) {
                            std::cerr << Phrases::ErrorMessage << "Exception occurred in \"" << name << "\" thread: " << Phrases::End << "    "
                                      << e.what() << Phrases::EndFlush;
                        } catch (...) {
                            std::cerr << Phrases::ErrorMessage << "Unknown error occurred in \"" << name << "\" thread." << Phrases::EndFlush;
                        }
                    }
#endif
                })
#ifdef PLATFORM_LINUX
                .native_handle(),
            name)
#endif
            ;
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
    convertValue(multimap, "package_search_response_limit", packageSearchResponseLimit);
    convertValue(multimap, "build_actions_response_limit", buildActionsResponseLimit);
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

ServiceSetup::BuildSetup::BuildSetup() = default;
ServiceSetup::BuildSetup::~BuildSetup() = default;

void ServiceSetup::BuildSetup::initStorage(const char *path)
{
    if (!m_storage) {
        m_storage = std::make_unique<Storage>(path);
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
    convertValue(multimap, "makecontainerpkg_path", makeContainerPkgPath);
    convertValue(multimap, "updpkgsums_path", updatePkgSumsPath);
    convertValue(multimap, "conversion_script_path", conversionScriptPath);
    convertValue(multimap, "repo_add_path", repoAddPath);
    convertValue(multimap, "repo_remove_path", repoRemovePath);
    convertValue(multimap, "pacman_path", pacmanPath);
    convertValue(multimap, "paccache_path", paccachePath);
    convertValue(multimap, "pkgctl_path", pkgctlPath);
    convertValue(multimap, "gpg_path", gpgPath);
    convertValue(multimap, "ccache_dir", ccacheDir);
    convertValue(multimap, "chroot_dir", chrootDir);
    convertValue(multimap, "chroot_root_user", chrootRootUser);
    convertValue(multimap, "chroot_default_user", chrootDefaultUser);
    convertValue(multimap, "default_gpg_key", defaultGpgKey);
    convertValue(multimap, "pacman_config_file_path", pacmanConfigFilePath);
    convertValue(multimap, "makepkg_config_file_path", makepkgConfigFilePath);
    convertValue(multimap, "makechrootpkg_flags", makechrootpkgFlags);
    convertValue(multimap, "makepkg_flags", makepkgFlags);
    convertValue(multimap, "package_cache_dir", packageCacheDir);
    convertValue(multimap, "package_download_size_limit", packageDownloadSizeLimit);
    convertValue(multimap, "test_files_dir", testFilesDir);
    convertValue(multimap, "build_action_retention", buildActionRetention);
    convertValue(multimap, "load_files_dbs", loadFilesDbs);
    convertValue(multimap, "db_path", dbPath);
    if (conversionScriptPath.empty() && !pkgbuildsDirs.empty()) {
        conversionScriptPath = pkgbuildsDirs.front() + "/devel/conv-variant.pl";
    }
}

void ServiceSetup::BuildSetup::readComplementaryVariants(const std::multimap<std::string, std::string> &multimap)
{
    for (const auto &[variant, complementaryVariantsStr] : multimap) {
        complementaryVariants[variant]
            = CppUtilities::splitString<std::vector<std::string>>(complementaryVariantsStr, " ", CppUtilities::EmptyPartsTreat::Omit);
    }
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
        cerr << Phrases::ErrorMessage << "Unable to deserialize presets file " << presetsFilePath << '\n'
             << Phrases::SubMessage << ReflectiveRapidJSON::formatJsonDeserializationError(e) << Phrases::End;
    } catch (const RAPIDJSON_NAMESPACE::ParseResult &e) {
        cerr << Phrases::ErrorMessage << "Unable to parse presets file " << presetsFilePath << '\n'
             << Phrases::SubMessage << "parse error at " << e.Offset() << ": " << RAPIDJSON_NAMESPACE::GetParseError_En(e.Code()) << Phrases::End;
    } catch (const std::runtime_error &e) {
        cerr << Phrases::ErrorMessage << "Unable to read presets file " << presetsFilePath << '\n' << Phrases::SubMessage << e.what() << Phrases::End;
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
    , ThreadPool("Worker", setup.ioContext, setup.threadCount)
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

LibPkg::StorageID ServiceSetup::BuildSetup::allocateBuildActionID()
{
    static const auto emptyBuildAction = BuildAction();
    auto txn = m_storage->buildActions.getRWTransaction();
    const auto id = txn.put(emptyBuildAction, txn.newID());
    txn.commit();
    return id;
}

std::shared_ptr<BuildAction> ServiceSetup::BuildSetup::getBuildAction(BuildActionIdType id)
{
    if (auto i = m_runningActions.find(id); i != m_runningActions.end()) {
        return i->second;
    }
    const auto res = std::make_shared<BuildAction>();
    auto txn = m_storage->buildActions.getROTransaction();
    return id <= std::numeric_limits<LibPkg::StorageID>::max() && txn.get(static_cast<LibPkg::StorageID>(id), *res) ? res : nullptr;
}

std::vector<std::shared_ptr<BuildAction>> ServiceSetup::BuildSetup::getBuildActions(const std::vector<BuildActionIdType> &ids)
{
    auto buildAction = std::shared_ptr<BuildAction>();
    auto buildActions = std::vector<std::shared_ptr<BuildAction>>();
    buildActions.reserve(ids.size());
    auto txn = m_storage->buildActions.getROTransaction();
    for (const auto id : ids) {
        if (auto i = m_runningActions.find(id); i != m_runningActions.end()) {
            buildActions.emplace_back(i->second);
            continue;
        }
        if (id > std::numeric_limits<LibPkg::StorageID>::max()) {
            continue;
        }
        if (!buildAction) {
            buildAction = std::make_shared<BuildAction>();
        }
        if (txn.get(static_cast<LibPkg::StorageID>(id), *buildAction)) {
            buildActions.emplace_back(std::move(buildAction));
        }
    }
    return buildActions;
}

StorageID ServiceSetup::BuildSetup::storeBuildAction(const std::shared_ptr<BuildAction> &buildAction)
{
    // update cache of running build actions
    if (buildAction->isExecuting()) {
        m_runningActions[buildAction->id] = buildAction;
    } else {
        m_runningActions.erase(buildAction->id);
    }
    // update index of follow-up actions for scheduled actions
    // note: This would break if startAfter would be modified after the initial build action creation.
    if (buildAction->isScheduled() && !buildAction->startAfter.empty()) {
        const auto previousBuildActions = getBuildActions(buildAction->startAfter);
        auto allSucceeded = false;
        for (auto &previousBuildAction : previousBuildActions) {
            if (!previousBuildAction->hasSucceeded()) {
                m_followUpActions[previousBuildAction->id].emplace(buildAction->id);
                allSucceeded = false;
            }
        }
        // immediately start if all follow-up actions have succeeded
        if (allSucceeded && buildAction->setup()) {
            // FIXME: Do we actually ever get here?
            return buildAction->start(*buildAction->setup(), std::unique_ptr<Io::PasswordFile>());
        }
    } else {
        for (const auto id : buildAction->startAfter) {
            if (const auto i = m_followUpActions.find(id); i != m_followUpActions.end()) {
                auto &followUps = i->second;
                followUps.erase(buildAction->id);
                if (followUps.empty()) {
                    m_followUpActions.erase(i);
                }
            }
        }
    }
    // update persistent storage
    auto txn = m_storage->buildActions.getRWTransaction();
    if (!buildAction->id) {
        buildAction->id = txn.newID();
    }
    const auto id = txn.put(*buildAction, static_cast<LibPkg::StorageID>(buildAction->id)); // buildAction->id expected to be a valid StorageID or 0
    txn.commit();
    return id;
}

void ServiceSetup::BuildSetup::deleteBuildAction(const std::vector<std::shared_ptr<BuildAction>> &actions)
{
    auto txn = m_storage->buildActions.getRWTransaction();
    for (const auto &action : actions) {
        // remove action from cache for running actions
        m_runningActions.erase(action->id);
        // remove any actions to start after the action to delete
        m_followUpActions.erase(action->id);
        // remove follow-up indexes for actions previous to the action to delete
        for (auto i = m_followUpActions.begin(), end = m_followUpActions.end(); i != end;) {
            auto &followUps = i->second;
            followUps.erase(action->id);
            if (followUps.empty()) {
                i = m_followUpActions.erase(i);
            } else {
                ++i;
            }
        }
        // delete action from storage
        if (action->id && action->id <= std::numeric_limits<LibPkg::StorageID>::max()) {
            txn.del(static_cast<LibPkg::StorageID>(action->id));
        }
    }
    txn.commit();
}

std::size_t ServiceSetup::BuildSetup::buildActionCount()
{
    return m_storage->buildActions.getROTransaction().size();
}

void ServiceSetup::BuildSetup::rebuildDb()
{
    std::cerr << "Rebuilding build actions database\n";
    auto txn = m_storage->buildActions.getRWTransaction();
    auto processed = std::size_t();
    auto ok = std::size_t();
    txn.rebuild([count = txn.size(), &processed, &ok](StorageID id, BuildAction *buildAction) mutable {
        std::cerr << "Processing build action " << ++processed << " / " << count << '\n';
        if (!buildAction) {
            std::cerr << "Deleting build action " << id << ": unable to deserialize\n";
            return false;
        }
        if (buildAction->id > std::numeric_limits<StorageID>::max()) {
            std::cerr << "Deleting build action " << id << ": object ID " << buildAction->id << " is out of range\n";
            return false;
        }
        if (buildAction->id != id) {
            std::cerr << "Deleting build action " << id << ": ID mismatch (object ID is " << buildAction->id << ")\n";
            return false;
        }
        if (buildAction->type == BuildActionType::Invalid || buildAction->type > BuildActionType::LastType) {
            std::cerr << "Deleting build action " << id << ": type is invalid\n";
            return false;
        }
        ++ok;
        return true;
    });
    if (ok < processed) {
        std::cerr << "Discarding " << (processed - ok) << " invalid build actions.\n";
    } else {
        std::cerr << "All " << ok << " build actions are valid.\n";
    }
    std::cerr << "Committing changes to build actions.\n";
    txn.commit();
}

void ServiceSetup::BuildSetup::forEachBuildAction(
    std::function<void(std::size_t)> count, ServiceSetup::BuildSetup::BuildActionVisitorBase &&func, std::size_t limit, std::size_t start)
{
    auto txn = m_storage->buildActions.getROTransaction();
    const auto total = txn.size();
    count(std::min(limit, total));
    const auto reverse = start == std::numeric_limits<std::size_t>::max();
    for (auto i = reverse ? txn.rbegin<decltype(txn)::DirectStorage, BuildActionBase>()
                          : txn.lower_bound<decltype(txn)::DirectStorage, BuildActionBase>(static_cast<LibPkg::StorageID>(
                                start > std::numeric_limits<LibPkg::StorageID>::max() ? std::numeric_limits<LibPkg::StorageID>::max() : start));
        i != txn.end() && limit; reverse ? --i : ++i, --limit) {
        if (func(i.getID(), std::move(i.value()))) {
            return;
        }
    }
}

void ServiceSetup::BuildSetup::forEachBuildAction(ServiceSetup::BuildSetup::BuildActionVisitorWriteable &&func, std::size_t *count)
{
    auto txn = m_storage->buildActions.getRWTransaction();
    if (count) {
        *count = txn.size();
    }
    for (auto i = txn.begin(); i != txn.end(); ++i) {
        try {
            const auto running = m_runningActions.find(i.getID());
            auto &action = running != m_runningActions.end() ? *running->second : i.value();
            auto visitorBehavior = VisitorBehavior::DiscardChanges;
            const auto stop = func(i.getID(), action, visitorBehavior);
            if (visitorBehavior == VisitorBehavior::SaveChanges) {
                if (running != m_runningActions.end() && !action.isExecuting()) {
                    m_runningActions.erase(running);
                }
                txn.put(action, i.getID());
            } else if (visitorBehavior == VisitorBehavior::Delete && !action.isExecuting()) {
                if (running != m_runningActions.end()) {
                    m_runningActions.erase(running);
                }
                txn.del(i.getID());
            }
            if (stop) {
                break;
            }
        } catch (const ReflectiveRapidJSON::BinaryVersionNotSupported &e) {
            cerr << Phrases::ErrorMessage << "Unable to load build action record " << e.record << " from database: version not supported (got "
                 << e.presentVersion << ", max supported is " << e.maxVersion << ')' << Phrases::EndFlush;
        }
    }
    txn.commit();
}

std::vector<std::shared_ptr<BuildAction>> ServiceSetup::BuildSetup::followUpBuildActions(BuildActionIdType forId)
{
    auto res = std::vector<std::shared_ptr<BuildAction>>();
    const auto i = m_followUpActions.find(forId);
    if (i == m_followUpActions.end()) {
        return res;
    }
    res.reserve(i->second.size());
    for (const auto followUpId : i->second) {
        if (auto buildAction = getBuildAction(followUpId)) {
            res.emplace_back(std::move(buildAction));
        }
    }
    return res;
}

void ServiceSetup::loadConfigFiles(bool doFirstTimeSetup)
{
    // read config files
    auto configIni = IniFile();
    for (const auto &configFilePath : configFilePaths) {
        std::cout << Phrases::InfoMessage << "Reading config file: " << configFilePath << Phrases::EndFlush;
        try {
            // parse ini
            auto configFile = std::ifstream();
            configFile.exceptions(std::fstream::badbit | std::fstream::failbit);
            configFile.open(configFilePath, std::fstream::in);
            configIni.parse(configFile);
            // read basic configuration values (not cached)
            for (const auto &iniEntry : configIni.data()) {
                if (iniEntry.first.empty()) {
                    convertValue(iniEntry.second, "pacman_config_file_path", pacmanConfigFilePath);
                    convertValue(iniEntry.second, "working_directory", workingDirectory);
                    convertValue(iniEntry.second, "default_arch", defaultArch);
                    convertValue(iniEntry.second, "db_path", dbPath);
                    convertValue(iniEntry.second, "max_dbs", maxDbs);
                    convertValue(iniEntry.second, "package_cache_limit", packageCacheLimit);
                }
            }
            // apply working directory
            // note: As this function can run multiple times (as live-reconfigurations are supported) we
            //       must restore the initial working directory here so relative paths are always treated
            //       relative to the initial working directory.
            if (!workingDirectory.empty()) {
                try {
                    if (initialWorkingDirectory.empty()) {
                        initialWorkingDirectory = std::filesystem::current_path();
                    } else {
                        std::filesystem::current_path(initialWorkingDirectory);
                    }
                    workingDirectory = std::filesystem::absolute(workingDirectory);
                } catch (const std::filesystem::filesystem_error &e) {
                    std::cerr << Phrases::WarningMessage << "Unable to determine absolute path of specified working directory: " << e.what()
                              << Phrases::EndFlush;
                }
                if (chdir(workingDirectory.c_str()) != 0) {
                    std::cerr << Phrases::WarningMessage << "Unable to change the working directory to \"" << workingDirectory
                              << "\": " << std::strerror(errno) << Phrases::EndFlush;
                    try {
                        workingDirectory = std::filesystem::current_path();
                    } catch (const std::filesystem::filesystem_error &e) {
                        std::cerr << Phrases::WarningMessage << "Unable to determine effective working directory: " << e.what() << Phrases::EndFlush;
                    }
                }
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
                } else if (iniEntry.first == "complementary_variants") {
                    building.readComplementaryVariants(iniEntry.second);
                } else if (startsWith(iniEntry.first, "user/")) {
                    auth.applyConfig(iniEntry.first.substr(5), iniEntry.second);
                }
            }
        } catch (const std::ios_base::failure &) {
            std::cerr << Phrases::WarningMessage << "An IO error occurred when parsing \"" << configFilePath << "\", using defaults"
                      << Phrases::EndFlush;
        }
    }

    // restore state/cache and discard databases
    if (doFirstTimeSetup) {
        initStorage();
        doFirstTimeSetup = false;
    } else {
        config.setPackageCacheLimit(packageCacheLimit);
    }

    // avoid stale values
    config.packageCacheDirs.clear();

    // read pacman config
    try {
        config.loadPacmanConfig(pacmanConfigFilePath.data());
    } catch (const ios_base::failure &e) {
        cerr << Phrases::ErrorMessage << "An IO error occurred when loading pacman config: " << e.what() << Phrases::EndFlush;
    } catch (const runtime_error &e) {
        cerr << Phrases::ErrorMessage << "An error occurred when loading pacman config: " << e.what() << Phrases::EndFlush;
    }

    // add databases declared in config
    std::unordered_map<std::string, std::string> globalDefinitions, dbDefinitions;
    for (auto &iniEntry : configIni.data()) {
        if (const auto &iniSection = iniEntry.first; iniSection == "definitions") {
            for (auto &definition : iniEntry.second) {
                globalDefinitions['$' + definition.first] = std::move(definition.second);
            }
        }
    }
    for (auto &iniEntry : configIni.data()) {
        const auto &iniSection = iniEntry.first;
        if (!startsWith(iniSection, "database/")) {
            continue;
        }
        // find existing database or create a new one; clear mirrors and other data from existing DBs
        auto *const db = config.findOrCreateDatabaseFromDenotation(std::string_view(iniSection.data() + 9, iniSection.size() - 9), true);
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
                db->arch = std::move(value);
                dbDefinitions["$arch"] = db->arch;
            } else if (key == "depends") {
                db->dependencies = splitString<vector<string>>(value, " ", EmptyPartsTreat::Omit);
            } else if (key == "pkgdir") {
                db->localPkgDir = std::move(value);
            } else if (key == "dbdir") {
                db->localDbDir = std::move(value);
            } else if (key == "path") {
                db->path = std::move(value);
            } else if (key == "filespath") {
                db->filesPath = std::move(value);
            } else if (key == "sync_from_mirror") {
                db->syncFromMirror = value == "on";
            } else if (key == "mirror") {
                db->mirrors.emplace_back(std::move(value));
            } else {
                dbDefinitions["$" + key] = std::move(value);
            }
        }
    }

    // deduce database paths from local database dirs; remove duplicated mirrors
    for (auto &db : config.databases) {
        db.deducePathsFromLocalDirs();
        deduplicateVector(db.mirrors);
    }

    // clear unused locks because locks might not be useful anymore with the new config (e.g. the lock was for a
    // repository directory which has been removed)
    locks.clear(); // FIXME: Do this regularly to avoid the locks table growing potentially endlessly?

    // log the most important config values
    cerr << Phrases::InfoMessage << "Working directory: " << workingDirectory << Phrases::End;
    cerr << Phrases::InfoMessage << "Package cache dirs:" << Phrases::End;
    for (const auto &dir : config.packageCacheDirs) {
        cerr << Phrases::SubMessage << dir << Phrases::End;
    }
    cerr << Phrases::InfoMessage << "Build configuration:" << Phrases::End;
    cerr << Phrases::SubMessage << "Package cache directory: " << building.packageCacheDir << Phrases::End;
    cerr << Phrases::SubMessage << "Package download limit: " << dataSizeToString(building.packageDownloadSizeLimit) << Phrases::End;
    cerr << Phrases::SubMessage << "Chroot directory: " << building.chrootDir << Phrases::End;
    cerr << Phrases::SubMessage << "Chroot root user: " << building.chrootRootUser << Phrases::End;
    cerr << Phrases::SubMessage << "Chroot default user: " << building.chrootDefaultUser << Phrases::End;
    cerr << Phrases::SubMessage << "Ccache directory: " << building.ccacheDir << Phrases::End;
}

#ifdef PLATFORM_LINUX
static void printLimitValue(auto value, bool size)
{
    if (value == RLIM_INFINITY) {
        cerr << "infinity";
    } else if (size) {
        cerr << dataSizeToString(value);
    } else {
        cerr << value;
    }
}

static void printLimit(auto field, std::string_view fieldName, bool size = false)
{
    auto limit = rlimit();
    getrlimit(field, &limit);
    cerr << Phrases::SubMessage << fieldName << ": ";
    printLimitValue(limit.rlim_cur, size);
    cerr << " / ";
    printLimitValue(limit.rlim_max, size);
    cerr << Phrases::End;
}
#endif

void ServiceSetup::printLimits()
{
#ifdef PLATFORM_LINUX
    cerr << Phrases::InfoMessage << "Limits (soft / hard):" << Phrases::End;
    printLimit(RLIMIT_NOFILE, "NOFILE (Number of open files)");
    printLimit(RLIMIT_MEMLOCK, "MEMLOCK (Locked-in-memory address space)", true);
    printLimit(RLIMIT_LOCKS, "LOCKS (Maximum number of file locks)");
#endif
}

void ServiceSetup::printDatabases()
{
    cerr << Phrases::SuccessMessage << "Found " << config.databases.size() << " databases:" << Phrases::End;
    for (const auto &db : config.databases) {
        cerr << Phrases::SubMessage << db.name << "@" << db.arch << ": " << db.packageCount() << " packages, last updated on "
             << db.lastUpdate.load().toString(DateTimeOutputFormat::DateAndTime) << Phrases::End << "     - path: " << db.path
             << "\n     - local db dir: " << db.localDbDir << "\n     - local package dir: " << db.localPkgDir << '\n';
    }
    cerr << Phrases::SubMessage << "AUR (" << config.aur.packageCount() << " packages cached)" << Phrases::End;
}

void ServiceSetup::printIoUringUsage()
{
    cout << Phrases::InfoMessage << "Using io_uring: ";
#ifdef BOOST_ASIO_HAS_IO_URING
    cout << "yes";
#ifdef BOOST_ASIO_DISABLE_EPOLL
    cout << ", epoll disabled";
#endif
#else
    cout << "no";
#endif
    cout << Phrases::End;
}

std::string_view ServiceSetup::cacheFilePath() const
{
    return "cache-v" LIBREPOMGR_CACHE_VERSION ".bin";
}

void ServiceSetup::restoreState()
{
    // restore configuration and maybe build actions from JSON file
    const auto cacheFilePath = this->cacheFilePath();
    auto size = std::size_t(0);
    try {
        auto cacheFile = std::fstream();
        cacheFile.exceptions(std::ios_base::failbit | std::ios_base::badbit);
        cacheFile.open(cacheFilePath.data(), std::ios_base::in | std::ios_base::binary);
        auto deserializer = ReflectiveRapidJSON::BinaryReflector::BinaryDeserializer(&cacheFile);
        deserializer.read(config);
        size = static_cast<std::uint64_t>(cacheFile.tellg());
        cacheFile.close();
        std::cerr << Phrases::SuccessMessage << "Restored cache file \"" << cacheFilePath << "\", " << dataSizeToString(size) << Phrases::EndFlush;
    } catch (const ConversionException &) {
        std::cerr << Phrases::WarningMessage << "A conversion error occurred when restoring cache file \"" << cacheFilePath << "\"."
                  << Phrases::EndFlush;
    } catch (const ios_base::failure &) {
        std::cerr << Phrases::WarningMessage << "An IO error occurred when restoring cache file \"" << cacheFilePath << "\"." << Phrases::EndFlush;
    }

    try {
        // open LMDB storage
        cout << Phrases::InfoMessage << "Opening config LMDB file: " << dbPath << " (max DBs: " << maxDbs << ')' << Phrases::End;
        config.initStorage(dbPath.data(), maxDbs);
        cout << Phrases::SubMessage << "Package cache limit: " << packageCacheLimit << Phrases::End;
        config.setPackageCacheLimit(packageCacheLimit);
        cout << Phrases::InfoMessage << "Opening actions LMDB file: " << building.dbPath << Phrases::EndFlush;
        building.initStorage(building.dbPath.data());

        // ensure no build actions are considered running anymore and populate follow up actions
        building.forEachBuildAction([this](LibPkg::StorageID, BuildAction &buildAction, BuildSetup::VisitorBehavior &visitorBehavior) {
            if (buildAction.isExecuting()) {
                buildAction.status = BuildActionStatus::Finished;
                buildAction.result = BuildActionResult::Failure;
                buildAction.resultData = "service crashed while exectuing";
                visitorBehavior = BuildSetup::VisitorBehavior::SaveChanges;
            } else if (buildAction.isScheduled()) {
                for (const auto previousBuildActionId : buildAction.startAfter) {
                    building.m_followUpActions[previousBuildActionId].emplace(buildAction.id);
                }
            }
            return false;
        });
    } catch (const std::exception &e) {
        cerr << Phrases::ErrorMessage << "Unable to load build actions from database: " << e.what() << Phrases::EndFlush;
    }
}

std::size_t ServiceSetup::saveState()
{
    // write cache file to be able to restore the service state when restarting the service efficiently
    const auto cacheFilePath = this->cacheFilePath();
    auto size = std::size_t(0);
    try {
        auto cacheFile = std::fstream();
        cacheFile.exceptions(std::ios_base::failbit | std::ios_base::badbit);
        cacheFile.open(cacheFilePath.data(), std::ios_base::out | std::ios_base::trunc | std::ios_base::binary);
        auto serializer = ReflectiveRapidJSON::BinaryReflector::BinarySerializer(&cacheFile);
        serializer.write(config);
        size = static_cast<std::uint64_t>(cacheFile.tellp());
        cacheFile.close();
        std::cerr << Phrases::SuccessMessage << "Wrote cache file \"" << cacheFilePath << "\", " << dataSizeToString(size) << Phrases::EndFlush;
    } catch (const ios_base::failure &) {
        std::cerr << Phrases::WarningMessage << "An IO error occurred when dumping the cache file \"" << cacheFilePath << "\"." << Phrases::EndFlush;
    }
    return size;
}

void ServiceSetup::initStorage()
{
    restoreState();
    config.markAllDatabasesToBeDiscarded();
}

int ServiceSetup::run()
{
#ifdef USE_LIBSYSTEMD
    sd_notify(0, "STATUS=Loading databases");
#endif

#ifndef CPP_UTILITIES_DEBUG_BUILD
    try {
#endif
        printLimits();
        printIoUringUsage();
        loadConfigFiles(true);
        if (!config.storage()) {
            cerr << Phrases::SubError << "Stopping as database could not be initialized." << endl;
            return EXIT_FAILURE + 1;
        }
        config.discardDatabases();
        config.loadAllPackages(building.loadFilesDbs, building.forceLoadingDbs);
#ifndef CPP_UTILITIES_DEBUG_BUILD
    } catch (const std::exception &e) {
        cerr << Phrases::SubError << e.what() << endl;
        return EXIT_FAILURE + 1;
    } catch (...) {
        cerr << Phrases::SubError << "An unknown error occurred." << endl;
        return EXIT_FAILURE + 1;
    }
#endif

#ifndef CPP_UTILITIES_DEBUG_BUILD
    try {
#else
    {
#endif
        printDatabases();

#ifdef USE_LIBSYSTEMD
        sd_notify(0, "STATUS=Initializing SSL");
#endif
        cout << Phrases::SuccessMessage << "Initializing SSL" << Phrases::End;
        webServer.initSsl();

#ifdef USE_LIBSYSTEMD
        sd_notify(0, "STATUS=Allocating worker thread pool");
#endif
        cout << Phrases::SuccessMessage << "Allocating worker thread pool (thread count: " << building.threadCount << ")" << Phrases::End;
        const auto buildWorker = building.allocateBuildWorker();

#ifdef USE_LIBSYSTEMD
        sd_notify(0, "STATUS=Starting web server");
#endif
        cout << Phrases::SuccessMessage << "Starting web server (thread count: " << webServer.threadCount << "):" << TextAttribute::Reset
             << " http://" << webServer.address << ':' << webServer.port << endl;
        WebAPI::Server::serve(*this);
        cout << Phrases::SuccessMessage << "Web server stopped." << Phrases::EndFlush;
#ifndef CPP_UTILITIES_DEBUG_BUILD
    } catch (const boost::exception &e) {
        cerr << Phrases::ErrorMessage << "Server terminated due to exception: " << Phrases::End << "    " << boost::diagnostic_information(e)
             << Phrases::EndFlush;
        return EXIT_FAILURE + 2;
    } catch (const std::exception &e) {
        cerr << Phrases::ErrorMessage << "Server terminated due to exception: " << Phrases::End << "    " << e.what() << Phrases::EndFlush;
        return EXIT_FAILURE + 2;
    } catch (...) {
        cerr << Phrases::ErrorMessage << "Server terminated due to an unknown error." << Phrases::EndFlush;
        return EXIT_FAILURE + 2;
    }
#else
    }
#endif

#ifndef CPP_UTILITIES_DEBUG_BUILD
    try {
#endif
        saveState();
        building.forEachBuildAction([](LibPkg::StorageID, BuildAction &buildAction, BuildSetup::VisitorBehavior &visitorBehavior) {
            if (buildAction.isExecuting()) {
                buildAction.status = BuildActionStatus::Finished;
                buildAction.result = BuildActionResult::Aborted;
                visitorBehavior = BuildSetup::VisitorBehavior::SaveChanges;
            }
            return false;
        });
#ifndef CPP_UTILITIES_DEBUG_BUILD
    } catch (const std::exception &e) {
        cerr << Phrases::ErrorMessage << "Exception occurred when terminating server: " << Phrases::End << "    " << e.what() << Phrases::EndFlush;
        return EXIT_FAILURE + 3;
    } catch (...) {
        cerr << Phrases::ErrorMessage << "Unknown error occurred when terminating server." << Phrases::EndFlush;
        return EXIT_FAILURE + 3;
    }
#endif

    return EXIT_SUCCESS;
}

int ServiceSetup::fixDb()
{
#ifndef CPP_UTILITIES_DEBUG_BUILD
    try {
#endif
        loadConfigFiles(true);
        building.initStorage(building.dbPath.data());
        building.rebuildDb();
        config.rebuildDb();
#ifndef CPP_UTILITIES_DEBUG_BUILD
    } catch (const std::exception &e) {
        cerr << Phrases::ErrorMessage << "Exception occurred: " << Phrases::End << "    " << e.what() << Phrases::EndFlush;
        return EXIT_FAILURE + 4;
    } catch (...) {
        cerr << Phrases::ErrorMessage << "Unknown error occurred." << Phrases::EndFlush;
        return EXIT_FAILURE + 4;
    }
#endif
    return EXIT_SUCCESS;
}

int ServiceSetup::dumpDb(std::string_view filterRegex)
{
#ifndef CPP_UTILITIES_DEBUG_BUILD
    try {
#endif
        loadConfigFiles(true);
        config.dumpDb(filterRegex.empty() ? std::nullopt : std::optional(std::regex(filterRegex.begin(), filterRegex.end())));
#ifndef CPP_UTILITIES_DEBUG_BUILD
    } catch (const std::exception &e) {
        cerr << Phrases::ErrorMessage << "Exception occurred: " << Phrases::End << "    " << e.what() << Phrases::EndFlush;
        return EXIT_FAILURE + 5;
    } catch (...) {
        cerr << Phrases::ErrorMessage << "Unknown error occurred." << Phrases::EndFlush;
        return EXIT_FAILURE + 5;
    }
#endif
    return EXIT_SUCCESS;
}

void ServiceSetup::Locks::clear()
{
    auto log = LogContext();
    const auto lock = std::unique_lock(m_cleanupMutex);
    for (auto i = m_locksByName.begin(), end = m_locksByName.end(); i != end;) {
        if (auto lock2 = i->second.tryLockToWrite(log, std::string(i->first)); lock2.lock()) { // check whether nobody holds the lock anymore
            lock2.lock().unlock(); // ~shared_mutex(): The behavior is undefined if the mutex is owned by any thread [...].
            m_locksByName.erase(i++); // we can be sure no other thead acquires i->second in the meantime because we're holding m_mutex
        } else {
            ++i;
        }
    }
}

std::string ServiceSetup::Locks::forDatabase(std::string_view dbName, std::string_view dbArch)
{
    return dbName % '@' + dbArch;
}

std::string ServiceSetup::Locks::forDatabase(const LibPkg::Database &db)
{
    return forDatabase(db.name, db.arch);
}

ServiceStatus::ServiceStatus(ServiceSetup &setup)
    : version(applicationInfo.version)
    , url(applicationInfo.url)
    , config(setup.config.computeStatus())
    , actions(setup.building.metaInfo)
    , presets(setup.building.presets)
    , defaultArch(setup.defaultArch)
    , resourceUsage(setup)
{
}

} // namespace LibRepoMgr
