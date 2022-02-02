#include "./utils.h"

#include "./data/package.h"

#include <c++utilities/conversion/stringbuilder.h>
#include <c++utilities/io/misc.h>

#include <archive.h>
#include <archive_entry.h>

#include <sys/time.h>

#include <chrono>
#include <exception>
#include <filesystem>
#include <regex>

using namespace std;
using namespace CppUtilities;

namespace LibPkg {

struct AddDirectoryToFileMap {
    void operator()(std::string &&path)
    {
        fileMap[std::move(path)];
    }
    FileMap &fileMap;
};

struct AddFileToFileMap {
    void operator()(std::string &&directoryPath, ArchiveFile &&file)
    {
        fileMap[std::move(directoryPath)].emplace_back(std::move(file));
    }
    FileMap &fileMap;
};

void walkThroughArchiveInternal(struct archive *ar, const string &archiveName, const FilePredicate &isFileRelevant, FileHandler &&fileHandler,
    DirectoryHandler &&directoryHandler)
{
    // iterate through all archive entries
    struct archive_entry *entry;
    while (archive_read_next_header(ar, &entry) == ARCHIVE_OK) {
        // check entry type (only dirs, files and symlinks relevant here)
        const auto entryType(archive_entry_filetype(entry));
        if (entryType != AE_IFDIR && entryType != AE_IFREG && entryType != AE_IFLNK) {
            continue;
        }

        // get file path
        const char *filePath = archive_entry_pathname_utf8(entry);
        if (!filePath) {
            filePath = archive_entry_pathname(entry);
        }
        if (!filePath) {
            continue;
        }

        // get permissions
        const mode_t perm = archive_entry_perm(entry);

        // add directories explicitly to get the entire tree though skipping irrelevant files
        if (entryType == AE_IFDIR) {
            // remove trailing slashes
            const char *dirEnd = filePath;
            for (const char *i = filePath; *i; ++i) {
                if (*i != '/') {
                    dirEnd = i + 1;
                }
            }

            directoryHandler(string(filePath, dirEnd));
            continue;
        }

        // split the path into dir and fileName
        const char *fileName = filePath, *dirEnd = filePath;
        for (const char *i = filePath; *i; ++i) {
            if (*i == '/') {
                fileName = i + 1;
                dirEnd = i;
            }
        }

        // prevent looking into irrelevant files
        if (isFileRelevant && !isFileRelevant(filePath, fileName, perm)) {
            continue;
        }

        // read timestamps
        const auto creationTime = DateTime::fromTimeStampGmt(archive_entry_ctime(entry));
        const auto modificationTime = DateTime::fromTimeStampGmt(archive_entry_mtime(entry));

        // read symlink
        if (entryType == AE_IFLNK) {
            fileHandler(string(filePath, static_cast<string::size_type>(dirEnd - filePath)),
                ArchiveFile(fileName, string(archive_entry_symlink_utf8(entry)), ArchiveFileType::Link, creationTime, modificationTime));
            continue;
        }

        // determine file size to pre-allocate buffer for file content
        const la_int64_t fileSize = archive_entry_size(entry);
        string fileContent;
        if (fileSize > 0) {
            fileContent.reserve(static_cast<string::size_type>(fileSize));
        }

        // read file content
        const char *buff;
        size_t size;
        la_int64_t offset;
        for (;;) {
            int returnCode = archive_read_data_block(ar, reinterpret_cast<const void **>(&buff), &size, &offset);
            if (returnCode == ARCHIVE_EOF || returnCode < ARCHIVE_OK) {
                break;
            }
            fileContent.append(buff, size);
        }

        // move it to results
        fileHandler(string(filePath, static_cast<string::size_type>(dirEnd - filePath)),
            ArchiveFile(fileName, move(fileContent), ArchiveFileType::Regular, creationTime, modificationTime));
    }

    // free resources used by libarchive
    int returnCode = archive_read_free(ar);
    if (returnCode != ARCHIVE_OK) {
        throw runtime_error("Unable to free archive: " + archiveName);
    }
}

void walkThroughArchiveFromBuffer(const string &archiveData, const string &archiveName, const FilePredicate &isFileRelevant,
    FileHandler &&fileHandler, DirectoryHandler &&directoryHandler)
{
    // refuse opening empty buffer
    if (archiveData.empty()) {
        throw std::runtime_error("Unable to open archive \"" % archiveName + "\": received data is empty");
    }
    // open archive buffer using libarchive
    struct archive *ar = archive_read_new();
    archive_read_support_filter_all(ar);
    archive_read_support_format_all(ar);
    int returnCode = archive_read_open_memory(ar, archiveData.data(), archiveData.size());
    if (returnCode != ARCHIVE_OK) {
        if (const char *const error = archive_error_string(ar)) {
            throw std::runtime_error("Unable to open/read archive \"" % archiveName % "\": " + error);
        } else {
            throw std::runtime_error("Unable to open/read archive \"" % archiveName + "\": unable to open archive from memory");
        }
    }
    walkThroughArchiveInternal(ar, archiveName, isFileRelevant, std::move(fileHandler), std::move(directoryHandler));
}

FileMap extractFilesFromBuffer(const string &archiveData, const string &archiveName, const FilePredicate &isFileRelevant)
{
    FileMap results;
    walkThroughArchiveFromBuffer(archiveData, archiveName, isFileRelevant, AddFileToFileMap{ results }, AddDirectoryToFileMap{ results });
    return results;
}

void walkThroughArchive(
    const string &archivePath, const FilePredicate &isFileRelevant, FileHandler &&fileHandler, DirectoryHandler &&directoryHandler)
{
    // open archive file using libarchive
    if (archivePath.empty()) {
        throw std::runtime_error("Unable to open archive: no path specified");
    }
    if (!std::filesystem::file_size(archivePath)) {
        throw std::runtime_error("Unable to open archive \"" % archivePath + "\": file is empty");
    }
    struct archive *ar = archive_read_new();
    archive_read_support_filter_all(ar);
    archive_read_support_format_all(ar);
    const auto returnCode = archive_read_open_filename(ar, archivePath.data(), 10240);
    if (returnCode != ARCHIVE_OK) {
        if (const char *const error = archive_error_string(ar)) {
            throw std::runtime_error("Unable to open/read archive \"" % archivePath % "\": " + error);
        } else {
            throw std::runtime_error("Unable to open/read archive \"" % archivePath + "\": unable to open archive from file");
        }
    }
    walkThroughArchiveInternal(ar, archivePath, isFileRelevant, std::move(fileHandler), std::move(directoryHandler));
}

FileMap extractFiles(const string &archivePath, const FilePredicate &isFileRelevant)
{
    FileMap results;
    walkThroughArchive(archivePath, isFileRelevant, AddFileToFileMap{ results }, AddDirectoryToFileMap{ results });
    return results;
}

/*!
 * \brief Returns the first non-alphanumeric character \a str.
 * \remarks The \a end is returned if \a str only contains alphanumeric characters.
 * \todo Care about ä, ö, ü, ß in version numbers?
 */
const char *firstNonAlphanumericCharacter(const char *str, const char *end)
{
    for (; str != end; ++str) {
        const char c = *str;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) {
            return str;
        }
    }
    return str;
}

