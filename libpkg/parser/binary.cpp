#define CPP_UTILITIES_PATHHELPER_STRING_VIEW

#include "./binary.h"

#include <c++utilities/conversion/stringbuilder.h>
#include <c++utilities/io/binaryreader.h>
#include <c++utilities/io/path.h>

#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string_view>

using namespace std;
using namespace CppUtilities;

namespace LibPkg {

namespace BinarySectionTypes {
enum KnownValues : std::uint32_t {
    Null,
    ProgramData,
    SymbolTable,
    StringTable,
    RelocationEntriesWithAddends,
    SymbolHashTable,
    DynamicLinkingInfo,
    Notes,
    ProgramSpace,
    RelocationEntries,
    Reserved,
    DynamicLinkerSymbolTable,
    ArrayOfConstructors = 0xE,
    ArrayOfDestructors,
    SectionGroup,
    ExtendedSectionIndeces,
    NumberOfDefinedTypes,
    OsSpecific = 0x60000000,
};
}

namespace BinaryDynamicTags {
enum KnownValues : std::uint32_t {
    Null,
    Needed,
    PltRelocationEntriesSize,
    PltOrGotAddress,
    SymbolHashTableAddress,
    StringTableAddress,
    SymbolTableAddress,
    RelativeRelocationTableAddress,
    RelativeRelocationTableSize,
    RelativeRelocationTableEntrySize,
    StringTableSize,
    SymbolTableEntrySize,
    InitializationFunctionAddress,
    TerminationFunctionAddress,
    Soname,
    RPath,
    Symbolic,
    RelRelocationTableAddress,
    RelocationTableSize,
    RelocationTableEntrySize,
    Pltrel,
    Debug,
    TextRel,
    JmpRel,
    BindNow,
    InitArray,
    FinitArray,
    InitArraySize,
    FinitArraySize,
    RunPath,
    Flags
};
}

namespace ProgramHeaderTypes {
enum KnownValues : std::uint32_t {
    Null,
    Load,
    Dynamic,
    Interpreter,
    Note,
    Shlib,
    Phdr,
    Loos = 0x60000000,
    Hios = 0x6fffffff,
    Loproc = 0x70000000,
    Hiproc = 0x7fffffff
};
}

std::uint64_t VirtualAddressMapping::virtualAddressToFileOffset(std::uint64_t virtualAddress) const
{
    for (const VirtualAddressMappingEntry &entry : *this) {
        if (entry.isVirtualAddressInRange(virtualAddress)) {
            return entry.virtualAddressToFileOffset(virtualAddress);
        }
    }
    return 0;
}

std::uint64_t VirtualAddressMapping::fileOffsetToVirtualAddress(std::uint64_t fileOffset) const
{
    for (const VirtualAddressMappingEntry &entry : *this) {
        if (entry.isFileOffsetInRange(fileOffset)) {
            return entry.fileOffsetToVirtualAddress(fileOffset);
        }
    }
    return 0;
}

ostream &operator<<(ostream &o, const BinaryType &mode)
{
    switch (mode) {
    case BinaryType::Invalid:
        o << "invalid";
        break;
    case BinaryType::Elf:
        o << "ELF";
        break;
    case BinaryType::Pe:
        o << "PE";
        break;
    case BinaryType::Ar:
        o << "Ar";
        break;
    }
    return o;
}

ostream &operator<<(ostream &o, const BinarySubType &mode)
{
    switch (mode) {
    case BinarySubType::None:
        o << "none";
        break;
    case BinarySubType::Relocatable:
        o << "relocatable";
        break;
    case BinarySubType::Executable:
        o << "executable";
        break;
    case BinarySubType::SharedObject:
        o << "shared object";
        break;
    case BinarySubType::Core:
        o << "core";
        break;
    case BinarySubType::WindowsImportLibrary:
        o << "Windows import library";
        break;
    case BinarySubType::LoProc:
        o << "lo proc";
        break;
    case BinarySubType::HiProc:
        o << "hi proc";
        break;
    }
    return o;
}

ostream &operator<<(ostream &o, const BinaryClass &mode)
{
    switch (mode) {
    case BinaryClass::Invalid:
        o << "invalid";
        break;
    case BinaryClass::Class32Bit:
        o << "32-bit";
        break;
    case BinaryClass::Class64Bit:
        o << "64-bit";
        break;
    }
    return o;
}

void Binary::load(std::string_view filePath)
{
    ifstream file;
    file.exceptions(ios_base::failbit | ios_base::badbit);
    file.open(filePath.data(), ios_base::in | ios_base::binary);
    parse(file);
    switch (type) {
    case BinaryType::Pe:
        // use name of library file as there's no soname field in PEs
        name = fileName(filePath);
        break;
    case BinaryType::Elf:
        // use name of regular file as library name if no soname could be determined
        if (auto ec = std::error_code(); name.empty() && filePath.ends_with(".so") && std::filesystem::is_regular_file(filePath, ec) && !ec) {
            name = fileName(filePath);
        }
        break;
    default:;
    }
}

void Binary::load(std::string_view fileContent, std::string_view fileName, std::string_view directoryPath, bool isRegularFile)
{
    stringstream fileStream(ios_base::in | ios_base::out | ios_base::binary);
    fileStream.exceptions(ios_base::failbit | ios_base::badbit);
#if defined(__GLIBCXX__) && !defined(_LIBCPP_VERSION)
    fileStream.rdbuf()->pubsetbuf(const_cast<char *>(fileContent.data()), static_cast<streamoff>(fileContent.size()));
#else
    fileStream.write(fileContent.data(), static_cast<std::streamsize>(fileContent.size()));
#endif
    parse(fileStream, &fileContent);
    switch (type) {
    case BinaryType::Pe:
        // use name of library file as there's no soname field in PEs
        name = fileName;
        break;
    case BinaryType::Elf:
        // use name of regular file as library name if no soname could be determined
        if (name.empty() && isRegularFile && fileName.ends_with(".so")) {
            name = fileName;
        }
        // add prefix to Android and compat libs to avoid confusion with normal GNU/Linux ELFs
        // note: Relying on the path is not nice. Have Android libs any special header to be distinguishable?
        if (directoryPath.starts_with("opt/android-libs")
            || (directoryPath.starts_with("opt/android-ndk")
                && (directoryPath.find("/sysroot/") != std::string::npos || directoryPath.find("/lib/linux/") != std::string::npos))) {
            extraPrefix = "android-";
        } else if (directoryPath.starts_with("usr/static-compat/lib") && fileName.find(".so") != std::string::npos) {
            extraPrefix = "static-compat-";
        }
        break;
    default:;
    }
}

static constexpr auto toLower(auto c)
{
    return static_cast<decltype(c)>((c >= 'A' && c <= 'Z') ? (c + ('a' - 'A')) : c);
}

static std::string toLower(std::string str)
{
    for (auto &c : str) {
        c = toLower(c);
    }
    return str;
}

std::string Binary::addPrefix(std::string_view dependencyName) const
{
    switch (type) {
    case BinaryType::Elf:
        return argsToString(extraPrefix, "elf-", architecture, ':', ':', dependencyName);
    case BinaryType::Pe:
        return argsToString(extraPrefix, "pe-", architecture, ':', ':', toLower(std::string(dependencyName)));
    case BinaryType::Ar:
        switch (subType) {
        case BinarySubType::WindowsImportLibrary:
            return argsToString(extraPrefix, "pe-", architecture, ':', ':', toLower(std::string(dependencyName)));
        default:;
        }
    default:
        return argsToString(extraPrefix, "unknown::", dependencyName);
    }
}

void Binary::parse(std::istream &stream, const std::string_view *fileContent)
{
    type = BinaryType::Invalid;

    BinaryReader reader(&stream);
    const auto magic = reader.readUInt32BE();
    if (magic == 0x7f454c46) {
        type = BinaryType::Elf;
        parseElf(reader, fileContent);
        return;
    }

    if ((magic & 0xffff0000) == 0x4d5a0000) {
        stream.seekg(0x3C, ios_base::beg);
        stream.seekg(reader.readUInt32LE(), ios_base::beg);
        if (reader.readUInt32BE() == 0x50450000) {
            type = BinaryType::Pe;
            parsePe(reader);
        }
        return;
    }

    if (magic == 0x213C6172 && reader.readUInt32BE() == 0x63683E0A) {
        type = BinaryType::Ar;
        parseAr(reader);
    }
}

void Binary::parseElf(BinaryReader &reader, const std::string_view *fileContent)
{
    istream &stream = *reader.stream();

    // read class
    switch (reader.readByte()) {
    case 1:
        binaryClass = BinaryClass::Class32Bit;
        break;
    case 2:
        binaryClass = BinaryClass::Class64Bit;
        break;
    default:
        binaryClass = BinaryClass::Invalid;
        throw runtime_error("invalid ELF class");
    }

    // read byte-order
    const auto endianness = reader.readByte();
    if (endianness != 1 && endianness != 2) {
        throw runtime_error("invalid endianness");
    }
    isBigEndian = endianness == 2;

    // check version
    if (reader.readByte() != 1) {
        throw runtime_error("invalid ELF version");
    }

    // skip padding
    stream.seekg(9, ios_base::cur);

    // read sub type
    const std::uint16_t subType = reader.readUInt16LE();
    if (subType < 5 || subType == static_cast<std::uint16_t>(BinarySubType::LoProc) || subType == static_cast<std::uint16_t>(BinarySubType::HiProc)) {
        this->subType = static_cast<BinarySubType>(subType);
    } else {
        throw runtime_error("invalid sub type");
    }

    // read machine/architecture
    switch (reader.readUInt16LE()) {
    case 0x02:
        architecture = "sparc";
        break;
    case 0x03:
        architecture = "i386";
        break;
    case 0x08:
        architecture = "mips";
        break;
    case 0x14:
        architecture = "powerpc";
        break;
    case 0x16:
        architecture = "s390";
        break;
    case 0x28:
        architecture = "arm";
        break;
    case 0x32:
        architecture = "ia64";
        break;
    case 0x3E:
        architecture = "x86_64";
        break;
    case 0xB7:
        architecture = "aarch64";
        break;
    default:
        architecture.clear();
    }

    // check version
    if (reader.readUInt32LE() != 1) {
        throw runtime_error("invalid section ELF version");
    }

    // skip entry point
    //const uint64 entryPoint = readElfAddress(reader);
    stream.seekg(binaryClass == BinaryClass::Class64Bit ? 8 : 4, ios_base::cur);
    // read offsets
    const std::uint64_t programHeaderOffset = readElfAddress(reader);
    const std::uint64_t sectionTableOffset = readElfAddress(reader);
    // skip flags
    stream.seekg(4, ios_base::cur);
    // read sizes
    /*const std::uint16_t elfHeaderSize = */ readElfInt16(reader);
    /*const std::uint16_t programHeaderEntrySize = */ readElfInt16(reader);
    const std::uint16_t programHeaderEntryCount = readElfInt16(reader);
    /*const std::uint16_t sectionHeaderSize = */ readElfInt16(reader);
    const std::uint16_t sectionHeaderCount = readElfInt16(reader);
    /*const std::uint16_t nameTableIndex = */ readElfInt16(reader);

    // read program header
    stream.seekg(static_cast<istream::off_type>(programHeaderOffset));
    virtualAddressMapping.reserve(2);
    for (std::uint16_t programHeaderIndex = 0; programHeaderIndex != programHeaderEntryCount; ++programHeaderIndex) {
        std::uint64_t fileOffset, virtualAddr, /*physicalAddr,*/ fileSize, virtualSize /*, flags, align*/;
        const std::uint32_t type = readElfInt32(reader);
        if (binaryClass == BinaryClass::Class32Bit) {
            fileOffset = readElfInt32(reader);
            virtualAddr = readElfInt32(reader);
            /*physicalAddr = */ readElfInt32(reader);
            fileSize = readElfInt32(reader);
            virtualSize = readElfInt32(reader);
            /*flags = */ readElfInt32(reader);
            /*align = */ readElfInt32(reader);
        } else {
            /*flags = */ readElfInt32(reader);
            fileOffset = readElfAddress(reader);
            virtualAddr = readElfAddress(reader);
            /*physicalAddr = */ readElfAddress(reader);
            fileSize = readElfAddress(reader);
            virtualSize = readElfAddress(reader);
            /*align = */ readElfAddress(reader);
        }
        switch (type) {
        case ProgramHeaderTypes::Load:
            virtualAddressMapping.emplace_back(fileOffset, fileSize, virtualAddr, virtualSize);
            break;
        }
    }

    // read section header
    /*std::uint64_t stringTableOffset = 0, stringTableSize = 0;*/
    stream.seekg(static_cast<istream::off_type>(sectionTableOffset));
    for (std::uint16_t sectionHeaderIndex = 0; sectionHeaderIndex != sectionHeaderCount; ++sectionHeaderIndex) {
        /*const std::uint32_t nameOffset = */ readElfInt32(reader);
        const std::uint32_t type = readElfInt32(reader);
        /*const std::uint64_t attributes = */ readElfAddress(reader);
        /*const std::uint64_t virtualMemoryAddress = */ readElfAddress(reader);
        const std::uint64_t offset = readElfAddress(reader);
        const std::uint64_t size = readElfAddress(reader);
        /*const std::uint32_t indexInAssociatedSection = */ readElfInt32(reader);
        /*const std::uint32_t extraInfo = */ readElfInt32(reader);
        /*const std::uint64_t requiredAlignment = */ readElfAddress(reader);
        /*const std::uint64_t entrySize = */ readElfAddress(reader);
        const auto nextSectionHeaderOffset = stream.tellg();

        // read section
        switch (type) {
        case BinarySectionTypes::StringTable: {
            /*stringTableOffset = offset;
            stringTableSize = size;*/
            break;
        }
        case BinarySectionTypes::DynamicLinkingInfo: {
            // read string address of properties
            stream.seekg(static_cast<istream::off_type>(offset));
            std::uint64_t dynamicStringTableAddress = 0, dynamicStringTableSize = 0, sonameAddr = 0, rpathAddr = 0;
            vector<std::uint64_t> neededLibs;
            for (std::uint64_t read = 0; read < size; read += (binaryClass == BinaryClass::Class64Bit ? 16 : 8)) {
                const std::uint64_t tag = readElfAddress(reader);
                const std::uint64_t value = readElfAddress(reader);
                switch (tag) {
                case BinaryDynamicTags::StringTableAddress:
                    dynamicStringTableAddress = value;
                    break;
                case BinaryDynamicTags::StringTableSize:
                    dynamicStringTableSize = value;
                    break;
                case BinaryDynamicTags::Soname:
                    sonameAddr = value;
                    break;
                case BinaryDynamicTags::RPath:
                    rpathAddr = value;
                    break;
                case BinaryDynamicTags::RunPath:
                    rpathAddr = value;
                    break;
                case BinaryDynamicTags::Needed:
                    neededLibs.push_back(value);
                    break;
                default:;
                }
            }

            // lookup string address in string table to get actual strings
            if (dynamicStringTableAddress && dynamicStringTableSize) {
                if (sonameAddr) {
                    name = readElfString(stream, fileContent, dynamicStringTableAddress, dynamicStringTableSize, sonameAddr);
                }
                if (rpathAddr) {
                    rpath = readElfString(stream, fileContent, dynamicStringTableAddress, dynamicStringTableSize, rpathAddr);
                }
                for (const std::uint64_t neededLibStrAddr : neededLibs) {
                    requiredLibs.emplace(readElfString(stream, fileContent, dynamicStringTableAddress, dynamicStringTableSize, neededLibStrAddr));
                }
            }
            break;
        }
        default:; // section not relevant
        }
        // read next section header
        stream.seekg(nextSectionHeaderOffset);
    }
}

struct PeSectionData {
    char name[8];
    std::uint32_t virtualSize;
    std::uint32_t virtualAddress;
    std::uint32_t fileSize;
    std::uint32_t fileOffset;
    std::uint32_t relocationPtr;
    std::uint32_t lineNumbersPtr;
    std::uint16_t relocationCount;
    std::uint16_t lineNumbersCount;
    std::uint32_t characteristics;

