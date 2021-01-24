#include "./database.h"
#include "./config.h"

#include "reflection/database.h"

#include <c++utilities/conversion/stringbuilder.h>

using namespace std;
using namespace CppUtilities;

namespace LibPkg {

void LibPkg::Database::deducePathsFromLocalDirs()
{
    if (localDbDir.empty()) {
        return;
    }
    if (path.empty()) {
        path = localDbDir % '/' % name + ".db";
    }
    if (filesPath.empty()) {
        filesPath = localDbDir % '/' % name + ".files";
    }
}

void Database::resetConfiguration()
{
    path.clear();
    filesPath.clear();
    mirrors.clear();
    usage = DatabaseUsage::None;
    signatureLevel = SignatureLevel::Default;
    arch = "x86_64";
    dependencies.clear();
    localPkgDir.clear();
    localDbDir.clear();
    syncFromMirror = false;
}

void Database::clearPackages()
{
    packages.clear();
    providedLibs.clear();
    requiredLibs.clear();
    providedDeps.clear();
    requiredDeps.clear();
    lastUpdate = CppUtilities::DateTime::gmtNow();
}

std::vector<std::shared_ptr<Package>> Database::findPackages(const std::function<bool(const Database &, const Package &)> &pred)
{
    std::vector<std::shared_ptr<Package>> pkgs;
    for (const auto &pkg : packages) {
        if (pred(*this, *pkg.second)) {
            pkgs.emplace_back(pkg.second);
        }
    }
    return pkgs;
}

void Database::removePackageDependencies(PackageMap::const_iterator packageIterator)
{
    const auto &package = packageIterator->second;
    providedDeps.remove(Dependency(package->name, package->version), package);
    for (const auto &dep : package->provides) {
        providedDeps.remove(dep, package);
    }
    for (const auto &dep : package->dependencies) {
        requiredDeps.remove(dep, package);
    }
    for (const auto &dep : package->optionalDependencies) {
        requiredDeps.remove(dep, package);
    }
    for (const auto &lib : package->libprovides) {
        const auto iterator(providedLibs.find(lib));
        if (iterator == providedLibs.end()) {
            continue;
        }
        auto &relevantPackages(iterator->second);
        relevantPackages.erase(remove(relevantPackages.begin(), relevantPackages.end(), package), relevantPackages.end());
        if (relevantPackages.empty()) {
            providedLibs.erase(iterator);
        }
    }
    for (const auto &lib : package->libdepends) {
        const auto iterator(requiredLibs.find(lib));
        if (iterator == requiredLibs.end()) {
            continue;
        }
        auto &relevantPackages(iterator->second);
        relevantPackages.erase(remove(relevantPackages.begin(), relevantPackages.end(), package), relevantPackages.end());
        if (relevantPackages.empty()) {
            requiredLibs.erase(iterator);
        }
    }
}

void Database::addPackageDependencies(const std::shared_ptr<Package> &package)
{
    providedDeps.add(Dependency(package->name, package->version), package);
    for (const auto &dep : package->provides) {
        providedDeps.add(dep, package);
    }
    for (const auto &dep : package->dependencies) {
        requiredDeps.add(dep, package);
    }
    for (const auto &dep : package->optionalDependencies) {
        requiredDeps.add(dep, package);
    }
    for (const auto &lib : package->libprovides) {
        providedLibs[lib].emplace_back(package);
    }
    for (const auto &lib : package->libdepends) {
        requiredLibs[lib].emplace_back(package);
    }
}

void Database::removePackage(const std::string &packageName)
{
    const auto packageIterator = packages.find(packageName);
    if (packageIterator == packages.end()) {
        return;
    }
    removePackage(packageIterator);
}

void LibPkg::Database::removePackage(PackageMap::const_iterator packageIterator)
{
    removePackageDependencies(packageIterator);
    packages.erase(packageIterator);
}

void Database::updatePackage(const std::shared_ptr<Package> &package)
{
    // check whether the package already exists
    const auto packageIterator = packages.find(package->name);
    if (packageIterator != packages.end()) {
        const auto &existingPackage = packageIterator->second;
        if (package == existingPackage) {
            return;
        }
        // retain certain information obtained from package contents if this is actually the same package as before
        package->addDepsAndProvidesFromOtherPackage(*existingPackage);
        // remove the existing package
        removePackage(packageIterator);
    }

    // add the new package
    addPackageDependencies(package);
    packages.emplace(package->name, package);
}

void Database::forceUpdatePackage(const std::shared_ptr<Package> &package)
{
    // check whether the package already exists
    const auto packageIterator = packages.find(package->name);
    auto differentPackage = true;
    if (packageIterator != packages.end()) {
        const auto &existingPackage = packageIterator->second;
        if ((differentPackage = package != existingPackage)) {
            // retain certain information obtained from package contents if this is actually the same package as before
            package->addDepsAndProvidesFromOtherPackage(*existingPackage);
            // remove the existing package
            removePackage(packageIterator);
        }
    }

    // add the new package
    addPackageDependencies(package);
    if (differentPackage) {
        packages.emplace(package->name, package);
    }
}

void Database::replacePackages(const std::vector<std::shared_ptr<Package>> &newPackages, DateTime lastModified)
{
    // retain certain information obtained from package contents
    for (auto &package : newPackages) {
        const auto packageIterator = packages.find(package->name);
        if (packageIterator == packages.end()) {
            continue;
        }
        package->addDepsAndProvidesFromOtherPackage(*packageIterator->second);
    }
    // clear current packages and add new ones
    clearPackages();
    for (auto &package : newPackages) {
        updatePackage(package);
    }
    lastUpdate = lastModified;
}

/*!
 * \brief Determines which packages are unresolvable.
 * \param config The configuration supposed to contain database dependencies.
 * \param newPackages Packages which should be added to the database.
 * \param removedProvides Provides which will be removed from the database.
 * \remarks "Resolvable" means here (so far) just that all dependencies are present. It does not mean "installable".
 */
std::unordered_map<std::shared_ptr<Package>, UnresolvedDependencies> Database::detectUnresolvedPackages(
    Config &config, const std::vector<std::shared_ptr<Package>> &newPackages, const DependencySet &removedProvides)
{
    std::unordered_map<std::shared_ptr<Package>, UnresolvedDependencies> unresolvedPackages;

    // determine new provides
    DependencySet newProvides;
    set<string> newLibProvides;
    for (const auto &newPackage : newPackages) {
        newProvides.add(Dependency(newPackage->name, newPackage->version), newPackage);
        for (const auto &newProvide : newPackage->provides) {
            newProvides.add(newProvide, newPackage);
        }
        for (const auto &newLibProvide : newPackage->libprovides) {
            newLibProvides.emplace(newLibProvide);
        }
    }

    // check whether all required dependencies are still provided
    for (const auto &requiredDep : requiredDeps) {
        const auto &[dependencyName, dependencyDetail] = requiredDep;
        const auto &affectedPackages = dependencyDetail.relevantPackages;

        // skip if new packages provide dependency
        if (newProvides.provides(dependencyName, dependencyDetail)) {
            continue;
        }

        // skip if db provides dependency
        if (!removedProvides.provides(dependencyName, dependencyDetail) && providedDeps.provides(dependencyName, dependencyDetail)) {
            continue;
        }

        // skip if dependency is provided by a database this database depends on
        const auto checkDb = [this, &config, &requiredDep](const std::string &dbName) {
            if (const auto *const db = config.findDatabase(dbName, arch)) {
                if (db->providedDeps.provides(requiredDep.first, requiredDep.second)) {
                    return true;
                }
            }
            return false;
        };
        auto providedByAnotherDb = false;
        for (const auto &dbName : dependencies) {
            if ((providedByAnotherDb = checkDb(dbName))) {
                break;
            }
        }
        if (providedByAnotherDb) {
            continue;
        }

        // skip if dependency is provided by the protected version of this db
        if (checkDb(name + "-protected")) {
            continue;
        }

        // add packages to list of unresolved packages
        for (const auto &affectedPackage : affectedPackages) {
            unresolvedPackages[affectedPackage].deps.emplace_back(Dependency(dependencyName, dependencyDetail.version, dependencyDetail.mode));
        }
    }

    // check whether all required libraries are still provided
    for (const auto &[requiredLib, affectedPackages] : requiredLibs) {

        // skip if new packages provide dependency
        if (newLibProvides.count(requiredLib)) {
            continue;
        }

        // skip if db provides dependency
        if (providedLibs.count(requiredLib)) {
            continue;
        }

        // skip if dependency is provided by a database this database depends on
        const auto checkDb = [this, &config, &requiredLib = requiredLib](const std::string &dbName) {
            if (const auto *const db = config.findDatabase(dbName, arch)) {
                if (db->providedLibs.count(requiredLib)) {
                    return true;
                }
            }
            return false;
        };
        auto providedByAnotherDb = false;
        for (const auto &dbName : dependencies) {
            if ((providedByAnotherDb = checkDb(dbName))) {
                break;
            }
        }
        if (providedByAnotherDb) {
            continue;
        }

        // skip if library is provided by the protected version of this db
        if (checkDb(name + "-protected")) {
            continue;
        }

        // add packages to list of unresolved packages
        for (const auto &affectedPackage : affectedPackages) {
            unresolvedPackages[affectedPackage].libs.emplace_back(requiredLib);
        }
    }

    return unresolvedPackages;
}

LibPkg::PackageUpdates LibPkg::Database::checkForUpdates(const std::vector<LibPkg::Database *> &updateSources)
{
    PackageUpdates results;
    for (const auto &myPackageIterator : packages) {
        const auto &myPackage = myPackageIterator.second;
        auto foundPackage = false;
        for (auto *updateSource : updateSources) {
            const auto updatePackageIterator = updateSource->packages.find(myPackageIterator.first);
            if (updatePackageIterator == updateSource->packages.cend()) {
                continue;
            }
            foundPackage = true;
            const auto &updatePackage = updatePackageIterator->second;
            const auto versionDiff = myPackage->compareVersion(*updatePackage);
            std::vector<PackageUpdate> *list = nullptr;
            switch (versionDiff) {
            case PackageVersionComparison::SoftwareUpgrade:
                list = &results.versionUpdates;
                break;
            case PackageVersionComparison::PackageUpgradeOnly:
                list = &results.packageUpdates;
                break;
            case PackageVersionComparison::NewerThanSyncVersion:
                list = &results.downgrades;
                break;
            default:;
            }
            if (list) {
                list->emplace_back(PackageSearchResult(*this, myPackage), PackageSearchResult(*updateSource, updatePackage));
            }
        }
        if (!foundPackage) {
            results.orphans.emplace_back(PackageSearchResult(*this, myPackage));
        }
    }
    return results;
}

PackageLocation Database::locatePackage(const string &packageName) const
{
    PackageLocation res;
    if (packageName.empty()) {
        return res;
    }
    res.pathWithinRepo = localPkgDir % '/' + packageName;
    try {
        switch (std::filesystem::symlink_status(res.pathWithinRepo).type()) {
        case std::filesystem::file_type::regular:
            res.exists = true;
            break;
        case std::filesystem::file_type::symlink:
            res.storageLocation = std::filesystem::read_symlink(res.pathWithinRepo);
            if (res.storageLocation.is_absolute()) {
                res.exists = std::filesystem::is_regular_file(res.storageLocation);
                break;
            }
            res.storageLocation = argsToString(localPkgDir, '/', res.storageLocation);
            if ((res.exists = std::filesystem::is_regular_file(res.storageLocation))) {
                res.storageLocation = std::filesystem::canonical(res.storageLocation);
            }
            break;
        default:
            break;
        }
    } catch (std::filesystem::filesystem_error &e) {
        res.error = move(e);
    }
    return res;
}

std::string Database::filesPathFromRegularPath() const
{
    if (path.empty()) {
        return std::string();
    }
    const auto ext = path.rfind(".db");
    return ext == std::string::npos ? path : argsToString(std::string_view(path.data(), ext), ".files");
}

} // namespace LibPkg

