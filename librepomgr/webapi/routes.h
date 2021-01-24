#ifndef LIBREPOMGR_ROUTES_H
#define LIBREPOMGR_ROUTES_H

#include "./typedefs.h"

#include "../buildactions/buildactionfwd.h"

namespace LibRepoMgr {
namespace WebAPI {

namespace Routes {
void getRoot(const Params &params, ResponseHandler &&handler);
void getVersion(const Params &params, ResponseHandler &&handler);
void getStatus(const Params &params, ResponseHandler &&handler);
void getDatabases(const Params &params, ResponseHandler &&handler);
void getUnresolved(const Params &params, ResponseHandler &&handler);
void getPackages(const Params &params, ResponseHandler &&handler);
void getBuildActions(const Params &params, ResponseHandler &&handler);
void getBuildActionDetails(const Params &params, ResponseHandler &&handler);
void getBuildActionOutput(const Params &params, ResponseHandler &&handler);
void getBuildActionLogFile(const Params &params, ResponseHandler &&handler);
void getBuildActionArtefact(const Params &params, ResponseHandler &&handler);
void postLoadPackages(const Params &params, ResponseHandler &&handler);
void postDumpCacheFile(const Params &params, ResponseHandler &&handler);
void postBuildAction(const Params &params, ResponseHandler &&handler);
void postBuildActionsFromTask(const Params &params, ResponseHandler &&handler, const std::string &taskName, const std::string &directory,
    const std::vector<BuildActionIdType> &startAfterIds, bool startImmediately);
void deleteBuildActions(const Params &params, ResponseHandler &&handler);
void postCloneBuildActions(const Params &params, ResponseHandler &&handler);
void postStartBuildActions(const Params &params, ResponseHandler &&handler);
void postStopBuildActions(const Params &params, ResponseHandler &&handler);
void postQuit(const Params &params, ResponseHandler &&handler);
} // namespace Routes

} // namespace WebAPI
} // namespace LibRepoMgr

#endif // LIBREPOMGR_ROUTES_H