/*!
 * \brief Determines when the file with the specified \a path has been modified the last time.
 * \fixme Make no assumptions on the internal resolution so the code is portable (seems not possible with C++17).
 */
CppUtilities::DateTime lastModified(const string &path)
{
    try {
        return CppUtilities::DateTime::fromChronoTimePointGmt(
            chrono::time_point<chrono::system_clock, chrono::nanoseconds>{ filesystem::last_write_time(path).time_since_epoch() }
            + chrono::seconds{ 6437664000 });
    } catch (const runtime_error &) {
        return CppUtilities::DateTime();
    }
}

/*!
 * \brief Sets when the file with the specified \a path has been modified the last time.
 * \fixme Use the std::filesystem library once the time point can be constructed in a portable way.
 */
bool setLastModified(const string &path, DateTime lastModified)
{
    timeval tv[2];
    tv[0].tv_usec = UTIME_OMIT;
    tv[1].tv_sec = lastModified.toTimeStamp();
    tv[1].tv_usec = lastModified.nanosecond();
    return utimes(path.data(), tv) == 0;
}

/*!
 * \brief Override an overridden variable assignment (to ensure the configured default value is actually used and not overridden).
 */
static void overrideOverriddenVariableAssignment(string &pkgbuildContents, string_view variableName, const string *configuredDefaultValue)
{
    if (!configuredDefaultValue || pkgbuildContents.find(variableName) == string::npos) {
        return;
    }
    pkgbuildContents.append(argsToString(
        pkgbuildContents.empty() || pkgbuildContents.back() == '\n' ? "" : "\n", variableName, '=', '\'', *configuredDefaultValue, '\'', '\n'));
}

