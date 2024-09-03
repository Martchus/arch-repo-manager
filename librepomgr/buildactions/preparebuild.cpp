#include "./buildactionprivate.h"
#include "./subprocess.h"

#include "../helper.h"
#include "../json.h"
#include "../serversetup.h"

#include "../webclient/aur.h"

#include "../webapi/params.h"

#include "../../libpkg/parser/database.h"
#include "../../libpkg/parser/utils.h"

#include <c++utilities/conversion/stringbuilder.h>
#include <c++utilities/conversion/stringconversion.h>
#include <c++utilities/io/ansiescapecodes.h>
#include <c++utilities/io/misc.h>

#include <boost/asio/read.hpp>

#include <boost/process/v1/search_path.hpp>
#include <boost/process/v1/start_dir.hpp>

#include <rapidjson/filewritestream.h>
#include <rapidjson/prettywriter.h>

#include <filesystem>
#include <iostream>

using namespace std;
using namespace CppUtilities;
using namespace CppUtilities::EscapeCodes;

namespace LibRepoMgr {

bool PackageBuildProgress::hasBeenAnyProgressMade() const
{
    return checksumsUpdated || hasSources || !finished.isNull() || addedToRepo || stagingNeeded != PackageStagingNeeded::Undetermined;
}

void PackageBuildProgress::resetProgress()
{
    checksumsUpdated = false;
    hasSources = false;
    finished = DateTime();
    addedToRepo = false;
    stagingNeeded = PackageStagingNeeded::Undetermined;
}

void PackageBuildProgress::resetChrootSettings()
{
    chrootDirectory.clear();
    chrootUser.clear();
    skipChrootUpgrade = skipChrootCleanup = keepPreviousSourceTree = false;
}

PrepareBuild::PrepareBuild(ServiceSetup &setup, const std::shared_ptr<BuildAction> &buildAction)
    : InternalBuildAction(setup, buildAction)
{
}

void PrepareBuild::run()
{
    // check whether a directory has been specified
    if (m_buildAction->directory.empty()) {
        reportError("Unable to create working directory: no directory name specified");
        return;
    }

    // check flags and settings
    const auto flags = static_cast<PrepareBuildFlags>(m_buildAction->flags);
    m_forceBumpPackageVersion = flags & PrepareBuildFlags::ForceBumpPkgRel;
    m_cleanSourceDirectory = flags & PrepareBuildFlags::CleanSrcDir;
    m_keepOrder = flags & PrepareBuildFlags::KeepOrder;
    m_keepPkgRelAndEpoch = flags & PrepareBuildFlags::KeepPkgRelAndEpoch;
    m_resetChrootSettings = flags & PrepareBuildFlags::ResetChrootSettings;
    m_pullingInFurtherDependenciesUnexpected = flags & PrepareBuildFlags::PullingInFurtherDependenciesUnexpected;
    m_fetchOfficialSources = flags & PrepareBuildFlags::FetchOfficialPackageSources;
    m_useContainer = flags & PrepareBuildFlags::UseContainer;
    m_aurOnly = flags & PrepareBuildFlags::AurOnly;
    if (m_forceBumpPackageVersion && m_keepPkgRelAndEpoch) {
        reportError("Can not force-bump pkgrel and keeping it at the same time.");
        return;
    }
    auto &metaInfo = m_setup.building.metaInfo;
    auto metaInfoLock = metaInfo.lockToRead();
    const auto &typeInfo = metaInfo.typeInfoForId(BuildActionType::PrepareBuild);
    const auto pkgbuildsDirsSetting = std::string(typeInfo.settings[static_cast<std::size_t>(PrepareBuildSettings::PKGBUILDsDirs)].param);
    metaInfoLock.unlock();
    if (const auto i = m_buildAction->settings.find(pkgbuildsDirsSetting); i != m_buildAction->settings.end()) {
        m_pkgbuildsDirs = splitString<std::vector<std::string>>(i->second, ":", EmptyPartsTreat::Omit);
    }

    // read values from global config
    auto setupReadLock = m_setup.lockToRead();
    if (m_useContainer) {
        m_makeContainerPkgPath = findExecutable(m_setup.building.makeContainerPkgPath);
    } else {
        m_makePkgPath = findExecutable(m_setup.building.makePkgPath);
    }
    copySecondVectorIntoFirstVector(m_pkgbuildsDirs, m_setup.building.pkgbuildsDirs);
    m_ignoreLocalPkgbuildsRegex = m_setup.building.ignoreLocalPkgbuildsRegex;
    m_workingDirectory = determineWorkingDirectory(buildDataWorkingDirectory);

    // check executables
    if (!checkExecutable(m_makePkgPath)) {
        reportError("Unable to find makepkg executable \"" % m_setup.building.makePkgPath + "\" in PATH.");
        return;
    }
    setupReadLock.unlock();

    // init build action
    auto configReadLock
        = init(BuildActionAccess::ReadConfig, RequiredDatabases::MaybeSource | RequiredDatabases::OneDestination, RequiredParameters::Packages);
    if (std::holds_alternative<std::monostate>(configReadLock)) {
        return;
    }

    // find dependencies of the destination repository
    auto *const destinationDb = (*m_destinationDbs.begin());
    auto databaseDependencyOrderRes = m_setup.config.computeDatabaseDependencyOrder(*destinationDb);
    if (std::holds_alternative<std::string>(databaseDependencyOrderRes)) {
        configReadLock = std::monostate{};
        reportError("Unable to find the destination DB's dependencies: " + get<string>(databaseDependencyOrderRes));
        return;
    }
    auto &databaseDependencyOrder = get<vector<LibPkg::Database *>>(databaseDependencyOrderRes);
    for (const auto *destinationDbOrDependency : databaseDependencyOrder) {
        m_requiredDbs.emplace(destinationDbOrDependency->name);
    }

    // find dependencies of the staging repository
    auto *stagingDb = m_setup.config.findDatabase(destinationDb->name + "-staging", destinationDb->arch);
    auto stagingDbOrder = std::variant<std::vector<LibPkg::Database *>, std::string>();
    auto hasStagingDbOrder = false;
    if (!stagingDb && endsWith(destinationDb->name, "-testing")) {
        stagingDb = m_setup.config.findDatabase(
            argsToString(std::string_view(destinationDb->name.data(), destinationDb->name.size() - 8), "-staging"), destinationDb->arch);
    }
    if (!stagingDb && endsWith(destinationDb->name, "-staging")) {
        // use the destination DB as staging DB if it ends with "-staging"; we can assume it was intended to use it and the auto-staging
        // was just passed as usual
        stagingDb = destinationDb;
        stagingDbOrder = databaseDependencyOrder;
        hasStagingDbOrder = true;
    }
    if (stagingDb) {
        if (!hasStagingDbOrder) {
            stagingDbOrder = m_setup.config.computeDatabaseDependencyOrder(*stagingDb);
        }
        if (std::holds_alternative<std::string>(stagingDbOrder)) {
            m_warnings.emplace_back("Unable to find the staging DB's dependencies: " + get<string>(stagingDbOrder));
        }
    } else {
        m_warnings.emplace_back("Unable to find staging DB for \"" + destinationDb->name + "\". Auto-staging will not work.");
    }

    // set target and staging info
    m_targetDbName = destinationDb->name;
    m_targetArch = destinationDb->arch;
    if (stagingDb) {
        m_stagingDbName = stagingDb->name;
    }

    // make set of databases considered as given from the specified source databases
    // notes: 1. If *no* source databases are specified the destination database and all databases it is based on are considered as "given" and
    //           therefore not included in the build unless their package names are explicitly specified. All direct and transitive dependencies
    //           of the destination repository and the destination repository itself are enabled within the build root.
    //        2. If source databases are specified only packages contained by these databases will be considered as "given". Any other packages
    //           will be included in the build. Dependencies of the destination repository and the destination repository itself are only enabled within
    //           the build root when also listed as source repositories.
    //        3. All specified source repositories should be equal to the destination repository or a direct or transitive dependency of the destination
    //           repository. If that is not the case the built packages might depend on packages which are not available at install time. Hence a warning
    //           is emitted in that case.
    std::vector<std::string> missingBaseDbs;
    for (auto *const sourceDb : m_sourceDbs) {
        const auto &[sourceDbName, added] = m_baseDbs.emplace(sourceDb->name);
        if (!added) {
            continue;
        }
        if (m_requiredDbs.find(*sourceDbName) == m_requiredDbs.end()) {
            missingBaseDbs.emplace_back(*sourceDbName);
            databaseDependencyOrder.emplace_back(sourceDb); // appending the "odd" source database (see note 3. above) should be sufficient
        }
    }
    if (!missingBaseDbs.empty()) {
        m_warnings.emplace_back(
            "The following source DBs are not the destination DB or a (transitive) dependency of it: " + joinStrings(missingBaseDbs, ", "));
    }

    // compose the list of repositories to enable within the build root (for staging)
    populateDbConfig(databaseDependencyOrder, false);
    if (std::holds_alternative<std::vector<LibPkg::Database *>>(stagingDbOrder)) {
        populateDbConfig(std::get<std::vector<LibPkg::Database *>>(stagingDbOrder), true);
    }

    // try to find denoted packages to determine the actual package base and the existing version
    // -> We might get something like "my-repo/foo" and "my-repo/bar" where "foo" and "bar" are only split
    //    packages from the base package "foobar" when blindly trying to "rebuild" packages from a binary
    //    repository. Luckily database files store the corresponding base packages.
    // -> That trick doesn't work for new packages of course.
    //    FIXME: Maybe the AUR RPC provides useful information (which would at least help with AUR packages).
    // -> The existing version is useful to know when we need to bump pkgrel or even epoch.
    size_t currentIndex = 0;
    for (const auto &packageName : m_buildAction->packageNames) {
        // pick some package matching the specified packageName which has a source info; preferring packages
        // from the destination db
        auto existingPackages = m_setup.config.findPackages(packageName);
        auto foundSourcePackage = false;
        auto packageBuildData = PackageBuildData();
        for (auto &existingPackage : existingPackages) {
            // skip if package not relevant
            if (!isExistingPackageRelevant(packageName, existingPackage, packageBuildData, *destinationDb)) {
                continue;
            }
            // skip if package has no source info
            const auto &sourceInfo = existingPackage.pkg->sourceInfo;
            if (!sourceInfo || sourceInfo->name.empty()) {
                continue;
            }
            //
            auto &buildData = m_buildDataByPackage[sourceInfo->name];
            buildData.specifiedIndex = currentIndex++;
            buildData.existingPackages.emplace_back(existingPackage.pkg);
            if (buildData.existingVersion.empty()
                || LibPkg::PackageVersion::isNewer(LibPkg::PackageVersion::compare(existingPackage.pkg->version, buildData.existingVersion))) {
                buildData.existingVersion = existingPackage.pkg->version;
            }
            // assume we don't have sources if going to clean up the source directory
            // FIXME: It is a little bit inconsistent that in other places existing PackageBuildData is overridden and here it is re-used.
            if (m_cleanSourceDirectory) {
                buildData.hasSource = false;
            }
            foundSourcePackage = true;
            if (std::get<LibPkg::Database *>(existingPackage.db) == destinationDb) {
                break;
            }
        }
        // add an empty BuildPackage nevertheless assuming packageName specifies the base package as-is
        if (!foundSourcePackage) {
            packageBuildData.specifiedIndex = currentIndex++;
            m_buildDataByPackage[packageName] = std::move(packageBuildData);
        }
    }

    configReadLock = std::monostate{};

    // make working directory
    try {
        filesystem::create_directories(m_workingDirectory);
    } catch (const filesystem::filesystem_error &e) {
        reportError("Unable to create working directory " % m_workingDirectory % ": " + e.what());
        return;
    }

    if (reportAbortedIfAborted()) {
        return;
    }

    // locate PKGBUILDs locally or download them from AUR
    fetchMissingBuildData();
}

/*!
 * \brief Populates m_dbConfig (or m_dbStagingConfig if \a forStaging) with the specified \a dbOrder.
 */
void PrepareBuild::populateDbConfig(const std::vector<LibPkg::Database *> &dbOrder, bool forStaging)
{
    auto &dbConfig = forStaging ? m_stagingDbConfig : m_dbConfig;
    for (auto i = dbOrder.rbegin(), end = dbOrder.rend(); i != end; ++i) {
        const auto *const db = *i;
        auto scopeData = IniFile::ScopeData{};
        scopeData.emplace(make_pair("SigLevel", LibPkg::signatureLevelToString(db->signatureLevel)));
        for (const auto &mirror : db->mirrors) {
            scopeData.emplace(make_pair("Server", mirror));
        }
        if (m_sourceDbs.empty() || m_baseDbs.find(db->name) != m_baseDbs.cend() || (forStaging && endsWith(db->name, "-staging"))) {
            dbConfig.emplace_back(IniFile::Scope(db->name, std::move(scopeData)));
        }
    }
}

/*!
 * \brief Determines whether the existing package \a package is relevant for providing the dependency with \a dependencyName considering the
 * specified \a destinationDb.
 * \remarks The \a package is added to the existing packages in \a packageBuildData in case it is useful for further processing even though
 * not for providing the dependency (e.g. bumping pkgrel).
 */
bool PrepareBuild::isExistingPackageRelevant(
    const string &dependencyName, LibPkg::PackageSearchResult &package, PackageBuildData &packageBuildData, const LibPkg::Database &destinationDb)
{
    // skip db if arch isn't matching the destination db arch
    const auto *const db = std::get<LibPkg::Database *>(package.db);
    if (db->arch != destinationDb.arch) {
        return false;
    }
    // add an exact package match to the build data if it is present in the destination db or any dependency because it is
    // still relevant for bumping versions, even if we ignore it because it is not present in the base DBs
    const auto dependencyPresentInRequiredDbs = m_requiredDbs.find(db->name) != m_requiredDbs.end();
    if (dependencyPresentInRequiredDbs && dependencyName == package.pkg->name) {
        if (packageBuildData.existingVersion.empty()
            || LibPkg::PackageVersion::isNewer(LibPkg::PackageVersion::compare(package.pkg->version, packageBuildData.existingVersion))) {
            packageBuildData.existingVersion = package.pkg->version;
        }
        packageBuildData.existingPackages.emplace_back(package.pkg);
    }
    // skip if package is not from any database available within that build
    return (m_baseDbs.empty() && dependencyPresentInRequiredDbs) || (m_baseDbs.find(db->name) != m_baseDbs.end());
}

void PrepareBuild::addPackageToLogLine(std::string &logLine, const std::string &packageName)
{
    logLine += ' ';
    logLine += packageName;
}

void PrepareBuild::makeSrcInfo(
    std::shared_ptr<WebClient::AurSnapshotQuerySession> &multiSession, const std::string &sourceDirectory, const std::string &packageName)
{
    auto processSession = make_shared<ProcessSession>(
        m_setup.building.ioContext, [multiSession, &sourceDirectory, &packageName](boost::process::child &&child, ProcessResult &&result) {
            processSrcInfo(*multiSession, sourceDirectory, packageName, std::move(child), std::move(result));
        });
    processSession->launch(
        boost::process::start_dir(sourceDirectory), (m_useContainer ? m_makeContainerPkgPath : m_makePkgPath).string(), "--printsrcinfo");
}

void PrepareBuild::processSrcInfo(WebClient::AurSnapshotQuerySession &multiSession, const std::string &sourceDirectory,
    const std::string &packageName, boost::process::child &&child, ProcessResult &&result)
{
    if (result.errorCode) {
        multiSession.addResponse(WebClient::AurSnapshotResult{ .packageName = packageName,
            .errorOutput = std::move(result.error),
            .error = argsToString("Unable to invoke makepkg --printsourceinfo: ", result.errorCode.message()) });
        return;
    }
    if (child.exit_code() != 0) {
        multiSession.addResponse(WebClient::AurSnapshotResult{ .packageName = packageName,
            .errorOutput = std::move(result.error),
            .error = argsToString("makepkg --printsourceinfo exited with non-zero exit code: ", child.exit_code()) });
        return;
    }

    const auto srcInfoPath = sourceDirectory + "/.SRCINFO";
    try {
        writeFile(srcInfoPath, result.output);
    } catch (const std::ios_base::failure &failure) {
        multiSession.addResponse(
            WebClient::AurSnapshotResult{ .packageName = packageName, .error = "Unable to write " % srcInfoPath % ": " + failure.what() });
        return;
    }

    addResultFromSrcInfo(multiSession, packageName, result.output);
}

void PrepareBuild::addResultFromSrcInfo(WebClient::AurSnapshotQuerySession &multiSession, const std::string &packageName, const std::string &srcInfo)
{
    auto snapshotResult = WebClient::AurSnapshotResult{ .packageName = packageName, .packages = LibPkg::Package::fromInfo(srcInfo, false) };
    if (snapshotResult.packages.empty() || snapshotResult.packages.front().pkg->name.empty()) {
        snapshotResult.error = "Unable to parse .SRCINFO: no package name present";
    } else if (!snapshotResult.packages.front().pkg->sourceInfo.has_value()) {
        snapshotResult.error = "Unable to parse .SRCINFO: no source info present";
    }
    multiSession.addResponse(std::move(snapshotResult));
}

void PrepareBuild::fetchMissingBuildData()
{
    auto multiSession
        = WebClient::AurSnapshotQuerySession::create(m_setup.building.ioContext, bind(&PrepareBuild::computeDependencies, this, placeholders::_1));
    vector<WebClient::AurSnapshotQueryParams> snapshotQueries;

    // prepare logging
    auto logLines = vector<string>{
        "  -> Generating .SRCINFO for local PKGBUILDs:", "  -> Retrieving sources from AUR:", "  -> Using existing source directories:"
    };
    auto needToGeneratedSrcInfo = false;
    auto usingExistingSourceDirectories = false;

    // fetch build data (PKGBUILD, .SRCINFO, ...) for all the packages we want to build
    for (auto &[packageName, buildData] : m_buildDataByPackage) {

        // skip packages where the source has already been fetched in a previous invocation or which have already failed on a previous invocation
        if (buildData.hasSource || !buildData.error.empty()) {
            continue;
        }

        buildData.sourceDirectory = m_workingDirectory % '/' % packageName + "/src";

        // skip fetching if source directory already exists (from previous run or manual provisioning)
        // clean existing source directory if cleanup is enabled
        try {
            const auto sourceDirectoryExists = filesystem::exists(buildData.sourceDirectory);
            if (m_cleanSourceDirectory && sourceDirectoryExists && m_cleanedSourceDirs.emplace(buildData.sourceDirectory).second) {
                filesystem::remove_all(buildData.sourceDirectory);
            } else if (sourceDirectoryExists) {
                // verify the PKGBUILD file exists
                const auto pkgbuildPath = buildData.sourceDirectory + "/PKGBUILD";
                if (!filesystem::exists(pkgbuildPath)) {
                    multiSession->addResponse(WebClient::AurSnapshotResult{ .packageName = packageName,
                        .error = "Existing source directory \"" % buildData.sourceDirectory + "\" does not contain a PKGBUILD file." });
                    continue;
                }
                // generate the .SRCINFO file if not already present; otherwise read the existing .SRCINFO
                const auto srcInfoPath = buildData.sourceDirectory + "/.SRCINFO";
                if (!filesystem::exists(srcInfoPath) || filesystem::last_write_time(srcInfoPath) < filesystem::last_write_time(pkgbuildPath)) {
                    makeSrcInfo(multiSession, buildData.sourceDirectory, packageName);
                    needToGeneratedSrcInfo = true;
                } else {
                    auto srcInfo = std::string();
                    try {
                        srcInfo = readFile(srcInfoPath, 0x10000);
                    } catch (const std::ios_base::failure &e) {
                        multiSession->addResponse(WebClient::AurSnapshotResult{ .packageName = packageName,
                            .error = "Unable to read .SRCINFO \"" % srcInfoPath % "\" from existing source directory: " + e.what() });
                        continue;
                    }
                    addResultFromSrcInfo(*multiSession, packageName, srcInfo);
                }
                usingExistingSourceDirectories = true;
                addPackageToLogLine(logLines[0], packageName);
                addPackageToLogLine(logLines[2], packageName);
                buildData.hasSource = true;
                continue;
            }
        } catch (const filesystem::filesystem_error &e) {
            multiSession->addResponse(WebClient::AurSnapshotResult{ .packageName = packageName,
                .error = "Unable to check existing source directory \"" % buildData.sourceDirectory % "\": " + e.what() });
            continue;
        }

        // clear the original source directory which is possibly still assigned from a previous run
        buildData.originalSourceDirectory.clear();

        // skip next block if package name matches configured pattern
        LibPkg::PackageNameData packageNameData;
        if (regex_match(packageName, m_ignoreLocalPkgbuildsRegex)) {
            goto addAurQuery;
        }

        // copy PKGBUILD from local PKGBUILDs directory and generate .SRCINFO from it via makepkg
        if (!m_pkgbuildsDirs.empty()) {
            packageNameData = LibPkg::PackageNameData::decompose(packageName);
        }
        for (const auto &pkgbuildsDir : m_pkgbuildsDirs) {
            const auto variant = packageNameData.variant();
            try {
                if (const auto directPkgbuildPath = pkgbuildsDir % '/' % packageName;
                    std::filesystem::exists(pkgbuildsDir % '/' % packageName + "/PKGBUILD")) {
                    buildData.originalSourceDirectory = tupleToString(directPkgbuildPath);
                } else if (const auto variantPkgbuildPath = pkgbuildsDir % '/' % packageNameData.actualName % '/' % variant;
                           filesystem::exists(variantPkgbuildPath + "/PKGBUILD")) {
                    buildData.originalSourceDirectory = tupleToString(variantPkgbuildPath);
                } else if (const auto svnPkgbuildPath = pkgbuildsDir % '/' % packageName % "/trunk";
                           filesystem::exists(svnPkgbuildPath + "/PKGBUILD")) {
                    buildData.originalSourceDirectory = tupleToString(svnPkgbuildPath);
                } else {
                    continue;
                }
                filesystem::create_directories(buildData.sourceDirectory);
                filesystem::copy(buildData.originalSourceDirectory, buildData.sourceDirectory, std::filesystem::copy_options::recursive);

            } catch (const filesystem::filesystem_error &e) {
                multiSession->addResponse(WebClient::AurSnapshotResult{ .packageName = packageName,
                    .error
                    = "Unable to copy files from PKGBUILDs directory " % pkgbuildsDir % " to " % buildData.sourceDirectory % ": " + e.what() });
                continue;
            }
            makeSrcInfo(multiSession, buildData.sourceDirectory, packageName);
            needToGeneratedSrcInfo = true;
            addPackageToLogLine(logLines[0], packageName);
            buildData.hasSource = true;
            break;
        }

    addAurQuery:
        // download latest snapshot containing PKGBUILD and .SRCINFO from AUR
        if (!buildData.hasSource) {
            snapshotQueries.emplace_back(WebClient::AurSnapshotQueryParams{
                .packageName = &packageName,
                .targetDirectory = &buildData.sourceDirectory,
                .tryOfficial = m_fetchOfficialSources,
            });
            addPackageToLogLine(logLines[1], packageName);
        }
    }

    // schedule async AUR downloads
    if (!snapshotQueries.empty()) {
        WebClient::queryAurSnapshots(m_buildAction->log(), m_setup, snapshotQueries, m_setup.building.ioContext, multiSession);
    }

    // log status
    if (!needToGeneratedSrcInfo) {
        logLines[0].clear();
    }
    if (snapshotQueries.empty()) {
        logLines[1].clear();
    }
    if (!usingExistingSourceDirectories) {
        logLines[2].clear();
    }
    m_buildAction->appendOutput(Phrases::InfoMessage, "Fetching missing build data\n", joinStrings(logLines, string(), true, string(), "\n"));
}

bool PrepareBuild::pullFurtherDependencies(const std::vector<LibPkg::Dependency> &dependencies)
{
    // pull further dependencies; similar to initial dependency lookup in run()
    auto dependencyAdded = false;
    const auto *const destinationDb = *m_destinationDbs.begin();
    for (const auto &dependency : dependencies) {
        // skip empty dependencies which might be present if split package contains `depends=()`
        if (dependency.name.empty()) {
            continue;
        }

        // skip if the dependency is already in the list of packages to be built (check for cycles is done later when computing batches)
        auto dependencyExists = false;
        if (m_buildDataByPackage.find(dependency.name) != m_buildDataByPackage.end()) {
            continue;
        }
        for (const auto &[packageName, buildData] : m_buildDataByPackage) {
            for (const auto &[packageID, package] : buildData.packages) {
                if (package->providesDependency(dependency)) {
                    dependencyExists = true;
                    break;
                }
            }
            if (dependencyExists) {
                break;
            }
        }
        if (dependencyExists) {
            continue;
        }

        // skip dependency if it is already present in one of the dependencies considered as "given" (see note in run() function)
        auto packageBuildData = PackageBuildData();
        auto existingPackages = m_setup.config.findPackages(dependency);
        for (auto &package : existingPackages) {
            // skip if package not relevant
            if (!isExistingPackageRelevant(dependency.name, package, packageBuildData, *destinationDb)) {
                continue;
            }
            dependencyExists = true;
        }
        if (dependencyExists) {
            continue;
        }

        // assume the dependency is provided by a package with the pkgbase of the dependency denotation
        // FIXME: support split packages
        m_buildDataByPackage[dependency.name] = std::move(packageBuildData);
        dependencyAdded = true;
    }
    return dependencyAdded;
}

struct BatchItem {
    struct MissingDependency {
        LibPkg::Dependency dependency;
        LibPkg::Dependency requiredBy;
    };

