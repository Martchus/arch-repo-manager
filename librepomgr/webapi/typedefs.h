#ifndef LIBREPOMGR_TYPEDEFS_H
#define LIBREPOMGR_TYPEDEFS_H

#include <boost/beast/http/file_body.hpp>
#include <boost/beast/http/message.hpp>
#include <boost/beast/http/parser.hpp>
#include <boost/beast/http/serializer.hpp>
#include <boost/beast/http/string_body.hpp>

#include <functional>
#include <memory>

namespace LibPkg {
struct Config;
}

namespace LibRepoMgr {
namespace WebAPI {

struct RouteId;
struct Params;

using Response = boost::beast::http::response<boost::beast::http::string_body>;
using ResponseHandler = std::function<void(std::shared_ptr<Response> &&)>;
using RouteHandler = std::function<void(const Params &, ResponseHandler &&)>;
using FileResponse = boost::beast::http::response<boost::beast::http::file_body>;
using FileStreamResponse = boost::beast::http::response_serializer<boost::beast::http::file_body>;
using Request = boost::beast::http::request<boost::beast::http::string_body>;
using RequestParser = boost::beast::http::request_parser<boost::beast::http::string_body>;

} // namespace WebAPI
} // namespace LibRepoMgr

#endif // LIBREPOMGR_TYPEDEFS_H