namespace ReflectiveRapidJSON {

namespace JsonReflector {

template <>
LIBPKG_EXPORT void push<LibPkg::PackageSearchResult>(
    const LibPkg::PackageSearchResult &reflectable, RAPIDJSON_NAMESPACE::Value &value, RAPIDJSON_NAMESPACE::Document::AllocatorType &allocator)
{
    // customize serialization of PackageSearchResult to render as if it was pkg itself with an additional db property
    value.SetObject();
    auto obj = value.GetObject();
    auto &pkg = reflectable.pkg;
    push(pkg->name, "name", obj, allocator);
    push(pkg->origin, "origin", obj, allocator);
    push(pkg->timestamp, "timestamp", obj, allocator);
    push(pkg->version, "version", obj, allocator);
    push(pkg->description, "description", obj, allocator);
    if (const auto &pkgInfo = pkg->packageInfo) {
        push(pkgInfo->arch, "arch", obj, allocator);
        push(pkgInfo->buildDate, "buildDate", obj, allocator);
    }
    if (const auto &srcInfo = pkg->sourceInfo) {
        push(srcInfo->archs, "archs", obj, allocator);
    }
    if (const auto *const dbInfo = std::get_if<LibPkg::DatabaseInfo>(&reflectable.db)) {
        if (!dbInfo->name.empty()) {
            push(dbInfo->name, "db", obj, allocator);
        }
        if (!dbInfo->arch.empty()) {
            push(dbInfo->arch, "dbArch", obj, allocator);
        }
    } else if (const auto *const db = std::get<LibPkg::Database *>(reflectable.db)) {
        push(db->name, "db", obj, allocator);
        if (!db->arch.empty()) {
            push(db->arch, "dbArch", obj, allocator);
        }
    }
}

template <>
LIBPKG_EXPORT void pull<LibPkg::PackageSearchResult>(LibPkg::PackageSearchResult &reflectable,
    const RAPIDJSON_NAMESPACE::GenericValue<RAPIDJSON_NAMESPACE::UTF8<char>> &value, JsonDeserializationErrors *errors)
{
    if (!value.IsObject()) {
        if (errors) {
            errors->reportTypeMismatch<LibPkg::PackageSearchResult>(value.GetType());
        }
        return;
    }

    auto obj = value.GetObject();
    auto &pkg = reflectable.pkg;
    if (!pkg) {
        pkg = make_shared<LibPkg::Package>();
    }
    ReflectiveRapidJSON::JsonReflector::pull(pkg->name, "name", obj, errors);
    ReflectiveRapidJSON::JsonReflector::pull(pkg->origin, "origin", obj, errors);
    ReflectiveRapidJSON::JsonReflector::pull(pkg->timestamp, "timestamp", obj, errors);
    ReflectiveRapidJSON::JsonReflector::pull(pkg->version, "version", obj, errors);
    ReflectiveRapidJSON::JsonReflector::pull(pkg->description, "description", obj, errors);
    auto &pkgInfo = pkg->packageInfo;
    if (!pkgInfo) {
        pkgInfo = make_unique<LibPkg::PackageInfo>();
    }
    ReflectiveRapidJSON::JsonReflector::pull(pkgInfo->arch, "arch", obj, errors);
    ReflectiveRapidJSON::JsonReflector::pull(pkgInfo->buildDate, "buildDate", obj, errors);
    auto &srcInfo = pkg->sourceInfo;
    if (!srcInfo) {
        srcInfo = make_shared<LibPkg::SourceInfo>();
    }
    ReflectiveRapidJSON::JsonReflector::pull(srcInfo->archs, "archs", obj, errors);
    auto &dbInfo = reflectable.db.emplace<LibPkg::DatabaseInfo>();
    ReflectiveRapidJSON::JsonReflector::pull(dbInfo.name, "db", obj, errors);
    ReflectiveRapidJSON::JsonReflector::pull(dbInfo.arch, "dbArch", obj, errors);
}

} // namespace JsonReflector

namespace BinaryReflector {

template <>
LIBPKG_EXPORT void writeCustomType<LibPkg::PackageSearchResult>(BinarySerializer &serializer, const LibPkg::PackageSearchResult &packageSearchResult)
{
    if (const auto *const dbInfo = std::get_if<LibPkg::DatabaseInfo>(&packageSearchResult.db)) {
        serializer.write(dbInfo->name);
    } else if (const auto *const db = std::get<LibPkg::Database *>(packageSearchResult.db)) {
        serializer.write(db->name);
    } else {
        serializer.write(std::string());
    }
    serializer.write(packageSearchResult.pkg);
}

template <>
LIBPKG_EXPORT void readCustomType<LibPkg::PackageSearchResult>(BinaryDeserializer &deserializer, LibPkg::PackageSearchResult &packageSearchResult)
{
    deserializer.read(packageSearchResult.db.emplace<LibPkg::DatabaseInfo>().name);
    deserializer.read(packageSearchResult.pkg);
}

} // namespace BinaryReflector

} // namespace ReflectiveRapidJSON
