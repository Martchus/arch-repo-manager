#include "./aur.h"

#include "../logging.h"

#include "../webapi/params.h"
#include "../webapi/server.h"

#include "../json.h"
#include "../multisession.h"

#include "../../libpkg/data/package.h"
#include "../../libpkg/parser/aur.h"

#include <c++utilities/conversion/stringbuilder.h>
#include <c++utilities/io/ansiescapecodes.h>
#include <c++utilities/io/misc.h>

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
    auto multiSession = AurQuerySession::create(ioContext, move(handler));

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
    return queryAurPackagesInternal(log, setup, packages, ioContext, move(handler));
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
    return queryAurPackagesInternal(log, setup, packages, ioContext, move(handler));
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
    return queryAurPackages(log, setup, missingPackages, ioContext, move(handler));
}

/*!
 * \brief Queries the AUR asynchronously to get the latest snapshot for the specified \a packageNames.
 */
void queryAurSnapshots(LogContext &log, ServiceSetup &setup, const std::vector<AurSnapshotQueryParams> &queryParams,
    boost::asio::io_context &ioContext, std::shared_ptr<AurSnapshotQuerySession> &multiSession)
{
    CPP_UTILITIES_UNUSED(log)
    for (const auto &params : queryParams) {
        auto session = std::make_shared<WebClient::Session>(ioContext, setup.webServer.sslContext,
            [multiSession, params](WebClient::Session &session2, const WebClient::HttpClientError &error) mutable {
                if (error.errorCode != boost::beast::errc::success && error.errorCode.message() != "stream truncated") {
                    multiSession->addResponse(WebClient::AurSnapshotResult{ .packageName = *params.packageName,
                        .error = "Unable to retrieve AUR snapshot tarball for package " % *params.packageName % ": " + error.what() });
                    return;
                }

                // parse retrieved archive
                const auto &response = get<Response>(session2.response);
                if (response.result() != boost::beast::http::status::ok) {
                    multiSession->addResponse(WebClient::AurSnapshotResult{ .packageName = *params.packageName,
                        .error
                        = "Unable to retrieve AUR snapshot tarball for package " % *params.packageName % ": AUR returned " % response.result_int()
                            + " response" });
                    return;
                }
                const auto &body = response.body();
                auto snapshotFiles = FileMap{};
                try {
                    snapshotFiles = extractFilesFromBuffer(body, *params.packageName, [](const char *, const char *, mode_t) { return true; });
                } catch (const std::runtime_error &extractionError) {
                    multiSession->addResponse(WebClient::AurSnapshotResult{ .packageName = *params.packageName,
                        .error = "Unable to extract AUR snapshot tarball for package " % *params.packageName % ": " + extractionError.what() });
                    return;
                }
                auto result = AurSnapshotResult{ .packageName = *params.packageName };
                auto haveSrcFileInfo = false, havePkgbuild = false;
                for (const auto &directory : snapshotFiles) {
                    // parse .SRCINFO and check for presence of PKGBUILD
                    if (!startsWith(directory.first, *params.packageName)) {
                        continue;
                    }
                    const auto directoryPath = string_view{ directory.first }.substr(params.packageName->size());
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
                        multiSession->addResponse(WebClient::AurSnapshotResult{
                            .packageName = *params.packageName, .error = "Unable to make directory " % targetDir % ": " + fileSystemError.what() });
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
                                    .error = "Unable to make symlink " % targetPath % ": " + fileSystemError.what() });
                                return;
                            }
                            continue;
                        }
                        try {
                            writeFile(targetPath, file.content);
                            setLastModified(targetPath, file.modificationTime);
                        } catch (const std::ios_base::failure &failure) {
                            multiSession->addResponse(WebClient::AurSnapshotResult{
                                .packageName = *params.packageName, .error = "Unable to write " % targetPath % ": " + failure.what() });
                            return;
                        }
                    }
                }

                // validate what we've got and add response
                if (!haveSrcFileInfo) {
                    result.error = ".SRCINFO is missing";
                }
                if (!havePkgbuild) {
                    result.error = "PKGINFO is missing";
                }
                if (result.packages.empty() || result.packages.front().pkg->name.empty()) {
                    result.error = "Unable to parse .SRCINFO: no package name present";
                } else if (!result.packages.front().pkg->sourceInfo.has_value()) {
                    result.error = "Unable to parse .SRCINFO: no source info present";
                }
                multiSession->addResponse(move(result));
            });

        // run query, e.g. https: //aur.archlinux.org/cgit/aur.git/snapshot/mingw-w64-configure.tar.gz
        const auto url = "/cgit/aur.git/snapshot/" % WebAPI::Url::encodeValue(*params.packageName) + ".tar.gz";
        session->run("aur.archlinux.org", "443", boost::beast::http::verb::get, url.data(), 11);
    }
}

} // namespace WebClient

} // namespace LibRepoMgr
