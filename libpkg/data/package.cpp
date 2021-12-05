#include "./package.h"

#include "../parser/utils.h"

#include "reflection/package.h"

#include <c++utilities/conversion/stringbuilder.h>

#include <ostream>

using namespace std;
using namespace CppUtilities;

namespace LibPkg {

bool Dependency::matches(const DependencyMode mode, const string &version1, const string &version2)
{
    switch (mode) {
    case DependencyMode::Any:
        return true;
    case DependencyMode::Equal:
        switch (PackageVersion::compareParts(version2, version1, true)) {
        case PackageVersionPartComparison::Equal:
            return true;
        default:;
        }
        break;
    case DependencyMode::GreatherEqual:
        switch (PackageVersion::compareParts(version2, version1, true)) {
        case PackageVersionPartComparison::Equal:
        case PackageVersionPartComparison::Newer:
            return true;
        default:;
        }
        break;
    case DependencyMode::GreatherThan:
        switch (PackageVersion::compareParts(version2, version1, true)) {
        case PackageVersionPartComparison::Newer:
            return true;
        default:;
        }
        break;
    case DependencyMode::LessEqual:
        switch (PackageVersion::compareParts(version2, version1, true)) {
        case PackageVersionPartComparison::Equal:
        case PackageVersionPartComparison::Older:
            return true;
        default:;
        }
        break;
    case DependencyMode::LessThan:
        switch (PackageVersion::compareParts(version2, version1, true)) {
        case PackageVersionPartComparison::Older:
            return true;
        default:;
        }
        break;
    }
    return false;
}

ostream &operator<<(ostream &o, const DependencyMode &mode)
{
    switch (mode) {
    case DependencyMode::Any:
        break;
    case DependencyMode::Equal:
        o << '=';
        break;
    case DependencyMode::GreatherEqual:
        o << '>' << '=';
        break;
    case DependencyMode::GreatherThan:
        o << '>';
        break;
    case DependencyMode::LessEqual:
        o << '<' << '=';
        break;
    case DependencyMode::LessThan:
        o << '<';
        break;
    }
    return o;
}

ostream &operator<<(ostream &o, const Dependency &dependency)
{
    o << dependency.name;
    if (dependency.mode != DependencyMode::Any) {
        o << dependency.mode << dependency.version;
    }
    if (!dependency.description.empty()) {
        o << ':' << ' ' << dependency.description;
    }
    return o;
}

ostream &operator<<(ostream &o, const std::vector<Dependency> &dependencies)
{
    for (const auto &dependency : dependencies) {
        o << dependency << '\n';
    }
    return o;
}

bool Package::providesDependency(const Dependency &dependency) const
{
    if (name == dependency.name) {
        if (Dependency::matches(dependency.mode, dependency.version, version)) {
            return true;
        }
    }
    for (const Dependency &provide : provides) {
        if (provide.matches(dependency)) {
            return true;
        }
    }
    return false;
}

void Package::exportProvides(
    const std::shared_ptr<Package> &package, DependencySet &destinationProvides, std::unordered_set<std::string> &destinationLibProvides)
{
    destinationProvides.add(LibPkg::Dependency(package->name, package->version), package);
    for (const auto &provide : package->provides) {
        destinationProvides.add(provide, package);
    }
    destinationLibProvides.insert(package->libprovides.begin(), package->libprovides.end());
}

ostream &operator<<(ostream &o, const PackageVersionComparison &res)
{
    switch (res) {
    case PackageVersionComparison::Equal:
        o << "equal";
        break;
    case PackageVersionComparison::SoftwareUpgrade:
        o << "software upgrade";
        break;
    case PackageVersionComparison::PackageUpgradeOnly:
        o << "package upgrade";
        break;
    case PackageVersionComparison::NewerThanSyncVersion:
        o << "newer than sync version";
        break;
    }
    return o;
}

ostream &operator<<(ostream &o, const PackageVersionPartComparison &res)
{
    switch (res) {
    case PackageVersionPartComparison::Equal:
        o << "equal";
        break;
    case PackageVersionPartComparison::Newer:
        o << "newer";
        break;
    case PackageVersionPartComparison::Older:
        o << "older";
        break;
    }
    return o;
}

PackageVersionComparison Package::compareVersion(const Package &other) const
{
    return PackageVersion::fromString(version).compare(PackageVersion::fromString(other.version));
}

/*!
 * \brief Compares the specified version segments which are supposed to only contain alphanumeric characters.
 */
PackageVersionPartComparison compareSegments(const char *segment1, const char *segment1End, const char *segment2, const char *segment2End)
{
    // trim leading zeros
    for (; segment1 != segment1End && *segment1 == '0'; ++segment1)
        ;
    for (; segment2 != segment2End && *segment2 == '0'; ++segment2)
        ;
    // calculate segment sizes
    const auto segment1Size = segment1End - segment1;
    const auto segment2Size = segment2End - segment2;
    if (segment1Size > segment2Size) {
        // segment 1 has more digits and hence is newer
        return PackageVersionPartComparison::Newer;
    } else if (segment2Size > segment1Size) {
        // segment 2 has more digits and hence is newer
        return PackageVersionPartComparison::Older;
    } else {
        for (; segment1 != segment1End; ++segment1, ++segment2) {
            if (*segment1 > *segment2) {
                // segment 1 digit has higher value and hence is newer
                return PackageVersionPartComparison::Newer;
            } else if (*segment2 > *segment1) {
                // segment 2 digit has higher value and hence is newer
                return PackageVersionPartComparison::Older;
            }
        }
    }
    return PackageVersionPartComparison::Equal;
}

/*!
 * \brief Compares the specified version parts (which are either epoch, pkgver or pkgrel).
 * \returns Returns whether \a part1 is newer or older than \a part2 or whether both are equal.
 * \remarks \a part1 and \a part2 are considered null-terminated. So processing them is stopped
 *          when the first null-byte occurs even if std::string::size() denotes that string is longer.
 */
PackageVersionPartComparison PackageVersion::compareParts(const string &part1, const string &part2, bool allowImplicitPkgRel)
{
    const char *part1Pos = part1.data(), *part2Pos = part2.data();
    const char *part1End = part1.data() + part1.size(), *part2End = part2.data() + part2.size();
    for (;;) {
        // determine current segments
        part1End = firstNonAlphanumericCharacter(part1Pos, part1End);
        part2End = firstNonAlphanumericCharacter(part2Pos, part2End);
        // compare segments
        const auto segmentComparsion = compareSegments(part1Pos, part1End, part2Pos, part2End);
        if (segmentComparsion != PackageVersionPartComparison::Equal) {
            return segmentComparsion;
        }
        // the segments are equal, check next segment
        if (*part1End && *part2End) {
            // there is another segment in both parts
            part1Pos = part1End + 1;
            part2Pos = part2End + 1;
        } else if (*part1End) {
            // only part 1 has another segment -> it is more specific and hence considered newer
            if (allowImplicitPkgRel && *part1End == '-') {
                // check whether the only further segment in part 1 is pkgrel (starts with '-' and only contains alphanumeric chars and '.')
                for (part1Pos = part1End + 1; part1Pos != part1End; ++part1Pos) {
                    const char c = *part1Pos;
                    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || (c == '.'))) {
                        break;
                    }
                }
                if (!*part1Pos) {
                    // consider both parts equal if part 2 doesn't have explicit pkgrel and part 1 does
                    return PackageVersionPartComparison::Equal;
                }
            }
            return PackageVersionPartComparison::Newer;
        } else if (*part2End) {
            // only part 2 has another segment -> it is more specific and hence considered newer
            return PackageVersionPartComparison::Older;
        } else {
            // none of the parts has another segment -> parts are equal
            return PackageVersionPartComparison::Equal;
        }
    }
}

