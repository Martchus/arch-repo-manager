#include "./buildactionprivate.h"

#include "../logging.h"
#include "../serversetup.h"

#include "../../libpkg/data/database.h"
#include "../../libpkg/data/package.h"
#include "../../libpkg/parser/utils.h"

#include <c++utilities/io/ansiescapecodes.h>

#include <regex>
#include <unordered_set>

using namespace std;
using namespace CppUtilities;
using namespace CppUtilities::EscapeCodes;

namespace LibRepoMgr {

ReloadLibraryDependencies::ReloadLibraryDependencies(ServiceSetup &setup, const std::shared_ptr<BuildAction> &buildAction)
    : InternalBuildAction(setup, buildAction)
{
}

void ReloadLibraryDependencies::run()
{
    // read configuration
    const auto flags = static_cast<ReloadLibraryDependenciesFlags>(m_buildAction->flags);
    const auto force = flags & ReloadLibraryDependenciesFlags::ForceReload;
    const auto skipDependencies = flags & ReloadLibraryDependenciesFlags::SkipDependencies;
    auto &metaInfo = m_setup.building.metaInfo;
    auto metaInfoLock = metaInfo.lockToRead();
    const auto &typeInfo = metaInfo.typeInfoForId(BuildActionType::ReloadLibraryDependencies);
    const auto packageExcludeRegexSetting = typeInfo.settings[static_cast<std::size_t>(ReloadLibraryDependenciesSettings::PackageExcludeRegex)].param;
    metaInfoLock.unlock();
    const auto &packageExcludeRegexValue = findSetting(packageExcludeRegexSetting);
    auto packageExcludeRegex = std::regex();
    if (!packageExcludeRegexValue.empty()) {
        try {
            packageExcludeRegex = std::regex(packageExcludeRegexValue);
        } catch (const std::regex_error &e) {
            reportError(argsToString("configured package exclude regex is invalid: ", e.what()));
            return;
        }
    }

    // initialize
    m_remainingPackages = 0;
    auto configReadLock = init(BuildActionAccess::ReadConfig, RequiredDatabases::MaybeDestination, RequiredParameters::None);
    if (std::holds_alternative<std::monostate>(configReadLock)) {
        return;
    }

    // use cache directory from global configuration
    auto buildLock = m_setup.building.lockToRead();
    const auto cacheDir = m_setup.building.packageCacheDir + '/';
    m_packageDownloadSizeLimit = m_setup.building.packageDownloadSizeLimit;
    buildLock.unlock();

    // find relevant databases and packages
    m_buildAction->appendOutput(Phrases::SuccessMessage, "Finding relevant databases/packages ...\n");
    m_relevantPackagesByDatabase.reserve(m_destinationDbs.empty() ? m_setup.config.databases.size() : m_destinationDbs.size());
    auto relevantDbs = std::unordered_set<LibPkg::Database *>();
    auto relevantPkgs = std::unordered_map<LibPkg::StorageID, std::shared_ptr<LibPkg::Package>>();
    auto missingDeps = LibPkg::DependencySet();
    auto visitedPackages = std::unordered_set<LibPkg::StorageID>();
    if (m_destinationDbs.empty()) {
        for (auto &db : m_setup.config.databases) {
            relevantDbs.emplace(&db);
        }
    } else {
        for (auto *const destinationDb : m_destinationDbs) {
            if (!relevantDbs.emplace(destinationDb).second || skipDependencies) {
                continue;
            }
            const auto databaseDependencyOrderRes = m_setup.config.computeDatabaseDependencyOrder(*destinationDb);
            if (std::holds_alternative<string>(databaseDependencyOrderRes)) {
                m_messages.errors.emplace_back(
                    destinationDb->name % ": unable to consider dependencies: " + std::get<std::string>(databaseDependencyOrderRes));
            }
            auto &databaseDependencyOrder = std::get<std::vector<LibPkg::Database *>>(databaseDependencyOrderRes);
            for (auto *const destinationDbOrDependency : databaseDependencyOrder) {
                relevantDbs.emplace(destinationDbOrDependency);
            }
        }
        for (auto *const destinationDb : m_destinationDbs) {
            destinationDb->allPackages([&, this](LibPkg::StorageID, const std::shared_ptr<LibPkg::Package> &package) {
                m_setup.config.pullDependentPackages(package, relevantDbs, relevantPkgs, missingDeps, visitedPackages);
                return false;
            });
        }
    }
    for (const auto &[dependencyName, dependencyDetail] : missingDeps) {
        auto packageNames = std::vector<std::string_view>();
        packageNames.reserve(dependencyDetail.relevantPackages.size());
        for (const auto &package : dependencyDetail.relevantPackages) {
            packageNames.emplace_back(package->name);
        }
        m_messages.warnings.emplace_back(
            "dependency " % dependencyName % " missing, required by " + joinStrings<decltype(packageNames), std::string>(packageNames, ", "));
    }
    for (auto *const db : relevantDbs) {
        const auto isDestinationDb = m_destinationDbs.empty() || m_destinationDbs.find(db) != m_destinationDbs.end();
        auto &relevantDbInfo = m_relevantPackagesByDatabase.emplace_back(DatabaseToConsider{ .name = db->name, .arch = db->arch });
        db->allPackages([&](LibPkg::StorageID packageID, const std::shared_ptr<LibPkg::Package> &package) {
            // allow aborting the build action
            if (reportAbortedIfAborted()) {
                return true;
            }
            // skip if package should be excluded
            if (!packageExcludeRegexValue.empty() && std::regex_match(package->name, packageExcludeRegex)) {
                m_messages.notes.emplace_back(db->name % '/' % package->name + ": matches exclude regex");
                return false;
            }
            // skip if the package info is missing (we need the binary package's file name here)
            const auto &packageInfo = package->packageInfo;
            if (!packageInfo) {
                m_messages.errors.emplace_back(db->name % '/' % package->name + ": no package info");
                return false;
            }
            // skip the package if it is not part of the destination DB or required by a package of the destination DB
            if (!isDestinationDb && relevantPkgs.find(packageID) == relevantPkgs.end()) {
                if (m_skippingNote.tellp()) {
                    m_skippingNote << ", ";
                }
                m_skippingNote << db->name << '/' << package->name;
                return false;
            }
            // find the package on disk; otherwise add an URL to download it from the configured mirror
            std::string path, url, cachePath;
            std::error_code ec;
            const auto &fileName = packageInfo->fileName;
            const auto &arch = packageInfo->arch;
            if (!db->localPkgDir.empty()) {
                path = db->localPkgDir % '/' + fileName;
            } else if (std::filesystem::file_size(cachePath = cacheDir + fileName, ec) && !ec) {
                path = std::move(cachePath);
            } else if (std::filesystem::file_size(cachePath = cacheDir % arch % '/' + fileName, ec) && !ec) {
                path = std::move(cachePath);
            } else {
                for (const auto &possibleCachePath : m_setup.config.packageCacheDirs) {
                    if (std::filesystem::file_size(path = possibleCachePath % '/' + fileName, ec) && !ec) {
                        break;
                    }
                    path.clear();
                }
            }
            if (path.empty() && !db->mirrors.empty()) {
                const auto &mirror = db->mirrors.front(); // just use the first mirror for now
                if (startsWith(mirror, "file:")) {
                    std::error_code ecCanonical;
                    const auto canonPath = std::filesystem::canonical(
                        argsToString(std::string_view(mirror.data() + 5, mirror.size() - 5), '/', fileName), ecCanonical);
                    if (!ecCanonical) {
                        path = canonPath.string();
                    }
                } else {
                    path = std::move(cachePath);
                    url = mirror % (endsWith(mirror, "/") ? std::string() : "/") + fileName;
                }
            }
            if (path.empty()) {
                m_messages.errors.emplace_back(db->name % '/' % package->name + ": binary package not found and no mirror configured");
                return false;
            }
            // skip if the package info has already been loaded from package contents and the present binary package is not newer
            auto lastModified = DateTime();
            if (url.empty()) {
                lastModified = LibPkg::lastModified(path);
                if (!force && package->origin == LibPkg::PackageOrigin::PackageContents && package->timestamp >= lastModified) {
                    m_messages.notes.emplace_back(db->name % '/' % package->name % ": skipping because \"" % path % "\" is newer ("
                            % package->timestamp.toString() % " >= " % lastModified.toString()
                        + ")\n");
                    return false;
                }
            }
            // add the full path to the binary package to relevant packages
            auto &relevantPkg = relevantDbInfo.packages.emplace_back(
                PackageToConsider{ .path = std::move(path), .url = std::move(url), .lastModified = lastModified });
            // create a temporary package object to hold the info parsed from the .PKGINFO file
            relevantPkg.info.name = package->name;
            // -> assign certain fields which are used by addDepsAndProvidesFromOtherPackage() to check whether the packages are matching
            relevantPkg.info.version = package->version;
            relevantPkg.info.packageInfo = std::make_unique<LibPkg::PackageInfo>();
            relevantPkg.info.packageInfo->buildDate = package->packageInfo->buildDate;
            // -> gather source info such as make and check dependencies as well
            relevantPkg.info.sourceInfo = std::make_shared<LibPkg::SourceInfo>();
            ++m_remainingPackages;
            return false;
        });
        if (reportAbortedIfAborted()) {
            return;
        }
    }
    configReadLock = std::monostate{};

    m_buildAction->appendOutput(Phrases::SubMessage, "Found ", m_remainingPackages.load(), "\n");

    // add note about skipped packages
    if (m_skippingNote.tellp()) {
        m_skippingNote << ": not required by any destination DB, skipping download";
        m_messages.notes.emplace_back(m_skippingNote.str());
        m_skippingNote = std::stringstream();
    }

    // stop here if no relevant packages were found
    if (m_relevantPackagesByDatabase.empty() || !m_remainingPackages) {
        conclude();
        return;
    }

    downloadPackagesFromMirror();
}

void LibRepoMgr::ReloadLibraryDependencies::downloadPackagesFromMirror()
{
    // prepare caching data
    std::size_t packagesWhichNeedCaching = 0;
    for (const auto &db : m_relevantPackagesByDatabase) {
        for (const auto &pkg : db.packages) {
            if (!pkg.url.empty()) {
                auto &cachingData = m_cachingData[db.name][pkg.info.name];
                cachingData.url = pkg.url;
                cachingData.destinationFilePath = pkg.path;
                ++packagesWhichNeedCaching;
            }
        }
    }

    // skip caching if not required
    if (!packagesWhichNeedCaching) {
        loadPackageInfoFromContents();
        return;
    }

    // allow aborting the build action
    if (reportAbortedIfAborted()) {
        return;
    }

    m_buildAction->appendOutput(Phrases::SuccessMessage, "Downloading ", packagesWhichNeedCaching, " binary packages from mirror ...\n");
    WebClient::cachePackages(m_buildAction->log(),
        std::make_shared<WebClient::PackageCachingSession>(m_cachingData, m_setup.building.ioContext, m_setup.webServer.sslContext,
            std::bind(&ReloadLibraryDependencies::loadPackageInfoFromContents, this)),
        m_packageDownloadSizeLimit ? std::make_optional(m_packageDownloadSizeLimit) : std::nullopt);
}

void ReloadLibraryDependencies::loadPackageInfoFromContents()
{
    // allow aborting the build action
    if (reportAbortedIfAborted()) {
        return;
    }

    // load info from package contents utilizing hardware concurrency
    std::mutex nextPackageMutex, submitErrorMutex, submitWarningMutex;
    auto dbIterator = m_relevantPackagesByDatabase.begin(), dbEnd = m_relevantPackagesByDatabase.end();
    auto pkgIterator = dbIterator->packages.begin(), pkgEnd = dbIterator->packages.end();
    m_buildAction->appendOutput(Phrases::SuccessMessage, "Parsing ", m_remainingPackages.load(), " binary packages ...\n");
    const auto processPackage = [this, &dbIterator, &dbEnd, &pkgIterator, &pkgEnd, &nextPackageMutex, &submitErrorMutex, &submitWarningMutex] {
        for (; !m_buildAction->isAborted();) {
            // get the next package
            std::unique_lock<std::mutex> nextPackagelock(nextPackageMutex);
            while (pkgIterator == pkgEnd) {
                if (dbIterator == dbEnd || ++dbIterator == dbEnd) {
                    return;
                }
                pkgIterator = dbIterator->packages.begin();
                pkgEnd = dbIterator->packages.end();
            }
            const auto &currentDb = *dbIterator;
            auto &currentPkg = *pkgIterator;
            // increment the current package
            ++pkgIterator;
            // allow other threads to get a package as well
            nextPackagelock.unlock();

            // log progress
            m_buildAction->appendOutput(
                Phrases::InfoMessage, m_remainingPackages--, " packages remaining to parse, next package: ", currentPkg.path, '\n');

            // check whether the package could be cached from the mirror and skip it with an error if not
            if (!currentPkg.url.empty()) {
                if (auto db = m_cachingData.find(currentDb.name); db != m_cachingData.end()) {
                    if (auto pkg = db->second.find(currentPkg.info.name); pkg != db->second.end()) {
                        auto &packageCachingInfo = pkg->second;
                        if (!packageCachingInfo.error.empty()) {
                            std::unique_lock<std::mutex> submitErrorLock(submitErrorMutex);
                            m_messages.errors.emplace_back(currentDb.name % '/' % currentPkg.info.name % ':' % ' ' + packageCachingInfo.error);
                            continue;
                        }
                    }
                }
            }

            // extract the binary package's files
            try {
                std::set<std::string> dllsReferencedByImportLibs;
                LibPkg::walkThroughArchive(
                    currentPkg.path, &LibPkg::Package::isPkgInfoFileOrBinary,
                    [&currentPkg, &dllsReferencedByImportLibs](std::string &&directoryPath, LibPkg::ArchiveFile &&file) {
                        if (directoryPath.empty() && file.name == ".PKGINFO") {
                            currentPkg.info.addInfoFromPkgInfoFile(file.content);
                            return;
                        }
                        currentPkg.info.addDepsAndProvidesFromContainedFile(file, dllsReferencedByImportLibs);
                    },
                    [&currentPkg](std::string &&directoryPath) {
                        if (directoryPath.empty()) {
                            return;
                        }
                        currentPkg.info.addDepsAndProvidesFromContainedDirectory(directoryPath);
                    });
                if (auto dllIssues = currentPkg.info.processDllsReferencedByImportLibs(std::move(dllsReferencedByImportLibs)); !dllIssues.empty()) {
                    std::unique_lock<std::mutex> submitWarningLock(submitWarningMutex);
                    for (auto &issue : dllIssues) {
                        m_messages.warnings.emplace_back(std::move(issue));
                    }
                }
                currentPkg.info.origin = LibPkg::PackageOrigin::PackageContents;
            } catch (const std::runtime_error &e) {
                std::unique_lock<std::mutex> submitErrorLock(submitErrorMutex);
                m_messages.errors.emplace_back(currentDb.name % '/' % currentPkg.info.name % ':' % ' ' + e.what());
            }
        }
    };
    auto threads = std::vector<std::thread>(std::thread::hardware_concurrency() - 1);
    for (std::thread &t : threads) {
        t = std::thread(processPackage);
    }
    processPackage();
    for (std::thread &t : threads) {
        t.join();
    }

    // store the information in the database
    m_buildAction->appendOutput(Phrases::SuccessMessage, "Adding parsed information to databases ...\n");
    std::size_t counter = 0;
    for (DatabaseToConsider &relevantDb : m_relevantPackagesByDatabase) {
        auto configWritelock = m_setup.config.lockToWrite(); // acquire lock within loop to allow intermediate reads
        auto *const db = m_setup.config.findDatabase(relevantDb.name, relevantDb.arch);
        if (!db) {
            continue; // the whole database has been removed while we were loading package contents
        }
        auto updater = LibPkg::PackageUpdater(*db);
        for (PackageToConsider &package : relevantDb.packages) {
            // skip if package info could not be parsed from package contents
            if (package.info.origin != LibPkg::PackageOrigin::PackageContents) {
                continue;
            }
            // find the package in the database again
            const auto [packageID, existingPackage] = updater.findPackageWithID(package.info.name);
            if (!existingPackage) {
                continue; // the package has been removed while we were loading package contents
            }
            // add the dependencies/provides to the existing package
            if (!existingPackage->addDepsAndProvidesFromOtherPackage(package.info)) {
                continue; // the package does no longer match what's in the database
            }
            // update timestamp so we can skip this package on the next run
            if (existingPackage->timestamp < package.lastModified) {
                existingPackage->timestamp = package.lastModified;
            }
            // add the new dependencies on database-level
            updater.update(existingPackage);
            ++counter;
        }
        updater.commit();
    }

    m_buildAction->appendOutput(Phrases::SuccessMessage, "Added dependency information for ", counter, " packages\n");
    conclude();
}

void ReloadLibraryDependencies::conclude()
{
    if (reportAbortedIfAborted()) {
        return;
    }
    const auto result = m_messages.errors.empty() ? BuildActionResult::Success : BuildActionResult::Failure;
    const auto buildActionWriteLock = m_setup.building.lockToWrite();
    m_buildAction->resultData = std::move(m_messages);
    reportResult(result);
}

} // namespace LibRepoMgr
