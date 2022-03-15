#include "./render.h"

#include <c++utilities/conversion/stringbuilder.h>

#include <boost/beast/core.hpp>
#include <boost/beast/version.hpp>

using namespace std;
using namespace boost::beast;
using namespace CppUtilities;

namespace LibRepoMgr {
namespace WebAPI {
namespace Render {

string escapeHtml(std::string_view unescapedStr)
{
    // allocate string for result
    string::size_type requiredSize = unescapedStr.size();
    for (const auto &c : unescapedStr) {
        switch (c) {
        case '&':
            requiredSize += 4;
            break;
        case '\"':
        case '\'':
            requiredSize += 5;
            break;
        case '<':
        case '>':
            requiredSize += 3;
            break;
        default:;
        }
    }
    string escapedStr;
    escapedStr.reserve(requiredSize);

    // make escaped string
    for (const auto &c : unescapedStr) {
        switch (c) {
        case '&':
            escapedStr += "&amp;";
            break;
        case '\"':
            escapedStr += "&quot;";
            break;
        case '\'':
            escapedStr += "&apos;";
            break;
        case '<':
            escapedStr += "&lt;";
            break;
        case '>':
            escapedStr += "&gt;";
            break;
        default:
            escapedStr += c;
        }
    }
    return escapedStr;
}

std::shared_ptr<Response> makeOptions(const Request &request, std::string_view origin, std::string_view methods, std::string_view headers)
{
    const auto res = make_shared<Response>(http::status::ok, request.version());
    res->set(http::field::server, BOOST_BEAST_VERSION_STRING);
    res->set(http::field::access_control_allow_origin, boost::beast::string_view(origin.data(), origin.size()));
    res->set(http::field::access_control_allow_methods, boost::beast::string_view(methods.data(), methods.size()));
    res->set(http::field::access_control_allow_headers, boost::beast::string_view(headers.data(), headers.size()));
    res->keep_alive(request.keep_alive());
    res->prepare_payload();
    return res;
}

std::shared_ptr<Response> makeBadRequest(const Request &request, std::string_view why)
{
    const auto res = make_shared<Response>(http::status::bad_request, request.version());
    res->set(http::field::server, BOOST_BEAST_VERSION_STRING);
    res->set(http::field::content_type, "text/plain");
    res->set(http::field::access_control_allow_origin, "*");
    res->keep_alive(request.keep_alive());
    res->body() = why;
    res->prepare_payload();
    return res;
}

std::shared_ptr<Response> makeBadRequest(const Request &request, RAPIDJSON_NAMESPACE::StringBuffer &&why)
{
    const auto res = make_shared<Response>(http::status::bad_request, request.version());
    res->set(http::field::server, BOOST_BEAST_VERSION_STRING);
    res->set(http::field::content_type, "application/json");
    res->set(http::field::access_control_allow_origin, "*");
    res->keep_alive(request.keep_alive());
    res->body().assign(why.GetString(), why.GetSize());
    res->prepare_payload();
    return res;
}

std::shared_ptr<Response> makeNotFound(const Request &request, std::string_view target)
{
    const auto res = make_shared<Response>(http::status::not_found, request.version());
    res->set(http::field::server, BOOST_BEAST_VERSION_STRING);
    res->set(http::field::content_type, "text/plain");
    res->set(http::field::access_control_allow_origin, "*");
    res->keep_alive(request.keep_alive());
    res->body() = "The resource '" % target + "' was not found.";
    res->prepare_payload();
    return res;
}

std::shared_ptr<Response> makeAuthRequired(const Request &request)
{
    const auto res = make_shared<Response>(http::status::unauthorized, request.version());
    res->set(http::field::server, BOOST_BEAST_VERSION_STRING);
    res->set(http::field::content_type, "text/plain");
    res->set(http::field::www_authenticate, "Basic realm=\"auth required\"");
    res->set(http::field::access_control_allow_origin, "*");
    res->keep_alive(request.keep_alive());
    res->body() = "Authenticate to access the resource.";
    res->prepare_payload();
    return res;
}

std::shared_ptr<Response> makeForbidden(const Request &request)
{
    const auto res = make_shared<Response>(http::status::forbidden, request.version());
    res->set(http::field::server, BOOST_BEAST_VERSION_STRING);
    res->set(http::field::content_type, "text/plain");
    res->set(http::field::access_control_allow_origin, "*");
    res->keep_alive(request.keep_alive());
    res->body() = "Access denied.";
    res->prepare_payload();
    return res;
}

std::shared_ptr<Response> makeServerError(const Request &request, std::string_view what)
{
    const auto res = make_shared<Response>(http::status::internal_server_error, request.version());
    res->set(http::field::server, BOOST_BEAST_VERSION_STRING);
    res->set(http::field::content_type, "text/plain");
    res->set(http::field::access_control_allow_origin, "*");
    res->keep_alive(request.keep_alive());
    res->body() = argsToString("An error occurred: '", what, "'");
    res->prepare_payload();
    return res;
}

std::shared_ptr<Response> makeData(const Request &request, const string &buffer, boost::beast::string_view mimeType)
{
    const auto res = make_shared<Response>(http::status::ok, request.version());
    res->set(http::field::server, BOOST_BEAST_VERSION_STRING);
    res->set(http::field::content_type, mimeType);
    res->set(http::field::access_control_allow_origin, "*");
    res->keep_alive(request.keep_alive());
    res->body() = buffer;
    res->prepare_payload();
    return res;
}

std::shared_ptr<Response> makeData(const Request &request, string &&buffer, boost::beast::string_view mimeType)
{
    const auto res = make_shared<Response>(http::status::ok, request.version());
    res->set(http::field::server, BOOST_BEAST_VERSION_STRING);
    res->set(http::field::content_type, mimeType);
    res->set(http::field::access_control_allow_origin, "*");
    res->keep_alive(request.keep_alive());
    res->body() = buffer;
    res->prepare_payload();
    return res;
}

std::shared_ptr<Response> makeData(const Request &request, rapidjson::StringBuffer &&buffer, boost::beast::string_view mimeType)
{
    const auto res = make_shared<Response>(http::status::ok, request.version());
    res->set(http::field::server, BOOST_BEAST_VERSION_STRING);
    if (!mimeType.empty()) {
        res->set(http::field::content_type, mimeType);
    }
    res->set(http::field::access_control_allow_origin, "*");
    res->keep_alive(request.keep_alive());
    res->body().assign(buffer.GetString(), buffer.GetSize());
    res->prepare_payload();
    return res;
}

std::shared_ptr<FileResponse> makeFile(const Request &request, const char *filePath, boost::beast::string_view mimeType,
    boost::beast::string_view contentDisposition, boost::beast::error_code &ec)
{
    const auto fileResponse = std::make_shared<FileResponse>();
    fileResponse->set(http::field::server, BOOST_BEAST_VERSION_STRING);
    if (!mimeType.empty()) {
        fileResponse->set(http::field::content_type, mimeType);
    }
    if (!contentDisposition.empty()) {
        fileResponse->set(http::field::content_disposition, contentDisposition);
    }
    fileResponse->set(http::field::access_control_allow_origin, "*");
    fileResponse->keep_alive(request.keep_alive());
    fileResponse->body().open(filePath, file_mode::scan, ec);
    return fileResponse;
}

inline ChunkResponse::ChunkResponse()
    : response(boost::beast::http::status::ok, 11)
    , serializer(response)
{
    response.set(boost::beast::http::field::server, BOOST_BEAST_VERSION_STRING);
    response.set(boost::beast::http::field::access_control_allow_origin, "*");
    response.chunked(true);
}

std::shared_ptr<ChunkResponse> makeChunkResponse(
    const Request &request, boost::beast::string_view mimeType, boost::beast::string_view contentDisposition)
{
    const auto chunkResponse = std::make_shared<ChunkResponse>();
    if (!mimeType.empty()) {
        chunkResponse->response.set(boost::beast::http::field::content_type, mimeType);
    }
    if (!contentDisposition.empty()) {
        chunkResponse->response.set(boost::beast::http::field::content_disposition, contentDisposition);
    }
    chunkResponse->response.keep_alive(request.keep_alive());
    return chunkResponse;
}

} // namespace Render
} // namespace WebAPI
} // namespace LibRepoMgr
