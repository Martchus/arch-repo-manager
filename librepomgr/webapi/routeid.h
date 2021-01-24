#ifndef LIBREPOMGR_ROUTE_IDS_H
#define LIBREPOMGR_ROUTE_IDS_H

#include "../authentication.h"

#include "./typedefs.h"

#include <boost/beast/http/verb.hpp>

#include <string>

namespace LibRepoMgr {
namespace WebAPI {

struct RouteId {
    boost::beast::http::verb method;
    std::string path;

    bool operator==(const RouteId &other) const;
};

inline bool RouteId::operator==(const RouteId &other) const
{
    return method == other.method && path == other.path;
}

struct Route {
    RouteHandler handler;
    UserPermissions permissions = UserPermissions::None;
};
using Router = std::unordered_map<RouteId, Route>;

} // namespace WebAPI
} // namespace LibRepoMgr

namespace std {

template <> struct hash<LibRepoMgr::WebAPI::RouteId> {
    std::size_t operator()(const LibRepoMgr::WebAPI::RouteId &id) const
    {
        using VerbType = typename std::underlying_type<boost::beast::http::verb>::type;
        return std::hash<VerbType>()(static_cast<VerbType>(id.method)) ^ (std::hash<std::string>()(id.path) << 1);
    }
};

} // namespace std

#endif // LIBREPOMGR_ROUTE_IDS_H
