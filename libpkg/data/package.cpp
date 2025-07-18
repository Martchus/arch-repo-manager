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

PackageVersionComparison PackageBase::compareVersion(const PackageBase &other) const
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
    for (int i = 0;; ++i) {
        // determine current segments
        part1End = firstNonAlphanumericCharacter(part1Pos, part1End);
        part2End = firstNonAlphanumericCharacter(part2Pos, part2End);
        // check whether one of the segments is the epoch
        if (!i) {
            const auto part1IsEpoch = *part1End == ':';
            const auto part2IsEpoch = *part2End == ':';
            // if one one has an epoch, consider the one with epoch newer
            if (part1IsEpoch && !part2IsEpoch) {
                return PackageVersionPartComparison::Newer;
            } else if (part2IsEpoch && !part1IsEpoch) {
                return PackageVersionPartComparison::Older;
            }
        }
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
    if (packageInfo && !packageInfo->fileName.empty()) {
        return packageInfo->fileName;
    }
    return argsToString(name, '-', version, '-', arch, '.', extension);
}

std::string_view LibPkg::PackageBase::computeRegularPackageName() const
{
    if (name == "mingw-w64-headers" || name == "mingw-w64-crt") {
        return std::string_view();
    }
    static constexpr std::string_view prefixes[] = {
        "lib32-",
        "mingw-w64-",
        "android-aarch64-",
        "android-x86-64-",
        "android-x86-",
        "android-armv7a-eabi-",
        "android-riscv64-",
        "static-compat-",
    };
    for (const auto prefix : prefixes) {
        if (name.starts_with(prefix)) {
            return std::string_view(name).substr(prefix.size());
        }
    }
    static constexpr std::string_view suffixes[] = { "-static-compat" };
    for (const auto suffix : suffixes) {
        if (name.ends_with(suffix)) {
            return std::string_view(name.data(), name.size() - suffix.size());
        }
    }
    return std::string_view();
}

void PackageBase::clear()
{
    origin = PackageOrigin::Default;
    timestamp = buildDate = DateTime();
    name.clear();
    version.clear();
    arch.clear();
    archs.clear();
    description.clear();
}

/*!
 * \brief Returns whether addDepsAndProvidesFromOtherPackage() would take over the information (without force).
 */
bool Package::canDepsAndProvidesFromOtherPackage(const Package &otherPackage) const
{
    return !((otherPackage.origin != PackageOrigin::PackageContents && otherPackage.origin != PackageOrigin::CustomSource)
        || version != otherPackage.version
        || !(!packageInfo || buildDate.isNull() || (otherPackage.packageInfo && buildDate == otherPackage.buildDate)));
}

/*!
 * \brief Takes over deps/provides from \a otherPackage if appropriate.
 * \remarks
 * The information is not taken over if \a otherPackage does not match the current instance (only
 * version and build date are considered) or if otherPackage has no info from package contents. Use
 * the \a force parameter to avoid these checks.
 */
bool Package::addDepsAndProvidesFromOtherPackage(const Package &otherPackage, bool force)
{
    if (&otherPackage == this || (!force && !canDepsAndProvidesFromOtherPackage(otherPackage))) {
        return false;
    }

    // add package info from other package if this package has none at all
    if (!packageInfo && otherPackage.packageInfo) {
        packageInfo = std::make_optional<PackageInfo>(*(otherPackage.packageInfo));
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

static bool containsUnexpectedCharacters(std::string_view value)
{
    for (auto c : value) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
            continue;
        }
        switch (c) {
        case '+':
        case '-':
        case '_':
        case '.':
            continue;
        default:
            return true;
        }
    }
    return false;
}

static bool containsUnprintableCharacters(std::string_view value)
{
    for (auto c : value) {
        if (c < ' ' || c >= 127) {
            return true;
        }
    }
    return false;
}