string PackageVersion::trimPackageVersion(const string &versionString)
{
    const auto lastDash = versionString.rfind('-');
    if (lastDash == string::npos) {
        return versionString;
    }
    return versionString.substr(0, lastDash);
}

/*!
 * \brief Compares the current instance with another version.
 * \returns Returns whether the current instance is newer or older than \a other or whether both are equal.
 */
PackageVersionComparison PackageVersion::compare(const PackageVersion &other) const
{
    // check whether epoch differs
    if (!epoch.empty() || !other.epoch.empty()) {
        switch (compareParts(other.epoch, epoch)) {
        case PackageVersionPartComparison::Newer:
            return PackageVersionComparison::SoftwareUpgrade;
        case PackageVersionPartComparison::Older:
            return PackageVersionComparison::NewerThanSyncVersion;
        case PackageVersionPartComparison::Equal:;
        }
    }
    // check whether upstream version differs
    switch (compareParts(other.upstream, upstream)) {
    case PackageVersionPartComparison::Newer:
        return PackageVersionComparison::SoftwareUpgrade;
    case PackageVersionPartComparison::Older:
        return PackageVersionComparison::NewerThanSyncVersion;
    case PackageVersionPartComparison::Equal:;
    }
    // check whether package version differs
    if (!package.empty() && !other.package.empty()) {
        // only consider package release if both versions specify it (otherwise consider packages equal)
        switch (compareParts(other.package, package)) {
        case PackageVersionPartComparison::Newer:
            return PackageVersionComparison::PackageUpgradeOnly;
        case PackageVersionPartComparison::Older:
            return PackageVersionComparison::NewerThanSyncVersion;
        case PackageVersionPartComparison::Equal:;
        }
    }
    // no difference -> equal
    return PackageVersionComparison::Equal;
}

