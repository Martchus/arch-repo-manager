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
#include <iostream>
#include <map>
#include <regex>
#include <string>
#include <string_view>

namespace LibRepoMgr {

namespace Traits = CppUtilities::Traits;

inline const char *getLastValue(const std::multimap<std::string, std::string> &multimap, const std::string &key)
{
    using namespace std;
    const auto it = find_if(multimap.crbegin(), multimap.crend(), [&key](const pair<string, string> &i) { return i.first == key; });
    if (it != multimap.rend()) {
        return it->second.data();
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

} // namespace LibRepoMgr

#endif // LIBREPOMGR_HELPER_H
