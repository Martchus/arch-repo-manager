#include "./buildactionprivate.h"
#include "./subprocess.h"

#include "../helper.h"
#include "../json.h"
#include "../logging.h"
#include "../serversetup.h"

#include <passwordfile/io/entry.h>
#include <passwordfile/io/field.h>
#include <passwordfile/io/passwordfile.h>

#include <c++utilities/conversion/stringbuilder.h>
#include <c++utilities/conversion/stringconversion.h>
#include <c++utilities/io/ansiescapecodes.h>
#include <c++utilities/io/buffersearch.h>
#include <c++utilities/io/inifile.h>
#include <c++utilities/io/misc.h>
#include <c++utilities/io/path.h>

#include <boost/asio/read.hpp>

#include <boost/process/start_dir.hpp>

#include <rapidjson/error/en.h>
#include <rapidjson/filewritestream.h>
#include <rapidjson/prettywriter.h>

#include <filesystem>
#include <fstream>
#include <unordered_set>

#if __has_include(<experimental/unordered_set>)
#include <experimental/unordered_set>
#endif
#if __has_include(<experimental/unordered_map>)
#include <experimental/unordered_map>
#endif

using namespace std;
using namespace CppUtilities;
using namespace CppUtilities::EscapeCodes;

namespace LibRepoMgr {

void RebuildInfo::replace(const LibPkg::DependencySet &deps, const std::unordered_set<std::string> &libs)
{
    provides.clear();
    libprovides.clear();
    add(deps, libs);
}

void RebuildInfo::add(const LibPkg::DependencySet &deps, const std::unordered_set<std::string> &libs)
{
    for (const auto &dep : deps) {
        const auto &[depName, depDetails] = dep;
        provides.emplace_back(depName, depDetails.version, depDetails.mode);
    }
    libprovides.insert(libprovides.end(), libs.cbegin(), libs.cend());
}

/*!
 * \brief Constructs a new BatchProcessingSession to start asynchronous tasks in batches.
 * \param relevantPackages If non-empty, getCurrentPackageNameIfValidAndRelevantAndSelectNext() will skip packages not contained.
 * \param batches The batches to build. This is a list of a list of packages.
 * \param ioContext The IO context to run the specified handler.
 * \param handler The handler to execute after processing all batches has finished (or is otherwise aborted).
 * \param skipBatchesAfterFailure Whether processing subsequent should be aborted after an error.
 */
BatchProcessingSession::BatchProcessingSession(const std::unordered_set<string_view> &relevantPackages,
    std::vector<std::vector<std::string>> &batches, boost::asio::io_context &ioContext, BatchProcessingSession::HandlerType &&handler,
    bool skipBatchesAfterFailure)
    : MultiSession<std::string>(ioContext, std::move(handler))
    , m_relevantPackages(relevantPackages)
    , m_batchBegin(batches.begin())
    , m_batchIterator(batches.begin())
    , m_batchEnd(batches.end())
    , m_packageIterator(m_batchIterator != m_batchEnd ? m_batchIterator->begin() : decltype(m_packageIterator)())
    , m_packageEnd(m_batchIterator != m_batchEnd ? m_batchIterator->end() : decltype(m_packageEnd)())
    , m_firstPackageInBatch(true)
    , m_skipBatchesAfterFailure(skipBatchesAfterFailure)
    , m_hasFailuresInPreviousBatches(false)
    , m_enableStagingInNextBatch(false)
    , m_enableStagingInThisBatch(false)
    , m_stagingEnabled(false)
{
}

/*!
// * \brief Returns whether currentPackageName() and selectNextPackage() can be called.
 * \remarks If not, that means there are no more packages to process or a failure within the previous batch prevents
 *          the session to proceed.
 */
bool BatchProcessingSession::isValid() const
{
    return m_packageIterator != m_packageEnd
        && (allResponses().empty() || !m_skipBatchesAfterFailure || m_packageIterator != m_batchIterator->begin());
}

/*!
 * \brief Returns whether staging is now active after a previous call to enableStagingInNextBatch().
 * \remarks
 * This function's return value relates to the package returned by getCurrentPackageNameIfValidAndRelevantAndSelectNext()
 * despite that function selecting the next package.
 */
bool BatchProcessingSession::isStagingEnabled() const
{
    return m_stagingEnabled;
}

/*!
 * \brief Returns whether there are failures within previously processed batches.
 */
bool BatchProcessingSession::hasFailuresInPreviousBatches() const
{
    return m_hasFailuresInPreviousBatches || (m_firstPackageInBatch && !allResponses().empty());
}

/*!
 * \brief Returns the current package name. Must not be called unless isValid() returns true.
 */
const std::string &BatchProcessingSession::currentPackageName() const
{
    return *m_packageIterator;
}

/*!
 * \brief Selects the next package, possibly from the next batch. Must not be called unless isValid() returns true.
 * \remarks If there's no next package isValid() will return false after the call.
 */
void BatchProcessingSession::selectNextPackage()
{
    if (++m_packageIterator != m_packageEnd) {
        m_firstPackageInBatch = false;
        m_stagingEnabled = m_stagingEnabled || m_enableStagingInThisBatch;
        return; // select the next package within the current batch
    }
    if ((m_hasFailuresInPreviousBatches = !allResponses().empty()) && m_skipBatchesAfterFailure) {
        return; // invalidate the session if there were failures within the previous batch
    }
    if (++m_batchIterator == m_batchEnd) {
        return; // invalidate the session; the current package was the last one within the last batch
    }
    // select the first package within the next batch
    m_packageIterator = m_batchIterator->begin();
    m_packageEnd = m_batchIterator->end();
    m_firstPackageInBatch = true;
    m_enableStagingInThisBatch.store(m_enableStagingInNextBatch);
}

/*!
 * \brief Grabs the next valid package, possibly from the next batch.
 * \returns Returns the package name or nullptr if there are no more packages to process.
 * \remarks
 * - To parallelize builds we needed an option to avoid the "possibly from the next batch" part until all packages
 *   from the current batch have been processed. (It needed to be an option because when downloading we can already
 *   grab packages from the next batch.)
 * - This function might be called from multiple threads at the same time.
 */
const std::string *BatchProcessingSession::getCurrentPackageNameIfValidAndRelevantAndSelectNext()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const std::string *packageName = nullptr;
    do {
        if (!isValid()) {
            return nullptr;
        }
        packageName = &currentPackageName();
        selectNextPackage();
    } while (!m_relevantPackages.empty() && m_relevantPackages.find(*packageName) == m_relevantPackages.end());
    return packageName;
}

/*!
 * \brief
 * Enables staging for the next batch. Enables staging for the current batch if the current package is the first package of
 * current batch and the current batch is not the first batch.
 * \remarks
 * - The behavior regarding the current batch was chosen because we call selectNextPackage() *before* enableStagingInNextBatch().
 * - This function might be called from multiple threads at the same time.
 */
void BatchProcessingSession::enableStagingInNextBatch()
{
    m_enableStagingInNextBatch = true;
    if (!m_stagingEnabled) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stagingEnabled
            = m_stagingEnabled || (m_batchIterator != m_batchEnd && m_batchIterator != m_batchBegin && m_packageIterator == m_batchIterator->begin());
    }
}

ConductBuild::ConductBuild(ServiceSetup &setup, const std::shared_ptr<BuildAction> &buildAction)
    : InternalBuildAction(setup, buildAction)
    , m_buildAsFarAsPossible(false)
    , m_saveChrootDirsOfFailures(false)
    , m_updateChecksums(false)
    , m_autoStaging(false)
    , m_useContainer(false)
{
}

[[nodiscard]] std::string ConductBuild::locateGlobalConfigPath(const std::string &chrootDir, std::string_view trailingPath) const
{
    const auto start = (chrootDir.empty() ? m_setup.building.chrootDir : chrootDir) % "/config-";
    const auto path = std::filesystem::path(start % m_buildPreparation.targetDb % '-' % m_buildPreparation.targetArch + trailingPath);
    if (std::filesystem::exists(path)) {
        // return repository-specific path if it exists
        return path.string();
    }
    // fallback to generic (but still arch-specific) location
    return start % m_buildPreparation.targetArch + trailingPath;
}