std::string PackageVersion::toString() const
{
    return epoch.empty() ? upstream % '-' + package : epoch % ':' % upstream % '-' + package;
}

/*!
 * \brief Returns the file of the binary package file.
 * \remarks This value is computed from name, version and arch if unknown.
 * \param extension Specifies the extension to be used when computing the value.
 */
string LibPkg::Package::computeFileName(const char *extension) const
{
    if (!packageInfo) {
        return string();
    }
    if (!packageInfo->fileName.empty()) {
        return packageInfo->fileName;
    }
    return argsToString(name, '-', version, '-', packageInfo->arch, '.', extension);
}

string LibPkg::Package::computeRegularPackageName() const
{
    if (name == "mingw-w64-headers" || name == "mingw-w64-crt") {
        return string();
    }
    if (startsWith(name, "lib32-")) {
        return name.substr(strlen("lib32-"));
    } else if (startsWith(name, "mingw-w64-")) {
        return name.substr(strlen("mingw-w64-"));
    } else if (startsWith(name, "android-aarch64-")) {
        return name.substr(strlen("android-aarch64-"));
    } else if (startsWith(name, "android-x86-64-")) {
        return name.substr(strlen("android-x86-64-"));
    } else if (startsWith(name, "android-x86-")) {
        return name.substr(strlen("android-x86-"));
    } else if (startsWith(name, "android-armv7a-eabi-")) {
        return name.substr(strlen("android-armv7a-eabi-"));
    }
    return string();
}

bool Package::addDepsAndProvidesFromOtherPackage(const Package &otherPackage, bool force)
{
    if (&otherPackage == this) {
        return false;
    }

    // skip if otherPackage does not match the current instance (only version and build date are considered) or if otherPackage has no info from package contents
    if (!force
        && ((otherPackage.origin != PackageOrigin::PackageContents && otherPackage.origin != PackageOrigin::CustomSource)
            || version != otherPackage.version
            || !(!packageInfo || packageInfo->buildDate.isNull()
                || (otherPackage.packageInfo && packageInfo->buildDate == otherPackage.packageInfo->buildDate)))) {
        return false;
    }

    // add package info from other package if this package has none at all
    if (!packageInfo && otherPackage.packageInfo) {
        packageInfo = make_unique<PackageInfo>(*(otherPackage.packageInfo));
    }
    // add source info from other package; at least add missing make and check dependencies
    if (!sourceInfo) {
        sourceInfo = otherPackage.sourceInfo;
    } else if (otherPackage.sourceInfo) {
        if (sourceInfo->checkDependencies.empty()) {
            sourceInfo->checkDependencies = otherPackage.sourceInfo->checkDependencies;
        }
        if (sourceInfo->makeDependencies.empty()) {
            sourceInfo->makeDependencies = otherPackage.sourceInfo->makeDependencies;
        }
    }

    // retain libdepends and libprovides added in Package::addDepsAndProvidesFromContents()
    libdepends = otherPackage.libdepends;
    libprovides = otherPackage.libprovides;

    // replace python/perl dependencies with ones added in Package::addDepsAndProvidesFromContents()
    const static std::unordered_set<std::string_view> relevantDeps{ "python", "python2", "perl" };
    std::unordered_multimap<std::string_view, const Dependency *> foundDeps;
    for (const auto &dependency : otherPackage.dependencies) {
        const auto relevantDep = relevantDeps.find(dependency.name);
        if (relevantDep != relevantDeps.end()) {
            foundDeps.insert(std::make_pair(*relevantDep, &dependency));
        }
    }
    dependencies.erase(std::remove_if(dependencies.begin(), dependencies.end(),
                           [&foundDeps](const Dependency &dependency) { return foundDeps.find(dependency.name) != foundDeps.end(); }),
        dependencies.end());
    const auto requiredCapacity = dependencies.size() + foundDeps.size();
    if (dependencies.capacity() < requiredCapacity) {
        dependencies.reserve(requiredCapacity);
    }
    for (const auto &[dependencyName, dependency] : foundDeps) {
        dependencies.emplace_back(*dependency);
    }

    // consider this package as good as if Package::addDepsAndProvidesFromContents() was called
    origin = PackageOrigin::PackageContents;
    if (otherPackage.timestamp > timestamp) {
        timestamp = otherPackage.timestamp;
    }
    return true;
}

bool Package::isArchAny() const
{
    const auto &a = archs.empty() && sourceInfo ? sourceInfo->archs : archs;
    if (a.empty()) {
        return false;
    }
    for (const auto &arch : a) {
        if (arch != "any") {
            return false;
        }
    }
    return true;
}

