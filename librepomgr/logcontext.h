#ifndef LIBREPOMGR_LOGCONTEXT_H
#define LIBREPOMGR_LOGCONTEXT_H

// Do NOT include this header directly, include "loggin.h" instead. This header only exists to resolve the
// cyclic dependency between LogContext and BuildAction but lacks definitions of operator().

#include "./global.h"

#include <c++utilities/io/ansiescapecodes.h>

namespace LibRepoMgr {

struct BuildAction;

struct LIBREPOMGR_EXPORT LogContext {
    explicit LogContext(BuildAction *buildAction = nullptr);
    LogContext &operator=(const LogContext &) = delete;
    template <typename... Args> LogContext &operator()(CppUtilities::EscapeCodes::Phrases phrase, Args &&...args);
    template <typename... Args> LogContext &operator()(Args &&...args);
    template <typename... Args> LogContext &operator()(std::string &&msg);

private:
    BuildAction *const m_buildAction;
};

inline LogContext::LogContext(BuildAction *buildAction)
    : m_buildAction(buildAction)
{
}

} // namespace LibRepoMgr

#endif // LIBREPOMGR_LOGCONTEXT_H