#define CHECK_FIELD_EMPTY(field)                                                                                                                     \
    if (field.empty()) {                                                                                                                             \
        problems.emplace_back(#field " is empty");                                                                                                   \
    }
#define CHECK_FIELD_FOR_UNEXPECTED_CHARS(field)                                                                                                      \
    if (containsUnexpectedCharacters(field)) {                                                                                                       \
        problems.emplace_back(#field " contains unexpected characters");                                                                             \
    }
#define CHECK_FIELD_FOR_UNPRINTABLE_CHARS(field)                                                                                                     \
    if (containsUnprintableCharacters(field)) {                                                                                                      \
        problems.emplace_back(#field " contains unprintable or non-ASCII characters");                                                               \
    }
#define CHECK_FIELD_STRICT(field)                                                                                                                    \
    CHECK_FIELD_EMPTY(field)                                                                                                                         \
    CHECK_FIELD_FOR_UNEXPECTED_CHARS(field)
#define CHECK_FIELD_RELAXED(field)                                                                                                                   \
    CHECK_FIELD_EMPTY(field)                                                                                                                         \
    CHECK_FIELD_FOR_UNPRINTABLE_CHARS(field)

/*!
 * \brief Performs a basic sanity check of the package's fields.
 * \returns Returns an empty vector if no problems were found; otherwise returns the problem descriptions.
 */
std::vector<std::string> Package::validate() const
{
    // check basic fields
    auto problems = std::vector<std::string>();
    CHECK_FIELD_STRICT(name)
    CHECK_FIELD_RELAXED(version)
    CHECK_FIELD_STRICT(arch)

    // check dependencies
    const auto checkDeps = sourceInfo ? sourceInfo->checkDependencies : std::vector<Dependency>();
    const auto makeDeps = sourceInfo ? sourceInfo->makeDependencies : std::vector<Dependency>();
    for (const auto &deps : { dependencies, optionalDependencies, provides, conflicts, replaces, checkDeps, makeDeps }) {
        for (const auto &dep : deps) {
            if (dep.name.empty()) {
                problems.emplace_back("dependency is empty");
                return problems;
            }
            if (containsUnexpectedCharacters(dep.name)) {
                problems.emplace_back("dependency contains unexpected characters");
                return problems;
            }
        }
    }
    return problems;
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

template <typename PackageSpecType, Traits::EnableIf<Traits::IsSpecializationOf<PackageSpecType, LibPkg::GenericPackageSpec>> * = nullptr>
static void internalPush(
    const PackageSpecType &reflectable, ::RAPIDJSON_NAMESPACE::Value::Object &value, RAPIDJSON_NAMESPACE::Document::AllocatorType &allocator)
{
    // just serialize the package (and ignore the ID)
    if (reflectable.pkg) {
        push(*reflectable.pkg, value, allocator);
    }
}

template <typename PackageSpecType, Traits::EnableIf<Traits::IsSpecializationOf<PackageSpecType, LibPkg::GenericPackageSpec>> * = nullptr>
static void internalPull(PackageSpecType &reflectable,
    const ::RAPIDJSON_NAMESPACE::GenericValue<::RAPIDJSON_NAMESPACE::UTF8<char>>::ConstObject &value, JsonDeserializationErrors *errors)
{
    // find member
    if (const auto pkg = value.FindMember("pkg"); pkg != value.MemberEnd()) {
        reflectable.pkg = std::make_shared<typename decltype(reflectable.pkg)::element_type>();
        pull(*reflectable.pkg, pkg->value, errors);
        if (const auto id = value.FindMember("id"); id != value.MemberEnd()) {
            pull(reflectable.id, id->value, errors);
        }
    } else {
        reflectable.pkg = std::make_shared<typename decltype(reflectable.pkg)::element_type>();
        pull(*reflectable.pkg, value, errors);
    }
}

template <>
LIBPKG_EXPORT void push<LibPkg::PackageSpec>(
    const LibPkg::PackageSpec &reflectable, ::RAPIDJSON_NAMESPACE::Value::Object &value, RAPIDJSON_NAMESPACE::Document::AllocatorType &allocator)
{
    internalPush(reflectable, value, allocator);
}
template <>
LIBPKG_EXPORT void push<LibPkg::GenericPackageSpec<LibPkg::PackageBase>>(const LibPkg::GenericPackageSpec<LibPkg::PackageBase> &reflectable,
    ::RAPIDJSON_NAMESPACE::Value::Object &value, RAPIDJSON_NAMESPACE::Document::AllocatorType &allocator)
{
    internalPush(reflectable, value, allocator);
}
template <>
LIBPKG_EXPORT void pull<LibPkg::PackageSpec>(LibPkg::PackageSpec &reflectable,
    const ::RAPIDJSON_NAMESPACE::GenericValue<::RAPIDJSON_NAMESPACE::UTF8<char>>::ConstObject &value, JsonDeserializationErrors *errors)
{
    internalPull(reflectable, value, errors);
}
template <>
LIBPKG_EXPORT void pull<LibPkg::GenericPackageSpec<LibPkg::PackageBase>>(LibPkg::GenericPackageSpec<LibPkg::PackageBase> &reflectable,
    const ::RAPIDJSON_NAMESPACE::GenericValue<::RAPIDJSON_NAMESPACE::UTF8<char>>::ConstObject &value, JsonDeserializationErrors *errors)
{
    internalPull(reflectable, value, errors);
}

} // namespace JsonReflector

namespace BinaryReflector {

template <typename PackageSpecType, Traits::EnableIf<Traits::IsSpecializationOf<PackageSpecType, LibPkg::GenericPackageSpec>> * = nullptr>
BinaryVersion internalReadCustomType(BinaryDeserializer &deserializer, PackageSpecType &reflectable, BinaryVersion version)
{
    // read version
    using V = Versioning<::ReflectiveRapidJSON::BinarySerializable<PackageSpecType>>;
    if constexpr (V::enabled) {
        V::assertVersion(version = deserializer.readVariableLengthUIntBE(), "LibPkg::GenericPackageSpec");
    }
    // read members
    deserializer.read(reflectable.id, version);
    deserializer.read(reflectable.pkg, version);
    return version;
}

template <typename PackageSpecType, Traits::EnableIf<Traits::IsSpecializationOf<PackageSpecType, LibPkg::GenericPackageSpec>> * = nullptr>
void internalWriteCustomType(BinarySerializer &serializer, const PackageSpecType &reflectable, BinaryVersion version)
{
    // write version
    using V = Versioning<::ReflectiveRapidJSON::BinarySerializable<PackageSpecType>>;
    if constexpr (V::enabled) {
        serializer.writeVariableLengthUIntBE(V::applyDefault(version));
    }
    // write members
    serializer.write(reflectable.id, version);
    serializer.write(reflectable.pkg, version);
}

template <>
LIBPKG_EXPORT BinaryVersion readCustomType<LibPkg::PackageSpec>(
    BinaryDeserializer &deserializer, LibPkg::PackageSpec &reflectable, BinaryVersion version)
{
    return internalReadCustomType(deserializer, reflectable, version);
}

template <>
LIBPKG_EXPORT BinaryVersion readCustomType<LibPkg::GenericPackageSpec<LibPkg::PackageBase>>(
    BinaryDeserializer &deserializer, LibPkg::GenericPackageSpec<LibPkg::PackageBase> &reflectable, BinaryVersion version)
{
    return internalReadCustomType(deserializer, reflectable, version);
}

template <>
LIBPKG_EXPORT void writeCustomType<LibPkg::PackageSpec>(BinarySerializer &serializer, const LibPkg::PackageSpec &reflectable, BinaryVersion version)
{
    internalWriteCustomType(serializer, reflectable, version);
}

template <>
LIBPKG_EXPORT void writeCustomType<LibPkg::GenericPackageSpec<LibPkg::PackageBase>>(
    BinarySerializer &serializer, const LibPkg::GenericPackageSpec<LibPkg::PackageBase> &reflectable, BinaryVersion version)
{
    internalWriteCustomType(serializer, reflectable, version);
}

} // namespace BinaryReflector

} // namespace ReflectiveRapidJSON