void ConductBuild::run()
{
    // validate and read parameter/settings
    if (auto error = validateParameter(RequiredDatabases::None, RequiredParameters::MaybePackages); !error.empty()) {
        reportError(move(error));
        return;
    }
    if (m_buildAction->directory.empty()) {
        reportError("Unable to find working directory: no directory name specified");
        return;
    }
    for (const auto &packageName : m_buildAction->packageNames) {
        const auto firstSlash = packageName.find('/');
        if (firstSlash == std::string::npos) {
            m_relevantPackages.emplace(packageName);
        } else {
            m_relevantPackages.emplace(packageName.data() + firstSlash + 1, packageName.size() - firstSlash - 1);
        }
    }
    const auto flags = static_cast<ConductBuildFlags>(m_buildAction->flags);
    m_buildAsFarAsPossible = flags & ConductBuildFlags::BuildAsFarAsPossible;
    m_saveChrootDirsOfFailures = flags & ConductBuildFlags::SaveChrootOfFailures;
    m_updateChecksums = flags & ConductBuildFlags::UpdateChecksums;
    m_autoStaging = flags & ConductBuildFlags::AutoStaging;
    m_useContainer = flags & ConductBuildFlags::UseContainer;
    auto &metaInfo = m_setup.building.metaInfo;
    auto metaInfoLock = metaInfo.lockToRead();
    const auto &typeInfo = metaInfo.typeInfoForId(BuildActionType::ConductBuild);
    const auto chrootDirSetting = typeInfo.settings[static_cast<std::size_t>(ConductBuildSettings::ChrootDir)].param;
    const auto chrootDefaultUserSetting = typeInfo.settings[static_cast<std::size_t>(ConductBuildSettings::ChrootDefaultUser)].param;
    const auto ccacheDirSetting = typeInfo.settings[static_cast<std::size_t>(ConductBuildSettings::CCacheDir)].param;
    const auto pkgCacheDirSetting = typeInfo.settings[static_cast<std::size_t>(ConductBuildSettings::PackageCacheDir)].param;
    const auto testFilesDirSetting = typeInfo.settings[static_cast<std::size_t>(ConductBuildSettings::TestFilesDir)].param;
    const auto gpgKeySetting = typeInfo.settings[static_cast<std::size_t>(ConductBuildSettings::GpgKey)].param;
    metaInfoLock.unlock();
    const auto &chrootDir = findSetting(chrootDirSetting);
    const auto &chrootDefaultUser = findSetting(chrootDefaultUserSetting);
    m_globalCcacheDir = findSetting(ccacheDirSetting);
    m_globalPackageCacheDir = findSetting(pkgCacheDirSetting);
    m_globalTestFilesDir = findSetting(testFilesDirSetting);
    m_gpgKey = findSetting(gpgKeySetting);
    auto setupReadLock = m_setup.lockToRead();
    m_workingDirectory = determineWorkingDirectory(buildDataWorkingDirectory);
    if (m_useContainer) {
        m_makeContainerPkgPath = findExecutable(m_setup.building.makeContainerPkgPath);
    } else {
        m_makePkgPath = findExecutable(m_setup.building.makePkgPath);
        m_makeChrootPkgPath = findExecutable(m_setup.building.makeChrootPkgPath);
    }
    m_updatePkgSumsPath = findExecutable(m_setup.building.updatePkgSumsPath);
    m_repoAddPath = findExecutable(m_setup.building.repoAddPath);
    m_gpgPath = findExecutable(m_setup.building.gpgPath);
    setupReadLock.unlock();

    // check executables
    if (m_useContainer) {
        if (!checkExecutable(m_makeContainerPkgPath)) {
            reportError("Unable to find makecontainerpkg executable \"" % m_setup.building.makeContainerPkgPath + "\" in PATH.");
            return;
        }
    } else {
        if (!checkExecutable(m_makePkgPath)) {
            reportError("Unable to find makepkg executable \"" % m_setup.building.makePkgPath + "\" in PATH.");
            return;
        }
        if (!checkExecutable(m_makeChrootPkgPath)) {
            reportError("Unable to find makechrootpkg executable \"" % m_setup.building.makeChrootPkgPath + "\" in PATH.");
            return;
        }
    }
    if (!checkExecutable(m_updatePkgSumsPath)) {
        reportError("Unable to find updpkgsums executable \"" % m_setup.building.updatePkgSumsPath + "\" in PATH.");
        return;
    }
    if (!checkExecutable(m_repoAddPath)) {
        reportError("Unable to find repo-add executable \"" % m_setup.building.repoAddPath + "\" in PATH.");
        return;
    }

    // assign paths
    m_makepkgConfigPath = m_workingDirectory + "/makepkg.conf";
    m_pacmanConfigPath = m_workingDirectory + "/pacman.conf";
    m_pacmanStagingConfigPath = m_workingDirectory + "/pacman-staging.conf";

    // read secrets
    readSecrets();

    // parse build preparation
    auto errors = ReflectiveRapidJSON::JsonDeserializationErrors();
    try {
        m_buildPreparationFilePath = restoreJsonObject(
            m_buildPreparation, m_workingDirectory, buildPreparationFileName, RestoreJsonExistingFileHandling::RequireExistingFile);
    } catch (const std::runtime_error &e) {
        reportError(e.what());
        return;
    }

    // parse build progress
    try {
        restoreJsonObject(m_buildProgress, m_workingDirectory, buildProgressFileName, RestoreJsonExistingFileHandling::Skip);
    } catch (const std::runtime_error &e) {
        reportError(e.what());
        return;
    }

    // check target db, arch and whether packages and source info are present
    if (m_buildPreparation.targetDb.empty() || m_buildPreparation.targetArch.empty()) {
        reportError("The destination database and target architecture specified in build-preparation.json must not be empty.");
        return;
    }
    for (const auto &[packageName, buildData] : m_buildPreparation.buildData) {
        if (packageName.empty()) {
            reportError("The build data contains an empty package name.");
            return;
        }
        if (!buildData.sourceInfo) {
            reportError(argsToString("The build data for \"" % packageName % "\" has no source info."));
            return;
        }
        if (buildData.packages.empty()) {
            reportError(argsToString("The build data for \"" % packageName % "\" has no packages."));
            return;
        }
        for (const auto &[packageID, package] : buildData.packages) {
            if (!package) {
                reportError(argsToString("The package of build data for \"" % packageName % "\" is null."));
                return;
            }
        }
    }
    m_buildAction->appendOutput(
        Phrases::InfoMessage, "Destination database: " % m_buildPreparation.targetDb % ", architecture: " % m_buildPreparation.targetArch + '\n');

    // read values from global config
    setupReadLock = m_setup.lockToRead();
    if (chrootDir.empty() && m_setup.building.chrootDir.empty()) {
        setupReadLock.unlock();
        reportError("The chroot directory is not configured.");
        return;
    }
    m_globalPacmanConfigPath = locateGlobalConfigPath(chrootDir, "/pacman.conf");
    m_globalMakepkgConfigFilePath = locateGlobalConfigPath(chrootDir, "/makepkg.conf");
    if (m_globalCcacheDir.empty()) {
        m_globalCcacheDir = m_setup.building.ccacheDir;
    }
    if (m_globalPackageCacheDir.empty()) {
        m_globalPackageCacheDir = m_setup.building.packageCacheDir;
    }
    if (m_globalTestFilesDir.empty()) {
        m_globalTestFilesDir = m_setup.building.testFilesDir;
    }
    m_chrootRootUser = m_setup.building.chrootRootUser;
    if (m_gpgKey == "none") {
        m_gpgKey.clear();
    } else if (m_gpgKey.empty()) {
        m_gpgKey = m_setup.building.defaultGpgKey;
    }
    setupReadLock.unlock();

    // use arch-specific sub-directory within cache dir
    m_globalPackageCacheDir = m_globalPackageCacheDir % '/' + m_buildPreparation.targetArch;

    // fill omitted build progress configuration with defaults from global config
    if (m_autoStaging && m_buildPreparation.stagingDb.empty()) {
        reportError("Auto-staging is enabled but no staging database has been specified in build-preparation.json.");
        return;
    }
    if (m_buildProgress.targetDbFilePath.empty() || m_buildProgress.targetRepoPath.empty()) {
        const auto configLock = m_setup.config.lockToRead();
        if (const auto *const targetDb = m_setup.config.findDatabase(m_buildPreparation.targetDb, m_buildPreparation.targetArch)) {
            if (m_buildProgress.targetDbFilePath.empty()) {
                m_buildProgress.targetDbFilePath = fileName(targetDb->path);
                std::error_code ec;
                auto symlinkTarget = std::filesystem::read_symlink(m_buildProgress.targetDbFilePath, ec);
                if (!ec) {
                    m_buildProgress.targetDbFilePath = symlinkTarget;
                }
            }
            if (m_buildProgress.targetRepoPath.empty()) {
                m_buildProgress.targetRepoPath = targetDb->localPkgDir;
            }
        }
        if (const auto *const stagingDb
            = m_autoStaging ? m_setup.config.findDatabase(m_buildPreparation.stagingDb, m_buildPreparation.targetArch) : nullptr) {
            if (m_buildProgress.stagingDbFilePath.empty()) {
                m_buildProgress.stagingDbFilePath = fileName(stagingDb->path);
                std::error_code ec;
                auto symlinkTarget = std::filesystem::read_symlink(m_buildProgress.stagingDbFilePath, ec);
                if (!ec) {
                    m_buildProgress.stagingDbFilePath = symlinkTarget;
                }
            }
            if (m_buildProgress.stagingRepoPath.empty()) {
                m_buildProgress.stagingRepoPath = stagingDb->localPkgDir;
            }
        } else if (m_autoStaging) {
            reportError("Auto-staging is enabled but the staging database \"" % m_buildPreparation.stagingDb % '@' % m_buildPreparation.targetArch
                + "\" specified in build-preparation.json can not be found.");
            return;
        }
    }
    if (m_buildProgress.targetDbFilePath.empty()) {
        reportError("Unable to determine path for database \"" % m_buildPreparation.targetDb % '@' % m_buildPreparation.targetArch
            + "\"; set \"dbdir\" for that database in the server config or amend build-progress.json manually and retry.");
        return;
    }
    if (m_buildProgress.targetRepoPath.empty()) {
        m_buildProgress.targetRepoPath = directory(m_buildProgress.targetDbFilePath);
    }
    if (m_autoStaging) {
        if (m_buildProgress.stagingDbFilePath.empty()) {
            reportError(
                "Unable to determine path for staging database \"" % m_buildPreparation.stagingDb % '@' % m_buildPreparation.targetArch + "\".");
            return;
        }
        if (m_buildProgress.stagingRepoPath.empty()) {
            m_buildProgress.stagingRepoPath = directory(m_buildProgress.stagingDbFilePath);
        }
    }
    try {
        if (!std::filesystem::exists(m_buildProgress.targetRepoPath)) {
            reportError("Destination repository \"" % m_buildProgress.targetRepoPath % "\" does not exist; set \"pkgdir\" for database \""
                    % m_buildPreparation.targetDb % '@' % m_buildPreparation.targetArch
                + "\" to an existing directory or amend build-pgrogress.json manually and retry.");
            return;
        }
        if (!std::filesystem::exists(m_buildProgress.targetRepoPath % '/' + m_buildProgress.targetDbFilePath)) {
            reportError("Destination database file \"" % m_buildProgress.targetDbFilePath % "\" does not exist in \"" % m_buildProgress.targetRepoPath
                + "\".");
            return;
        }
        if (m_autoStaging) {
            if (!std::filesystem::exists(m_buildProgress.stagingRepoPath)) {
                reportError("Staging repository \"" % m_buildProgress.stagingRepoPath % "\" does not exist; set \"pkgdir\" for database \""
                        % m_buildPreparation.targetDb % '@' % m_buildPreparation.targetArch
                    + "-staging\" to an existing directory or amend build-pgrogress.json manually and retry.");
                return;
            }
            if (!std::filesystem::exists(m_buildProgress.stagingRepoPath % '/' + m_buildProgress.stagingDbFilePath)) {
                reportError(
                    "Staging database file \"" % m_buildProgress.stagingDbFilePath % "\" does not exist in \"" % m_buildProgress.stagingRepoPath
                    + "\".");
                return;
            }
        }
    } catch (const std::filesystem::filesystem_error &e) {
        reportError(
            "Unable to check whether database/repository \"" % m_buildPreparation.targetDb % '@' % m_buildPreparation.targetArch % "\" exists: "
            + e.what());
        return;
    }
    auto setupReadLock2 = m_setup.lockToRead();
    for (auto &progressByPackage : m_buildProgress.progressByPackage) {
        const auto &packageName = progressByPackage.first;
        auto &progress = progressByPackage.second;
        if (progress.buildDirectory.empty()) {
            progress.buildDirectory = m_workingDirectory % '/' % packageName + "/pkg";
        }
        if (progress.chrootDirectory.empty()) {
            progress.chrootDirectory = chrootDir.empty() ? m_setup.building.chrootDir : chrootDir;
        }
        if (progress.chrootUser.empty()) {
            progress.chrootUser = chrootDefaultUser.empty() ? m_setup.building.chrootDefaultUser : chrootDefaultUser;
        }
        if (progress.makechrootpkgFlags.empty()) {
            progress.makechrootpkgFlags = m_setup.building.makechrootpkgFlags;
        }
        if (progress.makepkgFlags.empty()) {
            progress.makepkgFlags = m_setup.building.makepkgFlags;
        }
    }
    setupReadLock2.unlock();
    dumpBuildProgress();

    try {
        makeConfigFiles();
    } catch (const std::runtime_error &e) {
        reportError(argsToString("Unable to prepare pacman.conf and makepkg.conf: ", e.what()));
        return;
    }

    downloadSourcesAndContinueBuilding();
}

