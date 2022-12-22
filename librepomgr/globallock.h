#ifndef LIBREPOMGR_GLOBAL_LOCK_H
#define LIBREPOMGR_GLOBAL_LOCK_H

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <list>
#include <mutex>
#include <shared_mutex>

#include "./global.h"

namespace LibRepoMgr {

struct LogContext;

/// \brief A shared mutex where ownership is not tied to a thread (similar to a binary semaphore in that regard).
struct GlobalSharedMutex {
    void lock();
    bool try_lock();
    void lock_async(std::move_only_function<void()> &&callback);
    void unlock();

    void lock_shared();
    bool try_lock_shared();
    void lock_shared_async(std::move_only_function<void()> &&callback);
    void unlock_shared();

private:
    void notify(std::unique_lock<std::mutex> &lock);

    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::uint32_t m_sharedOwners = 0;
    bool m_exclusivelyOwned = false;
    std::list<std::move_only_function<void()>> m_sharedCallbacks;
    std::move_only_function<void()> m_exclusiveCallback;
};

inline void GlobalSharedMutex::lock()
{
    auto lock = std::unique_lock<std::mutex>(m_mutex);
    while (m_sharedOwners || m_exclusivelyOwned) {
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

inline void GlobalSharedMutex::lock_async(std::move_only_function<void()> &&callback)
{
    auto lock = std::unique_lock<std::mutex>(m_mutex);
    if (m_sharedOwners || m_exclusivelyOwned) {
        m_exclusiveCallback = std::move(callback);
    } else {
        m_exclusivelyOwned = true;
        lock.unlock();
        callback();
    }
}

inline void GlobalSharedMutex::unlock()
{
    auto lock = std::unique_lock<std::mutex>(m_mutex);
    m_exclusivelyOwned = false;
    notify(lock);
}

inline void GlobalSharedMutex::lock_shared()
{
    auto lock = std::unique_lock<std::mutex>(m_mutex);
    while (m_exclusivelyOwned) {
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

inline void GlobalSharedMutex::lock_shared_async(std::move_only_function<void()> &&callback)
{
    auto lock = std::unique_lock<std::mutex>(m_mutex);
    if (m_exclusivelyOwned) {
        m_sharedCallbacks.emplace_back(std::move(callback));
    } else {
        ++m_sharedOwners;
        lock.unlock();
        callback();
    }
}

inline void GlobalSharedMutex::unlock_shared()
{
    auto lock = std::unique_lock<std::mutex>(m_mutex);
    if (!--m_sharedOwners) {
        notify(lock);
    }
}

inline void GlobalSharedMutex::notify(std::unique_lock<std::mutex> &lock)
{
    // invoke callbacks for lock_shared_async()
    if (!m_sharedCallbacks.empty() && !m_exclusivelyOwned) {
        auto callbacks = std::move(m_sharedCallbacks);
        m_sharedOwners += static_cast<std::uint32_t>(callbacks.size());
        lock.unlock();
        for (auto &callback : callbacks) {
            callback();
        }
        return;
    }
    // invoke callbacks for lock_async()
    if (m_exclusiveCallback) {
        if (!m_sharedOwners && !m_exclusivelyOwned) {
            auto callback = std::move(m_exclusiveCallback);
            m_exclusivelyOwned = true;
            lock.unlock();
            callback();
            return;
        }
    }
    // resume threads blocked in lock() and lock_shared()
    lock.unlock();
    m_cv.notify_one();
}

/// \brief A wrapper around a standard lock which logs acquisition/release.
template <typename UnderlyingLockType> struct LoggingLock {
    using LockType = UnderlyingLockType;
    explicit LoggingLock(LogContext &log, std::string &&name);
    template <typename... Args> explicit LoggingLock(LogContext &log, std::string &&name, Args &&...args);
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
inline LoggingLock<UnderlyingLockType>::LoggingLock(LogContext &log, std::string &&name)
    : m_log(log)
    , m_name(std::move(name))
{
    m_log("Acquiring ", lockName(m_lock), " lock \"", m_name, "\"\n");
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
    void lockToRead(LogContext &log, std::string &&name, std::move_only_function<void(SharedLoggingLock &&lock)> &&callback) const;
    void lockToWrite(LogContext &log, std::string &&name, std::move_only_function<void(UniqueLoggingLock &&lock)> &&callback);

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

inline void LibRepoMgr::GlobalLockable::lockToRead(
    LogContext &log, std::string &&name, std::move_only_function<void(SharedLoggingLock &&)> &&callback) const
{
    m_mutex.lock_shared_async([this, lock = SharedLoggingLock(log, std::move(name)), cb = std::move(callback)]() mutable {
        lock.lock() = SharedLoggingLock::LockType(m_mutex, std::adopt_lock);
        cb(std::move(lock));
    });
}

inline void LibRepoMgr::GlobalLockable::lockToWrite(
    LogContext &log, std::string &&name, std::move_only_function<void(UniqueLoggingLock &&)> &&callback)
{
    m_mutex.lock_async([this, lock = UniqueLoggingLock(log, std::move(name)), cb = std::move(callback)]() mutable {
        lock.lock() = UniqueLoggingLock::LockType(m_mutex, std::adopt_lock);
        cb(std::move(lock));
    });
}

} // namespace LibRepoMgr

#endif // LIBREPOMGR_GLOBAL_LOCK_H
