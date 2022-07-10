#ifndef LIBREPOMGR_AUTHENTICATION_H
#define LIBREPOMGR_AUTHENTICATION_H

#include <cstdint>
#include <string>

namespace LibRepoMgr {

enum class UserPermissions : std::uint64_t {
    None = 0,
    ReadBuildActionsDetails = (1 << 0),
    DownloadArtefacts = (1 << 1),
    ModifyBuildActions = ReadBuildActionsDetails | DownloadArtefacts | (1 << 2),
    PerformAdminActions = (1 << 3),
    TryAgain = (1 << 4),
    DefaultPermissions = ReadBuildActionsDetails,
};

struct UserAuth {
    std::string_view name;
    std::string_view password;
    UserPermissions permissions = UserPermissions::DefaultPermissions;
};

constexpr UserPermissions operator|(UserPermissions lhs, UserPermissions rhs)
{
    return static_cast<UserPermissions>(
        static_cast<std::underlying_type_t<UserPermissions>>(lhs) | static_cast<std::underlying_type_t<UserPermissions>>(rhs));
}

struct UserInfo {
    std::string passwordSha512;
    UserPermissions permissions = UserPermissions::None;
};

} // namespace LibRepoMgr

#endif // LIBREPOMGR_AUTHENTICATION_H
