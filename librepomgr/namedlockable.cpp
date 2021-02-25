#include "./namedlockable.h"
#include "./logging.h"

namespace LibRepoMgr {

template struct NamedLock<std::shared_lock<std::shared_mutex>>;
template struct NamedLock<std::unique_lock<std::shared_mutex>>;

} // namespace LibRepoMgr
