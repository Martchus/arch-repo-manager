#ifndef LIBREPOMGR_AUTHENTICATION_H
#define LIBREPOMGR_AUTHENTICATION_H

#include <c++utilities/misc/flagenumclass.h>

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
    AccessSecrets = (1 << 5),
    DefaultPermissions = ReadBuildActionsDetails,
};

struct UserAuth {
    std::string_view name;
    std::string_view password;
    UserPermissions permissions = UserPermissions::DefaultPermissions;
};

} // namespace LibRepoMgr

CPP_UTILITIES_MARK_FLAG_ENUM_CLASS(LibPkg, LibRepoMgr::UserPermissions)

namespace LibRepoMgr {

struct UserInfo {
    std::string passwordSha512;
    UserPermissions permissions = UserPermissions::None;
};

} // namespace LibRepoMgr

#endif // LIBREPOMGR_AUTHENTICATION_H
