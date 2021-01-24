#ifndef LIBREPOMGR_LOGGING_H
#define LIBREPOMGR_LOGGING_H

#include "./buildactions/buildaction.h"

namespace LibRepoMgr {

inline auto ps(CppUtilities::EscapeCodes::Phrases phrase)
{
    return CppUtilities::EscapeCodes::formattedPhraseString(phrase);
}

template <typename... Args> LogContext &LogContext::operator()(std::string &&msg)
{
    std::cerr << msg;
    if (m_buildAction) {
        m_buildAction->appendOutput(std::move(msg));
    }
    return *this;
}

template <typename... Args> LogContext &LogContext::operator()(CppUtilities::EscapeCodes::Phrases phrase, Args &&...args)
{
    return (*this)(CppUtilities::argsToString(CppUtilities::EscapeCodes::formattedPhraseString(phrase), std::forward<Args>(args)...));
}

template <typename... Args> LogContext &LogContext::operator()(Args &&...args)
{
    return (*this)(CppUtilities::argsToString(
        CppUtilities::EscapeCodes::formattedPhraseString(CppUtilities::EscapeCodes::Phrases::InfoMessage), std::forward<Args>(args)...));
}

} // namespace LibRepoMgr

#endif // LIBREPOMGR_LOGGING_H
