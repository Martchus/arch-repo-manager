#ifndef LIBPKG_DATA_PACKAGE_H
#define LIBPKG_DATA_PACKAGE_H

#include "./storagefwd.h"

#include "../global.h"

#include "../parser/utils.h"

#include <reflective_rapidjson/binary/reflector-chronoutilities.h>
#include <reflective_rapidjson/binary/serializable.h>
#include <reflective_rapidjson/json/reflector-chronoutilities.h>
#include <reflective_rapidjson/json/serializable.h>

#include <c++utilities/chrono/datetime.h>

#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

namespace LibPkg {

/*!
 * \brief The InstallStatus enum specifies whether a package has been installed explicitly or as dependency.
 */
enum class InstallStatus { Explicit = 0, AsDependency = 1, Unknown = 20 };

/*!
 * \brief Either unknown, true or false.
 */
enum class UnknownTrueFalse : unsigned char { Unknown, True, False };

/*!
 * \brief The PackageValidation enum specifies methods used to validate a package.
 * \remarks ALMP type: alpm_pkgvalidation_t
 */
enum class PackageValidation { Unknown = 0, None = (1 << 0), Md5Sum = (1 << 1), Sha256Sum = (1 << 2), PgpSignature = (1 << 3) };

constexpr PackageValidation operator|(PackageValidation lhs, PackageValidation rhs)
{
    return static_cast<PackageValidation>(static_cast<int>(lhs) | static_cast<int>(rhs));
}

constexpr bool operator&(PackageValidation lhs, PackageValidation rhs)
{
    return (static_cast<int>(lhs) & static_cast<int>(rhs)) != 0;
}

constexpr int operator~(PackageValidation lhs)
{
    return ~static_cast<int>(lhs);
}

inline PackageValidation &operator|=(PackageValidation &lhs, PackageValidation rhs)
{
    lhs = static_cast<PackageValidation>(static_cast<int>(lhs) | static_cast<int>(rhs));
    return lhs;
}

inline PackageValidation &operator&=(PackageValidation &lhs, int rhs)
{
    lhs = static_cast<PackageValidation>(static_cast<int>(lhs) & rhs);
    return lhs;
}

/*!
 * \brief The DependencyMode enum specifies the version constraints in dependency specs.
 * \remarks ALPM: alpm_depmod_t
 */
enum class DependencyMode {
    Any = 1, /*! No version constraint */
    Equal, /*! Test version equality (package=x.y.z) */
    GreatherEqual, /*! Test for at least a version (package>=x.y.z) */
    LessEqual, /*! Test for at most a version (package<=x.y.z) */
    GreatherThan, /*! Test for greater than some version (package>x.y.z) */
    LessThan /*! Test for less than some version (package<x.y.z) */
};

struct LIBPKG_EXPORT Dependency : public ReflectiveRapidJSON::JsonSerializable<Dependency>,
                                  public ReflectiveRapidJSON::BinarySerializable<Dependency> {
    explicit Dependency() = default;
    explicit Dependency(const std::string &name, const std::string &version = std::string(), DependencyMode mode = DependencyMode::Any,
        const std::string &description = std::string());
    explicit Dependency(std::string &&name, std::string &&version = std::string(), DependencyMode mode = DependencyMode::Any,
        std::string &&description = std::string());
    explicit Dependency(const char *denotation, std::size_t denotationSize = std::numeric_limits<std::size_t>::max());
    explicit Dependency(std::string_view denotation);
    bool operator==(const Dependency &other) const;
    bool matches(const Dependency &other) const;
    static bool matches(const DependencyMode mode, const std::string &version1, const std::string &version2);

    static Dependency fromString(const char *dependency, std::size_t dependencySize = std::numeric_limits<std::size_t>::max());
    static Dependency fromString(std::string_view dependency);
    std::string toString() const;

    std::string name;
    std::string version;
    std::string description;
    DependencyMode mode = DependencyMode::Any;
};

inline Dependency::Dependency(const std::string &name, const std::string &version, DependencyMode mode, const std::string &description)
    : name(name)
    , version(version)
    , description(description)
    , mode(mode)
{
}

inline Dependency::Dependency(std::string &&name, std::string &&version, DependencyMode mode, std::string &&description)
    : name(std::move(name))
    , version(std::move(version))
    , description(std::move(description))
    , mode(mode)
{
}

inline Dependency::Dependency(std::string_view denotation)
    : Dependency(denotation.data(), denotation.size())
{
}

inline bool Dependency::operator==(const Dependency &other) const
{
    return name == other.name && version == other.version && description == other.description && mode == other.mode;
}

inline bool Dependency::matches(const Dependency &other) const
{
    return name == other.name && matches(other.mode, other.version, version);
}

inline Dependency Dependency::fromString(const char *denotation, size_t denotationSize)
{
    return Dependency(denotation, denotationSize);
}

inline Dependency Dependency::fromString(std::string_view dependency)
{
    return Dependency(dependency);
}

struct LIBPKG_EXPORT DatabaseDependency : public Dependency,
                                          public ReflectiveRapidJSON::JsonSerializable<DatabaseDependency>,
                                          public ReflectiveRapidJSON::BinarySerializable<DatabaseDependency, 1> {
    explicit DatabaseDependency() = default;
    explicit DatabaseDependency(const std::string &name, const std::string &version, DependencyMode mode);
    explicit DatabaseDependency(std::string &&name, std::string &&version, DependencyMode mode);
    std::unordered_set<StorageID> relevantPackages;
};

inline DatabaseDependency::DatabaseDependency(std::string &&name, std::string &&version, DependencyMode mode)
    : Dependency(std::move(name), std::move(version), mode, std::string())
{
}

inline DatabaseDependency::DatabaseDependency(const std::string &name, const std::string &version, DependencyMode mode)
    : Dependency(std::move(name), std::move(version), mode, std::string())
{
}

struct LIBPKG_EXPORT DatabaseLibraryDependency : public ReflectiveRapidJSON::JsonSerializable<DatabaseLibraryDependency>,
                                                 public ReflectiveRapidJSON::BinarySerializable<DatabaseLibraryDependency, 1> {
    explicit DatabaseLibraryDependency() = default;
    explicit DatabaseLibraryDependency(const std::string &name);
    std::string name;
    std::unordered_set<StorageID> relevantPackages;
};

inline DatabaseLibraryDependency::DatabaseLibraryDependency(const std::string &name)
    : name(name)
{
}

} // namespace LibPkg