/*!
 * \brief Reads secrets from the encrypted password file.
 * \remarks The password is supposed to be pre-supplied by the web session that started the build action (or
 *          passed by the previous build action).
 */
void LibRepoMgr::ConductBuild::readSecrets()
{
    auto *const secretsFile = m_buildAction->secrets();
    if (!secretsFile) {
        m_buildAction->log()(Phrases::WarningMessage, "No secrets present all.\n");
        return;
    }
    if (!secretsFile->hasRootEntry()) {
        try {
            if (!secretsFile->isOpen()) {
                secretsFile->open(Io::PasswordFileOpenFlags::ReadOnly);
            }
            secretsFile->load();
            secretsFile->close();
        } catch (const std::runtime_error &e) {
            const auto note = secretsFile->password().empty() ? " (password was empty)"sv : std::string_view();
            m_buildAction->log()(
                Phrases::WarningMessage, "Unable to load secrets from \"", secretsFile->path(), '\"', note, ':', ' ', e.what(), '\n');
            return;
        }
    }
    auto *const secrets = secretsFile->rootEntry();
    auto sudoPath = std::list<std::string>{ "build", "sudo" };
    auto gpgPath = std::list<std::string>{ "build", "gpg" };
    if (auto sudoEntry = secrets->entryByPath(sudoPath); sudoEntry && sudoEntry->type() == Io::EntryType::Account) {
        for (auto fields = static_cast<Io::AccountEntry *>(sudoEntry)->fields(); const auto &field : fields) {
            if (field.name() == "username") {
                m_sudoUser = field.value();
            } else if (field.name() == "password") {
                m_sudoPassword = field.value();
            }
        }
    }
    if (auto gpgEntry = secrets->entryByPath(gpgPath); gpgEntry && gpgEntry->type() == Io::EntryType::Account) {
        for (auto fields = static_cast<Io::AccountEntry *>(gpgEntry)->fields(); const auto &field : fields) {
            if (field.name() == "key") {
                m_gpgKey = field.value();
            } else if (field.name() == "passphrase") {
                m_gpgPassphrase = field.value();
            }
        }
    }
    if (m_sudoUser.empty() || m_sudoPassword.empty()) {
        m_buildAction->log()(Phrases::WarningMessage, "No sudo username and password present. Not switching to a dedicated build user.");
        m_sudoPassword.clear(); // don't write password to stdin if we don't invoke sudo
        return;
    }
    if (!m_gpgKey.empty() && m_gpgPassphrase.empty()) {
        m_buildAction->log()(Phrases::WarningMessage, "GPG key is prsent but no passphrase. Signing with key assuming no passphrase is required.");
        return;
    }
}

/// \cond
static void findPackageExtension(const IniFile::ScopeData &iniScope, const std::string &key, std::string &result)
{
    const auto *const value = getLastValue(iniScope, key);
    if (!value) {
        return;
    }
    result = value;
    // trim leading and trailing quotes and white-spaces
    string::size_type start = 0, end = result.size();
    const auto isQuoteOrWhitespace
        = [&result](string::size_type index) { return result[index] == '\'' || result[index] == '\"' || result[index] == ' '; };
    while (start < end && isQuoteOrWhitespace(start)) {
        ++start;
    }
    while (end > start && isQuoteOrWhitespace(end - 1)) {
        --end;
    }
    if (start || end != result.size()) {
        result = result.substr(start, end - start);
    }
}
/// \endcond

/*!
 * \brief Makes the makepkg config file for the build.
 * \remarks Not overriding existing files to allow manual tweaks.
 * \todo Catch std::ios_base::failure to provide better context.
 */
void ConductBuild::makeMakepkgConfigFile(const std::filesystem::path &makepkgConfigPath)
{
    if (!filesystem::exists(makepkgConfigPath)) {
        filesystem::copy(m_globalMakepkgConfigFilePath, makepkgConfigPath);
    }

    // read PKGEXT from makepkg.conf
    ifstream makepkgConfInputFile;
    makepkgConfInputFile.exceptions(ios_base::badbit | ios_base::failbit);
    makepkgConfInputFile.open(makepkgConfigPath, ios_base::in);
    IniFile makepkgConfIni;
    IniFile::ScopeList &iniData = makepkgConfIni.data();
    makepkgConfIni.parse(makepkgConfInputFile);
    static const struct {
        std::string pkg = "PKGEXT";
        std::string src = "SRCEXT";
    } extensionKeys;
    for (auto i = iniData.rbegin(), end = iniData.rend(); i != end; ++i) {
        findPackageExtension(i->second, extensionKeys.pkg, m_binaryPackageExtension);
        findPackageExtension(i->second, extensionKeys.src, m_sourcePackageExtension);
        if (!m_binaryPackageExtension.empty() && !m_sourcePackageExtension.empty()) {
            break;
        }
    }
    if (m_binaryPackageExtension.empty()) {
        throw std::runtime_error("unable to read PKGEXT from makepkg.conf");
    }
    if (m_sourcePackageExtension.empty()) {
        throw std::runtime_error("unable to read SRCEXT from makepkg.conf");
    }
}

/*!
 * \brief Makes the pacman configuration for the build and the specified \a dbConfig.
 * \remarks Not overriding existing files to allow manual tweaks.
 * \todo Catch std::ios_base::failure to provide better context.
 */
void ConductBuild::makePacmanConfigFile(
    const std::filesystem::path &pacmanConfigPath, const std::vector<std::pair<std::string, std::multimap<std::string, std::string>>> &dbConfig)
{
    // configure databases and cache directory; validate architecture
    const auto pacmanConfigAlreadyExists = filesystem::exists(pacmanConfigPath);
    if (pacmanConfigAlreadyExists && filesystem::last_write_time(pacmanConfigPath) > filesystem::last_write_time(m_buildPreparationFilePath)) {
        // skip if already exists unless the build preparation is newer
        // note: If the build preparation is newer but the pacman config already exist the *existing* config is updated.
        //       This allows keeping a custom config but still updating it on further build preparation runs.
        return;
    }
    ifstream pacmanConfInputFile;
    pacmanConfInputFile.exceptions(ios_base::badbit | ios_base::failbit);
    pacmanConfInputFile.open(pacmanConfigAlreadyExists ? pacmanConfigPath.string() : m_globalPacmanConfigPath, ios_base::in);
    AdvancedIniFile pacmanConfIni;
    pacmanConfIni.parse(pacmanConfInputFile);
    pacmanConfInputFile.close();
    auto &sections = pacmanConfIni.sections;
    // -> remove all existing database sections
    sections.erase(remove_if(sections.begin(), sections.end(), [](const AdvancedIniFile::Section &section) { return section.name != "options"; }),
        sections.end());
    // -> update options
    for (auto &section : sections) {
        if (section.name != "options") {
            continue;
        }
        section.fields.erase(
            std::remove_if(section.fields.begin(), section.fields.end(), [](const AdvancedIniFile::Field &field) { return field.key == "CacheDir"; }),
            section.fields.end());
        section.fields.emplace_back(
            AdvancedIniFile::Field{ .key = "CacheDir", .value = m_useContainer ? "/var/cache/pacman/pkg/" : m_globalPackageCacheDir });
        auto archField = section.findField("Architecture");
        if (archField == section.fieldEnd()) {
            throw std::runtime_error("pacman.conf lacks Architecture option");
        }
        for (; archField != section.fieldEnd(); archField = section.findField(archField, "Architecture")) {
            if (archField->value != m_buildPreparation.targetArch) {
                throw std::runtime_error(
                    "pacman.conf has wrong Architecture option \"" % archField->value % "\" (should have \"" % m_buildPreparation.targetArch % '\"'
                    + ')');
            }
        }
    }
    // -> add database configuration
    auto firstSection = true;
    for (const auto &dbSection : dbConfig) {
        auto &section = sections.emplace_back(AdvancedIniFile::Section{ .name = dbSection.first });
        if (firstSection) {
            section.precedingCommentBlock = argsToString(
                "\n# Database configuration for build action \"", m_buildAction->id, "\" (directory \"", m_workingDirectory, "\")\n\n");
            firstSection = false;
        } else {
            section.precedingCommentBlock = "\n";
        }
        section.fields.reserve(dbSection.second.size());
        for (const auto &dbField : dbSection.second) {
            section.fields.emplace_back(AdvancedIniFile::Field{ .key = dbField.first, .value = dbField.second });
        }
    }
    // -> write changes to disk
    ofstream pacmanConfOutputFile;
    pacmanConfOutputFile.exceptions(ios_base::badbit | ios_base::failbit);
    pacmanConfOutputFile.open(pacmanConfigPath, ios_base::out);
    pacmanConfIni.make(pacmanConfOutputFile);
    pacmanConfOutputFile.close();
}

/*!
 * \brief Make pacman.conf, pacman-staging.conf and makepkg.conf for the build.
 * \remarks Not overriding existing files to allow manual tweaks.
 * \todo Catch std::ios_base::failure to provide better context.
 */
void ConductBuild::makeConfigFiles()
{
    makeMakepkgConfigFile(m_makepkgConfigPath);
    makePacmanConfigFile(m_pacmanConfigPath, m_buildPreparation.dbConfig);
    makePacmanConfigFile(m_pacmanStagingConfigPath, m_buildPreparation.stagingDbConfig);
}

/*!
 * \brief Enqueue the first N downloads to run in parallel; when one download has concluded it will enqueue the next download to
 *        have always running 4 in parallel. Invokes makePackages() when all downloads have been concluded.
 */
void ConductBuild::downloadSourcesAndContinueBuilding()
{
    if (reportAbortedIfAborted()) {
        return;
    }
    constexpr std::size_t maxParallelDownloads = 4;
    enqueueDownloads(make_shared<BatchProcessingSession>(m_relevantPackages, m_buildPreparation.batches, m_setup.building.ioContext,
                         std::bind(&ConductBuild::checkDownloadErrorsAndMakePackages, this, std::placeholders::_1)),
        maxParallelDownloads);
}

