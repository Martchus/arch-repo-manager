#include "./globallock.h"
#include "./logging.h"

namespace LibRepoMgr {

template struct LoggingLock<std::shared_lock<GlobalSharedMutex>>;
template struct LoggingLock<std::unique_lock<GlobalSharedMutex>>;

} // namespace LibRepoMgr
