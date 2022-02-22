#ifndef LIBREPOMGR_DATABASE_H
#define LIBREPOMGR_DATABASE_H

#include "../multisession.h"
#include "../serversetup.h"

#include "./session.h"

#include <optional>
#include <string>
#include <vector>

namespace LibRepoMgr {

struct LogContext;

namespace WebClient {
using DatabaseQuerySession = MultiSession<std::string>;

struct DatabaseQueryParams {
    std::string databaseName;
    std::string databaseArch;
    std::string url;
    std::string destinationFilePath;
};
struct DatabaseQuery {
    std::vector<DatabaseQueryParams> queryParamsForDbs;
    std::vector<std::string> failedDbs;
};

[[nodiscard]] DatabaseQuery prepareDatabaseQuery(LogContext &log, const std::vector<LibPkg::Database *> &dbs, bool withFiles);
std::shared_ptr<DatabaseQuerySession> queryDatabases(
    LogContext &log, ServiceSetup &setup, std::vector<DatabaseQueryParams> &&urls, bool force, DatabaseQuerySession::HandlerType &&handler);
std::shared_ptr<DatabaseQuerySession> queryDatabases(LogContext &log, ServiceSetup &setup, std::shared_lock<std::shared_mutex> *configReadLock,
    const std::vector<LibPkg::Database *> &dbs, bool force, DatabaseQuerySession::HandlerType &&handler);
void queryDatabases(LogContext &log, ServiceSetup &setup, std::vector<DatabaseQueryParams> &&urls,
    std::shared_ptr<DatabaseQuerySession> &dbQuerySession, bool force = false);

struct PackageCachingDataForPackage {
    std::string_view url;
    std::string_view destinationFilePath;
    std::string error;
};
using PackageCachingDataForDatabase = std::unordered_map<std::string_view, PackageCachingDataForPackage>;
using PackageCachingDataForSession = std::unordered_map<std::string_view, PackageCachingDataForDatabase>;

struct PackageCachingSession;
void cachePackages(LogContext &log, std::shared_ptr<PackageCachingSession> &&packageCachingSession,
    std::optional<std::uint64_t> bodyLimit = std::nullopt, std::size_t maxParallelDownloads = 8);

struct PackageCachingSession : public MultiSession<void> {
    friend void cachePackages(LogContext &, std::shared_ptr<PackageCachingSession> &&, std::optional<std::uint64_t>, std::size_t);
    using SharedPointerType = std::shared_ptr<PackageCachingSession>;

    explicit PackageCachingSession(
        PackageCachingDataForSession &data, boost::asio::io_context &ioContext, boost::asio::ssl::context &sslContext, HandlerType &&handler);

    const std::atomic_bool *aborted = nullptr;

private:
    void selectNextPackage();
    PackageCachingDataForPackage *getCurrentDataAndSelectNext();

    boost::asio::ssl::context &m_sslContext;
    PackageCachingDataForSession &m_data;
    std::mutex m_mutex;
    PackageCachingDataForSession::iterator m_dbIterator, m_dbEnd;
    PackageCachingDataForDatabase::iterator m_packageIterator, m_packageEnd;
};

} // namespace WebClient

} // namespace LibRepoMgr

#endif // LIBREPOMGR_DATABASE_H