/*!
 * \brief Enqueues at most \a maxParallelDownloads downloads. Does nothing if there are no more outstanding downloads.
 */
void ConductBuild::enqueueDownloads(const BatchProcessingSession::SharedPointerType &downloadsSession, std::size_t maxParallelDownloads)
{
    if (reportAbortedIfAborted()) {
        return;
    }
    decltype(lockToRead()) lock;
    for (std::size_t startedDownloads = 0; startedDownloads < maxParallelDownloads;) {
        if (!lock.owns_lock()) {
            lock = lockToRead();
        }
        const auto *const packageName = downloadsSession->getCurrentPackageNameIfValidAndRelevantAndSelectNext();
        if (!packageName) {
            return;
        }
        auto &packageProgress = m_buildProgress.progressByPackage[*packageName];
        if (!packageProgress.finished.isNull() || packageProgress.hasSources || packageProgress.addedToRepo) {
            continue;
        }

        // prepare the build directory
        const auto &buildData = m_buildPreparation.buildData[*packageName];
        std::filesystem::path buildDirectory;
        try {
            buildDirectory = std::filesystem::absolute(packageProgress.buildDirectory);
            std::filesystem::create_directory(buildDirectory);
            std::filesystem::copy(buildData.sourceDirectory, packageProgress.buildDirectory,
                std::filesystem::copy_options::overwrite_existing | std::filesystem::copy_options::recursive);
        } catch (const std::filesystem::filesystem_error &e) {
            auto writeLock = lockToWrite(lock);
            packageProgress.error = argsToString("unable to prepare build directory: ", e.what());
            writeLock.unlock();
            m_buildAction->log()(Phrases::ErrorMessage, "Unable to prepare build directory for ", *packageName, ": ", e.what(), '\n');
            downloadsSession->addResponse(string(*packageName));
            continue;
        }

        // update checksums if configured
        switch (invokeUpdatePkgSums(downloadsSession, *packageName, packageProgress, buildDirectory.native())) {
        case InvocationResult::Ok:
            // consider the download being started; invokeMakepkgToMakeSourcePackage() will be invoked later by the handler
            startedDownloads += 1;
            continue;
        case InvocationResult::Error:
            // move on to the next package; invokeUpdatePkgSums() has already "recorded" the error
            continue;
        case InvocationResult::Skipped:
            // updating checksums not needed; continue with invokeMakepkgToMakeSourcePackage() directly
            break;
        }

        // launch makepkg to make source package
        switch (invokeMakepkgToMakeSourcePackage(downloadsSession, *packageName, packageProgress, buildDirectory.native())) {
        case InvocationResult::Ok:
            startedDownloads += 1;
            continue;
        case InvocationResult::Error:
            // move on to the next package; invokeMakepkgToMakeSourcePackage() has already "recorded" the error
            continue;
        case InvocationResult::Skipped:
            // the download is not needed after all; should not happen at this point
            break;
        }
    }
}

void ConductBuild::enqueueMakechrootpkg(const BatchProcessingSession::SharedPointerType &makepkgchrootSession, size_t maxParallelInvocations)
{
    if (reportAbortedIfAborted()) {
        return;
    }
    assert(maxParallelInvocations == 1); // FIXME: parallel builds not implemented yet (required unique working copies and locking repo-add)
    for (std::size_t invocations = 0; invocations < maxParallelInvocations;) {
        const auto hasFailuresInPreviousBatches = makepkgchrootSession->hasFailuresInPreviousBatches();
        const auto *const packageName = makepkgchrootSession->getCurrentPackageNameIfValidAndRelevantAndSelectNext();
        if (!packageName) {
            break;
        }
        switch (invokeMakechrootpkg(makepkgchrootSession, *packageName, hasFailuresInPreviousBatches)) {
        case InvocationResult::Ok:
            invocations += 1;
            break;
        case InvocationResult::Error:
            makepkgchrootSession->addResponse(string(*packageName));
            {
                auto lock = lockToRead();
                auto errorMessage = formattedPhraseString(Phrases::ErrorMessage) % "Unable to start build of " % *packageName
                        % formattedPhraseString(Phrases::End) % "->  reason: " % m_buildProgress.progressByPackage[*packageName].error
                    + '\n';
                lock.unlock();
                m_buildAction->log()(std::move(errorMessage));
            }
            break;
        default:;
        }
    }
    const auto lock = lockToRead();
    dumpBuildProgress();
}