namespace std {

template <> struct hash<LibPkg::Dependency> {
    std::size_t operator()(const LibPkg::Dependency &dep) const
    {
        using std::hash;
        using DepModeType = typename std::underlying_type<LibPkg::DependencyMode>::type;
        return ((hash<string>()(dep.name) ^ (hash<string>()(dep.version) << 1)) >> 1)
            ^ ((hash<string>()(dep.description) << 4) ^ (hash<DepModeType>()(static_cast<DepModeType>(dep.mode)) << 1) >> 1);
    }
};

} // namespace std

namespace LibPkg {

LIBPKG_EXPORT std::ostream &operator<<(std::ostream &o, const DependencyMode &mode);
LIBPKG_EXPORT std::ostream &operator<<(std::ostream &o, const Dependency &dependency);
LIBPKG_EXPORT std::ostream &operator<<(std::ostream &o, const std::vector<Dependency> &dependencies);

struct SourceFile : public ReflectiveRapidJSON::JsonSerializable<SourceFile>, public ReflectiveRapidJSON::BinarySerializable<SourceFile> {
    std::string path;
    std::string contents;
    bool operator==(const SourceFile &) const = default;
};

struct LIBPKG_EXPORT SourceInfo : public ReflectiveRapidJSON::JsonSerializable<SourceInfo>,
                                  public ReflectiveRapidJSON::BinarySerializable<SourceInfo> {
    std::string name;
    std::vector<std::string> archs; // archs specified in base package
    std::vector<Dependency> makeDependencies;
    std::vector<Dependency> checkDependencies;
    std::string maintainer;
    std::int64_t id = 0;
    std::int64_t category = 0;
    std::int64_t votes = 0;
    CppUtilities::DateTime outOfDate;
    CppUtilities::DateTime firstSubmitted;
    CppUtilities::DateTime lastModified;
    std::string url;
    std::vector<SourceFile> sources;
    std::string directory;
    bool operator==(const SourceInfo &other) const = default;
};

struct LIBPKG_EXPORT PackageInfo : public ReflectiveRapidJSON::JsonSerializable<PackageInfo>,
                                   public ReflectiveRapidJSON::BinarySerializable<PackageInfo> {
    std::string fileName;
    std::vector<std::string> files;
    CppUtilities::DateTime buildDate;
    std::string packager;
    std::string md5;
    std::string sha256;
    std::string pgpSignature;
    std::string arch; // arch of concrete binary package
    std::uint32_t size = 0;
    bool operator==(const PackageInfo &other) const = default;
};

struct LIBPKG_EXPORT InstallInfo : public ReflectiveRapidJSON::JsonSerializable<InstallInfo>,
                                   public ReflectiveRapidJSON::BinarySerializable<InstallInfo> {
    CppUtilities::DateTime installDate;
    std::uint32_t installedSize = 0;
    std::vector<std::string> backupFiles;
    InstallStatus installStatus = InstallStatus::Unknown;
    PackageValidation validationMethods = PackageValidation::None;
    bool operator==(const InstallInfo &other) const = default;
};

/*!
 * \brief The PackageVersionComparison enum defines possible results of packages version comparison.
 */
enum class PackageVersionComparison {
    Equal, /*!< The version of this package is the same as the version of the package from the sync db. */
    SoftwareUpgrade, /*!< The software version of the package from the sync db is newer.  */
    PackageUpgradeOnly, /*!< The package release number of the package from the sync db is newer. */
    NewerThanSyncVersion /*!< The version of this package is NEWER then the version of the package from the sync db. */
};

LIBPKG_EXPORT std::ostream &operator<<(std::ostream &o, const PackageVersionComparison &res);

/*!
 * \brief The ComparsionResult enum defines possible results of the package version part comparison
 *        provided by PackageVersion::compareParts().
 */
enum class PackageVersionPartComparison {
    Equal, /*!< Both parts are equal. */
    Newer, /*!< Part 1 is newer then part 2. */
    Older /*!< Part 2 is newer then part 1. */
};

LIBPKG_EXPORT std::ostream &operator<<(std::ostream &o, const PackageVersionPartComparison &res);

struct LIBPKG_EXPORT PackageVersion : public ReflectiveRapidJSON::JsonSerializable<Dependency> {
    static PackageVersion fromString(const char *versionString, std::size_t versionStringSize);
    static PackageVersion fromString(const std::string &versionString);
    static PackageVersionPartComparison compareParts(const std::string &part1, const std::string &part2, bool allowImplicitPkgRel = false);
    static std::string trimPackageVersion(const std::string &versionString);
    PackageVersionComparison compare(const PackageVersion &other) const;
    static PackageVersionComparison compare(const std::string &versionString1, const std::string &versionString2);
    static bool isNewer(PackageVersionComparison comparison);
    std::string toString() const;