/*!
 * \brief Amends the PKGBUILD with the specified \a path.
 * \throws Throws std::ios_base::failure when an IO error occurs.
 */
AmendedVersions amendPkgbuild(const string &path, const PackageVersion &existingVersion, const PackageAmendment &amendment)
{
    AmendedVersions amendedVersions;
    if (amendment.isEmpty()) {
        return amendedVersions;
    }

    auto pkgbuildContents = readFile(path, 0x10000);

    // set upstream version
    if (amendment.setUpstreamVersion) {
        static const auto pkgverRegex = regex{ "\npkgver=[^\n]*", regex::extended };
        pkgbuildContents = regex_replace(pkgbuildContents, pkgverRegex, "\npkgver=" + existingVersion.upstream);
    }

    // bump downstream version
    switch (amendment.bumpDownstreamVersion) {
    case PackageAmendment::VersionBump::None:
        break;
    case PackageAmendment::VersionBump::Epoch:
        amendedVersions.newEpoch = numberToString(stringToNumber<unsigned int>(existingVersion.epoch) + 1);
        amendedVersions.newPkgRel = "1";
        break;
    case PackageAmendment::VersionBump::PackageVersion:
        amendedVersions.newPkgRel = numberToString(stringToNumber<double>(existingVersion.package) + 0.1);
        break;
    }
    if (!amendedVersions.newEpoch.empty()) {
        if (pkgbuildContents.find("\nepoch=") != string::npos) {
            static const auto epochRegex = regex{ "\nepoch=[^\n]*", regex::extended };
            pkgbuildContents = regex_replace(pkgbuildContents, epochRegex, "\nepoch=" + amendedVersions.newEpoch);
        } else {
            static const auto epochRegex = regex{ "\npkgver=", regex::extended };
            pkgbuildContents = regex_replace(pkgbuildContents, epochRegex, "\nepoch=" % amendedVersions.newEpoch + "\npkgver=");
        }
    }
    if (!amendedVersions.newPkgRel.empty()) {
        static const auto pkgrelRegex = regex{ "\npkgrel=[^\n]*", regex::extended };
        pkgbuildContents = regex_replace(pkgbuildContents, pkgrelRegex, "\npkgrel=" + amendedVersions.newPkgRel);
    }

    // override overrides for SRCEXT/PKGEXT to ensure defaults from makepkg.conf are used
    overrideOverriddenVariableAssignment(pkgbuildContents, "SRCEXT", amendment.ensureSourceExtension);
    overrideOverriddenVariableAssignment(pkgbuildContents, "PKGEXT", amendment.ensurePackageExtension);

    writeFile(path, pkgbuildContents);
    return amendedVersions;
}

} // namespace LibPkg
