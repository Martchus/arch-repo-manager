#ifndef LIBREPOMGR_CLIENT_SESSION_H
#define LIBREPOMGR_CLIENT_SESSION_H

#include "../global.h"
#include "../webapi/typedefs.h"

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>

#include <functional>
#include <stdexcept>
#include <variant>

namespace LibRepoMgr {

struct ServiceSetup;

namespace WebClient {

struct LIBREPOMGR_EXPORT HttpClientError : std::runtime_error {
    explicit HttpClientError();
    explicit HttpClientError(const char *context, boost::beast::error_code errorCode);
    operator bool() const;

    const char *const context;
    const boost::beast::error_code errorCode;
};

inline HttpClientError::HttpClientError()
    : std::runtime_error(std::string())
    , context(nullptr)
    , errorCode(boost::beast::error_code())
{
}

inline LibRepoMgr::WebClient::HttpClientError::operator bool() const
{
    return errorCode ? true : false;
}

using Response = WebAPI::Response;
using FileResponse = boost::beast::http::response_parser<boost::beast::http::file_body>;
using EmptyResponse = boost::beast::http::response_parser<boost::beast::http::empty_body>;
using MultiResponse = std::variant<Response, FileResponse, EmptyResponse>;
using Request = boost::beast::http::request<boost::beast::http::empty_body>;
struct ChunkProcessing;

class LIBREPOMGR_EXPORT Session : public std::enable_shared_from_this<Session> {
public:
    using Handler = std::function<void(Session &, const HttpClientError &error)>;
    using ChunkHandler = std::function<void(const boost::beast::http::chunk_extensions &chunkExtensions, std::string_view chunkData)>;

    template <typename ResponseType = Response>
    explicit Session(boost::asio::io_context &ioContext, const Handler &handler = Handler());
    template <typename ResponseType = Response>
    explicit Session(boost::asio::io_context &ioContext, Handler &&handler = Handler());
    template <typename ResponseType = Response>
    explicit Session(boost::asio::io_context &ioContext, boost::asio::ssl::context &sslContext, const Handler &handler = Handler());
    template <typename ResponseType = Response>
    explicit Session(boost::asio::io_context &ioContext, boost::asio::ssl::context &sslContext, Handler &&handler);

    void setChunkHandler(ChunkHandler &&handler);
    void run(const char *host, const char *port, boost::beast::http::verb verb, const char *target, unsigned int version = 11);

private:
    using RawSocket = boost::asio::ip::tcp::socket;
    using SslStream = boost::asio::ssl::stream<boost::asio::ip::tcp::socket>;
    struct ChunkProcessing {
        boost::beast::http::chunk_extensions chunkExtensions;
        std::string currentChunk;
        std::function<void(std::uint64_t chunkSize, boost::beast::string_view extensions, boost::beast::error_code &ec)> onChunkHeader;
        std::function<std::size_t(std::uint64_t bytesLeftInThisChunk, boost::beast::string_view chunkBodyData, boost::beast::error_code &ec)> onChunkBody;
        Session::ChunkHandler handler;
    };

    RawSocket &socket();

    void resolved(boost::beast::error_code ec, boost::asio::ip::tcp::resolver::results_type results);
    void connected(boost::beast::error_code ec);
    void handshakeDone(boost::beast::error_code ec);
    void sendRequest();
    void requested(boost::beast::error_code ec, std::size_t bytesTransferred);
    void onChunkHeader(std::uint64_t chunkSize, boost::beast::string_view extensions, boost::beast::error_code &ec);
    std::size_t onChunkBody(std::uint64_t bytesLeftInThisChunk, boost::beast::string_view chunkBodyData, boost::beast::error_code &ec);
    void chunkReceived(boost::beast::error_code ec, std::size_t bytesTransferred);
    bool continueReadingChunks();
    void received(boost::beast::error_code ec, std::size_t bytesTransferred);
    void closed(boost::beast::error_code ec);

public:
    Request request;
    MultiResponse response;
    std::string destinationFilePath;

private:
    boost::asio::ip::tcp::resolver m_resolver;
    std::variant<RawSocket, SslStream> m_stream;
    boost::beast::flat_buffer m_buffer;
    std::unique_ptr<ChunkProcessing> m_chunkProcessing;
    Handler m_handler;
};

template <typename ResponseType>
inline Session::Session(boost::asio::io_context &ioContext, const Handler &handler)
    : response(ResponseType{})
    , m_resolver(ioContext)
    , m_stream(RawSocket{ioContext})
    , m_handler(handler)
{
}

template <typename ResponseType>
inline Session::Session(boost::asio::io_context &ioContext, Handler &&handler)
    : response(ResponseType{})
    , m_resolver(ioContext)
    , m_stream(RawSocket{ioContext})
    , m_handler(std::move(handler))
{
}

template <typename ResponseType>
inline Session::Session(boost::asio::io_context &ioContext, boost::asio::ssl::context &sslContext, const Handler &handler)
    : response(ResponseType{})
    , m_resolver(ioContext)
    , m_stream(SslStream{ioContext, sslContext})
    , m_handler(handler)
{
}

template <typename ResponseType>
inline Session::Session(boost::asio::io_context &ioContext, boost::asio::ssl::context &sslContext, Handler &&handler)
    : response(ResponseType{})
    , m_resolver(ioContext)
    , m_stream(SslStream{ioContext, sslContext})
    , m_handler(std::move(handler))
{
}

LIBREPOMGR_EXPORT std::variant<std::string, std::shared_ptr<Session>> runSessionFromUrl(
    boost::asio::io_context &ioContext, boost::asio::ssl::context &sslContext, std::string_view url,
    Session::Handler &&handler, std::string &&destinationPath = std::string(),
    std::string_view userName = std::string_view(), std::string_view password = std::string_view(),
    boost::beast::http::verb verb = boost::beast::http::verb::get, Session::ChunkHandler &&chunkHandler = Session::ChunkHandler());

} // namespace WebClient
} // namespace LibRepoMgr

#endif // LIBREPOMGR_CLIENT_SESSION_H
