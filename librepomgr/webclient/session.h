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
using MultiResponse = std::variant<Response, FileResponse>;
using Request = boost::beast::http::request<boost::beast::http::empty_body>;
struct LIBREPOMGR_EXPORT SessionData {
    std::shared_ptr<void> session;
    Request &request;
    MultiResponse &response;
    std::string &destinationFilePath;
};

class LIBREPOMGR_EXPORT Session : public std::enable_shared_from_this<Session> {
public:
    using Handler = std::function<void(Session &, const HttpClientError &error)>;

    template <typename ResponseType = Response> explicit Session(boost::asio::io_context &ioContext, const Handler &handler = Handler());

    void run(const char *host, const char *port, boost::beast::http::verb verb, const char *target, unsigned int version = 11);

private:
    void resolved(boost::beast::error_code ec, boost::asio::ip::tcp::resolver::results_type results);
    void connected(boost::beast::error_code ec);
    void requested(boost::beast::error_code ec, std::size_t bytesTransferred);
    void received(boost::beast::error_code ec, std::size_t bytesTransferred);

public:
    Request request;
    MultiResponse response;
    std::string destinationFilePath;

private:
    boost::asio::ip::tcp::resolver m_resolver;
    boost::asio::ip::tcp::socket m_socket;
    boost::beast::flat_buffer m_buffer;
    Handler m_handler;
};

template <typename ResponseType>
inline Session::Session(boost::asio::io_context &ioContext, const Handler &handler)
    : response(ResponseType{})
    , m_resolver(ioContext)
    , m_socket(ioContext)
    , m_handler(handler)
{
}

class LIBREPOMGR_EXPORT SslSession : public std::enable_shared_from_this<SslSession> {
public:
    using Handler = std::function<void(SslSession &, const HttpClientError &error)>;

    template <typename ResponseType = Response>
    explicit SslSession(boost::asio::io_context &ioContext, boost::asio::ssl::context &sslContext, const Handler &handler = Handler());
    template <typename ResponseType = Response>
    explicit SslSession(boost::asio::io_context &ioContext, boost::asio::ssl::context &sslContext, Handler &&handler);

    void run(const char *host, const char *port, boost::beast::http::verb verb, const char *target, unsigned int version = 11);

private:
    void resolved(boost::beast::error_code ec, boost::asio::ip::tcp::resolver::results_type results);
    void connected(boost::beast::error_code ec);
    void handshakeDone(boost::beast::error_code ec);
    void requested(boost::beast::error_code ec, std::size_t bytesTransferred);
    void received(boost::beast::error_code ec, std::size_t bytesTransferred);
    void closed(boost::beast::error_code ec);

public:
    Request request;
    MultiResponse response;
    std::string destinationFilePath;

private:
    boost::asio::ip::tcp::resolver m_resolver;
    boost::asio::ssl::stream<boost::asio::ip::tcp::socket> m_stream;
    boost::beast::flat_buffer m_buffer;
    Handler m_handler;
};

template <typename ResponseType>
inline SslSession::SslSession(boost::asio::io_context &ioContext, boost::asio::ssl::context &sslContext, const Handler &handler)
    : response(ResponseType{})
    , m_resolver(ioContext)
    , m_stream(ioContext, sslContext)
    , m_handler(handler)
{
}

template <typename ResponseType>
inline SslSession::SslSession(boost::asio::io_context &ioContext, boost::asio::ssl::context &sslContext, Handler &&handler)
    : response(ResponseType{})
    , m_resolver(ioContext)
    , m_stream(ioContext, sslContext)
    , m_handler(handler)
{
}

LIBREPOMGR_EXPORT std::variant<std::string, std::shared_ptr<Session>, std::shared_ptr<SslSession>> runSessionFromUrl(
    boost::asio::io_context &ioContext, boost::asio::ssl::context &sslContext, std::string_view url,
    std::function<void(SessionData data, const HttpClientError &error)> &&handler, std::string &&destinationPath = std::string(),
    std::string_view userName = std::string_view(), std::string_view password = std::string_view(),
    boost::beast::http::verb verb = boost::beast::http::verb::get);

} // namespace WebClient
} // namespace LibRepoMgr

#endif // LIBREPOMGR_CLIENT_SESSION_H
