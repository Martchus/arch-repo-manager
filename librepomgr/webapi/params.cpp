#include "./params.h"
#include "./session.h"

#include <c++utilities/conversion/stringconversion.h>

using namespace std;
using namespace CppUtilities;

namespace LibRepoMgr {
namespace WebAPI {

Url::Url(std::string_view path, std::string_view hash, std::vector<std::pair<std::string_view, std::string_view>> &&params)
    : path(path)
    , hash(hash)
    , params(std::move(params))
{
}

Url::Url(const Request &request)
{
    const auto target = request.target();
    path = std::string_view(target.data(), target.size());

    // find hash
    const auto hashBegin = path.find('#');
    if (hashBegin != std::string_view::npos) {
        hash = path.substr(hashBegin + 1);
        path = path.substr(0, hashBegin);
    }

    // find params
    const auto paramsBegin = path.find('?');
    if (paramsBegin != std::string_view::npos) {
        const auto joinedParams(path.substr(paramsBegin + 1));
        const auto paramParts(splitStringSimple<std::vector<std::string_view>>(joinedParams, "&"));
        params.reserve(paramParts.size());

        // split param parts into key-value pairs
        for (const auto &paramPart : paramParts) {
            const auto eqBegin(paramPart.find('='));
            if (eqBegin != std::string_view::npos) {
                params.emplace_back(paramPart.substr(0, eqBegin), paramPart.substr(eqBegin + 1));
            } else {
                params.emplace_back(paramPart, std::string_view());
            }
        }

        path = path.substr(0, paramsBegin);
    }
}

bool Url::hasFlag(std::string_view paramName) const
{
    for (const auto &param : params) {
        if (param.first == paramName) {
            return !param.second.empty() && param.second != "0";
        }
    }
    return false;
}

std::string_view Url::value(std::string_view paramName) const
{
    for (const auto &param : params) {
        if (param.first == paramName) {
            return param.second;
        }
    }
    return std::string_view();
}

std::vector<string> Url::decodeValues(std::string_view paramName) const
{
    vector<string> res;
    for (const auto &param : params) {
        if (param.first == paramName) {
            res.emplace_back(Url::decodeValue(param.second));
        }
    }
    return res;
}

std::string Url::decodeValue(std::string_view value)
{
    string res;
    res.reserve(value.size());

    enum {
        AnyChar,
        FirstDigit,
        SecondDigit,
    } state
        = AnyChar;
    auto encodedValue = boost::beast::string_view::value_type();
    try {
        for (const auto c : value) {
            switch (state) {
            case AnyChar:
                switch (c) {
                case '%':
                    state = FirstDigit;
                    break;
                default:
                    res += c;
                }
                break;
            case FirstDigit:
                encodedValue = charToDigit<boost::beast::string_view::value_type>(c, 16) * 16;
                state = SecondDigit;
                break;
            case SecondDigit:
                res += encodedValue + charToDigit<boost::beast::string_view::value_type>(c, 16);
                state = AnyChar;
                break;
            }
        }
    } catch (const ConversionException &) {
        throw BadRequest("Decoded URI parameter malformed.");
    }

    return res;
}

std::string Url::encodeValue(std::string_view value)
{
    string res;
    res.reserve(value.size());
    for (const auto c : value) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || (c == '-' || c == '_' || c == '.' || c == '~')) {
            res += c;
        } else {
            res += '%';
            res += digitToChar(static_cast<char>((c & 0xF0) >> 4));
            res += digitToChar(static_cast<char>((c & 0x0F) >> 0));
        }
    }
    return res;
}

} // namespace WebAPI
} // namespace LibRepoMgr