bool ConductBuild::checkForFailedDependency(
    const std::string &packageNameToCheck, const std::vector<const std::vector<LibPkg::Dependency> *> &dependencies) const
{
    // check whether any of the specified dependencies are provided by packages which are supposed to be built but failed
    for (const auto &[packageName, buildData] : m_buildPreparation.buildData) {
        // ignore the package we're checking for failed dependencies
        if (packageName == packageNameToCheck) {
            continue;
        }
        // ignore packages which have been added to the repository successfully and treat any other packages as failed
        const auto buildProgress = m_buildProgress.progressByPackage.find(packageName);
        if (buildProgress != m_buildProgress.progressByPackage.end() && buildProgress->second.addedToRepo) {
            continue;
        }
        for (const auto &[packageID, package] : buildData.packages) {
            for (const auto &deps : dependencies) {
                for (const auto &dependency : *deps) {
                    if (package->providesDependency(dependency)) {
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

InvocationResult ConductBuild::invokeUpdatePkgSums(const BatchProcessingSession::SharedPointerType &downloadsSession, const std::string &packageName,
    PackageBuildProgress &packageProgress, const std::string &buildDirectory)
{
    if (!m_updateChecksums || packageProgress.checksumsUpdated) {
        return InvocationResult::Skipped;
    }
    auto processSession = m_buildAction->makeBuildProcess(packageName + " checksum update", packageProgress.buildDirectory + "/updpkgsums.log",
        [this, downloadsSession, &packageProgress, &packageName, buildDirectory](boost::process::child &&child, ProcessResult &&result) {
            const auto hasError = result.errorCode || result.exitCode != 0;
            auto lock = lockToWrite();
            if (result.errorCode) {
                const auto errorMessage = result.errorCode.message();
                packageProgress.error = "unable to update checksums: " + errorMessage;
                lock.unlock();
                m_buildAction->log()(Phrases::ErrorMessage, "Unable to invoke updpkgsums for ", packageName, ": ", errorMessage, '\n');
                downloadsSession->addResponse(string(packageName));
            } else if (result.exitCode != 0) {
                packageProgress.error = argsToString("unable to update checksums: updpkgsums returned with exit code ", result.exitCode);
                lock.unlock();
                m_buildAction->log()(
                    Phrases::ErrorMessage, "updpkgsums invocation for ", packageName, " exited with non-zero exit code: ", child.exit_code(), '\n');
                downloadsSession->addResponse(string(packageName));
            } else {
                packageProgress.checksumsUpdated = true;
                lock.unlock();
            }
            // move on to the next download if an error occurred; otherwise continue making the source package
            if (hasError) {
                enqueueDownloads(downloadsSession, 1);
                return;
            }
            // copy the updated PKGBUILD back to original source directory
            copyPkgbuildToOriginalSourceDirectory(packageName, packageProgress, buildDirectory);
            // continue with the actual build
            switch (invokeMakepkgToMakeSourcePackage(downloadsSession, packageName, packageProgress, buildDirectory)) {
            case InvocationResult::Ok:
                break;
            case InvocationResult::Error:
            case InvocationResult::Skipped:
                // move on to the next package; invokeMakepkgToMakeSourcePackage() has already "recorded" any errors
                enqueueDownloads(downloadsSession, 1);
                break;
            }
        });
    m_buildAction->log()(Phrases::InfoMessage, "Updating checksums of ", packageName, " via ", m_updatePkgSumsPath.string(), '\n',
        ps(Phrases::SubMessage), "build dir: ", buildDirectory, '\n');
    processSession->launch(boost::process::start_dir(buildDirectory), m_updatePkgSumsPath);
    return InvocationResult::Ok;
}

InvocationResult ConductBuild::invokeMakepkgToMakeSourcePackage(const BatchProcessingSession::SharedPointerType &downloadsSession,
    const std::string &packageName, PackageBuildProgress &packageProgress, const std::string &buildDirectory)
{
    auto processSession = m_buildAction->makeBuildProcess(packageName + " download", packageProgress.buildDirectory + "/download.log",
        [this, downloadsSession, &packageProgress, &packageName](boost::process::child &&child, ProcessResult &&result) {
            auto lock = lockToWrite();
            if (result.errorCode) {
                const auto errorMessage = result.errorCode.message();
                packageProgress.error = "unable to make source package: " + errorMessage;
                lock.unlock();
                m_buildAction->log()(Phrases::ErrorMessage, "Unable to make source package for ", packageName, ": ", errorMessage, '\n');
                downloadsSession->addResponse(string(packageName));
            } else if (result.exitCode != 0) {
                packageProgress.error = argsToString("unable to make source package: makepkg returned with exit code ", result.exitCode);
                lock.unlock();
                m_buildAction->log()(Phrases::ErrorMessage, "makepkg invocation to make source package for \"", packageName,
                    "\" exited with non-zero exit code: ", child.exit_code(), '\n');
                downloadsSession->addResponse(string(packageName));
            } else {
                packageProgress.hasSources = true;
                lock.unlock();
            }
            enqueueDownloads(downloadsSession, 1);
        });
    auto additionalFlags = std::vector<std::string>();
    auto lock = lockToRead();
    if (const auto &sourceInfo = m_buildPreparation.buildData[packageName].sourceInfo) {
        for (const auto &source : sourceInfo->sources) {
            if (source.path.find(".git?signed") != std::string::npos) {
                // skip the GPG check at this point as makepkg won't actually clone the repository here
                additionalFlags.emplace_back("--skippgpcheck");
                break;
            }
        }
    }
    lock.unlock();
    m_buildAction->log()(Phrases::InfoMessage, "Making source package for ", packageName, " via ", m_makePkgPath.string(), '\n',
        ps(Phrases::SubMessage), "build dir: ", buildDirectory, '\n');
    if (m_useContainer) {
        processSession->launch(
            boost::process::start_dir(buildDirectory), m_makeContainerPkgPath, "--", "-f", "--nodeps", "--nobuild", "--source", additionalFlags);
    } else {
        processSession->launch(boost::process::start_dir(buildDirectory), m_makePkgPath, "-f", "--nodeps", "--nobuild", "--source", additionalFlags);
    }
    return InvocationResult::Ok;
}

InvocationResult ConductBuild::invokeMakechrootpkg(
    const BatchProcessingSession::SharedPointerType &makepkgchrootSession, const std::string &packageName, bool hasFailuresInPreviousBatches)
{
    auto lock = lockToRead();
    auto &packageProgress = m_buildProgress.progressByPackage[packageName];

    // skip if package has already been added to repo
    if (packageProgress.addedToRepo) {
        // enable staging if auto-staging is enabled and when it has already been determined to be required
        if (m_autoStaging && packageProgress.stagingNeeded == PackageStagingNeeded::Yes) {
            makepkgchrootSession->enableStagingInNextBatch();
        }
        return InvocationResult::Skipped;
    }

    // add package immediately to repository if it has already been built
    if (!packageProgress.finished.isNull()) {
        lock.unlock();
        addPackageToRepo(makepkgchrootSession, packageName, packageProgress);
        return InvocationResult::Ok;
    }

    // check whether we can build this package when building as far as possible or when the build order has been
    // manuall specified (otherwise we don't need to check because we take it as given that all packages in the
    // previous batch have been built and that the order batch compution is correct)
    if ((m_buildAsFarAsPossible || m_buildPreparation.manuallyOrdered) && hasFailuresInPreviousBatches) {
        const auto &buildData = m_buildPreparation.buildData[packageName];
        auto dependencies = std::vector<const std::vector<LibPkg::Dependency> *>();
        dependencies.reserve(buildData.packages.size() + 2);
        dependencies.emplace_back(&buildData.sourceInfo->makeDependencies);
        dependencies.emplace_back(&buildData.sourceInfo->checkDependencies);
        for (const auto &[packageID, package] : buildData.packages) {
            dependencies.emplace_back(&package->dependencies);
        }
        if (checkForFailedDependency(packageName, dependencies)) {
            const auto writeLock = lockToWrite(lock);
            packageProgress.error = "unable to build because dependency failed";
            return InvocationResult::Error;
        }
    }

    // don't start the build if we could not even download the sources
    if (!packageProgress.hasSources) {
        if (packageProgress.error.empty()) {
            const auto writeLock = lockToWrite(lock);
            packageProgress.error = "unable to build because sources are missing";
        }
        return InvocationResult::Error;
    }

    // determine options/variables to pass
    std::vector<std::string> makechrootpkgFlags, makepkgFlags, sudoArgs;
    // -> cleanup/upgrade
    if (!packageProgress.skipChrootCleanup) {
        makechrootpkgFlags.emplace_back("-c");
    }
    if (!packageProgress.skipChrootUpgrade) {
        makechrootpkgFlags.emplace_back("-u");
    }
    // -> ccache support (see https://wiki.archlinux.org/index.php/Ccache#makechrootpkg)
    if (!m_globalCcacheDir.empty()) {
        makechrootpkgFlags.emplace_back("-d");
        makechrootpkgFlags.emplace_back(m_globalCcacheDir + "/:/ccache");
        makepkgFlags.emplace_back("CCACHE_DIR=/ccache");
    }
    // -> directory for testfiles (required by tagparser and tageditor)
    if (!m_globalTestFilesDir.empty()) {
        makechrootpkgFlags.emplace_back("-d");
        makechrootpkgFlags.emplace_back(m_globalTestFilesDir + "/:/testfiles");
        makepkgFlags.emplace_back("TEST_FILE_PATH=/testfiles");
    }
    // -> "sudo …" prefixc to launch build as different user
    if (!m_sudoUser.empty() && !m_sudoPassword.empty()) {
        sudoArgs = { "sudo", "--user", m_sudoUser, "--stdin" };
    }

    // invoke makecontainerpkg instead if container-flag set
    if (m_useContainer) {
        return invokeMakecontainerpkg(makepkgchrootSession, packageName, packageProgress, makepkgFlags);
    }

    // do some sanity checks with the chroot
    const auto chrootDir = packageProgress.chrootDirectory % "/arch-" + m_buildPreparation.targetArch;
    const auto buildRoot = chrootDir % '/' + m_chrootRootUser;
    try {
        if (!std::filesystem::is_directory(buildRoot)) {
            auto writeLock = lockToWrite(lock);
            packageProgress.error = "Chroot directory \"" % buildRoot + "\" is no directory.";
            return InvocationResult::Error;
        }
        if (!std::filesystem::is_regular_file(buildRoot + "/.arch-chroot")) {
            auto writeLock = lockToWrite(lock);
            packageProgress.error = "Chroot directory \"" % buildRoot + "\" does not contain .arch-chroot file.";
            return InvocationResult::Error;
        }
        for (const auto *const dir : { "/usr/bin", "/usr/lib", "/usr/include", "/etc" }) {
            if (std::filesystem::is_directory(buildRoot + dir)) {
                continue;
            }
            auto writeLock = lockToWrite(lock);
            packageProgress.error = "Chroot directory \"" % buildRoot % "\" does not contain directory \"" % dir + "\".";
            return InvocationResult::Error;
        }
    } catch (const std::filesystem::filesystem_error &e) {
        auto writeLock = lockToWrite(lock);
        packageProgress.error = "Unable to check chroot directory \"" % buildRoot % "\": " + e.what();
        return InvocationResult::Error;
    }

    // lock the chroot directory to prevent other build tasks from using it
    m_buildAction->log()(Phrases::InfoMessage, "Building ", packageName, '\n');
    auto chrootLock = m_setup.locks.acquireToWrite(m_buildAction->log(), std::string(buildRoot));

    // copy config files into chroot directory
    try {
        std::filesystem::copy_file(makepkgchrootSession->isStagingEnabled() ? m_pacmanStagingConfigPath : m_pacmanConfigPath,
            buildRoot + "/etc/pacman.conf", std::filesystem::copy_options::overwrite_existing);
        std::filesystem::copy_file(m_makepkgConfigPath, buildRoot + "/etc/makepkg.conf", std::filesystem::copy_options::overwrite_existing);
    } catch (const std::filesystem::filesystem_error &e) {
        chrootLock.lock().unlock();
        const auto writeLock = lockToWrite(lock);
        packageProgress.error = "Unable to configure chroot \"" % buildRoot % "\": " + e.what();
        return InvocationResult::Error;
    }

    // prepare process session (after configuring chroot so we don't get stuck if configuring chroot fails)
    auto processSession = m_buildAction->makeBuildProcess(packageName + " build", packageProgress.buildDirectory + "/build.log",
        std::bind(&ConductBuild::handleMakechrootpkgErrorsAndAddPackageToRepo, this, makepkgchrootSession, std::ref(packageName),
            std::ref(packageProgress), std::placeholders::_1, std::placeholders::_2));
    processSession->registerNewDataHandler(BufferSearch("Updated version: ", "\e\n", "Starting build",
        std::bind(
            &ConductBuild::assignNewVersion, this, std::ref(packageName), std::ref(packageProgress), std::placeholders::_1, std::placeholders::_2)));
    processSession->registerNewDataHandler(BufferSearch("Synchronizing chroot copy", "\n", std::string_view(),
        [processSession = processSession.get()](BufferSearch &, std::string &&) { processSession->locks().pop_back(); }));

    // invoke makechrootpkg to build package
    m_buildAction->log()(Phrases::InfoMessage, "Invoking makechrootpkg for ", packageName, " via ", m_makeChrootPkgPath.string(), '\n',
        ps(Phrases::SubMessage), "build dir: ", packageProgress.buildDirectory, '\n', ps(Phrases::SubMessage), "chroot dir: ", chrootDir, '\n',
        ps(Phrases::SubMessage), "chroot user: ", packageProgress.chrootUser, '\n');
    auto &locks = processSession->locks();
    locks.reserve(2);
    locks.emplace_back(m_setup.locks.acquireToWrite(m_buildAction->log(), chrootDir % '/' + packageProgress.chrootUser));
    locks.emplace_back(std::move(chrootLock));
    processSession->launch(boost::process::start_dir(packageProgress.buildDirectory), m_makeChrootPkgPath, sudoArgs, makechrootpkgFlags, "-C",
        m_globalPackageCacheDir, "-r", chrootDir, "-l", packageProgress.chrootUser, packageProgress.makechrootpkgFlags, "--", makepkgFlags,
        packageProgress.makepkgFlags, boost::process::std_in < boost::asio::buffer(m_sudoPassword));
    return InvocationResult::Ok;
}

InvocationResult ConductBuild::invokeMakecontainerpkg(const BatchProcessingSession::SharedPointerType &makepkgchrootSession,
    const std::string &packageName, PackageBuildProgress &packageProgress, const std::vector<std::string> &makepkgFlags)
{
    // skip initial checks as this function is only supposed to be called from within invokeMakechrootpkg

    // copy config files into chroot directory
    try {
        std::filesystem::copy_file(makepkgchrootSession->isStagingEnabled() ? m_pacmanStagingConfigPath : m_pacmanConfigPath,
            packageProgress.buildDirectory + "/pacman.conf", std::filesystem::copy_options::overwrite_existing);
        std::filesystem::copy_file(
            m_makepkgConfigPath, packageProgress.buildDirectory + "/makepkg.conf", std::filesystem::copy_options::overwrite_existing);
    } catch (const std::filesystem::filesystem_error &e) {
        packageProgress.error = "Unable to copy config files into build directory \"" % packageProgress.buildDirectory % "\": " + e.what();
        return InvocationResult::Error;
    }

    // determine options/variables to pass
    // -> package cache
    auto makecontainerpkgFlags = std::vector<std::string>();
    if (!m_globalPackageCacheDir.empty()) {
        makecontainerpkgFlags.emplace_back("-v");
        makecontainerpkgFlags.emplace_back(m_globalPackageCacheDir + "/:/var/cache/pacman/pkg/");
    }
    // -> ccache support (see https://wiki.archlinux.org/index.php/Ccache#makechrootpkg)
    if (!m_globalCcacheDir.empty()) {
        makecontainerpkgFlags.emplace_back("-v");
        makecontainerpkgFlags.emplace_back(m_globalCcacheDir + "/:/ccache");
    }
    // -> directory for testfiles (required by tagparser and tageditor)
    if (!m_globalTestFilesDir.empty()) {
        makecontainerpkgFlags.emplace_back("-v");
        makecontainerpkgFlags.emplace_back(m_globalTestFilesDir + "/:/testfiles");
    }

    // prepare process session
    auto processSession = m_buildAction->makeBuildProcess(packageName + " build", packageProgress.buildDirectory + "/build.log",
        std::bind(&ConductBuild::handleMakechrootpkgErrorsAndAddPackageToRepo, this, makepkgchrootSession, std::ref(packageName),
            std::ref(packageProgress), std::placeholders::_1, std::placeholders::_2));
    processSession->registerNewDataHandler(BufferSearch("Updated version: ", "\e\n", "Starting build",
        std::bind(
            &ConductBuild::assignNewVersion, this, std::ref(packageName), std::ref(packageProgress), std::placeholders::_1, std::placeholders::_2)));

    // invoke makechrootpkg to build package
    m_buildAction->log()(Phrases::InfoMessage, "Building ", packageName, " within container via ", m_makeContainerPkgPath.string(), '\n',
        ps(Phrases::SubMessage), "build dir: ", packageProgress.buildDirectory, '\n');
    processSession->launch(boost::process::start_dir(packageProgress.buildDirectory), m_makeContainerPkgPath, makecontainerpkgFlags, "--",
        makepkgFlags, packageProgress.makepkgFlags);
    return InvocationResult::Ok;
}

void ConductBuild::addPackageToRepo(
    const BatchProcessingSession::SharedPointerType &makepkgchrootSession, const string &packageName, PackageBuildProgress &packageProgress)
{
    // make arrays to store binary package names
    auto binaryPackages = std::vector<BinaryPackageInfo>{};

    // determine name of source package to be copied
    auto buildResult = BuildResult{};
    auto readLock = lockToRead();
    const auto &buildData = m_buildPreparation.buildData[packageName];
    const auto &firstPackage = buildData.packages.front().pkg;
    auto sourcePackageName = packageName % '-' % firstPackage->version + m_sourcePackageExtension;

    // determine names of binary packages to be copied
    binaryPackages.reserve(buildData.packages.size());
    buildResult.binaryPackageNames.reserve(buildData.packages.size());
    for (const auto &[packageID, package] : buildData.packages) {
        const auto isAny = package->isArchAny();
        const auto &arch = isAny ? "any" : m_buildPreparation.targetArch;
        const auto &packageFileName = buildResult.binaryPackageNames.emplace_back(
            package->name % '-' % (packageProgress.updatedVersion.empty() ? package->version : packageProgress.updatedVersion) % '-' % arch
            + m_binaryPackageExtension);
        binaryPackages.emplace_back(
            BinaryPackageInfo{ package->name, packageFileName, packageProgress.buildDirectory % '/' + packageFileName, isAny, false });
    }

    // check whether all packages exists
    auto missingPackages = std::vector<std::string>{};
    try {
        if (!std::filesystem::is_regular_file(packageProgress.buildDirectory % '/' + sourcePackageName)) {
            missingPackages.emplace_back(std::move(sourcePackageName));
        }
        for (auto &binaryPackage : binaryPackages) {
            if (!std::filesystem::is_regular_file(binaryPackage.path)) {
                missingPackages.emplace_back(std::move(binaryPackage.fileName));
            }
        }
    } catch (const std::filesystem::filesystem_error &e) {
        auto writeLock = lockToWrite(readLock);
        packageProgress.error = argsToString("unable to check whether resulting package exist: ", e.what());
        dumpBuildProgress();
        writeLock.unlock();
        m_buildAction->log()(Phrases::ErrorMessage, "Unable to check resulting package for ", packageName, ": ", e.what());
        makepkgchrootSession->addResponse(string(packageName));
        enqueueMakechrootpkg(makepkgchrootSession, 1);
        return;
    }
    if (!missingPackages.empty()) {
        const auto missingBinaryPackagesJoined = joinStrings(missingPackages, ", ");
        auto writeLock = lockToWrite(readLock);
        packageProgress.error = "not all source/binary packages exist after the build as expected: " + missingBinaryPackagesJoined;
        dumpBuildProgress();
        writeLock.unlock();
        m_buildAction->log()(
            Phrases::ErrorMessage, "Not all source/binary packages exist after building ", packageName, ": ", missingBinaryPackagesJoined, '\n');
        makepkgchrootSession->addResponse(string(packageName));
        enqueueMakechrootpkg(makepkgchrootSession, 1);
        return;
    }

    // check whether staging is needed
    // note: Calling checkWhetherStagingIsNeededAndPopulateRebuildList() in the condition first do prevent short-circuit evaluation. We always
    //       want to do the check in order to populate the rebuild list - even if staging is already enabled anyways.
    buildResult.needsStaging = makepkgchrootSession->isStagingEnabled();
    try {
        if (packageProgress.stagingNeeded != PackageStagingNeeded::No) {
            packageProgress.stagingNeeded = checkWhetherStagingIsNeededAndPopulateRebuildList(packageName, buildData, binaryPackages);
            if (packageProgress.stagingNeeded == PackageStagingNeeded::Yes) {
                buildResult.needsStaging = true;
                makepkgchrootSession->enableStagingInNextBatch();
            }
        }
    } catch (const std::runtime_error &e) {
        auto writeLock = lockToWrite(readLock);
        packageProgress.error = argsToString("unable to determine whether staging is needed: ", e.what());
        dumpBuildProgress();
        writeLock.unlock();
        m_buildAction->log()(Phrases::ErrorMessage, "Unable to determine whether staging of ", packageName, " is needed: ", e.what(), '\n');
        makepkgchrootSession->addResponse(string(packageName));
        enqueueMakechrootpkg(makepkgchrootSession, 1);
        return;
    }

    // add artefacts
    // -> make paths and check whether these are already present from previous runs
    const auto sourcePackagePath = packageProgress.buildDirectory % '/' + sourcePackageName;
    bool sourcePackageArtefactAlreadyPresent = false;
    for (const auto &artefact : m_buildAction->artefacts) {
        if (artefact == sourcePackagePath) {
            sourcePackageArtefactAlreadyPresent = true;
            break;
        }
    }
    bool binaryPackageArtefactsAlreadyPresent = true;
    for (auto &binaryPackage : binaryPackages) {
        binaryPackage.path = packageProgress.buildDirectory % '/' + binaryPackage.fileName;
        for (const auto &artefact : m_buildAction->artefacts) {
            if (artefact == binaryPackage.path) {
                binaryPackage.artefactAlreadyPresent = true;
                break;
            }
            if (!binaryPackage.artefactAlreadyPresent) {
                binaryPackageArtefactsAlreadyPresent = false;
            }
        }
    }
    // -> add missing artefacts
    if (!sourcePackageArtefactAlreadyPresent || !binaryPackageArtefactsAlreadyPresent) {
        const auto buildActionsWriteLock = m_setup.building.lockToWrite();
        if (!sourcePackageArtefactAlreadyPresent) {
            m_buildAction->artefacts.emplace_back(sourcePackagePath);
        }
        for (const auto &binaryPackage : binaryPackages) {
            if (!binaryPackage.artefactAlreadyPresent) {
                m_buildAction->artefacts.emplace_back(binaryPackage.path);
            }
        }
    }

    // copy source and binary packages
    buildResult.repoPath = buildResult.needsStaging ? &m_buildProgress.stagingRepoPath : &m_buildProgress.targetRepoPath;
    buildResult.dbFilePath = buildResult.needsStaging ? &m_buildProgress.stagingDbFilePath : &m_buildProgress.targetDbFilePath;
    try {
        auto anyRepoPath = std::filesystem::path();
        const auto sourceRepoPath = std::filesystem::path(argsToString(buildResult.repoPath, "/../src/"));
        std::filesystem::create_directories(sourceRepoPath);
        std::filesystem::copy(sourcePackagePath, sourceRepoPath / sourcePackageName, std::filesystem::copy_options::overwrite_existing);
        for (const auto &binaryPackage : binaryPackages) {
            if (!binaryPackage.isAny) {
                std::filesystem::copy(
                    binaryPackage.path, *buildResult.repoPath % '/' + binaryPackage.fileName, std::filesystem::copy_options::overwrite_existing);
                continue;
            }
            if (anyRepoPath.empty()) {
                std::filesystem::create_directories(anyRepoPath = argsToString(buildResult.repoPath, "/../any"));
            }
            std::filesystem::copy(binaryPackage.path, anyRepoPath / binaryPackage.fileName, std::filesystem::copy_options::overwrite_existing);
            const auto symlink = std::filesystem::path(*buildResult.repoPath % '/' + binaryPackage.fileName);
            std::filesystem::remove(symlink);
            std::filesystem::create_symlink("../any/" + binaryPackage.fileName, symlink);
        }
    } catch (const std::filesystem::filesystem_error &e) {
        auto writeLock = lockToWrite(readLock);
        packageProgress.error = argsToString("unable to copy package to destination repository: ", e.what());
        dumpBuildProgress();
        writeLock.unlock();
        m_buildAction->log()(Phrases::ErrorMessage, "Unable to copy package to destination repository: ", e.what(), '\n');
        makepkgchrootSession->addResponse(string(packageName));
        enqueueMakechrootpkg(makepkgchrootSession, 1);
        return;
    }
    readLock.unlock();

    if (!m_gpgKey.empty()) {
        // sign package before adding to repository if GPG key specified
        invokeGpg(makepkgchrootSession, packageName, packageProgress, std::move(binaryPackages), std::move(buildResult));
    } else {
        invokeRepoAdd(makepkgchrootSession, packageName, packageProgress, std::move(buildResult));
    }
}

void ConductBuild::invokeGpg(const BatchProcessingSession::SharedPointerType &makepkgchrootSession, const string &packageName,
    PackageBuildProgress &packageProgress, std::vector<BinaryPackageInfo> &&binaryPackages, BuildResult &&buildResult)
{
    const auto signingSession = std::make_shared<SigningSession>(std::move(binaryPackages), buildResult.repoPath, m_setup.building.ioContext,
        [this, makepkgchrootSession, &packageName, &packageProgress, buildResult = std::move(buildResult)](
            MultiSession<std::string>::ContainerType &&failedPackages) mutable {
            checkGpgErrorsAndContinueAddingPackagesToRepo(
                makepkgchrootSession, packageName, packageProgress, std::move(buildResult), std::move(failedPackages));
        });
    constexpr auto gpgParallelLimit = 4;
    const auto lock = std::unique_lock<std::mutex>(signingSession->mutex);
    for (auto i = 0; i != gpgParallelLimit && signingSession->currentPackage != signingSession->binaryPackages.end();
         ++i, ++signingSession->currentPackage) {
        invokeGpg(signingSession, packageName, packageProgress);
    }
}

void ConductBuild::invokeGpg(
    const std::shared_ptr<SigningSession> &signingSession, const std::string &packageName, PackageBuildProgress &packageProgress)
{
    const auto &binaryPackage = *signingSession->currentPackage;
    auto processSession = m_buildAction->makeBuildProcess("gpg for " + binaryPackage.name,
        packageProgress.buildDirectory % "/gpg-" % binaryPackage.name + ".log",
        [this, signingSession, &packageName, &packageProgress, isAny = binaryPackage.isAny, binaryPackageName = binaryPackage.fileName](
            boost::process::child &&child, ProcessResult &&result) mutable {
            // make the next gpg invocation
            if (const auto lock = std::unique_lock<std::mutex>(signingSession->mutex);
                signingSession->currentPackage != signingSession->binaryPackages.end()
                && ++signingSession->currentPackage != signingSession->binaryPackages.end()) {
                invokeGpg(signingSession, packageName, packageProgress);
            }

            // handle results of gpg invocation
            if (result.errorCode) {
                // check for invocation error
                m_buildAction->log()(Phrases::ErrorMessage, "Unable to invoke gpg for ", binaryPackageName, ": ", result.errorCode.message(), '\n');
            } else if (child.exit_code() != 0) {
                // check for bad exit code
                m_buildAction->log()(
                    Phrases::ErrorMessage, "gpg invocation for ", binaryPackageName, " exited with non-zero exit code: ", child.exit_code(), '\n');
            } else {
                // move signature to repository
                try {
                    const auto buildDirSignaturePath
                        = std::filesystem::path(argsToString(packageProgress.buildDirectory, '/', binaryPackageName, ".sig"));
                    if (!std::filesystem::exists(buildDirSignaturePath)) {
                        m_buildAction->log()(Phrases::ErrorMessage, "Signature of \"", binaryPackageName,
                            "\" could not be created: ", buildDirSignaturePath, " does not exist after invoking gpg\n");
                    } else if (!isAny) {
                        std::filesystem::copy(buildDirSignaturePath, *signingSession->repoPath % '/' % binaryPackageName + ".sig",
                            std::filesystem::copy_options::overwrite_existing);
                        return;
                    } else {
                        std::filesystem::copy(buildDirSignaturePath, argsToString(signingSession->repoPath, "/../any/", binaryPackageName, ".sig"),
                            std::filesystem::copy_options::overwrite_existing);
                        const auto symlink = std::filesystem::path(argsToString(signingSession->repoPath, '/', binaryPackageName, ".sig"));
                        std::filesystem::remove(symlink);
                        std::filesystem::create_symlink("../any/" % binaryPackageName + ".sig", symlink);
                        return;
                    }
                } catch (const std::filesystem::filesystem_error &e) {
                    m_buildAction->log()(
                        Phrases::ErrorMessage, "Unable to copy signature of \"", binaryPackageName, "\" to repository: ", e.what(), '\n');
                }
            }

            // consider the package failed
            signingSession->addResponse(std::move(binaryPackageName));
        });
    auto pinentryArgs = std::vector<std::string>();
    if (!m_gpgPassphrase.empty()) {
        pinentryArgs = { "--pinentry-mode", "loopback", "--passphrase-fd", "0" };
    }
    processSession->launch(boost::process::start_dir(packageProgress.buildDirectory), m_gpgPath, pinentryArgs, "--detach-sign", "--yes",
        "--use-agent", "--no-armor", "-u", m_gpgKey, binaryPackage.fileName, boost::process::std_in < boost::asio::buffer(m_gpgPassphrase));
    m_buildAction->log()(Phrases::InfoMessage, "Signing ", binaryPackage.fileName, '\n');
}

void ConductBuild::invokeRepoAdd(const BatchProcessingSession::SharedPointerType &makepkgchrootSession, const string &packageName,
    PackageBuildProgress &packageProgress, BuildResult &&buildResult)
{
    auto processSession = m_buildAction->makeBuildProcess("repo-add for " + packageName, packageProgress.buildDirectory + "/repo-add.log",
        std::bind(&ConductBuild::handleRepoAddErrorsAndMakeNextPackage, this, makepkgchrootSession, std::ref(packageName), std::ref(packageProgress),
            std::placeholders::_1, std::placeholders::_2));
    processSession->locks().emplace_back(m_setup.locks.acquireToWrite(m_buildAction->log(),
        ServiceSetup::Locks::forDatabase(
            buildResult.needsStaging ? m_buildPreparation.stagingDb : m_buildPreparation.targetDb, m_buildPreparation.targetArch)));
    processSession->launch(boost::process::start_dir(*buildResult.repoPath), m_repoAddPath, *buildResult.dbFilePath, buildResult.binaryPackageNames);
    m_buildAction->log()(Phrases::InfoMessage, "Adding ", packageName, " to repo\n", ps(Phrases::SubMessage), "repo path: ", buildResult.repoPath,
        '\n', ps(Phrases::SubMessage), "db path: ", buildResult.dbFilePath, '\n', ps(Phrases::SubMessage),
        "package(s): ", joinStrings(buildResult.binaryPackageNames), '\n');
}

void ConductBuild::checkDownloadErrorsAndMakePackages(BatchProcessingSession::ContainerType &&failedPackages)
{
    // dump progress
    auto readLock = lockToRead();
    dumpBuildProgress();
    readLock.unlock();

    // validate whether we have all sources
    if (!failedPackages.empty()) {
        const auto failedPackagesStr = joinStrings(failedPackages, ", ");
        if (!m_buildAsFarAsPossible) {
            reportError("failed to download sources: " + failedPackagesStr);
            return;
        }
        m_buildAction->log()(Phrases::WarningMessage, "Ignoring download failures: ", failedPackagesStr, '\n');
        // note: Packages without sources will be treated later as errors even before invoking makechrootpkg. At this point there's nothing to do
        //       for ignoring the packages.
    }

    if (reportAbortedIfAborted()) {
        return;
    }

    // enqueue building the first package
    constexpr std::size_t maxParallelInvocations = 1;
    enqueueMakechrootpkg(make_shared<BatchProcessingSession>(m_relevantPackages, m_buildPreparation.batches, m_setup.building.ioContext,
                             std::bind(&ConductBuild::checkBuildErrors, this, std::placeholders::_1), !m_buildAsFarAsPossible),
        maxParallelInvocations);
}

void ConductBuild::checkGpgErrorsAndContinueAddingPackagesToRepo(const BatchProcessingSession::SharedPointerType &makepkgchrootSession,
    const std::string &packageName, PackageBuildProgress &packageProgress, BuildResult &&buildResult,
    MultiSession<std::string>::ContainerType &&failedPackages)
{
    if (!failedPackages.empty()) {
        auto lock = lockToWrite();
        packageProgress.error = argsToString("failed to sign packages: ", joinStrings(failedPackages, ", "));
        dumpBuildProgress();
        lock.unlock();
        makepkgchrootSession->addResponse(std::string(packageName));
        enqueueMakechrootpkg(makepkgchrootSession, 1);
        return;
    }
    if (!reportAbortedIfAborted()) {
        invokeRepoAdd(makepkgchrootSession, packageName, packageProgress, std::move(buildResult));
    }
}

static void assignNewChrootUser(std::string &&newChrootUser, PackageBuildProgress &packageProgress)
{
    if (newChrootUser.empty()) {
        return;
    }
    packageProgress.skipChrootCleanup = packageProgress.skipChrootUpgrade = true;
    packageProgress.chrootUser = std::move(newChrootUser);
}

void ConductBuild::handleMakechrootpkgErrorsAndAddPackageToRepo(const BatchProcessingSession::SharedPointerType &makepkgchrootSession,
    const string &packageName, PackageBuildProgress &packageProgress, boost::process::child &&child, ProcessResult &&result)
{
    // check for makechrootpkg error
    const auto hasError = result.errorCode || child.exit_code() != 0;
    // -> save chroot directory
    auto newChrootUser = std::string();
    if (hasError && m_saveChrootDirsOfFailures) {
        // rename the chroot working copy directory from e.g. buildservice to buildservice-somepkg; append number if already present
        const auto chrootSuffix = '-' + packageName;
        auto lock = lockToRead();
        if (!endsWith(packageProgress.chrootUser, chrootSuffix)) {
            auto assignNewChrootUser = [&newChrootUser, &packageProgress, &chrootSuffix, attempt = 0u]() mutable {
                newChrootUser = attempt++ ? (packageProgress.chrootUser % chrootSuffix % '-' + attempt) : (packageProgress.chrootUser + chrootSuffix);
            };
            assignNewChrootUser();
            try {
                const auto chrootPathStart = packageProgress.chrootDirectory % "/arch-" % m_buildPreparation.targetArch % '/';
                auto newChrootPath = std::filesystem::path(chrootPathStart + newChrootUser);
                while (std::filesystem::exists(newChrootPath)) {
                    assignNewChrootUser();
                    newChrootPath = chrootPathStart + newChrootUser;
                }
                std::filesystem::rename(chrootPathStart + packageProgress.chrootUser, chrootPathStart + newChrootUser);
            } catch (const std::filesystem::filesystem_error &e) {
                lock.unlock();
                newChrootUser.clear();
                m_buildAction->log()(Phrases::ErrorMessage, "Unable to rename chroot directory for ", packageName, ':', ' ', e.what(), '\n');
            }
        }
    }
    // -> add error message
    auto lock = lockToWrite();
    if (result.errorCode) {
        const auto errorMessage = result.errorCode.message();
        packageProgress.error = "unable to build: " + errorMessage;
        assignNewChrootUser(std::move(newChrootUser), packageProgress);
        dumpBuildProgress();
        lock.unlock();
        m_buildAction->log()(Phrases::ErrorMessage, "Unable to invoke makechrootpkg for ", packageName, ": ", errorMessage, '\n');
    }
    if (child.exit_code() != 0) {
        packageProgress.error = argsToString("unable to build: makechrootpkg returned with exit code ", child.exit_code());
        assignNewChrootUser(std::move(newChrootUser), packageProgress);
        dumpBuildProgress();
        lock.unlock();
        m_buildAction->log()(
            Phrases::ErrorMessage, "makechrootpkg invocation for ", packageName, " exited with non-zero exit code: ", child.exit_code(), '\n');
    }
    if (hasError) {
        makepkgchrootSession->addResponse(string(packageName));
        enqueueMakechrootpkg(makepkgchrootSession, 1);
        return;
    }

    // set "finished" to record the package has been built successfully
    packageProgress.finished = DateTime::gmtNow();
    lock.unlock();

    addPackageToRepo(makepkgchrootSession, packageName, packageProgress);
}

void ConductBuild::handleRepoAddErrorsAndMakeNextPackage(const BatchProcessingSession::SharedPointerType &makepkgchrootSession,
    const string &packageName, PackageBuildProgress &packageProgress, boost::process::child &&child, ProcessResult &&result)
{
    // handle repo-add error; update build progress JSON after each package
    auto lock = lockToWrite();
    if (result.errorCode) {
        const auto errorMessage = result.errorCode.message();
        packageProgress.error = "unable to add package to repo: " + errorMessage;
        dumpBuildProgress();
        lock.unlock();
        m_buildAction->log()(Phrases::ErrorMessage, "Unable to invoke repo-add for ", packageName, ": ", errorMessage, '\n');
        makepkgchrootSession->addResponse(string(packageName));
    } else if (child.exit_code() != 0) {
        packageProgress.error = argsToString("unable to add package to repo: repo-add returned with exit code ", child.exit_code());
        dumpBuildProgress();
        lock.unlock();
        m_buildAction->log()(
            Phrases::ErrorMessage, "repo-add invocation for ", packageName, " exited with non-zero exit code: ", child.exit_code(), '\n');
        makepkgchrootSession->addResponse(std::string(packageName));
    } else {
        packageProgress.addedToRepo = true;
        dumpBuildProgress();
        lock.unlock();
    }

    // make next package
    enqueueMakechrootpkg(makepkgchrootSession, 1);
}

void ConductBuild::checkBuildErrors(BatchProcessingSession::ContainerType &&failedPackages)
{
    // check whether build errors occurred
    if (!failedPackages.empty()) {
        reportError("failed to build packages: " + joinStrings(failedPackages, ", "));
        return;
    }

    auto buildActionsWriteLock = m_setup.building.lockToWrite();
    reportSuccess();
}

void ConductBuild::dumpBuildProgress()
{
    try {
        dumpJsonDocument(
            [this] { return m_buildProgress.toJsonDocument(); }, m_workingDirectory, buildProgressFileName, DumpJsonExistingFileHandling::Override);
#ifdef CPP_UTILITIES_DEBUG_BUILD
        m_buildAction->appendOutput(Phrases::InfoMessage, "Updated ", buildProgressFileName, ".json\n");
#endif
    } catch (const std::runtime_error &e) {
        m_buildAction->appendOutput(Phrases::ErrorMessage, e.what(), '\n');
    }
}

void ConductBuild::addLogFile(std::string &&logFilePath)
{
    // skip if log file already present from previous run
    auto buildActionsReadLock = m_setup.building.lockToRead();
    for (const auto &logFile : m_buildAction->logfiles) {
        if (logFile == logFilePath) {
            return;
        }
    }

    const auto buildActionsWriteLock = m_setup.building.lockToWrite(buildActionsReadLock);
    m_buildAction->logfiles.emplace_back(std::move(logFilePath));
}

void ConductBuild::assignNewVersion(
    const std::string &packageName, PackageBuildProgress &packageProgress, BufferSearch &, std::string &&updatedVersionInfo)
{
    auto updatedVersionInfoParts = splitString(updatedVersionInfo, " ", EmptyPartsTreat::Omit);
    if (updatedVersionInfoParts.empty()) {
        return;
    }
    auto &newVersion = updatedVersionInfoParts.back();
    m_buildAction->appendOutput(Phrases::InfoMessage, "Version of \"", packageName, "\" has been updated to: ", newVersion, '\n');
    const auto lock = lockToWrite();
    packageProgress.updatedVersion = std::move(newVersion);
}

void ConductBuild::copyPkgbuildToOriginalSourceDirectory(
    const string &packageName, PackageBuildProgress &packageProgress, const string &buildDirectory)
{
    const auto &buildData = m_buildPreparation.buildData[packageName];
    const auto &originalSourceDirectory = buildData.originalSourceDirectory;
    if (originalSourceDirectory.empty()) {
        const auto lock = lockToWrite();
        packageProgress.warnings.emplace_back("Unable to copy back updated PKGBUILD: original source directory empty");
        return;
    }
    try {
        std::filesystem::copy(buildDirectory + "/PKGBUILD", originalSourceDirectory + "/PKGBUILD", std::filesystem::copy_options::overwrite_existing);
    } catch (const std::filesystem::filesystem_error &e) {
        const auto lock = lockToWrite();
        packageProgress.warnings.emplace_back(argsToString("Unable to copy back updated PKGBUILD: ", e.what()));
    }
}

PackageStagingNeeded ConductBuild::checkWhetherStagingIsNeededAndPopulateRebuildList(
    const std::string &packageName, const PackageBuildData &buildData, const std::vector<BinaryPackageInfo> &builtPackages)
{
    CPP_UTILITIES_UNUSED(buildData)

    // skip if auto-staging is disabled
    if (!m_autoStaging) {
        return PackageStagingNeeded::Undetermined;
    }

    m_buildAction->log()(Phrases::InfoMessage, "Checking whether staging of ", packageName, " is needed\n");
    LibPkg::DependencySet removedProvides, addedProvides;
    std::unordered_set<std::string> removedLibProvides, addedLibProvides;
    auto needsStaging = false;

    // check for existing packages (which would be replaced/shadowed by adding the newly built packages) in the destination repository
    // -> make list of provides would be removed when adding the newly built package to the repo
    auto affectedDbName = std::string_view{};
    auto configLock = m_setup.config.lockToRead();
    for (const auto &[dbName, dbConfig] : m_buildPreparation.dbConfig) {
        auto *const db = m_setup.config.findDatabase(dbName, m_buildPreparation.targetArch);
        if (!db) {
            throw std::runtime_error("Configured database \"" % dbName + "\" has been removed.");
        }
        for (const auto &builtPackage : builtPackages) {
            auto existingPackage = db->findPackage(builtPackage.name);
            if (!existingPackage) {
                continue;
            }
            LibPkg::Package::exportProvides(existingPackage, removedProvides, removedLibProvides);
            if (affectedDbName.empty()) {
                affectedDbName = dbName;
            }
        }
    }
    configLock.unlock();

    // parse built packages
    // -> make list of provides which would be added when adding the newly built package to the repo
    for (const auto &builtPackage : builtPackages) {
        LibPkg::Package::exportProvides(LibPkg::Package::fromPkgFile(builtPackage.path), addedProvides, addedLibProvides);
    }

    // reduce list of removed provides if they are also provided by the newly built package
    std::erase_if(removedProvides, [&addedProvides](const auto &provide) { return addedProvides.provides(provide.first, provide.second); });
    std::erase_if(removedLibProvides, [&addedLibProvides](const auto &provide) { return addedLibProvides.find(provide) != addedLibProvides.end(); });

    // populate m_buildProgress.producedProvides and m_buildProgress.removedProvides (which only serve informal purposes)
    std::lock_guard rebuildListLock(m_rebuildListMutex);
    m_buildProgress.producedProvides[packageName].replace(addedProvides, addedLibProvides);
    m_buildProgress.removedProvides[packageName].replace(removedProvides, removedLibProvides);

    // skip any further checks if nothing will be removed anyways
    if (removedProvides.empty() && removedLibProvides.empty()) {
        m_buildAction->log()(Phrases::SubMessage, "no: Adding the package would not replace any present provides\n");
        return PackageStagingNeeded::No;
    }

    // find all relevant databases to check whether any of the removed provides are needed by other packages
    configLock = m_setup.config.lockToRead();
    auto *const affectedDb = m_setup.config.findDatabase(affectedDbName, m_buildPreparation.targetArch);
    if (!affectedDb) {
        throw std::runtime_error("Affected database \"" % affectedDbName + "\" has been removed.");
    }
    const auto relevantDbs = m_setup.config.computeDatabasesRequiringDatabase(*affectedDb);
    m_buildAction->log()(Phrases::SubMessage,
        "checking the following databases for affected packages: ", joinStrings(names<std::vector<std::string>>(relevantDbs), ", "), '\n');
    auto listOfAffectedPackages = std::vector<std::string>();
    const auto isPackageWeWantToUpdateItself = [&builtPackages](const LibPkg::Package &affectedPackage) {
        return std::find_if(builtPackages.begin(), builtPackages.end(), [affectedPackage](const auto &p) { return p.name == affectedPackage.name; })
            != builtPackages.end();
    };
    for (const auto &db : relevantDbs) {
        const auto isDestinationDb = db->name == m_buildPreparation.targetDb && db->arch == m_buildPreparation.targetArch;
        RebuildInfoByPackage *rebuildInfoForDb = nullptr;
        for (const auto &removedDependency : removedProvides) {
            const auto &removedDependencyName = removedDependency.first;
            const auto &removedDependencyDetail = removedDependency.second;
            db->providingPackages(LibPkg::Dependency(removedDependencyName, removedDependencyDetail.version, removedDependencyDetail.mode), true,
                [&](LibPkg::StorageID, const std::shared_ptr<LibPkg::Package> &affectedPackage) {
                    if (isDestinationDb && isPackageWeWantToUpdateItself(*affectedPackage)) {
                        return false; // skip if that's just the package we want to update itself
                    }
                    if (!rebuildInfoForDb) {
                        rebuildInfoForDb = &m_buildProgress.rebuildList[db->name];
                    }
                    needsStaging = true;
                    (*rebuildInfoForDb)[affectedPackage->name].provides.emplace_back(
                        removedDependencyName, removedDependencyDetail.version, removedDependencyDetail.mode);
                    listOfAffectedPackages.emplace_back(db->name % '/' + affectedPackage->name);
                    return false;
                });
        }
        for (const auto &removedLibProvide : removedLibProvides) {
            db->providingPackages(removedLibProvide, true, [&](LibPkg::StorageID, const std::shared_ptr<LibPkg::Package> &affectedPackage) {
                if (isDestinationDb && isPackageWeWantToUpdateItself(*affectedPackage)) {
                    return false; // skip if that's just the package we want to update itself
                }
                if (!rebuildInfoForDb) {
                    rebuildInfoForDb = &m_buildProgress.rebuildList[db->name];
                }
                needsStaging = true;
                (*rebuildInfoForDb)[affectedPackage->name].libprovides.emplace_back(removedLibProvide);
                listOfAffectedPackages.emplace_back(db->name % '/' + affectedPackage->name);
                return false;
            });
        }
    }

    if (needsStaging) {
        m_buildAction->log()(Phrases::SubMessage, "yes: Other packages need to be re-built:", formattedPhraseString(Phrases::End),
            joinStrings(listOfAffectedPackages, "\n", false, "      - "), '\n');
    } else {
        m_buildAction->log()(
            Phrases::SubMessage, "no: The package replaces another one but it doesn't affect other packages", formattedPhraseString(Phrases::End));
    }
    return needsStaging ? PackageStagingNeeded::Yes : PackageStagingNeeded::No;
}

} // namespace LibRepoMgr
