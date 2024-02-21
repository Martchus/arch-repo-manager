#include "./authentication.h"

#include "./helper.h"
#include "./serversetup.h"

#include <c++utilities/conversion/stringconversion.h>
#include <c++utilities/io/ansiescapecodes.h>

#include <openssl/sha.h>
#include <openssl/crypto.h>

namespace LibRepoMgr {

template <> inline void convertValue(const std::multimap<std::string, std::string> &multimap, const std::string &key, UserPermissions &result)
{
    using namespace std;
    using namespace CppUtilities::EscapeCodes;

    const auto value = getLastValueSv(multimap, key);
    if (!value.has_value()) {
        return;
    }

    result = UserPermissions::None;
    const auto parts = CppUtilities::splitStringSimple<std::vector<std::string_view>>(value.value(), " "sv);
    for (const auto &part : parts) {
        if (part.empty()) {
        } else if (part == "read_build_actions_details") {
            result = result | UserPermissions::ReadBuildActionsDetails;
        } else if (part == "download_artefacts") {
            result = result | UserPermissions::DownloadArtefacts;
        } else if (part == "modify_build_actions") {
            result = result | UserPermissions::ModifyBuildActions;
        } else if (part == "perform_admin_actions") {
            result = result | UserPermissions::PerformAdminActions;
        } else {
            std::cerr << Phrases::Error << "Specified permission \"" << part << "\" for key \"" << key << "\" is invalid." << Phrases::End;
            std::exit(-1);
        }
    }
}

static constexpr char toUpper(const char c)
{
    return (c >= 'a' && c <= 'z') ? (c - ('a' - 'A')) : c;
}

void ServiceSetup::Authentication::applyConfig(const std::string &userName, const std::multimap<std::string, std::string> &multimap)
{
    auto &user = users[userName];
    convertValue(multimap, "password_sha512", user.passwordSha512);
    convertValue(multimap, "permissions", user.permissions);
    for (auto &c : user.passwordSha512) {
        c = toUpper(c);
    }
}

UserAuth ServiceSetup::Authentication::authenticate(std::string_view authorizationHeader) const
{
    // extract user name and password from base64 encoded header value
    auto auth = UserAuth();
    if (!CppUtilities::startsWith(authorizationHeader, "Basic ") && authorizationHeader.size() < 100) {
        return auth;
    }
    std::pair<std::unique_ptr<std::uint8_t[]>, std::uint32_t> data;
    try {
        data = CppUtilities::decodeBase64(authorizationHeader.data() + 6, static_cast<std::uint32_t>(authorizationHeader.size() - 6));
    } catch (const CppUtilities::ConversionException &) {
        return auth;
    }
    const auto parts = CppUtilities::splitStringSimple<std::vector<std::string_view>>(
        std::string_view(reinterpret_cast<const char *>(data.first.get()), data.second), ":", 2);
    if (parts.size() != 2) {
        return auth;
    }

    // find relevant user
    const std::string_view userName = parts[0], password = parts[1];
    if (userName.empty() || password.empty()) {
        return auth;
    }
    if (userName == "try" && password == "again") {
        auth.permissions = UserPermissions::TryAgain;
        return auth;
    }
    const auto user = users.find(std::string(userName));
    if (user == users.cend()) {
        return auth;
    }
    constexpr auto sha512HexSize = SHA512_DIGEST_LENGTH * 2;
    if (user->second.passwordSha512.size() != sha512HexSize) {
        return auth;
    }

    // hash password
    auto hash = std::array<unsigned char, SHA512_DIGEST_LENGTH>();
    SHA512(reinterpret_cast<const unsigned char *>(password.data()), password.size(), hash.data());

    // convert hash to string (hexadecimal)
    auto hashHex = std::array<char, sha512HexSize>();
    auto hashHexIter = hashHex.begin();
    for (const auto hashNumber : hash) {
        *(hashHexIter++) = static_cast<char>(CppUtilities::digitToChar((hashNumber / 16) % 16));
        *(hashHexIter++) = static_cast<char>(CppUtilities::digitToChar(hashNumber % 16));
    }

    // check whether password hash matches
    if (CRYPTO_memcmp(user->second.passwordSha512.data(), hashHex.data(), sha512HexSize) != 0) {
        return auth;
    }

    // return the user's permissions
    auth.permissions = user->second.permissions;
    auth.name = userName;
    auth.password = password;
    return auth;
}

} // namespace LibRepoMgr