    std::string epoch;
    std::string upstream;
    std::string package;
};

inline PackageVersion PackageVersion::fromString(const std::string &versionString)
{
    return fromString(versionString.data(), versionString.size());
}

inline PackageVersionComparison PackageVersion::compare(const std::string &versionString1, const std::string &versionString2)
{
    return fromString(versionString1).compare(fromString(versionString2));
}

inline bool PackageVersion::isNewer(PackageVersionComparison comparison)
{
    return comparison == PackageVersionComparison::SoftwareUpgrade || comparison == PackageVersionComparison::PackageUpgradeOnly;
}

/*!
 * \brief The PackageOrigin enum specifies where the information contained by a Package object come from.
 *
 * This is useful to know because the different origins provide a different information. For instance, a Package
 * which has only been created from a file name (via Package::fromPkgFileName()) will not contain any dependencies.
 */
enum class PackageOrigin {
    Default, /*!< The package has been default-constructed. That means the package instance is empty. */
    PackageFileName, /*!< A package file name has been parsed via Package::fromPkgFileName(). */
    SourceInfo, /*!< A .SRCINFO file has been parsed via Package::fromInfo(). */
    PackageInfo, /*!< A .PKGINFO file has been parsed via Package::fromInfo(). */
    Database, /*!< A description file from a database has been parsed via Package::fromDescription(). */
    PackageContents, /*!< A binary package has been parsed via Package::fromPkgFile(). */
    AurRpcInfo, /*!< A JSON document from the AUR RPC (info, multiinfo) has been parsed via Package::fromAurRpcJson(). */
    CustomSource, /*! The package info has been populated from a custom source. */
    AurRpcSearch, /*!< A JSON document from the AUR RPC (search) has been parsed via Package::fromAurRpcJson(). */
};

struct LIBPKG_EXPORT PackageNameData {
    std::string compose() const;
    std::string variant() const;
    bool isVcsPackage() const;
    static PackageNameData decompose(std::string_view packageName);