    void read(BinaryReader &reader)
    {
        reader.read(name, 8);
        virtualSize = reader.readUInt32LE();
        virtualAddress = reader.readUInt32LE();
        fileSize = reader.readUInt32LE();
        fileOffset = reader.readUInt32LE();
        relocationPtr = reader.readUInt32LE();
        lineNumbersPtr = reader.readUInt32LE();
        relocationCount = reader.readUInt16LE();
        lineNumbersCount = reader.readUInt16LE();
        characteristics = reader.readUInt32LE();
    }
};

struct PeImportTableEntry {
    std::uint32_t originalFirstThunk;
    std::uint32_t timeDateStamp;
    std::uint32_t forwarderChain;
    std::uint32_t nameVirtualAddress;
    std::uint32_t firstThunk;

    void read(BinaryReader &reader)
    {
        originalFirstThunk = reader.readUInt32LE();
        timeDateStamp = reader.readUInt32LE();
        forwarderChain = reader.readUInt32LE();
        nameVirtualAddress = reader.readUInt32LE();
        firstThunk = reader.readUInt32LE();
    }

    constexpr bool isEmpty() const
    {
        return !originalFirstThunk && !timeDateStamp && !forwarderChain && !nameVirtualAddress && !firstThunk;
    }
};

void Binary::parsePe(BinaryReader &reader, iostream::off_type baseFileOffset)
{
    istream &stream = *reader.stream();

    // read machine/architecture
    switch (reader.readUInt16LE()) {
    case 0x14c:
        architecture = "i386";
        break;
    case 0x8664:
        architecture = "x86_64";
        break;
    case 0x162:
    case 0x168:
    case 0x266:
        architecture = "mips";
        break;
    case 0x1c0:
    case 0x1c2:
    case 0x1c4:
        architecture = "arm";
        break;
    case 0x32:
        architecture = "ia64";
        break;
    default:
        architecture.clear();
    }

    // read rest of COFF header
    const auto numberOfSections = reader.readUInt16LE();
    /*const auto timeDateStamp = */ reader.readUInt32LE();
    /*const auto symbolTableOffset = */ reader.readUInt32LE();
    /*const auto symbolTableSize = */ reader.readUInt32LE();
    const auto optionHeaderSize = reader.readUInt16LE();
    /*const auto characteristics = */ reader.readUInt16LE();

    // read PE optional header
    int64_t /*exportDirVirtualAddress = -1, exportDirSize = -1, */ importDirVirtualAddress = -1 /*, importDirSize = -1*/;
    if (optionHeaderSize) {
        const auto optionHeaderStart = static_cast<long>(stream.tellg());
        unsigned char minPeHeaderSize;
        /*uint64_t imageBase;*/
        switch (reader.readUInt16LE()) {
        case 0x020b:
            binaryClass = BinaryClass::Class64Bit;
            stream.seekg(optionHeaderStart + 24, ios_base::beg);
            /*imageBase = */ reader.readUInt64LE();
            minPeHeaderSize = 112;
            break;
        case 0x010b:
            binaryClass = BinaryClass::Class32Bit;
            stream.seekg(optionHeaderStart + 28, ios_base::beg);
            /*imageBase = */ reader.readUInt32LE();
            minPeHeaderSize = 96;
            break;
            // case 0x0107: ROM image, not relevant
        default:
            return;
        }
        if (optionHeaderSize < minPeHeaderSize) {
            throw runtime_error("PE optional header is truncated");
        }
        // read virtual addresses of directories
        stream.seekg(optionHeaderStart + minPeHeaderSize - 4);
        const auto numberOfDirs = reader.readUInt32LE();
        if (numberOfDirs < 16) {
            throw runtime_error("expected at least 16 directories in PE file");
        }
        /*exportDirVirtualAddress = */ reader.readUInt32LE();
        /*exportDirSize = */ reader.readUInt32LE();
        importDirVirtualAddress = reader.readUInt32LE();
        /*importDirSize = */ reader.readUInt32LE();
        // skip remaining dirs (not relevant here)
        stream.seekg(optionHeaderStart + optionHeaderSize, ios_base::beg);
    }

    // read section table for mapping virtual addresses to file offsets
    PeSectionData importDataSection;
    std::int64_t importLibraryDllNameOffset = -1, importLibraryDllNameSize = -1;
    for (auto sectionsLeft = numberOfSections; sectionsLeft; --sectionsLeft) {
        importDataSection.read(reader);
        if (!strncmp(importDataSection.name, ".idata$7", 8)) {
            importLibraryDllNameOffset = importDataSection.fileOffset;
            importLibraryDllNameSize = importDataSection.fileSize;
        }
        virtualAddressMapping.emplace_back(
            importDataSection.fileOffset, importDataSection.fileSize, importDataSection.virtualAddress, importDataSection.virtualSize);
    }

    // read import dir to get dependencies
    if (importDirVirtualAddress >= 0) {
        const auto importDirFileAddress
            = static_cast<istream::off_type>(virtualAddressMapping.virtualAddressToFileOffset(static_cast<uint32_t>(importDirVirtualAddress)));
        if (!importDirFileAddress) {
            throw runtime_error("unable to map virtual address of import directory to its file offset");
        }
        stream.seekg(baseFileOffset + importDirFileAddress, ios_base::beg);
        PeImportTableEntry importEntry;
        vector<istream::off_type> dllNameOffsets;
        for (;;) {
            importEntry.read(reader);
            if (importEntry.isEmpty()) {
                break;
            }
            const auto nameFileAddress
                = static_cast<istream::off_type>(virtualAddressMapping.virtualAddressToFileOffset(importEntry.nameVirtualAddress));
            if (!nameFileAddress) {
                throw runtime_error("unable to map virtual address of import DLL name to its file offset");
            }
            dllNameOffsets.emplace_back(nameFileAddress);
        }
        for (const auto dllNameOffset : dllNameOffsets) {
            stream.seekg(dllNameOffset, ios_base::beg);
            requiredLibs.emplace(reader.readTerminatedString());
        }
    }

    // read import library name
    if (importLibraryDllNameOffset >= 0) {
        stream.seekg(baseFileOffset + importLibraryDllNameOffset, ios_base::beg);
        name = reader.readTerminatedString(static_cast<size_t>(importLibraryDllNameSize), 0);
    }
}

void Binary::parseAr(BinaryReader &reader)
{
    for (auto remainingSize = reader.readStreamsize(); remainingSize >= 60;) {
        // read file header
        char fileName[17] = { 0 };
        reader.read(fileName, sizeof(fileName) - 1);
        reader.stream()->seekg(12 + 6 + 6 + 8, ios_base::cur); // skip file modification timestamp, owner, group and file mode
        char fileSizeStr[11] = { 0 };
        reader.read(fileSizeStr, sizeof(fileSizeStr) - 1);
        if (reader.readUInt16BE() != 0x600A) {
            throw runtime_error("ending characters not present");
        }

        // make file name and file size zero-terminated
        for (auto &c : fileName) {
            if (c == ' ' || c == '/') {
                c = '\0';
                break;
            }
        }
        for (auto &c : fileSizeStr) {
            if (c == ' ') {
                c = '\0';
                break;
            }
        }

        const auto fileOffset = reader.stream()->tellg();
        static_assert(std::is_scalar_v<std::decay_t<decltype(fileSizeStr)>>);
        const auto fileSize = stringToNumber<iostream::off_type>(fileSizeStr);
        const auto nextFileOffset = fileOffset + fileSize;
        if (endsWith(string_view(fileName), ".o")) {
            const auto magic = reader.readUInt32BE();
            if (magic == 0x7f454c46u) {
                return; // we're not interested in static libraries containing ELF files
            }

            reader.stream()->seekg(-4, ios_base::cur);
            parsePe(reader, fileOffset);
            if (!name.empty()) {
                subType = BinarySubType::WindowsImportLibrary;
                return; // stop if the DLL name has been determined
            }
        }

        // parse the next file
        remainingSize -= fileSize;
        reader.stream()->seekg(nextFileOffset);
    }
}

std::uint64_t Binary::readElfAddress(BinaryReader &reader)
{
    switch (binaryClass) {
    case BinaryClass::Class64Bit:
        return isBigEndian ? reader.readUInt64BE() : reader.readUInt64LE();
    case BinaryClass::Class32Bit:
        return isBigEndian ? reader.readUInt32BE() : reader.readUInt32LE();
    default:
        throw runtime_error("Invalid binary class");
    }
}

std::uint32_t Binary::readElfInt32(BinaryReader &reader)
{
    return isBigEndian ? reader.readUInt32BE() : reader.readUInt32LE();
}

std::uint16_t Binary::readElfInt16(BinaryReader &reader)
{
    return isBigEndian ? reader.readUInt16BE() : reader.readUInt16LE();
}

std::string Binary::readElfString(std::istream &stream, const std::string_view *fileContent, std::uint64_t stringTableAddress,
    std::uint64_t stringTableSize, std::uint64_t relativeStringAddress)
{
    // check bounds
    if (relativeStringAddress >= stringTableSize) {
        throw runtime_error(
            argsToString("string address ", relativeStringAddress, " exceeds size of string table (", stringTableSize, ") at ", stringTableAddress));
    }

    // take shortcut when file content has already been buffered
    if (fileContent) {
        return string(fileContent->data() + virtualAddressMapping.virtualAddressToFileOffset(stringTableAddress + relativeStringAddress));
    }

    // read string from stream
    const auto stringOffset
        = static_cast<istream::off_type>(virtualAddressMapping.virtualAddressToFileOffset(stringTableAddress + relativeStringAddress));
    stream.seekg(stringOffset);
    size_t strSize = 0, bytesAvailable = stringTableSize - relativeStringAddress;
    for (; stream.get() && bytesAvailable; ++strSize, --bytesAvailable)
        ;
    stream.seekg(stringOffset);
    auto buffer = make_unique<char[]>(strSize);
    stream.read(buffer.get(), static_cast<streamsize>(strSize));
    return string(buffer.get(), strSize);
}
} // namespace LibPkg
