#ifndef LIBREPOMGR_CLIENT_SESSION_H
#define LIBREPOMGR_CLIENT_SESSION_H

#include "../global.h"
#include "../webapi/typedefs.h"

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>

#include <functional>
#include <optional>
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
using StringResponse = boost::beast::http::response_parser<boost::beast::http::string_body>;
using EmptyResponse = boost::beast::http::response_parser<boost::beast::http::empty_body>;
using MultiResponse = std::variant<Response, FileResponse, StringResponse>;
using Request = boost::beast::http::request<boost::beast::http::empty_body>;
struct ChunkProcessing;

class LIBREPOMGR_EXPORT Session : public std::enable_shared_from_this<Session> {
public:
    using Handler = std::function<void(Session &, const HttpClientError &error)>;
    using HeadHandler = std::function<void(Session &)>;
    using ChunkHandler = std::function<void(const boost::beast::http::chunk_extensions &chunkExtensions, std::string_view chunkData)>;

    template <typename ResponseType = Response> explicit Session(boost::asio::io_context &ioContext, const Handler &handler = Handler());
    template <typename ResponseType = Response>
    explicit Session(boost::asio::io_context &ioContext, Handler &&handler = Handler(), HeadHandler &&headHandler = HeadHandler());
    template <typename ResponseType = Response>
    explicit Session(boost::asio::io_context &ioContext, boost::asio::ssl::context &sslContext, const Handler &handler = Handler());
    template <typename ResponseType = Response>
    explicit Session(
        boost::asio::io_context &ioContext, boost::asio::ssl::context &sslContext, Handler &&handler, HeadHandler &&headHandler = HeadHandler());

    void setChunkHandler(ChunkHandler &&handler);
    void run(const char *host, const char *port, boost::beast::http::verb verb, const char *target,
        std::optional<std::uint64_t> bodyLimit = std::nullopt, unsigned int version = 11);

private:
    using RawSocket = boost::asio::ip::tcp::socket;
    using SslStream = boost::asio::ssl::stream<boost::asio::ip::tcp::socket>;
    struct ChunkProcessing {
        boost::beast::http::chunk_extensions chunkExtensions;
        std::string currentChunk;
        std::function<void(std::uint64_t chunkSize, boost::beast::string_view extensions, boost::beast::error_code &ec)> onChunkHeader;
        std::function<std::size_t(std::uint64_t bytesLeftInThisChunk, boost::beast::string_view chunkBodyData, boost::beast::error_code &ec)>
            onChunkBody;
        Session::ChunkHandler handler;
    };

    RawSocket &socket();

    bool openDestinationFile();
    bool closeDestinationFile(bool skipHandler);
    void resolved(boost::beast::error_code ec, boost::asio::ip::tcp::resolver::results_type results);
    void connected(boost::beast::error_code ec);
    void handshakeDone(boost::beast::error_code ec);
    void sendRequest();
    void headRequested(boost::beast::error_code ec, std::size_t bytesTransferred);
    void requested(boost::beast::error_code ec, std::size_t bytesTransferred);
    void onChunkHeader(std::uint64_t chunkSize, boost::beast::string_view extensions, boost::beast::error_code &ec);
    std::size_t onChunkBody(std::uint64_t bytesLeftInThisChunk, boost::beast::string_view chunkBodyData, boost::beast::error_code &ec);
    void chunkReceived(boost::beast::error_code ec, std::size_t bytesTransferred);
    bool continueReadingChunks();
    void headReceived(boost::beast::error_code ec, std::size_t bytesTransferred);
    void received(boost::beast::error_code ec, std::size_t bytesTransferred);
    void closeGracefully();
    void closed(boost::beast::error_code ec);
    void invokeHandler(const HttpClientError &error = HttpClientError());

public:
    Request request;
    MultiResponse response;
    EmptyResponse headResponse;
    std::string destinationFilePath;
    boost::beast::http::verb method = boost::beast::http::verb::get;
    bool skip = false;

private:
    boost::asio::ip::tcp::resolver m_resolver;
    std::variant<RawSocket, SslStream> m_stream;
    boost::beast::flat_buffer m_buffer;
    std::unique_ptr<ChunkProcessing> m_chunkProcessing;
    Request m_headRequest;
    Handler m_handler;
    HeadHandler m_headHandler;
    std::uint64_t m_bodyLimit;
};

template <typename ResponseType>
inline Session::Session(boost::asio::io_context &ioContext, const Handler &handler)
    : response(ResponseType{})
    , m_resolver(ioContext)
    , m_stream(RawSocket{ ioContext })
    , m_handler(handler)
{
}

template <typename ResponseType>
inline Session::Session(boost::asio::io_context &ioContext, Handler &&handler, HeadHandler &&headHandler)
    : response(ResponseType{})
    , m_resolver(ioContext)
    , m_stream(RawSocket{ ioContext })
    , m_handler(std::move(handler))
    , m_headHandler(std::move(headHandler))
{
}

template <typename ResponseType>
inline Session::Session(boost::asio::io_context &ioContext, boost::asio::ssl::context &sslContext, const Handler &handler)
    : response(ResponseType{})
    , m_resolver(ioContext)
    , m_stream(SslStream{ ioContext, sslContext })
    , m_handler(handler)
{
}

template <typename ResponseType>
inline Session::Session(boost::asio::io_context &ioContext, boost::asio::ssl::context &sslContext, Handler &&handler, HeadHandler &&headHandler)
    : response(ResponseType{})
    , m_resolver(ioContext)
    , m_stream(SslStream{ ioContext, sslContext })
    , m_handler(std::move(handler))
    , m_headHandler(std::move(headHandler))
{
}

LIBREPOMGR_EXPORT std::variant<std::string, std::shared_ptr<Session>> runSessionFromUrl(boost::asio::io_context &ioContext,
    boost::asio::ssl::context &sslContext, std::string_view url, Session::Handler &&handler, std::string &&destinationPath = std::string(),
    std::string_view userName = std::string_view(), std::string_view password = std::string_view(),
    boost::beast::http::verb verb = boost::beast::http::verb::get, std::optional<std::uint64_t> bodyLimit = std::nullopt,
    Session::ChunkHandler &&chunkHandler = Session::ChunkHandler());
LIBREPOMGR_EXPORT std::variant<std::string, std::shared_ptr<Session>> runSessionFromUrl(boost::asio::io_context &ioContext,
    boost::asio::ssl::context &sslContext, std::string_view url, Session::Handler &&handler, Session::HeadHandler &&headHandler,
    std::string &&destinationPath = std::string(), std::string_view userName = std::string_view(), std::string_view password = std::string_view(),
    boost::beast::http::verb verb = boost::beast::http::verb::get, std::optional<std::uint64_t> bodyLimit = std::nullopt,
    Session::ChunkHandler &&chunkHandler = Session::ChunkHandler());

} // namespace WebClient
} // namespace LibRepoMgr

#endif // LIBREPOMGR_CLIENT_SESSION_H