    const std::string *const name;
    const PackageBuildData *const buildData;
    std::unordered_map<const BatchItem *, std::unordered_set<const BatchItem *>> neededItems;
    LibPkg::DependencySet requiredDependencies;
    LibPkg::DependencySet providedDependencies;
    std::vector<MissingDependency> missingDependencies;
    bool done = false;

    void determineNeededItems(LibPkg::Config &config, const std::unordered_set<string> &requiredDbs, const LibPkg::Database *destinationDb,
        unordered_map<string, BatchItem> &batchItems);
    template <typename... DependencyParams>
    bool addNeededItemForRequiredDependencyFromOtherItems(
        const unordered_map<string, BatchItem> &otherItems, DependencyParams &&...requiredDependencyParams);
    static bool addItemsToBatch(Batch &currentBatch, BatchMap &items);

private:
    void addNeededBatchItemsForPackage(LibPkg::Config &config, const std::unordered_set<string> &requiredDbs, const LibPkg::Database *destinationDb,
        unordered_map<string, BatchItem> &batchItems, unordered_set<const LibPkg::Package *> &visitedPackages, LibPkg::Package &package);
};

template <typename... DependencyParams>
bool BatchItem::addNeededItemForRequiredDependencyFromOtherItems(
    const unordered_map<string, BatchItem> &otherItems, DependencyParams &&...requiredDependencyParams)
{
    // skip if the dependency of a package is provided by the package itself (can happen when dealing with split packages)
    if (providedDependencies.provides(std::forward<DependencyParams>(requiredDependencyParams)...)) {
        return true;
    }

    // go through all the items to find one which provides the dependency; continue to look for alternatives as well
    const BatchItem *relevantItem = nullptr;
    std::unordered_set<const BatchItem *> alternativeItems;
    for (const auto &item : otherItems) {
        if (&item.second == this) {
            continue;
        }
        if (item.second.providedDependencies.provides(std::forward<DependencyParams>(requiredDependencyParams)...)) {
            if (!relevantItem) {
                relevantItem = &item.second;
            } else {
                alternativeItems.emplace(&item.second);
            }
        }
    }

    if (relevantItem) {
        // FIXME: actually, we need to handle the case when the item is already present as the alternative items might differ
        neededItems.emplace(relevantItem, std::move(alternativeItems));
        return true;
    }
    return false;
}

void BatchItem::determineNeededItems(LibPkg::Config &config, const std::unordered_set<std::string> &requiredDbs,
    const LibPkg::Database *destinationDb, unordered_map<string, BatchItem> &batchItems)
{
    unordered_set<const LibPkg::Package *> visitedPackages;

    for (const auto &require : requiredDependencies) {
        // check whether its provided directly by some other item
        if (addNeededItemForRequiredDependencyFromOtherItems(batchItems, require.first, require.second)) {
            continue;
        }

        // check by which other package the item is provided
        // skip dependency if it is already present in one of the dbs
        auto dependency = LibPkg::Dependency(require.first, require.second.version, require.second.mode);
        auto foundPackage = false;
        const auto existingPackages = config.findPackages(dependency);
        for (const auto &package : existingPackages) {
            // skip if package is not from any database available within that build
            const auto *const db = std::get<LibPkg::Database *>(package.db);
            if (requiredDbs.find(db->name) == requiredDbs.end()) {
                continue;
            }
            if (db->arch != destinationDb->arch) {
                continue;
            }
            addNeededBatchItemsForPackage(config, requiredDbs, destinationDb, batchItems, visitedPackages, *package.pkg);
            foundPackage = true;
        }
        if (!foundPackage) {
            missingDependencies.emplace_back(MissingDependency{ .dependency = std::move(dependency), .requiredBy = LibPkg::Dependency(*name) });
        }
    }
}

bool BatchItem::addItemsToBatch(Batch &currentBatch, BatchMap &items)
{
    auto allDone = true;

    for (auto &[packageName, batchItem] : items) {
        // skip items which are already done
        if (batchItem.done) {
            continue;
        }

        allDone = false;

        // skip if not all dependencies of the items are done
        auto allDepsDone = true;
        for (const auto &[neededItem, alternatives] : batchItem.neededItems) {
            if (neededItem->done) {
                continue;
            }
            auto oneAlternativeDone = false;
            for (const auto &alternative : alternatives) {
                if (alternative->done) {
                    oneAlternativeDone = true;
                    break;
                }
            }
            if (oneAlternativeDone) {
                continue;
            }
            allDepsDone = false;
            break;
        }
        if (!allDepsDone) {
            continue;
        }

        // add item to current batch
        currentBatch.emplace_back(&batchItem);
    }

    return allDone;
}

void BatchItem::addNeededBatchItemsForPackage(LibPkg::Config &config, const std::unordered_set<string> &requiredDbs,
    const LibPkg::Database *destinationDb, unordered_map<string, BatchItem> &batchItems, unordered_set<const LibPkg::Package *> &visitedPackages,
    LibPkg::Package &package)
{
    // stop if there's a cycle (not a critical error here; we've just seen enough; only cycles between packages we actually want to build are interesting)
    const auto [i, notVisitedBefore] = visitedPackages.emplace(&package);
    if (!notVisitedBefore) {
        return;
    }

    // check whether the dependencies of that package lead to another build item
    for (const auto &dep : package.dependencies) {
        // check whether the dependency is directly one of the build items
        if (addNeededItemForRequiredDependencyFromOtherItems(batchItems, dep)) {
            continue;
        }

        // check other packages recursively
        auto foundPackage = false;
        const auto existingPackages = config.findPackages(dep);
        for (const auto &existingPackage : existingPackages) {
            // skip if package is not from any database available within that build
            const auto *const db = std::get<LibPkg::Database *>(existingPackage.db);
            if (requiredDbs.find(db->name) == requiredDbs.end()) {
                continue;
            }
            if (db->arch != destinationDb->arch) {
                continue;
            }
            addNeededBatchItemsForPackage(config, requiredDbs, destinationDb, batchItems, visitedPackages, *existingPackage.pkg);
            foundPackage = true;
        }
        if (!foundPackage) {
            missingDependencies.emplace_back(MissingDependency{ .dependency = dep, .requiredBy = LibPkg::Dependency(package.name) });
        }
    }
}

static bool sortBatch(const BatchItem *lhs, const BatchItem *rhs)
{
    if (lhs->buildData->specifiedIndex != numeric_limits<size_t>::max() && rhs->buildData->specifiedIndex != numeric_limits<size_t>::max()) {
        return lhs->buildData->specifiedIndex < rhs->buildData->specifiedIndex;
    } else if (lhs->buildData->specifiedIndex != numeric_limits<size_t>::max()) {
        return true;
    } else if (rhs->buildData->specifiedIndex != numeric_limits<size_t>::max()) {
        return false;
    } else {
        return *lhs->name < *rhs->name;
    }
}

void PrepareBuild::bumpVersions()
{
    // bump pkgrel or even epoch to ensure the new packages are actually considered newer by pacman when updating
    if (m_keepPkgRelAndEpoch) {
        return;
    }
    for (const auto &[packageName, buildData] : m_buildDataByPackage) {
        auto &existingVersionStr = buildData.existingVersion;
        if (existingVersionStr.empty() && !m_forceBumpPackageVersion) {
            continue;
        }
        auto existingVersion = existingVersionStr.empty() ? LibPkg::PackageVersion{} : LibPkg::PackageVersion::fromString(existingVersionStr);
        auto amendment = LibPkg::PackageAmendment();
        auto newVersion = LibPkg::PackageVersion();
        for (const auto &[packageID, package] : buildData.packages) {
            newVersion = LibPkg::PackageVersion::fromString(package->version);
            if (existingVersionStr.empty()) {
                existingVersion = newVersion;
                break;
            }
            switch (newVersion.compare(existingVersion)) {
            case LibPkg::PackageVersionComparison::Equal:
            case LibPkg::PackageVersionComparison::PackageUpgradeOnly:
                amendment.bumpDownstreamVersion = LibPkg::PackageAmendment::VersionBump::PackageVersion;
                m_warnings.emplace_back("Bumping pkgrel of " % package->name % "; version " % existingVersionStr + " already exists");
                break;
            case LibPkg::PackageVersionComparison::SoftwareUpgrade:
                if (package->decomposeName().isVcsPackage()) {
                    amendment.setUpstreamVersion = true;
                    amendment.bumpDownstreamVersion = LibPkg::PackageAmendment::VersionBump::PackageVersion;
                    m_warnings.emplace_back("Bumping pkgver and pkgrel of VCS package " % package->name % "; its version " % package->version
                            % " is older than existing version "
                        + existingVersionStr);
                } else {
                    amendment.bumpDownstreamVersion = LibPkg::PackageAmendment::VersionBump::Epoch;
                    m_warnings.emplace_back(
                        "Bumping epoch of " % package->name % "; its version " % package->version % " is older than existing version "
                        + existingVersionStr);
                    goto breakLoop;
                }
                break;
            default:;
            }
        }
    breakLoop:
        if (m_forceBumpPackageVersion && amendment.isEmpty()) {
            amendment.bumpDownstreamVersion = LibPkg::PackageAmendment::VersionBump::PackageVersion;
            m_warnings.emplace_back("Bumping pkgrel of " % packageName + " (and its split packages); forcing pkgrel bump has been enabled");
        }
        if (!amendment.isEmpty()) {
            auto amendedVersions = LibPkg::amendPkgbuild(buildData.sourceDirectory + "/PKGBUILD", existingVersion, amendment);
            if (!amendedVersions.newEpoch.empty()) {
                newVersion.epoch = std::move(amendedVersions.newEpoch);
            }
            if (!amendedVersions.newUpstreamVersion.empty()) {
                newVersion.upstream = std::move(amendedVersions.newUpstreamVersion);
            }
            if (!amendedVersions.newPkgRel.empty()) {
                newVersion.package = std::move(amendedVersions.newPkgRel);
            }
            const auto newVersionStr = newVersion.toString();
            for (const auto &[packageID, package] : buildData.packages) {
                package->version = newVersionStr;
            }
            m_warnings.emplace_back("New version of " % packageName % " (and its split packages) is " + newVersionStr);
        }
    }
}

void PrepareBuild::computeDependencies(WebClient::AurSnapshotQuerySession::ContainerType &&responses)
{
    if (reportAbortedIfAborted()) {
        return;
    }

    // find databases again
    auto configReadLock = m_setup.config.lockToRead();
    if (auto error = findDatabases(); !error.empty()) {
        reportError(std::move(error));
        return;
    }

    // populate build data from responses and add further dependencies
    auto furtherDependenciesNeeded = false;
    auto sourcesMissing = false;
    auto needToFetchAgain = false;
    for (auto &response : responses) {
        if (response.packageName.empty()) {
            auto &buildData = m_buildDataByPackage["?"];
            auto &error = buildData.error;
            if (!error.empty()) {
                error += '\n';
            }
            error = response.error.empty() ? "got response with no package name (internal error)" : std::move(response.error);
            sourcesMissing = true;
            continue;
        }
        auto &buildData = m_buildDataByPackage[response.packageName];
        if (!response.packages.empty() && response.packages.front().pkg) {
            buildData.sourceInfo = response.packages.front().pkg->sourceInfo;
        }
        buildData.packages = std::move(response.packages);
        buildData.error = std::move(response.error);
        buildData.hasSource = buildData.sourceInfo && !buildData.packages.empty() && buildData.error.empty();
        if (buildData.hasSource) {
            const auto buildActionsWriteLock = m_setup.building.lockToWrite();
            m_buildAction->artefacts.emplace_back(buildData.sourceDirectory + "/PKGBUILD"); // FIXME: add all files as artefacts
            continue;
        }
        if (response.isOfficial && buildData.error.empty()) {
            needToFetchAgain = true;
            continue;
        }
        if (buildData.error.empty()) {
            buildData.error = "no build data available";
        }
        sourcesMissing = true;
    }
    for (auto &response : responses) {
        if (response.packageName.empty()) {
            continue;
        }
        auto &buildData = m_buildDataByPackage[response.packageName];
        if (!buildData.hasSource) {
            continue;
        }
        furtherDependenciesNeeded = pullFurtherDependencies(buildData.sourceInfo->makeDependencies) || furtherDependenciesNeeded;
        furtherDependenciesNeeded = pullFurtherDependencies(buildData.sourceInfo->checkDependencies) || furtherDependenciesNeeded;
        for (const auto &[packageID, package] : buildData.packages) {
            furtherDependenciesNeeded = pullFurtherDependencies(package->dependencies) || furtherDependenciesNeeded;
        }
    }
    configReadLock.unlock();

    if (reportAbortedIfAborted()) {
        return;
    }

    // pull missing dependencies if all sources could be retrieved so far
    if (furtherDependenciesNeeded) {
        m_pulledInFurtherDependencies = true;
    }
    if (!sourcesMissing && (furtherDependenciesNeeded || needToFetchAgain)) {
        fetchMissingBuildData();
        return;
    }

    // check for errors
    auto failedPackages = std::set<std::string>();
    auto localPackages = std::set<std::string>();
    auto errorMessage = std::string();
    for (const auto &buildData : m_buildDataByPackage) {
        if (!buildData.second.error.empty()) {
            failedPackages.emplace(buildData.first);
        } else if (m_aurOnly && !buildData.second.originalSourceDirectory.empty()) {
            localPackages.emplace(buildData.first);
        }
    }
    if (!failedPackages.empty()) {
        errorMessage = "Unable to retrieve the following packages (see result data for details): " + joinStrings(failedPackages, " ");
    } else if (!localPackages.empty()) {
        errorMessage = "The following packages have a local override but the AUR-only flag was set: " + joinStrings(localPackages, " ");
    }
    if (!errorMessage.empty()) {
        m_buildAction->appendOutput(Phrases::ErrorMessage, errorMessage, '\n');
        auto resultData = makeResultData(std::move(errorMessage));
        auto buildActionWriteLock = m_setup.building.lockToWrite();
        m_buildAction->resultData = std::move(resultData);
        reportError();
        return;
    }

    bumpVersions();

    if (reportAbortedIfAborted()) {
        return;
    }

    if (m_keepOrder) {
        deduceBatchesFromSpecifiedOrder();
    } else {
        computeBatches();
    }

    // provoke an error if previously pulled-in further dependencies and that's not wanted
    auto error = std::string();
    if (m_pulledInFurtherDependencies && m_pullingInFurtherDependenciesUnexpected) {
        error = "Had to pull-in further dependencies which is considered unexpected";
    }

    auto resultData = makeResultData(std::move(error));
    auto wasSuccess = resultData.error.empty() && resultData.cyclicLeftovers.empty();
    auto buildActionWriteLock = m_setup.building.lockToWrite();
    m_buildAction->resultData = std::move(resultData);
    if (wasSuccess) {
        reportSuccess();
    } else {
        reportError();
    }
}

std::unordered_map<std::string, BatchItem> PrepareBuild::prepareBatches()
{
    std::unordered_map<std::string, BatchItem> batchItems;
    for (const auto &[packageName, buildData] : m_buildDataByPackage) {
        auto [i, newItem] = batchItems.try_emplace(packageName, BatchItem{ .name = &packageName, .buildData = &buildData });
        auto &batchItem = i->second;
        const auto &sourceInfo = buildData.sourceInfo;
        for (const auto &[packageID, package] : buildData.packages) {
            for (const auto &deps : { sourceInfo->makeDependencies, sourceInfo->checkDependencies, package->dependencies }) {
                for (const auto &dep : deps) {
                    batchItem.requiredDependencies.add(dep, package);
                }
            }
            for (auto &dep : package->provides) {
                batchItem.providedDependencies.add(dep, package);
            }
        }
    }
    return batchItems;
}

void PrepareBuild::deduceBatchesFromSpecifiedOrder()
{
    m_buildAction->appendOutput(Phrases::InfoMessage, "Fetched sources; making batches for specified order ...\n"sv);

    auto batchItems = prepareBatches();
    BatchList batches;
    for (auto &[packageName, batchItem] : batchItems) {
        batches.emplace_back(std::vector<BatchItem *>{ &batchItem });
    }
    std::sort(batches.begin(), batches.end(), [](const Batch &lhs, const Batch &rhs) { return sortBatch(lhs.front(), rhs.front()); });
    addBatchesToResult(std::move(batches), Batch());
}

void PrepareBuild::computeBatches()
{
    m_buildAction->appendOutput(Phrases::InfoMessage, "Fetched sources; computing build batches ...\n"sv);

    // prepare computing batches
    auto batchItems = prepareBatches();
    auto configReadLock2 = m_setup.config.lockToRead();
    if (auto error = findDatabases(); !error.empty()) {
        reportError(std::move(error));
        return;
    }

    // add dependency relations to batch items
    for (auto &batchItem : batchItems) {
        batchItem.second.determineNeededItems(m_setup.config, m_requiredDbs, *m_destinationDbs.begin(), batchItems);
    }
    configReadLock2.unlock();

    // FIXME: check for missing dependencies

    // add batch items into batches
    BatchList batches;
    bool allDone;
    size_t addedItems;
    do {
        if (reportAbortedIfAborted()) {
            return;
        }
        auto &newBatch = batches.emplace_back();
        allDone = BatchItem::addItemsToBatch(newBatch, batchItems);
        addedItems = newBatch.size();
        if (!addedItems) {
            batches.pop_back();
        }
        for (auto &addedItem : newBatch) {
            addedItem->done = true;
        }
    } while (!allDone && addedItems);

    // check for cyclic leftovers
    Batch cyclicLeftovers;
    if (!allDone) {
        for (auto &item : batchItems) {
            if (!item.second.done) {
                cyclicLeftovers.emplace_back(&item.second);
            }
        }
        if (!cyclicLeftovers.empty()) {
            m_warnings.emplace_back("Dependency cycles have been detected. Add a bootstrap package to the package list to resolve this.");
        }
    }

    // sort batches so the order in which packages have been specified is preserved within the batches
    for (auto &batch : batches) {
        std::sort(batch.begin(), batch.end(), sortBatch);
    }
    std::sort(cyclicLeftovers.begin(), cyclicLeftovers.end(), sortBatch);

    // add batches to result
    addBatchesToResult(std::move(batches), std::move(cyclicLeftovers));
}

void PrepareBuild::addBatchesToResult(BatchList &&batches, Batch &&cyclicLeftovers)
{
    const auto returnItemName = [](const BatchItem *item) { return *item->name; };
    m_batches.reserve(batches.size());
    std::transform(batches.cbegin(), batches.cend(), std::back_inserter(m_batches), [&returnItemName](const Batch &batch) {
        std::vector<std::string> newBatch;
        newBatch.reserve(batch.size());
        std::transform(batch.cbegin(), batch.cend(), std::back_inserter(newBatch), returnItemName);
        return newBatch;
    });
    m_cyclicLeftovers.reserve(cyclicLeftovers.size());
    std::transform(cyclicLeftovers.cbegin(), cyclicLeftovers.cend(), std::back_inserter(m_cyclicLeftovers), returnItemName);
}

BuildPreparation PrepareBuild::makeResultData(std::string &&error)
{
    auto resultData = BuildPreparation{
        .buildData = std::move(m_buildDataByPackage),
        .dbConfig = std::move(m_dbConfig),
        .stagingDbConfig = std::move(m_stagingDbConfig),
        .targetDb = std::move(m_targetDbName),
        .targetArch = std::move(m_targetArch),
        .stagingDb = std::move(m_stagingDbName),
        .batches = std::move(m_batches),
        .cyclicLeftovers = std::move(m_cyclicLeftovers),
        .warnings = std::move(m_warnings),
        .error = std::move(error),
        .manuallyOrdered = m_keepOrder,
    };

    // write results into the working directory so the actual build can pick it up
    try {
        dumpJsonDocument(
            [this, &resultData] {
                const auto configLock = m_setup.config.lockToRead();
                return resultData.toJsonDocument();
            },
            m_workingDirectory, buildPreparationFileName, DumpJsonExistingFileHandling::Backup);
    } catch (const std::runtime_error &e) {
        const auto what = string_view(e.what());
        if (resultData.error.empty()) {
            resultData.error = what;
        } else {
            resultData.warnings.emplace_back(what);
        }
        m_buildAction->appendOutput(Phrases::ErrorMessage, what, '\n');
    }

    // write build progress skeletion (a BuildProgress with a default-initialized PackageBuildProgress for each package)
    try {
        BuildProgress progress;
        try {
            restoreJsonObject(progress, m_workingDirectory, buildProgressFileName, RestoreJsonExistingFileHandling::Skip);
        } catch (const std::runtime_error &e) {
            m_buildAction->appendOutput(Phrases::ErrorMessage, "Unable to read existing build-progress.json: ", e.what(), '\n');
        }
        if (dumpJsonDocument(
                [this, &resultData, &progress] {
                    // reset database-specific fields as the database configuration might have changed
                    progress.targetDbFilePath.clear();
                    progress.targetRepoPath.clear();
                    progress.stagingDbFilePath.clear();
                    progress.stagingRepoPath.clear();
                    for (const auto &[packageName, buildData] : resultData.buildData) {
                        auto &buildProgress = progress.progressByPackage[packageName];
                        // reset the build progress if the PKGBUILD has been updated
                        if (!buildProgress.hasBeenAnyProgressMade()) {
                            continue;
                        }
                        if (buildProgress.buildDirectory.empty()) {
                            buildProgress.resetProgress();
                            if (m_resetChrootSettings) {
                                buildProgress.resetChrootSettings();
                            }
                            continue;
                        }
                        const std::filesystem::path srcDirPkgbuild = buildData.sourceDirectory + "/PKGBUILD";
                        const std::filesystem::path buildDirPkgbuild = buildProgress.buildDirectory + "/PKGBUILD";
                        try {
                            if (!std::filesystem::exists(buildDirPkgbuild)
                                || std::filesystem::last_write_time(srcDirPkgbuild) > std::filesystem::last_write_time(buildDirPkgbuild)) {
                                buildProgress.resetProgress();
                                if (m_resetChrootSettings) {
                                    buildProgress.resetChrootSettings();
                                }
                            }
                        } catch (const std::filesystem::filesystem_error &e) {
                            m_buildAction->appendOutput(
                                Phrases::ErrorMessage, "Unable to determine whether PKGBUILD has been updated: ", e.what(), '\n');
                        }
                    }
                    return progress.toJsonDocument();
                },
                m_workingDirectory, buildProgressFileName, DumpJsonExistingFileHandling::Backup)
                .empty()) {
            m_buildAction->appendOutput(Phrases::ErrorMessage, "Unable to write build-progress.json\n"sv);
        }
    } catch (const std::runtime_error &e) {
        const auto what = string_view(e.what());
        resultData.warnings.emplace_back(what);
        m_buildAction->appendOutput(Phrases::ErrorMessage, what);
    }

    return resultData;
}

} // namespace LibRepoMgr