    std::string_view actualName;
    std::string_view targetPrefix;
    std::string_view vcsSuffix;
};

struct DependencySet;
struct Package;

/*!
 * \brief The PackageSpec struct holds a reference to a package.
 * \remarks If id is non-zero, the package is part of a database using that ID.
 */
template <typename PackageType = Package>
struct LIBPKG_EXPORT GenericPackageSpec : public ReflectiveRapidJSON::JsonSerializable<GenericPackageSpec<PackageType>>,
                                          public ReflectiveRapidJSON::BinarySerializable<GenericPackageSpec<PackageType>> {
    explicit GenericPackageSpec(StorageID id = 0, const std::shared_ptr<PackageType> &pkg = nullptr);
    bool operator==(const GenericPackageSpec &other) const;

    StorageID id;
    std::shared_ptr<PackageType> pkg;
};
using PackageSpec = GenericPackageSpec<>;

template <typename PackageType>
inline GenericPackageSpec<PackageType>::GenericPackageSpec(StorageID id, const std::shared_ptr<PackageType> &pkg)
    : id(id)
    , pkg(pkg)
{
}

template <typename PackageType> inline bool GenericPackageSpec<PackageType>::operator==(const GenericPackageSpec &other) const
{
    return id ? id == other.id : pkg == other.pkg;
}

} // namespace LibPkg

namespace std {

template <typename PackageType> struct hash<LibPkg::GenericPackageSpec<PackageType>> {
    std::size_t operator()(const LibPkg::GenericPackageSpec<PackageType> &spec) const
    {
        using std::hash;
        return spec.id ? hash<decltype(spec.id)>()(spec.id) : hash<decltype(spec.pkg)>()(spec.pkg);
    }
};

} // namespace std