DependencySetBase::iterator DependencySet::find(const Dependency &dependency)
{
    for (auto range = equal_range(dependency.name); range.first != range.second; ++range.first) {
        if (Dependency::matches(dependency.mode, dependency.version, range.first->second.version)) {
            return range.first;
        }
    }
    return end();
}

// FIXME: maybe remove again
DependencySetBase::iterator DependencySet::find(const std::string &dependencyName, const DependencyDetail &dependencyDetail)
{
    for (auto range = equal_range(dependencyName); range.first != range.second; ++range.first) {
        if (Dependency::matches(dependencyDetail.mode, dependencyDetail.version, range.first->second.version)) {
            return range.first;
        }
    }
    return end();
}

DependencySetBase::iterator DependencySet::findExact(const Dependency &dependency)
{
    for (auto range = equal_range(dependency.name); range.first != range.second; ++range.first) {
        if (dependency.version == range.first->second.version) {
            return range.first;
        }
    }
    return end();
}

// FIXME: maybe remove again
DependencySetBase::iterator DependencySet::findExact(const std::string &dependencyName, const DependencyDetail &dependencyDetail)
{
    for (auto range = equal_range(dependencyName); range.first != range.second; ++range.first) {
        if (dependencyDetail.version == range.first->second.version) {
            return range.first;
        }
    }
    return end();
}

bool DependencySet::provides(const Dependency &dependency) const
{
    for (auto range = equal_range(dependency.name); range.first != range.second; ++range.first) {
        if (Dependency::matches(dependency.mode, dependency.version, range.first->second.version)) {
            return true;
        }
    }
    return false;
}

bool DependencySet::provides(const string &dependencyName, const DependencyDetail &dependencyDetail) const
{
    for (auto range = equal_range(dependencyName); range.first != range.second; ++range.first) {
        if (Dependency::matches(dependencyDetail.mode, dependencyDetail.version, range.first->second.version)) {
            return true;
        }
    }
    return false;
}

DependencySetBase::iterator DependencySet::add(const Dependency &dependency, const std::shared_ptr<Package> &relevantPackage)
{
    auto iterator = findExact(dependency);
    if (iterator == end()) {
        iterator = insert(std::make_pair(dependency.name, DependencyDetail(dependency.version, dependency.mode, { relevantPackage })));
    } else {
        iterator->second.relevantPackages.emplace(relevantPackage);
    }
    return iterator;
}

// FIXME: maybe remove again
DependencySetBase::iterator DependencySet::add(
    const string &dependencyName, const DependencyDetail &dependencyDetail, const std::shared_ptr<Package> &relevantPackage)
{
    auto iterator = findExact(dependencyName, dependencyDetail);
    if (iterator == end()) {
        iterator = insert(std::make_pair(dependencyName, DependencyDetail{ dependencyDetail.version, dependencyDetail.mode, { relevantPackage } }));
    } else {
        iterator->second.relevantPackages.emplace(relevantPackage);
    }
    return iterator;
}

DependencySetBase::iterator DependencySet::add(string &&dependencyName, DependencyDetail &&dependencyDetail)
{
    auto iterator = find(dependencyName, dependencyDetail);
    if (iterator == end() || iterator->second.version != dependencyDetail.version) {
        iterator = insert(std::pair(std::move(dependencyName), std::move(dependencyDetail)));
    }
    return iterator;
}

void DependencySet::remove(const Dependency &dependency, const std::shared_ptr<Package> &relevantPackage)
{
    for (auto range = equal_range(dependency.name); range.first != range.second;) {
        auto &relevantPackages = range.first->second.relevantPackages;
        relevantPackages.erase(relevantPackage);
        if (relevantPackages.empty()) {
            erase(range.first);
            range = equal_range(dependency.name);
        } else {
            ++range.first;
        }
    }
}

void DependencySet::remove(const string &name)
{
    for (auto range = equal_range(name); range.first != range.second;) {
        erase(range.first);
        range = equal_range(name);
    }
}

} // namespace LibPkg

namespace ReflectiveRapidJSON {

namespace JsonReflector {

template <>
LIBPKG_EXPORT void push<LibPkg::PackageSpec>(
    const LibPkg::PackageSpec &reflectable, RAPIDJSON_NAMESPACE::Value &value, RAPIDJSON_NAMESPACE::Document::AllocatorType &allocator)
{
    // just serialize the package (and ignore the ID)
    push(reflectable.pkg, value, allocator);
}

template <>
LIBPKG_EXPORT void pull<LibPkg::PackageSpec>(LibPkg::PackageSpec &reflectable,
    const RAPIDJSON_NAMESPACE::GenericValue<RAPIDJSON_NAMESPACE::UTF8<char>> &value, JsonDeserializationErrors *errors)
{
    // just deserialize the package (and ignore the ID)
    pull(reflectable.pkg, value, errors);
}

} // namespace JsonReflector

} // namespace ReflectiveRapidJSON
