#ifndef LIBREPOMGR_RENDER_H
#define LIBREPOMGR_RENDER_H

#include "./typedefs.h"

#include <boost/beast/http/empty_body.hpp>

#include <reflective_rapidjson/json/reflector.h>

#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>

namespace LibRepoMgr {

namespace WebAPI {

namespace Render {
struct ChunkResponse {
    explicit ChunkResponse();
    boost::beast::http::response<boost::beast::http::empty_body> response;
    boost::beast::http::response_serializer<boost::beast::http::empty_body> serializer;
};

std::string escapeHtml(std::string_view unescapedStr);

std::shared_ptr<Response> makeOptions(const Request &request, std::string_view origin = "*", std::string_view methods = "GET,POST",
    std::string_view headers = "Content-Type, Access-Control-Allow-Headers, Authorization, X-API-Key, X-Client-ID");
std::shared_ptr<Response> makeBadRequest(const Request &request, std::string_view why);
std::shared_ptr<Response> makeBadRequest(const Request &request, RAPIDJSON_NAMESPACE::StringBuffer &&why);
std::shared_ptr<Response> makeNotFound(const Request &request, std::string_view target);
std::shared_ptr<Response> makeAuthRequired(const Request &request);
std::shared_ptr<Response> makeForbidden(const Request &request);
std::shared_ptr<Response> makeServerError(const Request &request, std::string_view what);
std::shared_ptr<Response> makeData(const Request &request, const std::string &data, const char *mimeType);
std::shared_ptr<Response> makeData(const Request &request, std::string &&data, const char *mimeType);
std::shared_ptr<Response> makeData(const Request &request, RAPIDJSON_NAMESPACE::StringBuffer &&buffer, const char *mimeType);
std::shared_ptr<Response> makeText(const Request &request, const std::string &text);
std::shared_ptr<Response> makeText(const Request &request, std::string &&text);
std::shared_ptr<Response> makeJson(const Request &request, std::string &&json);
std::shared_ptr<FileResponse> makeFile(const Request &request, const char *filePath, const char *mimeType, boost::beast::error_code &ec);
std::shared_ptr<ChunkResponse> makeChunkResponse(const Request &request, const char *mimeType);

inline std::shared_ptr<Response> makeText(const Request &request, const std::string &text)
{
    return makeData(request, text, "text/plain");
}

inline std::shared_ptr<Response> makeText(const Request &request, std::string &&text)
{
    return makeData(request, std::move(text), "text/plain");
}

inline std::shared_ptr<Response> makeHtml(const Request &request, std::string &&text)
{
    return makeData(request, std::move(text), "text/html");
}

inline std::shared_ptr<Response> makeJson(const Request &request, std::string &&json)
{
    return makeData(request, std::move(json), "application/json");
}

inline std::shared_ptr<Response> makeJson(const Request &request, RAPIDJSON_NAMESPACE::StringBuffer &&buffer)
{
    return makeData(request, std::move(buffer), "application/json");
}

inline std::shared_ptr<Response> makeJson(const Request &request, const RAPIDJSON_NAMESPACE::Document &document, bool pretty = true)
{
    RAPIDJSON_NAMESPACE::StringBuffer buffer;
    if (pretty) {
        RAPIDJSON_NAMESPACE::PrettyWriter<RAPIDJSON_NAMESPACE::StringBuffer> writer(buffer);
        document.Accept(writer);
    } else {
        RAPIDJSON_NAMESPACE::Writer<RAPIDJSON_NAMESPACE::StringBuffer> writer(buffer);
        document.Accept(writer);
    }
    return makeJson(request, std::move(buffer));
}

template <typename Type,
    CppUtilities::Traits::EnableIfAny<ReflectiveRapidJSON::JsonReflector::IsJsonSerializable<Type>, ReflectiveRapidJSON::IsMapOrHash<Type>>
        * = nullptr>
inline std::shared_ptr<Response> makeJson(const Request &request, const Type &serializable, bool pretty = true)
{
    RAPIDJSON_NAMESPACE::Document document(RAPIDJSON_NAMESPACE::kObjectType);
    ReflectiveRapidJSON::JsonReflector::push(serializable, document, document.GetAllocator());
    return makeJson(request, document, pretty);
}

template <typename Type, CppUtilities::Traits::EnableIfAny<ReflectiveRapidJSON::IsArray<Type>> * = nullptr>
inline std::shared_ptr<Response> makeJson(const Request &request, const Type &serializable, bool pretty = true)
{
    RAPIDJSON_NAMESPACE::Document document(RAPIDJSON_NAMESPACE::kArrayType);
    ReflectiveRapidJSON::JsonReflector::push(serializable, document, document.GetAllocator());
    return makeJson(request, document, pretty);
}

} // namespace Render

} // namespace WebAPI
} // namespace LibRepoMgr

#endif // LIBREPOMGR_RENDER_H
