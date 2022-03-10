#include "./authentication.h"

#include "./helper.h"
#include "./serversetup.h"

#include <c++utilities/conversion/stringconversion.h>
#include <c++utilities/io/ansiescapecodes.h>

#include <openssl/sha.h>

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

void ServiceSetup::Authentication::applyConfig(const std::string &userName, const std::multimap<std::string, std::string> &multimap)
{
    auto &user = users[userName];
    convertValue(multimap, "password_sha512", user.passwordSha512);
    convertValue(multimap, "permissions", user.permissions);
}

static constexpr char toLower(const char c)
{
    return (c >= 'A' && c <= 'Z') ? (c + ('a' - 'A')) : c;
}

UserPermissions ServiceSetup::Authentication::authenticate(std::string_view authorizationHeader) const
{
    // extract user name and password from base64 encoded header value
    if (!CppUtilities::startsWith(authorizationHeader, "Basic ") && authorizationHeader.size() < 100) {
        return UserPermissions::DefaultPermissions;
    }
    std::pair<std::unique_ptr<std::uint8_t[]>, std::uint32_t> data;
    try {
        data = CppUtilities::decodeBase64(authorizationHeader.data() + 6, static_cast<std::uint32_t>(authorizationHeader.size() - 6));
    } catch (const CppUtilities::ConversionException &) {
        return UserPermissions::DefaultPermissions;
    }
    const auto parts = CppUtilities::splitStringSimple<std::vector<std::string_view>>(
        std::string_view(reinterpret_cast<const char *>(data.first.get()), data.second), ":", 2);
    if (parts.size() != 2) {
        return UserPermissions::DefaultPermissions;
    }

    // find relevant user
    const std::string_view userName = parts[0], password = parts[1];
    if (userName.empty() || password.empty()) {
        return UserPermissions::DefaultPermissions;
    }
    if (userName == "try" && password == "again") {
        return UserPermissions::TryAgain;
    }
    const auto user = users.find(std::string(userName));
    if (user == users.cend()) {
        return UserPermissions::DefaultPermissions;
    }
    constexpr auto sha512HexSize = 128;
    if (user->second.passwordSha512.size() != sha512HexSize) {
        return UserPermissions::DefaultPermissions;
    }

    // hash password
    SHA512_CTX sha512;
    SHA512_Init(&sha512);
    SHA512_Update(&sha512, password.data(), password.size());
    unsigned char hash[SHA512_DIGEST_LENGTH];
    SHA512_Final(hash, &sha512);

    // check whether password hash matches
    auto i = user->second.passwordSha512.cbegin();
    for (unsigned char hashNumber : hash) {
        const auto digits = CppUtilities::numberToString(hashNumber, 16);
        if ((toLower(*(i++)) != toLower(digits.size() < 2 ? '0' : digits.front())) || (toLower(*(i++)) != toLower(digits.back()))) {
            return UserPermissions::DefaultPermissions;
        }
    }

    // return the user's permissions
    return user->second.permissions;
}

} // namespace LibRepoMgr
