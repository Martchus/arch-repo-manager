#ifndef LIBREPOMGR_AUTHENTICATION_H
#define LIBREPOMGR_AUTHENTICATION_H

#include <cstdint>
#include <string>

namespace LibRepoMgr {

enum class UserPermissions : std::uint64_t {
    None = 0x0,
    ReadBuildActionsDetails = 0x1,
    ModifyBuildActions = ReadBuildActionsDetails | 0x2,
    PerformAdminActions = 0x4,
    TryAgain = 0x8,
    DefaultPermissions = ReadBuildActionsDetails,
};

constexpr UserPermissions operator|(UserPermissions lhs, UserPermissions rhs)
{
    return static_cast<UserPermissions>(static_cast<int>(lhs) | static_cast<int>(rhs));
}

struct UserInfo {
    std::string passwordSha512;
    UserPermissions permissions = UserPermissions::None;
};

} // namespace LibRepoMgr

#endif // LIBREPOMGR_AUTHENTICATION_H
