#ifndef LIBPKG_DATA_LOCKABLE_H
#define LIBPKG_DATA_LOCKABLE_H

#include <mutex>
#include <shared_mutex>

namespace LibPkg {

struct Lockable {
    [[nodiscard]] std::shared_lock<std::shared_mutex> lockToRead() const;
    [[nodiscard]] std::unique_lock<std::shared_mutex> lockToWrite();
    [[nodiscard]] std::shared_lock<std::shared_mutex> tryLockToRead() const;
    [[nodiscard]] std::unique_lock<std::shared_mutex> tryLockToWrite();
    [[nodiscard]] std::unique_lock<std::shared_mutex> lockToWrite(std::shared_lock<std::shared_mutex> &readLock);

private:
    mutable std::shared_mutex m_mutex;
};

inline std::shared_lock<std::shared_mutex> Lockable::lockToRead() const
{
    return std::shared_lock<std::shared_mutex>(m_mutex);
}

inline std::unique_lock<std::shared_mutex> Lockable::lockToWrite()
{
    return std::unique_lock<std::shared_mutex>(m_mutex);
}

inline std::shared_lock<std::shared_mutex> Lockable::tryLockToRead() const
{
    return std::shared_lock<std::shared_mutex>(m_mutex, std::try_to_lock);
}

inline std::unique_lock<std::shared_mutex> Lockable::tryLockToWrite()
{
    return std::unique_lock<std::shared_mutex>(m_mutex, std::try_to_lock);
}

inline std::unique_lock<std::shared_mutex> Lockable::lockToWrite(std::shared_lock<std::shared_mutex> &readLock)
{
    readLock.unlock();
    return std::unique_lock<std::shared_mutex>(m_mutex);
}

} // namespace LibPkg

#endif // LIBPKG_DATA_LOCKABLE_H
