#ifndef LIBREPOMGR_AUR_H
#define LIBREPOMGR_AUR_H

#include "../multisession.h"
#include "../serversetup.h"

#include "./session.h"

#include <string>
#include <vector>

#include <boost/asio/io_context.hpp>

namespace LibRepoMgr {

struct LogContext;

namespace WebClient {
struct AurSnapshotResult {
    std::string packageName;
    std::string errorOutput;
    std::shared_ptr<LibPkg::SourceInfo> sourceInfo;
    std::vector<std::shared_ptr<LibPkg::Package>> packages;
    std::string error;
};
struct AurSnapshotQueryParams {
    const std::string *packageName;
    const std::string *targetDirectory;
};

using AurQuerySession = MultiSession<std::shared_ptr<LibPkg::Package>>;
using AurSnapshotQuerySession = MultiSession<AurSnapshotResult>;

void searchAurPackages(LogContext &log, ServiceSetup &setup, const std::string &searchTerms, boost::asio::io_context &ioContext,
    std::shared_ptr<AurQuerySession> &multiSession);

std::shared_ptr<AurQuerySession> queryAurPackages(LogContext &log, ServiceSetup &setup, const std::vector<std::string> &packages,
    boost::asio::io_context &ioContext, typename AurQuerySession::HandlerType &&handler);
std::shared_ptr<AurQuerySession> queryAurPackages(LogContext &log, ServiceSetup &setup,
    const std::unordered_map<std::string, std::shared_ptr<LibPkg::Package>> &packages, boost::asio::io_context &ioContext,
    typename AurQuerySession::HandlerType &&handler);
std::shared_ptr<AurQuerySession> queryAurPackagesForDatabase(LogContext &log, ServiceSetup &setup, boost::asio::io_context &ioContext,
    std::shared_lock<std::shared_mutex> *configReadLock, LibPkg::Database &database, typename AurQuerySession::HandlerType &&handler);

void queryAurSnapshots(LogContext &log, ServiceSetup &setup, const std::vector<AurSnapshotQueryParams> &queryParams,
    boost::asio::io_context &ioContext, std::shared_ptr<AurSnapshotQuerySession> &multiSession);

} // namespace WebClient

} // namespace LibRepoMgr

#endif // LIBREPOMGR_AUR_H
