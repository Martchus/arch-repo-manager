#ifndef LIBREPOMGR_HELPER_H
#define LIBREPOMGR_HELPER_H

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
    const auto i = find_if(multimap.crbegin(), multimap.crend(), [&key](const pair<string, string> &i) { return i.first == key; });
    if (i != multimap.rend()) {
        return i->second.data();
    }
    return nullptr;
}

inline std::optional<std::string_view> getLastValueSv(const std::multimap<std::string, std::string> &multimap, const std::string &key)
{
    using namespace std;
    const auto i = find_if(multimap.crbegin(), multimap.crend(), [&key](const pair<string, string> &i) { return i.first == key; });
    if (i != multimap.rend()) {
        return i->second.data();
    }
    return std::nullopt;
}

template <typename TargetType> void convertValue(const std::multimap<std::string, std::string> &multimap, const std::string &key, TargetType &result);

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
            exit(-1);
        }
        result = ip;
    }
}

template <> inline void convertValue(const std::multimap<std::string, std::string> &multimap, const std::string &key, unsigned short &result)
{
    using namespace std;
    using namespace CppUtilities;
    using namespace CppUtilities::EscapeCodes;

    if (const char *const value = getLastValue(multimap, key)) {
        try {
            result = stringToNumber<unsigned short>(value);
        } catch (const ConversionException &) {
            cerr << Phrases::ErrorMessage << "Specified number \"" << value << "\" for key \"" << key << "\" is invalid." << Phrases::End;
            exit(-1);
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
    using namespace std;
    using namespace CppUtilities::EscapeCodes;

    if (const char *const value = getLastValue(multimap, key)) {
        try {
            result = value;
        } catch (const regex_error &e) {
            cerr << Phrases::ErrorMessage << "Specified regex \"" << value << "\" for key \"" << key << "\" is invalid: " << Phrases::End;
            cerr << e.what() << '\n';
            exit(-1);
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
