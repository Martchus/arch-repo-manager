#ifndef LIBREPOMGR_ROUTES_H
#define LIBREPOMGR_ROUTES_H

#include "../global.h"
#include "./typedefs.h"

#include "../buildactions/buildactionfwd.h"

namespace LibRepoMgr {
namespace WebAPI {

namespace Routes {
LIBREPOMGR_EXPORT void getRoot(const Params &params, ResponseHandler &&handler);
LIBREPOMGR_EXPORT void getVersion(const Params &params, ResponseHandler &&handler);
LIBREPOMGR_EXPORT void getStatus(const Params &params, ResponseHandler &&handler);
LIBREPOMGR_EXPORT void getDatabases(const Params &params, ResponseHandler &&handler);
LIBREPOMGR_EXPORT void getUnresolved(const Params &params, ResponseHandler &&handler);
LIBREPOMGR_EXPORT void getPackages(const Params &params, ResponseHandler &&handler);
LIBREPOMGR_EXPORT void getBuildActions(const Params &params, ResponseHandler &&handler);
LIBREPOMGR_EXPORT void getBuildActionDetails(const Params &params, ResponseHandler &&handler);
LIBREPOMGR_EXPORT void getBuildActionOutput(const Params &params, ResponseHandler &&handler);
LIBREPOMGR_EXPORT void getBuildActionLogFile(const Params &params, ResponseHandler &&handler);
LIBREPOMGR_EXPORT void getBuildActionArtefact(const Params &params, ResponseHandler &&handler);
LIBREPOMGR_EXPORT void postLoadPackages(const Params &params, ResponseHandler &&handler);
LIBREPOMGR_EXPORT void postBuildAction(const Params &params, ResponseHandler &&handler);
LIBREPOMGR_EXPORT void postBuildActionsFromTask(const Params &params, ResponseHandler &&handler, const std::string &taskName,
    const std::string &directory, const std::vector<BuildActionIdType> &startAfterIds, bool startImmediately);
LIBREPOMGR_EXPORT void deleteBuildActions(const Params &params, ResponseHandler &&handler);
LIBREPOMGR_EXPORT void postCloneBuildActions(const Params &params, ResponseHandler &&handler);
LIBREPOMGR_EXPORT void postStartBuildActions(const Params &params, ResponseHandler &&handler);
LIBREPOMGR_EXPORT void postStopBuildActions(const Params &params, ResponseHandler &&handler);
LIBREPOMGR_EXPORT void postQuit(const Params &params, ResponseHandler &&handler);
} // namespace Routes

} // namespace WebAPI
} // namespace LibRepoMgr

#endif // LIBREPOMGR_ROUTES_H
