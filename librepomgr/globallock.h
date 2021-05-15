#ifndef LIBREPOMGR_GLOBAL_LOCK_H
#define LIBREPOMGR_GLOBAL_LOCK_H

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <shared_mutex>

#include "./global.h"

namespace LibRepoMgr {

struct LogContext;

/// \brief A shared mutex where ownership is not tied to a thread (similar to a binary semaphore in that regard).
struct GlobalSharedMutex {
    void lock();
    bool try_lock();
    void unlock();

    void lock_shared();
    bool try_lock_shared();
    void unlock_shared();

private:
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::uint32_t m_sharedOwners = 0;
    bool m_exclusivelyOwned = false;
};

inline void GlobalSharedMutex::lock()
{
    auto lock = std::unique_lock<std::mutex>(m_mutex);
    if (m_sharedOwners || m_exclusivelyOwned) {
        m_cv.wait(lock);
    }
    m_exclusivelyOwned = true;
}

inline bool GlobalSharedMutex::try_lock()
{
    auto lock = std::unique_lock<std::mutex>(m_mutex);
    if (m_sharedOwners || m_exclusivelyOwned) {
        return false;
    } else {
        return m_exclusivelyOwned = true;
    }
}

inline void GlobalSharedMutex::unlock()
{
    m_exclusivelyOwned = false;
    m_cv.notify_one();
}

inline void GlobalSharedMutex::lock_shared()
{
    auto lock = std::unique_lock<std::mutex>(m_mutex);
    if (m_exclusivelyOwned) {
        m_cv.wait(lock);
    }
    ++m_sharedOwners;
}

inline bool GlobalSharedMutex::try_lock_shared()
{
    auto lock = std::unique_lock<std::mutex>(m_mutex);
    if (m_exclusivelyOwned) {
        return false;
    } else {
        return ++m_sharedOwners;
    }
}

inline void GlobalSharedMutex::unlock_shared()
{
    auto lock = std::unique_lock<std::mutex>(m_mutex);
    if (!--m_sharedOwners) {
        lock.unlock();
        m_cv.notify_one();
    }
}

/// \brief A wrapper around a standard lock which logs acquisition/release.
template <typename UnderlyingLockType> struct LoggingLock {
    template <typename... Args> LoggingLock(LogContext &log, std::string &&name, Args &&...args);
    LoggingLock(LoggingLock &&) = default;
    ~LoggingLock();

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

constexpr std::string_view lockName(std::shared_lock<GlobalSharedMutex> &)
{
    return "shared";
}
constexpr std::string_view lockName(std::unique_lock<GlobalSharedMutex> &)
{
    return "exclusive";
}

template <typename UnderlyingLockType>
template <typename... Args>
inline LoggingLock<UnderlyingLockType>::LoggingLock(LogContext &log, std::string &&name, Args &&...args)
    : m_log(log)
    , m_name(std::move(name))
{
    m_log("Acquiring ", lockName(m_lock), " lock \"", m_name, "\"\n");
    m_lock = UnderlyingLockType(std::forward<Args>(args)...);
}

template <typename UnderlyingLockType> inline LoggingLock<UnderlyingLockType>::~LoggingLock()
{
    if (m_lock) {
        m_lock.unlock();
        m_log("Released ", lockName(m_lock), " lock \"", m_name, "\"\n");
    }
}

using SharedLoggingLock = LoggingLock<std::shared_lock<GlobalSharedMutex>>;
using UniqueLoggingLock = LoggingLock<std::unique_lock<GlobalSharedMutex>>;

extern template struct LIBREPOMGR_EXPORT LoggingLock<std::shared_lock<GlobalSharedMutex>>;
extern template struct LIBREPOMGR_EXPORT LoggingLock<std::unique_lock<GlobalSharedMutex>>;

/// \brief Same as LibPkg::Lockable but using GlobalSharedMutex.
struct GlobalLockable {
    [[nodiscard]] SharedLoggingLock lockToRead(LogContext &log, std::string &&name) const;
    [[nodiscard]] UniqueLoggingLock lockToWrite(LogContext &log, std::string &&name);
    [[nodiscard]] SharedLoggingLock tryLockToRead(LogContext &log, std::string &&name) const;
    [[nodiscard]] UniqueLoggingLock tryLockToWrite(LogContext &log, std::string &&name);
    [[nodiscard]] UniqueLoggingLock lockToWrite(LogContext &log, std::string &&name, SharedLoggingLock &readLock);

private:
    mutable GlobalSharedMutex m_mutex;
};

inline SharedLoggingLock GlobalLockable::lockToRead(LogContext &log, std::string &&name) const
{
    return SharedLoggingLock(log, std::move(name), m_mutex);
}

inline UniqueLoggingLock GlobalLockable::lockToWrite(LogContext &log, std::string &&name)
{
    return UniqueLoggingLock(log, std::move(name), m_mutex);
}

inline SharedLoggingLock GlobalLockable::tryLockToRead(LogContext &log, std::string &&name) const
{
    return SharedLoggingLock(log, std::move(name), m_mutex, std::try_to_lock);
}

inline UniqueLoggingLock GlobalLockable::tryLockToWrite(LogContext &log, std::string &&name)
{
    return UniqueLoggingLock(log, std::move(name), m_mutex, std::try_to_lock);
}

inline UniqueLoggingLock GlobalLockable::lockToWrite(LogContext &log, std::string &&name, SharedLoggingLock &readLock)
{
    readLock.lock().unlock();
    return UniqueLoggingLock(log, std::move(name), m_mutex);
}

} // namespace LibRepoMgr

#endif // LIBREPOMGR_GLOBAL_LOCK_H
