#ifndef LIBPKG_PARSER_BINARY_H
#define LIBPKG_PARSER_BINARY_H

#include "../global.h"

#include <iostream>
#include <set>
#include <string>
#include <vector>

namespace CppUtilities {
class BinaryReader;
}

namespace LibPkg {

enum class BinaryType { Invalid, Elf, Pe, Ar };

LIBPKG_EXPORT std::ostream &operator<<(std::ostream &o, const BinaryType &mode);

enum class BinarySubType { None, Relocatable, Executable, SharedObject, Core, WindowsImportLibrary, LoProc = 0xff00, HiProc = 0xffff };

LIBPKG_EXPORT std::ostream &operator<<(std::ostream &o, const BinarySubType &mode);

enum class BinaryClass { Invalid, Class32Bit, Class64Bit };

LIBPKG_EXPORT std::ostream &operator<<(std::ostream &o, const BinaryClass &mode);

struct LIBPKG_EXPORT VirtualAddressMappingEntry {
    constexpr VirtualAddressMappingEntry(std::uint64_t fileOffset, std::uint64_t fileSize, std::uint64_t virtualAddress, std::uint64_t virtualSize);

    constexpr bool isVirtualAddressInRange(std::uint64_t virtualAddress) const;
    constexpr std::uint64_t virtualAddressToFileOffset(std::uint64_t virtualAddress) const;
    constexpr bool isFileOffsetInRange(std::uint64_t fileOffset) const;
    constexpr std::uint64_t fileOffsetToVirtualAddress(std::uint64_t fileOffset) const;

    const std::uint64_t fileStartOffset;
    const std::uint64_t fileEndOffset;
    const std::uint64_t virtualStartAddress;
    const std::uint64_t virtualEndAddress;
};

constexpr VirtualAddressMappingEntry::VirtualAddressMappingEntry(
    std::uint64_t fileOffset, std::uint64_t fileSize, std::uint64_t virtualAddress, std::uint64_t virtualSize)
    : fileStartOffset(fileOffset)
    , fileEndOffset(fileOffset + fileSize)
    , virtualStartAddress(virtualAddress)
    , virtualEndAddress(virtualAddress + virtualSize)
{
}

constexpr bool VirtualAddressMappingEntry::isVirtualAddressInRange(std::uint64_t virtualAddress) const
{
    return virtualAddress >= virtualStartAddress && virtualAddress < virtualEndAddress;
}

constexpr std::uint64_t VirtualAddressMappingEntry::virtualAddressToFileOffset(std::uint64_t virtualAddress) const
{
    return virtualStartAddress > fileStartOffset ? (virtualAddress - (virtualStartAddress - fileStartOffset))
                                                 : (virtualAddress + (fileStartOffset - virtualStartAddress));
}

constexpr bool VirtualAddressMappingEntry::isFileOffsetInRange(std::uint64_t fileOffset) const
{
    return fileOffset >= fileStartOffset && fileOffset < fileEndOffset;
}

constexpr std::uint64_t VirtualAddressMappingEntry::fileOffsetToVirtualAddress(std::uint64_t fileOffset) const
{
    return virtualStartAddress > fileStartOffset ? (fileOffset + (virtualStartAddress - fileStartOffset))
                                                 : (fileOffset - (fileStartOffset - virtualStartAddress));
}

struct LIBPKG_EXPORT VirtualAddressMapping : public std::vector<VirtualAddressMappingEntry> {
    VirtualAddressMapping();
    std::uint64_t virtualAddressToFileOffset(std::uint64_t virtualAddress) const;
    std::uint64_t fileOffsetToVirtualAddress(std::uint64_t fileOffset) const;
};

inline VirtualAddressMapping::VirtualAddressMapping()
{
}

struct LIBPKG_EXPORT Binary {
    void load(const char *filePath);
    void load(const std::string &fileContent, const std::string &fileName, bool isRegularFile = false);
    std::string addPrefix(const std::string &dependencyName) const;

    BinaryType type = BinaryType::Invalid;
    BinarySubType subType = BinarySubType::None;
    std::string name;
    BinaryClass binaryClass = BinaryClass::Invalid;
    bool isBigEndian = false;
    std::string architecture;
    std::set<std::string> symbols;
    std::set<std::string> requiredLibs;
    std::string rpath;
    VirtualAddressMapping virtualAddressMapping;

private:
    void parse(std::istream &stream, const std::string *fileContent = nullptr);
    void parseElf(CppUtilities::BinaryReader &reader, const std::string *fileContent = nullptr);
    void parsePe(CppUtilities::BinaryReader &reader, typename std::iostream::off_type baseFileOffset = 0);
    void parseAr(CppUtilities::BinaryReader &reader);
    void parseCoff(CppUtilities::BinaryReader &reader);

    std::uint64_t readElfAddress(CppUtilities::BinaryReader &reader);
    std::uint32_t readElfInt32(CppUtilities::BinaryReader &reader);
    std::uint16_t readElfInt16(CppUtilities::BinaryReader &reader);
    std::string readElfString(std::istream &stream, const std::string *fileContent, std::uint64_t stringTableOffset, std::uint64_t stringTableSize,
        std::uint64_t stringOffset);
};
} // namespace LibPkg

#endif // LIBPKG_PARSER_BINARY_H
