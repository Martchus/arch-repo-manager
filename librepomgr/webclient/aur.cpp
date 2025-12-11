#include "./aur.h"

#include "../logging.h"

#include "../webapi/params.h"
#include "../webapi/server.h"

#include "../json.h"
#include "../multisession.h"

#include "../buildactions/buildactionprivate.h"
#include "../buildactions/subprocess.h"

#include "../../libpkg/data/package.h"
#include "../../libpkg/parser/aur.h"

#include <c++utilities/conversion/stringbuilder.h>
#include <c++utilities/io/ansiescapecodes.h>
#include <c++utilities/io/misc.h>

#include <boost/process/v1/start_dir.hpp>

#include <filesystem>
#include <iostream>
#include <numeric>
#include <sstream>

using namespace std;
using namespace CppUtilities;
using namespace CppUtilities::EscapeCodes;
using namespace LibPkg;

namespace LibRepoMgr {

namespace WebClient {

/// \cond
constexpr auto aurHost = "aur.archlinux.org";
constexpr auto aurPort = "443";
/// \endcond

/*!
 * \brief Searches the AUR for packages which name contains the specified \a searchTerm.
 */
void searchAurPackages(LogContext &log, ServiceSetup &setup, const std::string &searchTerm, boost::asio::io_context &ioContext,
    std::shared_ptr<AurQuerySession> &multiSession)
{
    auto session = std::make_shared<WebClient::Session>(ioContext, setup.webServer.sslContext,
        [&log, &setup, multiSession](WebClient::Session &session2, const WebClient::HttpClientError &error) mutable {
            if (error.errorCode != boost::beast::errc::success && error.errorCode != boost::asio::ssl::error::stream_truncated) {
                log(Phrases::ErrorMessage, "Failed to search AUR: ", error.what(), '\n');
                return;
            }

            // parse retrieved JSON
            const auto &body = get<Response>(session2.response).body();
            try {
                // parse and cache the AUR packages
                auto packages = Package::fromAurRpcJson(body.data(), body.size(), PackageOrigin::AurRpcSearch);
                auto lock = setup.config.lockToRead();
                auto updater = LibPkg::PackageUpdater(setup.config.aur);
                for (auto &[packageID, package] : packages) {
                    packageID = updater.update(package);
                }
                updater.commit();
                lock.unlock();
                multiSession->addResponses(packages);
            } catch (const RAPIDJSON_NAMESPACE::ParseResult &e) {
                log(Phrases::ErrorMessage, "Unable to parse AUR search result: ", serializeParseError(e), '\n');
            }
        });
    session->run(aurHost, aurPort, boost::beast::http::verb::get, ("/rpc/?v=5&type=search&arg=" + WebAPI::Url::encodeValue(searchTerm)).data(), 11);
}

/// \cond
const std::string &packageNameFromIterator(std::vector<std::string>::const_iterator i)
{
    return *i;
}

const std::string &packageNameFromIterator(std::unordered_map<string, std::shared_ptr<Package>>::const_iterator i)
{
    return i->first;
}

template <typename PackageCollection>
std::shared_ptr<AurQuerySession> queryAurPackagesInternal(LogContext &log, ServiceSetup &setup, const PackageCollection &packages,
    boost::asio::io_context &ioContext, typename AurQuerySession::HandlerType &&handler)
{
    log("Retrieving ", packages.size(), " packages from the AUR\n");

    constexpr auto packagesPerQuery = 100;
    auto multiSession = AurQuerySession::create(ioContext, std::move(handler));

    for (auto i = packages.cbegin(), end = packages.cend(); i != end;) {
        auto session = make_shared<WebClient::Session>(ioContext, setup.webServer.sslContext,
            [&log, &setup, multiSession](WebClient::Session &session2, const WebClient::HttpClientError &error) mutable {
                if (error.errorCode != boost::beast::errc::success && error.errorCode != boost::asio::ssl::error::stream_truncated) {
                    log(Phrases::ErrorMessage, "Failed to retrieve AUR packages from RPC: ", error.what(), '\n');
                    return;
                }

                // parse retrieved JSON
                const auto &body = get<Response>(session2.response).body();
                try {
                    // parse and cache the AUR packages
                    auto packagesFromAur = Package::fromAurRpcJson(body.data(), body.size());
                    auto lock = setup.config.lockToRead();
                    auto updater = PackageUpdater(setup.config.aur);
                    for (auto &[packageID, package] : packagesFromAur) {
                        packageID = updater.update(package);
                    }
                    updater.commit();
                    lock.unlock();
                    multiSession->addResponses(packagesFromAur);
                } catch (const RAPIDJSON_NAMESPACE::ParseResult &e) {
                    log(Phrases::ErrorMessage, "Unable to parse AUR package from RPC: ", serializeParseError(e), '\n');
                }
            });

        const auto url = [&] {
            stringstream urlSteam;
            urlSteam << "/rpc/?v=5&type=info";
            for (size_t batchIndex = 0; batchIndex != packagesPerQuery && i != end; ++batchIndex, ++i) {
                urlSteam << "&arg[]=";
                urlSteam << WebAPI::Url::encodeValue(packageNameFromIterator(i));
            }
            return urlSteam.str();
        }();

        session->run(aurHost, aurPort, boost::beast::http::verb::get, url.data(), 11);
    }
    return multiSession;
}
/// \endcond

/*!
 * \brief Queries the AUR asnychronously to populate \a setup.config.aur.packages for all specified \a packages.
 * \remarks When results are available, a write-lock is acquired to insert the results. When this is done, the write-lock is released and \a handler
 *          is called.
 */
std::shared_ptr<AurQuerySession> queryAurPackages(LogContext &log, ServiceSetup &setup, const std::vector<string> &packages,
    boost::asio::io_context &ioContext, typename AurQuerySession::HandlerType &&handler)
{
    return queryAurPackagesInternal(log, setup, packages, ioContext, std::move(handler));
}

/*!
 * \brief Queries the AUR asnychronously to populate \a setup.config.aur.packages for all package names within the specified map of \a packages.
 * \remarks When results are available, a write-lock is acquired to insert the results. When this is done, the write-lock is released and \a handler
 *          is called.
 */
std::shared_ptr<AurQuerySession> queryAurPackages(LogContext &log, ServiceSetup &setup,
    const std::unordered_map<string, std::shared_ptr<Package>> &packages, boost::asio::io_context &ioContext,
    typename AurQuerySession::HandlerType &&handler)
{
    return queryAurPackagesInternal(log, setup, packages, ioContext, std::move(handler));
}

/*!
 * \brief Queries the AUR asnychronously to populate \a setup.config.aur.packages for all package names contained by \a database.
 * \returns Returns true if all these packages are already present and no async operation needed to be started. Returns false when an async
 *          operation has been started.
 * \remarks If the current thread already holds a read-only lock to the config one must pass it via \a configReadLock. Otherwise this function
 *          will attempt to acquire its own lock (which makes no sense if already locked). In case the function returns false the lock is released.
 *          When results are available, a write-lock is acquired to insert the results. When this is done, the write-lock is released and \a handler
 *          is called.
 */
std::shared_ptr<AurQuerySession> queryAurPackagesForDatabase(LogContext &log, ServiceSetup &setup, boost::asio::io_context &ioContext,
    std::shared_lock<std::shared_mutex> *configReadLock, LibPkg::Database &database, typename AurQuerySession::HandlerType &&handler)
{
    auto missingPackages = std::vector<std::string>();
    auto ownConfigReadLock = std::shared_lock<std::shared_mutex>();
    if (!configReadLock) {
        ownConfigReadLock = setup.config.lockToRead();
        configReadLock = &ownConfigReadLock;
    }
    auto &aurDb = setup.config.aur;
    database.allPackages([&aurDb, &missingPackages](StorageID, std::shared_ptr<Package> &&package) {
        if (const auto aurPackage = aurDb.findPackage(package->name); !aurPackage) {
            missingPackages.emplace_back(std::move(package->name));
        }
        return false;
    });
    if (missingPackages.empty()) {
        return nullptr;
    }
    configReadLock->unlock();
    return queryAurPackages(log, setup, missingPackages, ioContext, std::move(handler));
}

/*!
 * \brief Marks target directory as from AUR to prevent running `makepkg --printsrcinfo` in that directory, also on subsequent runs.
 */
static bool markAurPackageDirectory(const AurSnapshotQueryParams &params, std::shared_ptr<AurSnapshotQuerySession> &multiSession)
{
    try {
        filesystem::create_directories(*params.targetDirectory);
        writeFile(*params.targetDirectory + "/from-aur", *params.lookupPackageName);
        return true;
    } catch (const std::runtime_error &e) {
        multiSession->addResponse(WebClient::AurSnapshotResult{ .packageName = *params.packageName,
            .errorOutput = std::string(),
            .packages = {},
            .error = argsToString("Unable to create directory marker for AUR contents: ", e.what()) });
        return false;
    }
}

/*!
 * \brief Populates \a error if \a packages are not valid.
 */
void AurSnapshotResult::checkPackages()
{
    if (packages.empty() || packages.front().pkg->name.empty()) {
        error = "Unable to parse .SRCINFO: no package name present";
    } else if (!packages.front().pkg->sourceInfo.has_value()) {
        error = "Unable to parse .SRCINFO: no source info present";
    }
}

/*!
 * \brief Downloads the latest snapshot from the AUR via HTTP as tar archive for the specified \a queryParams.
 */
static void queryAurSnapshotsViaTarDownload(LogContext &log, ServiceSetup &setup, const std::vector<AurSnapshotQueryParams> &queryParams,
    boost::asio::io_context &ioContext, std::shared_ptr<AurSnapshotQuerySession> &multiSession)
{
    CPP_UTILITIES_UNUSED(log)
    for (const auto &params : queryParams) {
        auto session = std::make_shared<WebClient::Session>(ioContext, setup.webServer.sslContext,
            [multiSession, params = params, &log, &setup, &ioContext](WebClient::Session &session2, const WebClient::HttpClientError &error) mutable {
                if (error.errorCode != boost::beast::errc::success && error.errorCode.message() != "stream truncated") {
                    // retry on connection errors
                    if (params.retries) {
                        --params.retries;
                        queryAurSnapshotsViaTarDownload(log, setup, { params }, ioContext, multiSession);
                        return;
                    }

                    multiSession->addResponse(WebClient::AurSnapshotResult{ .packageName = *params.packageName,
                        .errorOutput = std::string(),
                        .packages = {},
                        .error = "Unable to retrieve AUR snapshot tarball for package " % *params.packageName % ": " + error.what() });
                    return;
                }

                // parse retrieved archive
                const auto &response = get<Response>(session2.response);
                if (response.result() != boost::beast::http::status::ok) {
                    // download from AUR after all if the sources cannot be found in the official Arch Linux Git repositories
                    if (response.result() == boost::beast::http::status::not_found && params.tryOfficial) {
                        params.tryOfficial = false;
                        queryAurSnapshotsViaTarDownload(log, setup, { params }, ioContext, multiSession);
                        return;
                    }
                    // retry on 5xx errors
                    if (params.retries && response.result_int() >= 500 && response.result_int() < 600) {
                        --params.retries;
                        queryAurSnapshotsViaTarDownload(log, setup, { params }, ioContext, multiSession);
                        return;
                    }
                    multiSession->addResponse(WebClient::AurSnapshotResult{ .packageName = *params.packageName,
                        .errorOutput = std::string(),
                        .packages = {},
                        .error
                        = "Unable to retrieve AUR snapshot tarball for package " % *params.packageName % ": AUR returned " % response.result_int()
                            + " response",
                        .is404 = response.result() == boost::beast::http::status::not_found });
                    return;
                }
                const auto &body = response.body();
                auto snapshotFiles = FileMap{};
                try {
                    snapshotFiles = extractFilesFromBuffer(body, *params.packageName, [](const char *, const char *, mode_t) { return true; });
                } catch (const std::runtime_error &extractionError) {
                    multiSession->addResponse(WebClient::AurSnapshotResult{ .packageName = *params.packageName,
                        .errorOutput = std::string(),
                        .packages = {},
                        .error = "Unable to extract AUR snapshot tarball for package " % *params.packageName % ": " + extractionError.what() });
                    return;
                }
                if (!markAurPackageDirectory(params, multiSession)) {
                    return;
                }

                auto result
                    = AurSnapshotResult{ .packageName = *params.packageName, .errorOutput = std::string(), .packages = {}, .error = std::string() };
                auto haveSrcFileInfo = false, havePkgbuild = false;
                const auto &lookupName = params.lookupPackageName ? *params.lookupPackageName : *params.packageName;
                for (const auto &directory : snapshotFiles) {
                    // parse .SRCINFO and check for presence of PKGBUILD
                    if (!startsWith(directory.first, lookupName)) {
                        continue;
                    }
                    auto directoryPath = string_view{ directory.first }.substr(lookupName.size());
                    if (directoryPath.starts_with("-main")) {
                        directoryPath = directoryPath.substr(5);
                    }
                    if (directoryPath.empty()) {
                        for (const auto &rootFile : directory.second) {
                            if (rootFile.name == ".SRCINFO") {
                                result.packages = Package::fromInfo(rootFile.content, false);
                                haveSrcFileInfo = true;
                            } else if (rootFile.name == "PKGBUILD") {
                                havePkgbuild = true;
                            }
                            if (haveSrcFileInfo && havePkgbuild) {
                                continue;
                            }
                        }
                    }

                    // store files in target directory
                    const auto targetDir = *params.targetDirectory % '/' + directoryPath;
                    try {
                        filesystem::create_directories(targetDir);
                    } catch (const filesystem::filesystem_error &fileSystemError) {
                        multiSession->addResponse(WebClient::AurSnapshotResult{ .packageName = *params.packageName,
                            .errorOutput = std::string(),
                            .packages = {},
                            .error = "Unable to make directory " % targetDir % ": " + fileSystemError.what() });
                        return;
                    }
                    for (const auto &file : directory.second) {
                        const auto targetPath = directory.first.empty() ? (*params.targetDirectory % '/' + file.name)
                                                                        : (*params.targetDirectory % '/' % directoryPath % '/' + file.name);
                        if (file.type == ArchiveFileType::Link) {
                            try {
                                filesystem::create_symlink(file.content, targetPath);
                            } catch (const filesystem::filesystem_error &fileSystemError) {
                                multiSession->addResponse(WebClient::AurSnapshotResult{ .packageName = *params.packageName,
                                    .errorOutput = std::string(),
                                    .packages = {},
                                    .error = "Unable to make symlink " % targetPath % ": " + fileSystemError.what() });
                                return;
                            }
                            continue;
                        }
                        try {
                            writeFile(targetPath, file.content);
                            setLastModified(targetPath, file.modificationTime);
                        } catch (const std::ios_base::failure &failure) {
                            multiSession->addResponse(WebClient::AurSnapshotResult{ .packageName = *params.packageName,
                                .errorOutput = std::string(),
                                .packages = {},
                                .error = "Unable to write " % targetPath % ": " + failure.what() });
                            return;
                        }
                    }
                }

                // validate what we've got and add response
                if (!havePkgbuild) {
                    result.error = "PKGINFO is missing";
                }
                if (params.tryOfficial) {
                    result.isOfficial = true;
                } else {
                    if (!haveSrcFileInfo) {
                        result.error = ".SRCINFO is missing";
                    }
                    result.checkPackages();
                }
                multiSession->addResponse(std::move(result));
            });

        // run query, e.g. https: //aur.archlinux.org/cgit/aur.git/snapshot/mingw-w64-configure.tar.gz
        const auto encodedPackageName = WebAPI::Url::encodeValue(params.lookupPackageName ? *params.lookupPackageName : *params.packageName);
        if (params.tryOfficial) {
            const auto url = "/archlinux/packaging/packages/" % encodedPackageName % "/-/archive/main/" % encodedPackageName + "-main.tar.gz";
            session->run("gitlab.archlinux.org", "443", boost::beast::http::verb::get, url.data(), 11);
        } else {
            const auto url = "/cgit/aur.git/snapshot/" % encodedPackageName + ".tar.gz";
            session->run("aur.archlinux.org", "443", boost::beast::http::verb::get, url.data(), 11);
        }
    }
}

/*!
 * \brief Downloads the latest snapshot from the AUR via the specified external downloader script for the specified \a queryParams.
 */
void queryAurSnapshotsViaScript(std::shared_ptr<BuildAction> &buildAction, const boost::filesystem::path &downloaderPath,
    const std::vector<AurSnapshotQueryParams> &queryParams, std::shared_ptr<AurSnapshotQuerySession> &multiSession)
{
    for (const auto &params : queryParams) {
        try {
            const auto targetPath = std::filesystem::path(*params.targetDirectory);
            if (params.targetDirectory->empty()) {
                throw std::filesystem::filesystem_error(
                    "Target path must not be empty", targetPath, std::make_error_code(std::errc::invalid_argument));
            }
            if (std::filesystem::exists(targetPath)) {
                std::filesystem::remove_all(targetPath);
            }
            std::filesystem::create_directories(targetPath);
        } catch (const std::filesystem::filesystem_error &e) {
            multiSession->addResponse(WebClient::AurSnapshotResult{ .packageName = *params.packageName,
                .errorOutput = std::string(),
                .packages = {},
                .error = "Unable to create empty directory to store AUR snapshot of package " % *params.packageName % ": " + e.what() });
            continue;
        }
        auto session = buildAction->makeBuildProcess("AUR download of " + *params.packageName,
            argsToString(buildAction->directory, "/aur-download-", *params.packageName, ".log"),
            [multiSession, params = params](boost::process::v1::child &&child, ProcessResult &&processResult) mutable {
                auto errorMessage = std::string();
                if (processResult.errorCode) {
                    errorMessage = processResult.errorCode.message();
                } else if (child.exit_code() != 0) {
                    errorMessage = argsToString("script exited with code ", child.exit_code());
                }
                if (!errorMessage.empty()) {
                    multiSession->addResponse(WebClient::AurSnapshotResult{ .packageName = *params.packageName,
                        .errorOutput = std::string(),
                        .packages = {},
                        .error = "Unable to download AUR snapshot via script for package " % *params.packageName % ": " + errorMessage });
                    return;
                }
                if (!markAurPackageDirectory(params, multiSession)) {
                    return;
                }

                auto result
                    = AurSnapshotResult{ .packageName = *params.packageName, .errorOutput = std::string(), .packages = {}, .error = std::string() };
                try {
                    if (!std::filesystem::exists(*params.targetDirectory + "/PKGBUILD")) {
                        result.error = "PKGINFO is missing";
                    }
                    if (!std::filesystem::exists(*params.targetDirectory + "/.SRCINFO")) {
                        result.error = ".SRCINFO is missing";
                    }
                    result.packages = Package::fromInfo(readFile(*params.targetDirectory + "/.SRCINFO"));
                    result.checkPackages();
                } catch (const std::ios_base::failure &e) {
                    result.error = "I/O error occurred when reading AUR snapshot for package " % *params.packageName % ": " + e.what();
                } catch (const std::filesystem::filesystem_error &e) {
                    result.error = "'Filesystem error occurred when reading AUR snapshot for package " % *params.packageName % ": " + e.what();
                }
                multiSession->addResponse(std::move(result));
            });
        const auto &packageName = params.lookupPackageName ? *params.lookupPackageName : *params.packageName;
        session->launch(downloaderPath, packageName, *params.targetDirectory);
    }
}

/*!
 * \brief Queries the AUR asynchronously to get the latest snapshot for the specified \a packageNames.
 * \remarks
 * - By default, downloads the tar file via http. If setup.building.aurDownloaderPath is set then this external script is invoked to do
 *   the download.
 * - If the "tryOfficial" flag in the parameter is set then this function attempts to download the sources from official Git repositories
 *   first. If the official repositories are unavailable or the package cannot be found there it will still fallback to downloading the
 *   sources from AUR. This is only used when downloading a tar file via http.
 * - The specified \a ioContext is only used when downloading a tar file via http.
 */
void queryAurSnapshots(std::shared_ptr<BuildAction> &buildAction, ServiceSetup &setup, const std::vector<AurSnapshotQueryParams> &queryParams,
    boost::asio::io_context &ioContext, std::shared_ptr<AurSnapshotQuerySession> &multiSession)
{
    auto configLock = setup.lockToRead();
    auto configuredAurDownloaderPath = setup.building.aurDownloaderPath;
    configLock.unlock();
    if (!configuredAurDownloaderPath.empty()) {
        if (const auto downloaderPath = findExecutable(configuredAurDownloaderPath); !downloaderPath.empty()) {
            queryAurSnapshotsViaScript(buildAction, downloaderPath, queryParams, multiSession);
            return;
        }
        buildAction->log()(Phrases::Warning, "Unable to find configured AUR downloader path \"", configuredAurDownloaderPath, "\". Falling back to HTTP download.");
    }
    queryAurSnapshotsViaTarDownload(buildAction->log(), setup, queryParams, ioContext, multiSession);
}

} // namespace WebClient

} // namespace LibRepoMgr
