#ifndef LIBREPOMGR_HELPER_H
#define LIBREPOMGR_HELPER_H

#include <c++utilities/chrono/timespan.h>
#include <c++utilities/conversion/conversionexception.h>
#include <c++utilities/conversion/stringconversion.h>
#include <c++utilities/io/ansiescapecodes.h>
#include <c++utilities/misc/traits.h>

#include <boost/asio/ip/address.hpp>
#include <boost/system/error_code.hpp>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <map>
#include <regex>
#include <string>
#include <string_view>

namespace LibRepoMgr {

namespace Traits = CppUtilities::Traits;

template <typename Container> inline const char *getLastValue(const Container &multimap, const std::string &key)
{
    using namespace std;
    const auto it = std::find_if(multimap.crbegin(), multimap.crend(), [&key](const auto &i) {
        if constexpr (Traits::IsSpecializationOf<decltype(i), std::pair>()) {
            return i.first == key;
        } else {
            return i.key == key;
        }
    });
    if (it != multimap.rend()) {
        if constexpr (Traits::IsSpecializationOf<decltype(*it), std::pair>()) {
            return it->second.data();
        } else {
            return it->value.data();
        }
    }
    return nullptr;
}

inline std::optional<std::string_view> getLastValueSv(const std::multimap<std::string, std::string> &multimap, const std::string &key)
{
    using namespace std;
    const auto it = find_if(multimap.crbegin(), multimap.crend(), [&key](const pair<string, string> &i) { return i.first == key; });
    if (it != multimap.rend()) {
        return it->second.data();
    }
    return std::nullopt;
}

template <typename TargetType, Traits::DisableIfAny<std::is_integral<TargetType>, Traits::IsSpecializationOf<TargetType, std::atomic>> * = nullptr>
void convertValue(const std::multimap<std::string, std::string> &multimap, const std::string &key, TargetType &result);

template <>
inline void convertValue(const std::multimap<std::string, std::string> &multimap, const std::string &key, boost::asio::ip::address &result)
{
    using namespace std;
    using namespace CppUtilities::EscapeCodes;

    if (const char *const value = getLastValue(multimap, key)) {
        boost::system::error_code error;
        const auto ip = boost::asio::ip::make_address(value, error);
        if (error) {
            cerr << Phrases::ErrorMessage << "Specified IP address \"" << value << "\" for key \"" << key << "\" is invalid" << Phrases::End
                 << Phrases::SubError << error.message() << Phrases::End;
            return;
        }
        result = ip;
    }
}

template <typename TargetType, Traits::EnableIfAny<std::is_integral<TargetType>, Traits::IsSpecializationOf<TargetType, std::atomic>> * = nullptr>
inline void convertValue(const std::multimap<std::string, std::string> &multimap, const std::string &key, TargetType &result)
{
    using namespace CppUtilities;
    using namespace CppUtilities::EscapeCodes;

    if (const char *const value = getLastValue(multimap, key)) {
        try {
            if constexpr (Traits::IsSpecializationOf<TargetType, std::atomic>::value) {
                result = stringToNumber<typename TargetType::value_type>(value);
            } else {
                result = stringToNumber<TargetType>(value);
            }
        } catch (const ConversionException &) {
            std::cerr << Phrases::ErrorMessage << "Specified number \"" << value << "\" for key \"" << key << "\" is invalid." << Phrases::End;
        }
    }
}

template <> inline void convertValue(const std::multimap<std::string, std::string> &multimap, const std::string &key, std::string &result)
{
    if (const char *const value = getLastValue(multimap, key)) {
        result = value;
    }
}

template <> inline void convertValue(const std::multimap<std::string, std::string> &multimap, const std::string &key, std::regex &result)
{
    using namespace CppUtilities::EscapeCodes;

    if (const char *const value = getLastValue(multimap, key)) {
        try {
            result = value;
        } catch (const std::regex_error &e) {
            std::cerr << Phrases::ErrorMessage << "Specified regex \"" << value << "\" for key \"" << key << "\" is invalid: " << Phrases::End;
            std::cerr << e.what() << '\n';
        }
    }
}

template <>
inline void convertValue(const std::multimap<std::string, std::string> &multimap, const std::string &key, std::vector<std::string> &result)
{
    for (auto range = multimap.equal_range(key); range.first != range.second; ++range.first) {
        result.emplace_back(range.first->second);
    }
}

template <> inline void convertValue(const std::multimap<std::string, std::string> &multimap, const std::string &key, bool &result)
{
    if (const char *const value = getLastValue(multimap, key)) {
        result = !strcmp(value, "on") || !strcmp(value, "yes");
    }
}

template <> inline void convertValue(const std::multimap<std::string, std::string> &multimap, const std::string &key, CppUtilities::TimeSpan &result)
{
    using namespace CppUtilities::EscapeCodes;

    if (const char *const value = getLastValue(multimap, key)) {
        try {
            result = CppUtilities::TimeSpan::fromString(value);
        } catch (const CppUtilities::ConversionException &e) {
            std::cerr << Phrases::ErrorMessage << "Specified duration \"" << value << "\" for key \"" << key << "\" is invalid: " << Phrases::End;
            std::cerr << e.what() << '\n';
        }
    }
}

template <typename VectorType> void mergeSecondVectorIntoFirstVector(VectorType &firstVector, VectorType &secondVector)
{
    const auto requiredSize = firstVector.size() + secondVector.size();
    if (firstVector.capacity() < requiredSize) {
        firstVector.reserve(requiredSize);
    }
    for (auto &i : secondVector) {
        firstVector.emplace_back(std::move(i));
    }
    secondVector.clear();
}

template <typename VectorType> void copySecondVectorIntoFirstVector(VectorType &firstVector, const VectorType &secondVector)
{
    const auto requiredSize = firstVector.size() + secondVector.size();
    if (firstVector.capacity() < requiredSize) {
        firstVector.reserve(requiredSize);
    }
    for (auto &i : secondVector) {
        firstVector.emplace_back(i);
    }
}

template <class ListType, class Objects, class Accessor> auto map(const Objects &objects, Accessor accessor)
{
    ListType things;
    things.reserve(objects.size());
    for (const auto &object : objects) {
        things.emplace_back(accessor(object));
    }
    return things;
}

template <class ListType, class Objects> auto names(const Objects &objects)
{
    return map<ListType, Objects>(objects, [](const auto &object) { return Traits::dereferenceMaybe(object).name; });
}

/**
 * \brief Recursively copies a directory (or copies a single file/symlink).
 * \remarks Overrides any existing files or directories even if they are read-only (as long as the
 *          current process has the permissions to delete/unlink them).
 */
inline void copyDirectoryRecursive(const std::filesystem::path &source, const std::filesystem::path &destination)
{
    const auto sourceStat = std::filesystem::status(source);
    if (std::filesystem::is_directory(sourceStat)) {
        std::filesystem::create_directories(destination);
        for (const auto &entry : std::filesystem::recursive_directory_iterator(source, std::filesystem::directory_options::skip_permission_denied)) {
            const auto &srcPath = entry.path();
            const auto relative = std::filesystem::relative(srcPath, source);
            const auto destPath = destination / relative;
            if (entry.is_directory()) {
                if (std::filesystem::exists(destPath) && !std::filesystem::is_directory(destPath)) {
                    std::filesystem::remove_all(destPath);
                }
                std::filesystem::create_directories(destPath);
            } else if (entry.is_symlink()) {
                if (std::filesystem::exists(destPath) || std::filesystem::is_symlink(destPath)) {
                    std::filesystem::remove_all(destPath);
                }
                std::filesystem::copy_symlink(srcPath, destPath);
            } else {
                if (std::filesystem::exists(destPath) || std::filesystem::is_symlink(destPath)) {
                    std::filesystem::remove_all(destPath);
                }
                std::filesystem::copy_file(srcPath, destPath);
            }
        }
        return;
    }
    const auto destinationStat = std::filesystem::status(destination);
    if (std::filesystem::exists(destinationStat) || std::filesystem::is_symlink(destinationStat)) {
        std::filesystem::remove_all(destination);
    }
    if (std::filesystem::is_symlink(sourceStat)) {
        std::filesystem::copy_symlink(source, destination);
    } else {
        std::filesystem::copy_file(source, destination);
    }
}

} // namespace LibRepoMgr

#endif // LIBREPOMGR_HELPER_H
