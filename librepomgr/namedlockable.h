#ifndef LIBREPOMGR_NAMED_LOCKABLE_H
#define LIBREPOMGR_NAMED_LOCKABLE_H

#include <mutex>
#include <shared_mutex>

#include "./global.h"

namespace LibRepoMgr {

struct LogContext;

template <typename UnderlyingLockType> struct NamedLock {
    template <typename... Args> NamedLock(LogContext &log, std::string &&name, Args &&...args);
    NamedLock(NamedLock &&) = default;
    ~NamedLock();

    const std::string &name() const
    {
        return m_name;
    };
    UnderlyingLockType &lock()
    {
        return m_lock;
    };

private:
    LogContext &m_log;
    std::string m_name;
    UnderlyingLockType m_lock;
};

constexpr std::string_view lockName(std::shared_lock<std::shared_mutex> &)
{
    return "shared";
}
constexpr std::string_view lockName(std::unique_lock<std::shared_mutex> &)
{
    return "exclusive";
}

template <typename UnderlyingLockType>
template <typename... Args>
inline NamedLock<UnderlyingLockType>::NamedLock(LogContext &log, std::string &&name, Args &&...args)
    : m_log(log)
    , m_name(std::move(name))
{
    m_log("Acquiring ", lockName(m_lock), " lock \"", m_name, "\"\n");
    m_lock = UnderlyingLockType(std::forward<Args>(args)...);
}

template <typename UnderlyingLockType> inline NamedLock<UnderlyingLockType>::~NamedLock()
{
    if (m_lock) {
        m_lock.unlock();
        m_log("Released ", lockName(m_lock), " lock \"", m_name, "\"\n");
    }
}

using SharedNamedLock = NamedLock<std::shared_lock<std::shared_mutex>>;
using UniqueNamedLock = NamedLock<std::unique_lock<std::shared_mutex>>;

extern template struct LIBREPOMGR_EXPORT NamedLock<std::shared_lock<std::shared_mutex>>;
extern template struct LIBREPOMGR_EXPORT NamedLock<std::unique_lock<std::shared_mutex>>;

struct NamedLockable {
    [[nodiscard]] SharedNamedLock lockToRead(LogContext &log, std::string &&name) const;
    [[nodiscard]] UniqueNamedLock lockToWrite(LogContext &log, std::string &&name);
    [[nodiscard]] SharedNamedLock tryLockToRead(LogContext &log, std::string &&name) const;
    [[nodiscard]] UniqueNamedLock tryLockToWrite(LogContext &log, std::string &&name);
    [[nodiscard]] UniqueNamedLock lockToWrite(LogContext &log, std::string &&name, SharedNamedLock &readLock);

private:
    mutable std::shared_mutex m_mutex;
};

inline SharedNamedLock NamedLockable::lockToRead(LogContext &log, std::string &&name) const
{
    return SharedNamedLock(log, std::move(name), m_mutex);
}

inline UniqueNamedLock NamedLockable::lockToWrite(LogContext &log, std::string &&name)
{
    return UniqueNamedLock(log, std::move(name), m_mutex);
}

inline SharedNamedLock NamedLockable::tryLockToRead(LogContext &log, std::string &&name) const
{
    return SharedNamedLock(log, std::move(name), m_mutex, std::try_to_lock);
}

inline UniqueNamedLock NamedLockable::tryLockToWrite(LogContext &log, std::string &&name)
{
    return UniqueNamedLock(log, std::move(name), m_mutex, std::try_to_lock);
}

inline UniqueNamedLock NamedLockable::lockToWrite(LogContext &log, std::string &&name, SharedNamedLock &readLock)
{
    readLock.lock().unlock();
    return UniqueNamedLock(log, std::move(name), m_mutex);
}

} // namespace LibRepoMgr

#endif // LIBREPOMGR_NAMED_LOCKABLE_H