namespace LibPkg {

struct LIBPKG_EXPORT PackageBase : public ReflectiveRapidJSON::JsonSerializable<PackageBase>,
                                   public ReflectiveRapidJSON::BinarySerializable<PackageBase, 1> {
    PackageBase() = default;
    PackageBase(const PackageBase &other) = default;
    PackageBase(PackageBase &&other) = default;
    PackageBase &operator=(PackageBase &&other) = default;

    bool isSame(const PackageBase &other) const;
    PackageVersionComparison compareVersion(const PackageBase &other) const;
    std::string computeRegularPackageName() const;
    void clear();

    PackageOrigin origin = PackageOrigin::Default;
    CppUtilities::DateTime timestamp, buildDate;
    std::string name;
    std::string version;
    std::string arch;
    std::vector<std::string> archs; // set if a split package overrides the base archs; if empty, archs from sourceInfo apply
    std::string description;
};

struct LIBPKG_EXPORT Package : public PackageBase,
                               public ReflectiveRapidJSON::JsonSerializable<Package>,
                               public ReflectiveRapidJSON::BinarySerializable<Package, 1> {
    Package() = default;
    Package(const Package &other) = default;
    Package(Package &&other) = default;
    Package &operator=(Package &&other) = default;
    bool providesDependency(const Dependency &dependency) const;
    static void exportProvides(
        const std::shared_ptr<Package> &package, DependencySet &destinationProvides, std::unordered_set<std::string> &destinationLibProvides);
    std::string computeFileName(const char *extension = "pkg.tar.zst") const;
    PackageNameData decomposeName() const;
    void addInfoFromPkgInfoFile(const std::string &info);
    void addDepsAndProvidesFromContainedDirectory(std::string_view directoryPath);
    void addDepsAndProvidesFromContainedFile(
        std::string_view directoryPath, const ArchiveFile &file, std::set<std::string> &dllsReferencedByImportLibs);
    void addDepsAndProvidesFromContents(const FileMap &contents);
    std::vector<std::string> processDllsReferencedByImportLibs(std::set<std::string> &&dllsReferencedByImportLibs);
    bool addDepsAndProvidesFromOtherPackage(const Package &otherPackage, bool force = false);
    bool isArchAny() const;
    using ReflectiveRapidJSON::JsonSerializable<Package>::fromJson;
    using ReflectiveRapidJSON::JsonSerializable<Package>::toJson;
    using ReflectiveRapidJSON::JsonSerializable<Package>::toJsonDocument;
    using ReflectiveRapidJSON::BinarySerializable<Package, 1>::toBinary;
    using ReflectiveRapidJSON::BinarySerializable<Package, 1>::restoreFromBinary;
    using ReflectiveRapidJSON::BinarySerializable<Package, 1>::fromBinary;

    static bool isPkgInfoFileOrBinary(const char *filePath, const char *fileName, mode_t mode);
    static bool isLicense(const char *filePath, const char *fileName, mode_t mode);

    static std::vector<GenericPackageSpec<Package>> fromInfo(const std::string &info, bool isPackageInfo = false);
    static std::shared_ptr<Package> fromDescription(const std::vector<std::string> &descriptionParts);
    static void fromDatabaseFile(const std::string &archivePath, const std::function<bool(const std::shared_ptr<Package> &)> &visitor);
    static std::shared_ptr<Package> fromPkgFile(const std::string &path);
    static std::tuple<std::string_view, std::string_view, std::string_view> fileNameComponents(std::string_view fileName);
    static std::shared_ptr<Package> fromPkgFileName(std::string_view fileName);
    static std::vector<GenericPackageSpec<Package>> fromAurRpcJson(
        const char *jsonData, std::size_t jsonSize, PackageOrigin origin = PackageOrigin::AurRpcInfo);

    using PackageBase::version;
    std::string upstreamUrl;
    std::vector<std::string> licenses;
    std::vector<std::string> groups;
    std::vector<Dependency> dependencies;
    std::vector<Dependency> optionalDependencies;
    std::vector<Dependency> conflicts;
    std::vector<Dependency> provides;
    std::vector<Dependency> replaces;
    std::set<std::string> libprovides;
    std::set<std::string> libdepends;
    std::optional<SourceInfo> sourceInfo;
    std::optional<PackageInfo> packageInfo;
    std::optional<InstallInfo> installInfo;
};

inline bool PackageBase::isSame(const PackageBase &other) const
{
    return name == other.name && version == other.version;
}

inline PackageNameData Package::decomposeName() const
{
    return PackageNameData::decompose(name);
}

struct LIBPKG_EXPORT DependencyDetail : public ReflectiveRapidJSON::JsonSerializable<DependencyDetail>,
                                        public ReflectiveRapidJSON::BinarySerializable<DependencyDetail> {
    DependencyDetail(const std::string &version = std::string(), DependencyMode mode = DependencyMode::Any,
        const std::unordered_set<std::shared_ptr<Package>> &relevantPackages = std::unordered_set<std::shared_ptr<Package>>());
    DependencyDetail(std::string &&version, DependencyMode mode);

    std::string version;
    DependencyMode mode = DependencyMode::Any;
    std::unordered_set<std::shared_ptr<Package>> relevantPackages;
};

inline DependencyDetail::DependencyDetail(
    const std::string &version, DependencyMode mode, const std::unordered_set<std::shared_ptr<Package>> &relevantPackages)
    : version(version)
    , mode(mode)
    , relevantPackages(relevantPackages)
{
}

inline DependencyDetail::DependencyDetail(std::string &&version, DependencyMode mode)
    : version(move(version))
    , mode(mode)
{
}

using DependencySetBase = std::unordered_multimap<std::string, DependencyDetail>;

struct LIBPKG_EXPORT DependencySet : public DependencySetBase {
    DependencySetBase::iterator find(const Dependency &dependency);
    DependencySetBase::iterator find(const std::string &dependencyName, const DependencyDetail &dependencyDetail);
    DependencySetBase::iterator findExact(const Dependency &dependency);
    DependencySetBase::iterator findExact(const std::string &dependencyName, const DependencyDetail &dependencyDetail);
    bool provides(const Dependency &dependency) const;
    bool provides(const std::string &dependencyName, const DependencyDetail &dependencyDetail) const;
    DependencySetBase::iterator add(const Dependency &dependency, const std::shared_ptr<Package> &relevantPackage);
    DependencySetBase::iterator add(
        const std::string &dependencyName, const DependencyDetail &dependencyDetail, const std::shared_ptr<Package> &relevantPackage);
    DependencySetBase::iterator add(std::string &&dependencyName, DependencyDetail &&dependencyDetail);
    void remove(const Dependency &dependency, const std::shared_ptr<Package> &relevantPackage);
    void remove(const std::string &name);
};

} // namespace LibPkg

namespace ReflectiveRapidJSON {

REFLECTIVE_RAPIDJSON_TREAT_AS_MULTI_MAP_OR_HASH(LibPkg::DependencySet);

} // namespace ReflectiveRapidJSON

#endif // LIBPKG_DATA_PACKAGE_H
