#include "./package.h"
#include "./aur.h"
#include "./binary.h"
#include "./utils.h"

#include "../data/database.h"

#include <c++utilities/conversion/stringbuilder.h>
#include <c++utilities/conversion/stringconversion.h>
#include <c++utilities/io/ansiescapecodes.h>
#include <c++utilities/io/path.h>

#include <cstring>
#include <iostream>
#include <memory>
#include <regex>

#include <sys/stat.h>

using namespace std;
using namespace CppUtilities;
using namespace EscapeCodes;

namespace LibPkg {

Dependency::Dependency(const char *denotation, std::size_t denotationSize)
    : Dependency()
{
    if (denotationSize == std::numeric_limits<std::size_t>::max()) {
        denotationSize = std::strlen(denotation);
    }

    const char *version = nullptr, *description = nullptr;
    size_t versionSize = 0, descriptionSize = 0, nameSize = 0;
    bool hasEpoch = false;
    for (const char *c = denotation, *end = denotation + denotationSize; c != end; ++c) {
        if (description) {
            if (!descriptionSize && *c == ' ') {
                ++description;
            } else {
                ++descriptionSize;
            }
            continue;
        }
        switch (mode) {
        case DependencyMode::Any:
            switch (*c) {
            case '<':
                mode = DependencyMode::LessThan;
                version = c + 1;
                break;
            case '>':
                mode = DependencyMode::GreatherThan;
                version = c + 1;
                break;
            case '=':
                mode = DependencyMode::Equal;
                version = c + 1;
                break;
            case ':':
                description = c + 1;
                break;
            default:
                ++nameSize;
            }
            break;
        case DependencyMode::LessThan:
            switch (*c) {
            case '=':
                mode = DependencyMode::LessEqual;
                ++version;
                break;
            case ':':
                if (hasEpoch) {
                    description = c + 1;
                    break;
                }
                hasEpoch = true;
                [[fallthrough]];
            default:
                ++versionSize;
            }
            break;
        case DependencyMode::GreatherThan:
            switch (*c) {
            case '=':
                mode = DependencyMode::GreatherEqual;
                ++version;
                break;
            case ':':
                if (hasEpoch) {
                    description = c + 1;
                    break;
                }
                hasEpoch = true;
                [[fallthrough]];
            default:
                ++versionSize;
            }
            break;
        default:
            switch (*c) {
            case ':':
                if (hasEpoch) {
                    description = c + 1;
                    break;
                }
                hasEpoch = true;
                [[fallthrough]];
            default:
                ++versionSize;
            }
        }
    }

    // assign values
    name.assign(denotation, nameSize);
    if (version) {
        this->version.assign(version, versionSize);
    }
    if (description) {
        this->description.assign(description, descriptionSize);
    }
}

std::string Dependency::toString() const
{
    // check for empty desc any any mode -> only name relevant
    if (description.empty() && mode == DependencyMode::Any) {
        return name;
    }

    // convert mode to string
    const char *modeStr = nullptr;
    switch (mode) {
    case DependencyMode::Equal:
        modeStr = "=";
        break;
    case DependencyMode::GreatherEqual:
        modeStr = ">=";
        break;
    case DependencyMode::LessEqual:
        modeStr = "<=";
        break;
    case DependencyMode::GreatherThan:
        modeStr = ">";
        break;
    case DependencyMode::LessThan:
        modeStr = "<";
        break;
    default:
        // mode is any, but a desc is present
        return argsToString(name, ':', ' ', description);
    }

    // no desc but mode
    if (description.empty()) {
        return argsToString(name, modeStr, version);
    }
    // all parts present
    return argsToString(name, modeStr, version, ':', ' ', description);
}

PackageVersion PackageVersion::fromString(const char *versionString, size_t versionStringSize)
{
    PackageVersion version;
    // find epoch
    const char *const firstAlphanumeric = firstNonAlphanumericCharacter(versionString, versionString + versionStringSize);
    if (*firstAlphanumeric == ':') {
        version.epoch.assign(versionString, static_cast<string::size_type>(firstAlphanumeric - versionString));
        versionStringSize -= static_cast<size_t>(firstAlphanumeric - versionString) + 1;
        versionString = firstAlphanumeric + 1;
    }
    // find pkgrel
    const char *const end = versionString + versionStringSize;
    for (const char *i = end - 1; i >= versionString; --i) {
        if (*i == '-') {
            const auto pkgrelSize = end - i - 1;
            if (pkgrelSize > 0) {
                version.package.assign(i + 1, static_cast<string::size_type>(pkgrelSize));
            }
            versionStringSize -= static_cast<size_t>(end - i);
            break;
        }
    }
    // pkgver remains
    version.upstream.assign(versionString, versionStringSize);
    return version;
}

#define if_field(x) if (!strncmp(field, x, fieldSize))
#define else_if_field(x) else if (!strncmp(field, x, fieldSize))
#define valueString std::string_view(value, valueSize)
#define ensure_pkg_info                                                                                                                              \
    if (!package.packageInfo)                                                                                                                        \
    package.packageInfo = make_optional<PackageInfo>()
#define ensure_install_info                                                                                                                          \
    if (!package.installInfo)                                                                                                                        \
    package.installInfo = make_optional<InstallInfo>()

static void addPackageInfo(Package &package, PackageVersion &version, const char *field, size_t fieldSize, const char *value, size_t valueSize,
    bool isPackageInfo, std::size_t packageCount)
{
    if_field("pkgbase")
    {
        package.sourceInfo->name = valueString;
    }
    else_if_field("pkgname")
    {
        package.name = valueString;
    }
    else_if_field("epoch")
    {
        version.epoch = valueString;
    }
    else_if_field("pkgver")
    {
        version.upstream = valueString;
    }
    else_if_field("pkgrel")
    {
        version.package = valueString;
    }
    else_if_field("pkgdesc")
    {
        package.description = valueString;
    }
    else_if_field("url")
    {
        package.upstreamUrl = valueString;
    }
    else_if_field("arch")
    {
        if (isPackageInfo) {
            // add as binary arch when parsing PKGINFO
            ensure_pkg_info;
            package.arch = valueString;
        } else if (!packageCount) {
            // add to sourceInfo when still parsing base info
            package.sourceInfo->archs.emplace_back(value, valueSize);
        } else {
            // add to package itself when a split package overrides the archs from the base
            package.archs.emplace_back(value, valueSize);
        }
    }
    else_if_field("license")
    {
        package.licenses.emplace_back(value, valueSize);
    }
    else_if_field("depends")
    {
        package.dependencies.emplace_back(Dependency::fromString(value, valueSize));
    }
    else_if_field("makedepends")
    {
        package.sourceInfo->makeDependencies.emplace_back(Dependency::fromString(value, valueSize));
    }
    else_if_field("checkdepends")
    {
        package.sourceInfo->checkDependencies.emplace_back(Dependency::fromString(value, valueSize));
    }
    else_if_field("optdepends")
    {
        package.optionalDependencies.emplace_back(Dependency::fromString(value, valueSize));
    }
    else_if_field("conflicts")
    {
        package.conflicts.emplace_back(Dependency::fromString(value, valueSize));
    }
    else_if_field("provides")
    {
        package.provides.emplace_back(Dependency::fromString(value, valueSize));
    }
    else_if_field("replaces")
    {
        package.replaces.emplace_back(Dependency::fromString(value, valueSize));
    }
    else_if_field("source")
    {
        package.sourceInfo->sources.emplace_back().path = valueString;
    }
    else_if_field("size")
    {
        package.packageInfo->size = stringToNumber<decltype(package.packageInfo->size)>(valueString);
    }
    else_if_field("builddate")
    {
        package.buildDate = DateTime::fromTimeStampGmt(stringToNumber<std::time_t>(valueString));
        ensure_pkg_info;
        package.packageInfo->buildDate = package.buildDate;
    }
    else_if_field("packager")
    {
        ensure_pkg_info;
        package.packageInfo->packager = valueString;
    }
}

static void addPackageDescription(Package &package, const char *field, size_t fieldSize, const char *value, size_t valueSize)
{
    if_field("BASE")
    {
        package.sourceInfo->name = valueString;
    }
    else_if_field("NAME")
    {
        package.name = valueString;
    }
    else_if_field("VERSION")
    {
        package.version = valueString;
    }
    else_if_field("DESC")
    {
        package.description = valueString;
    }
    else_if_field("URL")
    {
        package.upstreamUrl = valueString;
    }
    else_if_field("ARCH")
    {
        package.arch = valueString;
    }
    else_if_field("LICENSE")
    {
        package.licenses.emplace_back(value, valueSize);
    }
    else_if_field("DEPENDS")
    {
        package.dependencies.emplace_back(value, valueSize);
    }
    else_if_field("MAKEDEPENDS")
    {
        package.sourceInfo->makeDependencies.emplace_back(value, valueSize);
    }
    else_if_field("CHECKDEPENDS")
    {
        package.sourceInfo->checkDependencies.emplace_back(value, valueSize);
    }
    else_if_field("OPTDEPENDS")
    {
        package.optionalDependencies.emplace_back(value, valueSize);
    }
    else_if_field("CONFLICTS")
    {
        package.conflicts.emplace_back(value, valueSize);
    }
    else_if_field("PROVIDES")
    {
        package.provides.emplace_back(value, valueSize);
    }
    else_if_field("REPLACES")
    {
        package.replaces.emplace_back(value, valueSize);
    }
    else_if_field("BUILDDATE")
    {
        package.buildDate = package.packageInfo->buildDate = DateTime::fromTimeStampGmt(stringToNumber<time_t>(valueString));
    }
    else_if_field("INSTALLDATE")
    {
        ensure_install_info;
        package.installInfo->installDate = DateTime::fromTimeStampGmt(stringToNumber<time_t>(valueString));
    }
    else_if_field("ISIZE")
    {
        ensure_install_info;
        package.installInfo->installedSize = stringToNumber<std::uint32_t>(valueString);
    }
    else_if_field("SIZE")
    {
        ensure_install_info;
        package.installInfo->installedSize = stringToNumber<std::uint32_t>(valueString);
    }
    else_if_field("CSIZE")
    {
        package.packageInfo->size = stringToNumber<std::uint32_t>(valueString);
    }
    else_if_field("PACKAGER")
    {
        package.packageInfo->packager = valueString;
    }
    else_if_field("MD5SUM")
    {
        package.packageInfo->md5 = valueString;
    }
    else_if_field("SHA256SUM")
    {
        package.packageInfo->sha256 = valueString;
    }
    else_if_field("PGPSIG")
    {
        package.packageInfo->pgpSignature = valueString;
    }
    else_if_field("FILES")
    {
        package.packageInfo->files.emplace_back(value, valueSize);
    }
    else_if_field("REASON")
    {
        ensure_install_info;
        package.installInfo->installStatus = (valueSize == 1 && *value == '1') ? InstallStatus::AsDependency : InstallStatus::Explicit;
    }
    else_if_field("VALIDATION")
    {
        if (!strncmp(value, "md5", valueSize)) {
            package.installInfo->validationMethods |= PackageValidation::Md5Sum;
        } else if (!strncmp(value, "sha256", valueSize)) {
            package.installInfo->validationMethods |= PackageValidation::Sha256Sum;
        } else if (!strncmp(value, "pgp", valueSize)) {
            package.installInfo->validationMethods |= PackageValidation::PgpSignature;
        } else {
            throw ConversionException("invalid validation " + string(value, valueSize));
        }
    }
    else_if_field("GROUPS")
    {
        package.groups.emplace_back(value, valueSize);
    }
    else_if_field("FILENAME")
    {
        package.packageInfo->fileName = valueString;
    }
}

#undef if_field
#undef else_if_field
#undef valueString
#undef ensure_pkg_info

static void addVersionInfo(Package &package, PackageVersion &version, bool isPackageInfo)
{
    if (isPackageInfo) {
        // when parsing .PKGINFO pkgver specifies the complete version which is
        // treated in addPackageInfo always as upstream version
        package.version = version.upstream;
    } else {
        // when parsing .SRCINFO pkgver only contains upstream version; epoch and pkgrel are
        // specified separately
        if (version.package.empty()) {
            version.package = '1';
        }
        if (version.epoch.empty()) {
            package.version = version.upstream % '-' + version.package;
        } else {
            package.version = version.epoch % ':' % version.upstream % '-' + version.package;
        }
    }
}

static void parsePkgInfo(const std::string &info, const std::function<Package *(Package &)> &nextPackage, bool isPackageInfo)
{
    // define variables to store intermediate results while still parsing package base
    auto version = PackageVersion();
    auto packageCount = std::size_t();
    auto basePackage = Package();
    basePackage.origin = isPackageInfo ? PackageOrigin::PackageInfo : PackageOrigin::SourceInfo;
    basePackage.sourceInfo = std::make_optional<SourceInfo>();
    auto &packageBase = basePackage.sourceInfo->name;

    // states
    enum {
        FieldName, // reading field name (initial state)
        EquationSign, // expecting equation sign
        Pad, // expecting padding
        FieldValue, // reading field value
        Comment // reading comment
    } state
        = FieldName;

    // variables for current field
    const char *currentFieldName = nullptr;
    size_t currentFieldNameSize = 0;
    const char *currentFieldValue = nullptr;
    size_t currentFieldValueSize = 0;

    // do actual parsing via state machine
    Package *currentPackage = nullptr;
    for (const char *i = info.data(); *i; ++i) {
        const char c = *i;
        switch (state) {
        case FieldName:
            switch (c) {
            case '#':
                // discard truncated line
                currentFieldName = nullptr;
                currentFieldNameSize = 0;
                state = Comment;
                [[fallthrough]];
            case ' ':
                // field name complete, expect equation sign
                if (currentFieldName) {
                    state = EquationSign;
                }
                break;
            case '\n':
            case '\r':
            case '\t':
                // discard truncated line
                currentFieldName = nullptr;
                currentFieldNameSize = 0;
                break;
            default:
                if (!currentFieldName) {
                    currentFieldName = i;
                }
                ++currentFieldNameSize;
            }
            break;
        case EquationSign:
            switch (c) {
            case '=':
                state = Pad;
                break;
            case '\n':
            case '\r':
            case '\t':
                // unexpected new line -> discard truncated line
                state = FieldName;
                currentFieldName = nullptr;
                currentFieldNameSize = 0;
                break;
            default:; // ignore unexpected characters
            }
            break;
        case Pad:
            switch (c) {
            case ' ':
                state = FieldValue;
                break;
            case '\n':
            case '\r':
            case '\t':
                // unexpected new line -> discard truncated line
                state = FieldName;
                currentFieldName = nullptr;
                currentFieldNameSize = 0;
                break;
            default:; // ignore unexpected characters
            }
            break;
        case FieldValue:
            switch (c) {
            case '\n':
            case '\r':
                // field concluded
                // -> expect next field name
                state = FieldName;
                // -> handle pkgbase/pkgname
                if (currentFieldName && !strncmp(currentFieldName, "pkgbase", currentFieldNameSize)) {
                    packageBase = string(currentFieldValue, currentFieldValueSize);
                } else if (currentFieldName && !strncmp(currentFieldName, "pkgname", currentFieldNameSize)) {
                    // next package
                    if (packageBase.empty()) {
                        // no pkgbase specified so far -> use the first pkgname as pkgbase
                        packageBase = string(currentFieldValue, currentFieldValueSize);
                    }
                    if (currentPackage) {
                        addVersionInfo(*currentPackage, version, isPackageInfo);
                        currentPackage->provides.emplace_back(currentPackage->name, currentPackage->version);
                    }
                    // find next package
                    currentPackage = nextPackage(basePackage);
                    ++packageCount;
                }
                // -> add field to ...
                try {
                    if (currentPackage) {
                        // ... concrete package info if there's already a concrete package
                        addPackageInfo(*currentPackage, version, currentFieldName, currentFieldNameSize, currentFieldValue, currentFieldValueSize,
                            isPackageInfo, packageCount);
                    } else {
                        // ... base info if still parsing general info
                        addPackageInfo(basePackage, version, currentFieldName, currentFieldNameSize, currentFieldValue, currentFieldValueSize,
                            isPackageInfo, packageCount);
                    }
                } catch (const ConversionException &) {
                    // FIXME: error handling
                }

                currentFieldName = currentFieldValue = nullptr;
                currentFieldNameSize = currentFieldValueSize = 0;
                break;
            default:
                if (!currentFieldValue) {
                    currentFieldValue = i;
                }
                ++currentFieldValueSize;
            }
            break;
        case Comment:
            switch (c) {
            case '\n':
            case '\r':
            case '\t':
                state = FieldName;
                break;
            default:; // ignore outcommented characters
            }
            break;
        }
    }
    if (currentPackage) {
        addVersionInfo(*currentPackage, version, isPackageInfo);
        currentPackage->provides.emplace_back(currentPackage->name, currentPackage->version);
    }
}

/*!
 * \brief Parses the specified \a info and returns the results as one or more (in case of split packages) packages.
 * \remarks The returned storage ID is always zero as the returned packages are not part of a database yet. One might
 *          set the ID later when placing the package into a database.
 */
std::vector<PackageSpec> Package::fromInfo(const std::string &info, bool isPackageInfo)
{
    auto packages = std::vector<PackageSpec>();
    const auto nextPackage = [&](Package &basePackage) { return packages.emplace_back(0, std::make_shared<Package>(basePackage)).pkg.get(); };
    parsePkgInfo(info, nextPackage, isPackageInfo);
    return packages;
}

/*!
 * \brief Parses the specified \a descriptionParts and returns the results as package.
 */
std::shared_ptr<Package> Package::fromDescription(const std::vector<std::string> &descriptionParts)
{
    auto package = std::make_shared<Package>();
    package->origin = PackageOrigin::Database;
    package->sourceInfo = std::make_optional<SourceInfo>();
    package->packageInfo = std::make_optional<PackageInfo>();
    for (const auto &desc : descriptionParts) {
        // states
        enum {
            FieldName, // reading field name
            NewLine, // expecting new line (after field name)
            Next, // start reading next field value / next field name (initial state)
            FieldValue, // reading field value
        } state
            = Next;

        // variables for current field
        const char *currentFieldName = nullptr;
        std::size_t currentFieldNameSize = 0;
        const char *currentFieldValue = nullptr;
        std::size_t currentFieldValueSize = 0;

        // do actual parsing via state machine
        for (const char *i = desc.data(); *i; ++i) {
            const char c = *i;
            switch (state) {
            case FieldName:
                switch (c) {
                case '%':
                    state = NewLine;
                    break;
                default:
                    if (!currentFieldName) {
                        currentFieldName = i;
                    }
                    ++currentFieldNameSize;
                }
                break;
            case NewLine:
                switch (c) {
                case '\n':
                case '\r':
                    state = Next;
                    break;
                default:; // ignore unexpected characters
                }
                break;
            case Next:
                switch (c) {
                case '\n':
                case '\r':
                case '\t':
                case ' ':
                    break;
                case '%':
                    state = FieldName;
                    currentFieldName = nullptr;
                    currentFieldNameSize = 0;
                    break;
                default:
                    state = FieldValue;
                    if (!currentFieldValue) {
                        currentFieldValue = i;
                    }
                    ++currentFieldValueSize;
                }
                break;
            case FieldValue:
                switch (c) {
                case '\n':
                case '\r':
                    state = Next;
                    try {
                        addPackageDescription(*package, currentFieldName, currentFieldNameSize, currentFieldValue, currentFieldValueSize);
                    } catch (const ConversionException &) {
                        // FIXME: error handling
                    }
                    currentFieldValue = nullptr;
                    currentFieldValueSize = 0;
                    break;
                default:
                    if (!currentFieldValue) {
                        currentFieldValue = i;
                    }
                    ++currentFieldValueSize;
                }
            }
        }
        if (currentFieldName && currentFieldValue) {
            addPackageDescription(*package, currentFieldName, currentFieldNameSize, currentFieldValue, currentFieldValueSize);
        }
    }
    package->provides.emplace_back(package->name, package->version);
    return package;
}

void Package::fromDatabaseFile(const std::string &archivePath, const std::function<bool(const std::shared_ptr<Package> &)> &visitor)
{
    // walk though archive, file-by-file; parse files as soon as desc/files available and return via visitor
    auto packages = std::unordered_map<std::string, std::vector<std::string>>();
    walkThroughArchive(
        archivePath, &Database::isFileRelevant,
        [&packages, &visitor](std::string_view path, ArchiveFile &&file) {
            auto i = packages.find(std::string(path)); // FIXME: this should actually accept std::string_view in C++20
            if (i == packages.end()) {
                i = packages.emplace(std::string(path), 2).first;
            }
            auto &parts = i->second;
            if (file.name == "desc") {
                parts[0] = std::move(file.content);
            } else if (file.name == "files") {
                parts[1] = std::move(file.content);
            }
            if (!parts[0].empty() && !parts[1].empty()) {
                if (visitor(Package::fromDescription(parts))) {
                    return true;
                }
                // remove buffered data immediately to avoid using too much memory
                packages.erase(i);
            }
            return false;
        },
        [](std::string_view) { return false; });

    // take care of packages without "files" file
    for (auto &[packageName, parts] : packages) {
        if (!parts[0].empty()) {
            if (visitor(Package::fromDescription(parts))) {
                return;
            }
        }
    }
}

bool Package::isPkgInfoFileOrBinary(const char *filePath, const char *fileName, mode_t mode)
{
    return !strcmp(fileName, ".PKGINFO") || mode == S_IXUSR || strstr(filePath, "usr/bin") == filePath || strstr(filePath, "usr/lib") == filePath
        || strstr(fileName, ".so") > fileName || strstr(fileName, ".dll") > fileName || strstr(fileName, ".a") > fileName;
}

bool LibPkg::Package::isLicense(const char *filePath, const char *fileName, mode_t mode)
{
    CPP_UTILITIES_UNUSED(mode)
    return strstr(fileName, "LICENSE") == fileName || strstr(filePath, "usr/share/licenses") == filePath;
}

void Package::addInfoFromPkgInfoFile(const string &info)
{
    parsePkgInfo(info, [this](Package &) { return this; }, true);
}

static const regex pythonVersionRegex("usr/lib/python(2|3)\\.([0-9]*)(\\..*)?/site-packages");
static const regex perlVersionRegex("usr/lib/perl5/5\\.([0-9]*)(\\..*)?/vendor_perl");

void Package::addDepsAndProvidesFromContainedDirectory(std::string_view directoryPath)
{
    // check for Python modules
    thread_local auto match = std::match_results<std::string_view::const_iterator>();
    if (std::regex_match(directoryPath.begin(), directoryPath.end(), match, pythonVersionRegex)) {
        const auto majorVersion = match[1].str();
        const auto minorVersion = match[2].str();
        const char *const pythonPackage(majorVersion == "3" ? "python" : "python2");
        auto currentVersion = argsToString(majorVersion, '.', minorVersion);
        auto nextVersion = argsToString(majorVersion, '.', stringToNumber<unsigned int>(minorVersion) + 1);
        dependencies.emplace_back(pythonPackage, std::move(currentVersion), DependencyMode::GreatherEqual);
        dependencies.emplace_back(pythonPackage, std::move(nextVersion), DependencyMode::LessThan);
    }

    // check for Perl modules
    if (std::regex_match(directoryPath.begin(), directoryPath.end(), match, perlVersionRegex)) {
        const auto minorVersion = match[1].str();
        auto currentVersion = "5." + minorVersion;
        auto nextVersion = "5." + numberToString(stringToNumber<unsigned int>(minorVersion) + 1);
        dependencies.emplace_back("perl", std::move(currentVersion), DependencyMode::GreatherEqual);
        dependencies.emplace_back("perl", std::move(nextVersion), DependencyMode::LessThan);
    }
}

void Package::addDepsAndProvidesFromContainedFile(
    std::string_view directoryPath, const ArchiveFile &file, std::set<std::string> &dllsReferencedByImportLibs)
{
    // ignore files under "/opt" except Android libraries as those files are not for system-wide use
    // note: Otherwise, if a package contains e.g. an old version of libxml2 this library would be considered available and a possibly required rebuild
    //       due to an update of the system-wide libxml2 library not be considered.
    // note: For Android we should only consider custom libs and sysroot libs provided by the NDK. Other libraries are also just distracting.
    if (directoryPath.starts_with("opt/")
        && !(directoryPath.starts_with("opt/android-libs/")
            || (directoryPath.starts_with("opt/android-ndk/")
                && (directoryPath.contains("/sysroot/") // Android platform libs, e.g. libc.so, libm.so, libandroid.so …
                    || directoryPath.contains("/lib/linux/") // compiler runtime and libomp.so
                    )))) {
        return;
    }
    try {
        Binary binary;
        binary.load(file.content, file.name, directoryPath, file.type == ArchiveFileType::Regular);
        if (!binary.name.empty()) {
            if (binary.type == BinaryType::Ar && binary.subType == BinarySubType::WindowsImportLibrary) {
                dllsReferencedByImportLibs.emplace(binary.addPrefix(binary.name));
            } else {
                libprovides.emplace(binary.addPrefix(binary.name));
            }
        }
        for (const auto &require : binary.requiredLibs) {
            libdepends.emplace(binary.addPrefix(require));
        }
    } catch (const std::ios_base::failure &) {
        // TODO: handle IO error
    } catch (const std::runtime_error &) {
        // TODO: handle parsing error
    }
}

std::vector<std::string> Package::processDllsReferencedByImportLibs(std::set<string> &&dllsReferencedByImportLibs)
{
    // check whether all DLLs referenced by import libraries are actually part of the package
    auto issues = std::vector<std::string>();
    if (dllsReferencedByImportLibs.empty()) {
        return issues;
    } else if (name == "mingw-w64-crt" || (name.starts_with("mingw-w64-clang-") && name.ends_with("-crt"))) {
        // assume the CRT references DLLs provided by Windows itself
        libprovides = std::move(dllsReferencedByImportLibs);
        return issues;
    }
    for (const auto &referencedDLL : dllsReferencedByImportLibs) {
        if (libprovides.find(referencedDLL) == libprovides.end()) {
            issues.emplace_back("DLL " % referencedDLL % " is missing in " + name);
        }
    }
    return issues;
}

/*!
 * \brief Adds dependencies and provides from the specified \a contents.
 * \deprecated This function is not actually used anymore because ReloadLibraryDependencies does this in a better way
 *             using LibPkg::walkThroughArchive().
 */
void Package::addDepsAndProvidesFromContents(const FileMap &contents)
{
    std::set<std::string> dllsReferencedByImportLibs;
    for (const auto &[directoryPath, files] : contents) {
        addDepsAndProvidesFromContainedDirectory(directoryPath);
        for (const auto &file : files) {
            addDepsAndProvidesFromContainedFile(directoryPath, file, dllsReferencedByImportLibs);
        }
    }
    processDllsReferencedByImportLibs(std::move(dllsReferencedByImportLibs));
}

std::shared_ptr<Package> Package::fromPkgFile(const string &path)
{
    std::set<std::string> dllsReferencedByImportLibs;
    Package tmpPackageForLibraryDeps;
    shared_ptr<Package> package;
    CppUtilities::walkThroughArchive(
        path, &LibPkg::Package::isPkgInfoFileOrBinary,
        [&package, &tmpPackageForLibraryDeps, &dllsReferencedByImportLibs](std::string_view directoryPath, CppUtilities::ArchiveFile &&file) {
            if (directoryPath.empty() && file.name == ".PKGINFO") {
                if (package) {
                    return false; // only consider one .PKGINFO file (multiple ones are likely not possible in any supported archive formats anyways)
                }
                auto packages = fromInfo(file.content, true);
                if (!packages.empty()) {
                    package = std::move(packages.front().pkg);
                }
                return false;
            }
            tmpPackageForLibraryDeps.addDepsAndProvidesFromContainedFile(directoryPath, file, dllsReferencedByImportLibs);
            return false;
        },
        [&tmpPackageForLibraryDeps](std::string_view directoryPath) {
            if (directoryPath.empty()) {
                return false;
            }
            tmpPackageForLibraryDeps.addDepsAndProvidesFromContainedDirectory(directoryPath);
            return false;
        });
    if (!package) {
        throw runtime_error("Package " % path + " does not contain a valid .PKGINFO");
    }
    if (!package->packageInfo) {
        package->packageInfo = std::make_optional<PackageInfo>();
    }
    package->packageInfo->fileName = fileName(path);
    package->addDepsAndProvidesFromOtherPackage(tmpPackageForLibraryDeps, true);
    package->processDllsReferencedByImportLibs(std::move(dllsReferencedByImportLibs));
    return package;
}

std::tuple<std::string_view, std::string_view, std::string_view> Package::fileNameComponents(std::string_view fileName)
{
    auto extBegin = fileName.rfind(".pkg");
    auto isSourcePackage = false;
    if (extBegin == string::npos) {
        extBegin = fileName.rfind(".src");
        isSourcePackage = true;
    }
    if (extBegin == string::npos) {
        throw runtime_error("File name " % fileName + " does not have .pkg or .src extension.");
    }
    const auto archBegin = isSourcePackage ? (extBegin) : fileName.rfind('-', extBegin - 1);
    if (archBegin == string::npos) {
        throw runtime_error("File name " % fileName + " does not contain architecture.");
    }
    auto pkgrelBegin = fileName.rfind('-', archBegin - 1);
    if (pkgrelBegin == string::npos) {
        throw runtime_error("File name " % fileName + " does not contain pkgrel.");
    }
    const auto pkgverBegin = fileName.rfind('-', pkgrelBegin - 1);
    if (pkgverBegin == string::npos) {
        throw runtime_error("File name " % fileName + " does not contain pkgver.");
    }
    return make_tuple(fileName.substr(0, pkgverBegin), fileName.substr(pkgverBegin + 1, archBegin - pkgverBegin - 1),
        isSourcePackage ? "src" : fileName.substr(archBegin + 1, extBegin - archBegin - 1));
}

std::shared_ptr<Package> Package::fromPkgFileName(std::string_view fileName)
{
    const auto [name, version, arch] = fileNameComponents(fileName);
    auto pkg = make_shared<Package>();
    pkg->origin = PackageOrigin::PackageFileName;
    pkg->name = name;
    pkg->version = version;
    pkg->arch = arch;
    pkg->provides.emplace_back(pkg->name, pkg->version);
    pkg->packageInfo = std::make_optional<PackageInfo>();
    pkg->packageInfo->fileName = fileName;
    return pkg;
}

std::vector<PackageSpec> Package::fromAurRpcJson(const char *jsonData, std::size_t jsonSize, PackageOrigin origin)
{
    auto errors = ReflectiveRapidJSON::JsonDeserializationErrors();
    auto rpcMultiInfo = AurRpcMultiInfo::fromJson(jsonData, jsonSize, &errors);

    auto packages = std::vector<PackageSpec>();
    packages.reserve(rpcMultiInfo.results.size());

    for (auto &result : rpcMultiInfo.results) {
        auto &spec = packages.emplace_back();
        auto *package = &*(spec.pkg = std::make_shared<Package>());
        auto &sourceInfo = package->sourceInfo = std::make_optional<SourceInfo>();
        package->origin = origin;
        package->timestamp = DateTime::fromTimeStampGmt(result.LastModified);
        package->name = std::move(result.Name);
        package->version = std::move(result.Version);
        package->description = std::move(result.Description);
        package->upstreamUrl = std::move(result.URL);
        package->licenses = std::move(result.License);
        package->groups = std::move(result.Groups);
        package->conflicts.reserve(result.Conflicts.size());
        for (const auto &dep : result.Conflicts) {
            package->conflicts.emplace_back(dep.data(), dep.size());
        }
        package->provides.reserve(result.Provides.size());
        for (const auto &dep : result.Provides) {
            package->provides.emplace_back(dep.data(), dep.size());
        }
        package->replaces.reserve(result.Replaces.size());
        for (const auto &dep : result.Replaces) {
            package->replaces.emplace_back(dep.data(), dep.size());
        }
        for (auto &dependencyName : result.Depends) {
            package->dependencies.emplace_back(dependencyName.data(), dependencyName.size());
        }
        for (auto &dependencyName : result.OptDepends) {
            package->optionalDependencies.emplace_back(dependencyName.data(), dependencyName.size());
        }
        for (auto &dependencyName : result.MakeDepends) {
            sourceInfo->makeDependencies.emplace_back(dependencyName.data(), dependencyName.size());
        }
        for (auto &dependencyName : result.CheckDepends) {
            sourceInfo->checkDependencies.emplace_back(dependencyName.data(), dependencyName.size());
        }
        sourceInfo->name = std::move(result.PackageBase);
        sourceInfo->maintainer = std::move(result.Maintainer);
        sourceInfo->id = std::move(result.ID);
        sourceInfo->votes = std::move(result.NumVotes);
        if (result.OutOfDate) {
            sourceInfo->outOfDate = DateTime::fromTimeStampGmt(*result.OutOfDate);
        }
        sourceInfo->firstSubmitted = DateTime::fromTimeStampGmt(result.FirstSubmitted);
        sourceInfo->lastModified = package->timestamp;
        sourceInfo->url = std::move(result.URLPath);
    }
    return packages;
}

string PackageNameData::compose() const
{
    auto res = std::string();
    res.reserve(targetPrefix.size() + vcsSuffix.size() + actualName.size() + 2);
    if (!targetPrefix.empty()) {
        res += targetPrefix;
        res += '-';
    }
    res += actualName;
    if (!vcsSuffix.empty()) {
        res += '-';
        res += vcsSuffix;
    }
    return res;
}

string PackageNameData::variant() const
{
    if (targetPrefix.empty() && vcsSuffix.empty()) {
        return "default";
    }
    if (targetPrefix.empty()) {
        return string(vcsSuffix);
    }
    if (vcsSuffix.empty()) {
        return string(targetPrefix);
    }
    return targetPrefix % '-' + vcsSuffix;
}

bool PackageNameData::isVcsPackage() const
{
    if (vcsSuffix.empty()) {
        return false;
    }
    static const auto vcsSuffixes = std::unordered_set<std::string_view>{ "cvs", "svn", "hg", "darcs", "bzr", "git" };
    const auto lastDash = vcsSuffix.rfind('-');
    return vcsSuffixes.find(lastDash == std::string_view::npos ? vcsSuffix : vcsSuffix.substr(lastDash + 1)) != vcsSuffixes.end();
}

PackageNameData PackageNameData::decompose(std::string_view packageName)
{
    static constexpr auto targetPrefixBrackets = 5;
    static const auto packageNameRegex
        = std::regex("((lib32|mingw-w64(-(ucrt|clang(-x86_64|-aarch64)?)?)?|android(-(aarch64|x86-64|x86|armv7a-eabi))?|arm-none-"
                     "eabi|aarch64-linux-"
                     "gnu|static-compat|riscv64-linux|avr|psp|wasm)-)?(.*?)((-(cvs|svn|hg|darcs|bzr|git|custom|compat|static|qt\\d+|doc|cli|gui))*)");
    auto data = PackageNameData{};
    auto match = std::cmatch{};
    if (!regex_match(packageName.cbegin(), packageName.cend(), match, packageNameRegex)) {
        return data;
    }
    static constexpr auto matchToStringView = [](auto regexMatch, std::size_t offset = 0) {
        return std::string_view(
            regexMatch.first + offset, static_cast<std::size_t>(regexMatch.length() - static_cast<std::cmatch::difference_type>(offset)));
    };
    data.targetPrefix = matchToStringView(match[2]);
    data.actualName = matchToStringView(match[3 + targetPrefixBrackets]);
    data.vcsSuffix = match[4 + targetPrefixBrackets].length() ? matchToStringView(match[4 + targetPrefixBrackets], 1) : std::string_view{};
    return data;
}

} // namespace LibPkg
