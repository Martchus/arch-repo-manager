#ifndef LIBREPOMGR_SUB_PROCESS_FWD_H
#define LIBREPOMGR_SUB_PROCESS_FWD_H

#include <functional>

namespace boost {
namespace process {
inline namespace v1 {
class child;
}
} // namespace process
} // namespace boost

namespace LibRepoMgr {

struct ProcessResult;
using ProcessHandler = std::function<void(boost::process::v1::child &&child, ProcessResult &&)>;
class BaseProcessSession;
class BasicProcessSession;
class ProcessSession;

} // namespace LibRepoMgr

#endif // LIBREPOMGR_SUB_PROCESS_FWD_H
