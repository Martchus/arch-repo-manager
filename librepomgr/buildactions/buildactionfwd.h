#ifndef LIBREPOMGR_BUILD_ACTION_FWD_H
#define LIBREPOMGR_BUILD_ACTION_FWD_H

#include <memory>
#include <vector>

namespace LibRepoMgr {

struct BuildAction;
using BuildActionIdType = std::vector<std::shared_ptr<BuildAction>>::size_type; // build actions are stored in a vector and the ID is used as index

} // namespace LibRepoMgr

#endif // LIBREPOMGR_BUILD_ACTION_FWD_H
