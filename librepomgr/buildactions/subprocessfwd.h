#ifndef LIBREPOMGR_SUB_PROCESS_FWD_H
#define LIBREPOMGR_SUB_PROCESS_FWD_H

#include <functional>

namespace boost {
namespace process {
class child;
}
} // namespace boost

namespace LibRepoMgr {

struct ProcessResult;
using ProcessHandler = std::function<void(boost::process::child &&child, ProcessResult &&)>;
class BaseProcessSession;
class BasicProcessSession;
class ProcessSession;

} // namespace LibRepoMgr

#endif // LIBREPOMGR_SUB_PROCESS_FWD_H
