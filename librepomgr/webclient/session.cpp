#include "./session.h"

#include "../serversetup.h"

#include "resources/config.h"

#include <c++utilities/conversion/stringbuilder.h>
#include <c++utilities/conversion/stringconversion.h>
#include <c++utilities/io/ansiescapecodes.h>

#include <boost/asio/connect.hpp>
#include <boost/asio/error.hpp>
#include <boost/system/error_code.hpp>

#include <iostream>

using namespace std;
using namespace boost::asio;
using namespace boost::beast;
using namespace CppUtilities;
using namespace CppUtilities::EscapeCodes;

namespace LibRepoMgr {
namespace WebClient {

HttpClientError::HttpClientError(const char *context, boost::beast::error_code errorCode)
    : std::runtime_error(argsToString(context, ':', ' ', errorCode.message()))
    , context(context)
    , errorCode(errorCode)
{
}

void Session::run(const char *host, const char *port, http::verb verb, const char *target, unsigned int version)
{
    // set up an HTTP request message
    request.version(version);
    request.method(verb);
    request.target(target);
    request.set(http::field::host, host);
    request.set(http::field::user_agent, APP_NAME " " APP_VERSION);

    // setup a file response
    if (!destinationFilePath.empty()) {
        auto &fileResponse = response.emplace<FileResponse>();
        boost::beast::error_code errorCode;
        fileResponse.body_limit(100 * 1024 * 1024);
        fileResponse.get().body().open(destinationFilePath.data(), file_mode::write, errorCode);
        if (errorCode != boost::beast::errc::success) {
            m_handler(*this, HttpClientError("opening output file", errorCode));
            return;
        }
    }

    // look up the domain name
    m_resolver.async_resolve(host, port,
        boost::asio::ip::tcp::resolver::canonical_name | boost::asio::ip::tcp::resolver::passive | boost::asio::ip::tcp::resolver::all_matching,
        std::bind(&Session::resolved, shared_from_this(), std::placeholders::_1, std::placeholders::_2));
}

void Session::resolved(boost::beast::error_code ec, ip::tcp::resolver::results_type results)
{
    if (ec) {
        m_handler(*this, HttpClientError("resolving", ec));
        return;
    }

    // make the connection on the IP address we get from a lookup
    boost::asio::async_connect(m_socket, results.begin(), results.end(), std::bind(&Session::connected, shared_from_this(), std::placeholders::_1));
}

void Session::connected(boost::beast::error_code ec)
{
    if (ec) {
        m_handler(*this, HttpClientError("connecting", ec));
        return;
    }

    // perform the SSL handshake
    http::async_write(m_socket, request, std::bind(&Session::requested, shared_from_this(), std::placeholders::_1, std::placeholders::_2));
}

void Session::requested(boost::beast::error_code ec, std::size_t bytesTransferred)
{
    boost::ignore_unused(bytesTransferred);
    if (ec) {
        m_handler(*this, HttpClientError("sending request", ec));
        return;
    }

    // receive the HTTP response
    std::visit(
        [this](auto &&response) {
            http::async_read(
                m_socket, m_buffer, response, std::bind(&Session::received, shared_from_this(), std::placeholders::_1, std::placeholders::_2));
        },
        response);
}

void Session::received(boost::beast::error_code ec, std::size_t bytesTransferred)
{
    boost::ignore_unused(bytesTransferred);
    if (ec) {
        m_handler(*this, HttpClientError("receiving response", ec));
        return;
    }

    // close the stream gracefully
    m_socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);

    if (ec && ec != boost::beast::errc::not_connected) {
        m_handler(*this, HttpClientError("closing connection", ec));
        return;
    }
    // if we get here then the connection is closed gracefully
    m_handler(*this, HttpClientError());
}

void SslSession::run(const char *host, const char *port, http::verb verb, const char *target, unsigned int version)
{
    // set SNI Hostname (many hosts need this to handshake successfully)
    if (!SSL_set_tlsext_host_name(m_stream.native_handle(), const_cast<char *>(host))) {
        m_handler(*this,
            HttpClientError(
                "setting SNI hostname", boost::beast::error_code{ static_cast<int>(::ERR_get_error()), boost::asio::error::get_ssl_category() }));
        return;
    }

    // setup an HTTP request message
    request.version(version);
    request.method(verb);
    request.target(target);
    request.set(http::field::host, host);
    request.set(http::field::user_agent, APP_NAME " " APP_VERSION);

    // setup a file response
    if (!destinationFilePath.empty()) {
        auto &fileResponse = response.emplace<FileResponse>();
        boost::beast::error_code errorCode;
        fileResponse.body_limit(100 * 1024 * 1024);
        fileResponse.get().body().open(destinationFilePath.data(), file_mode::write, errorCode);
        if (errorCode != boost::beast::errc::success) {
            m_handler(*this, HttpClientError("opening output file", errorCode));
            return;
        }
    }

    // look up the domain name
    m_resolver.async_resolve(host, port,
        boost::asio::ip::tcp::resolver::canonical_name | boost::asio::ip::tcp::resolver::passive | boost::asio::ip::tcp::resolver::all_matching,
        std::bind(&SslSession::resolved, shared_from_this(), std::placeholders::_1, std::placeholders::_2));
}

void SslSession::resolved(boost::beast::error_code ec, ip::tcp::resolver::results_type results)
{
    if (ec) {
        m_handler(*this, HttpClientError("resolving", ec));
        return;
    }

    // make the connection on the IP address we get from a lookup
    boost::asio::async_connect(
        m_stream.next_layer(), results.begin(), results.end(), std::bind(&SslSession::connected, shared_from_this(), std::placeholders::_1));
}

void SslSession::connected(boost::beast::error_code ec)
{
    if (ec) {
        m_handler(*this, HttpClientError("connecting", ec));
        return;
    }

    // perform the SSL handshake
    m_stream.async_handshake(ssl::stream_base::client, std::bind(&SslSession::handshakeDone, shared_from_this(), std::placeholders::_1));
}

void SslSession::handshakeDone(boost::beast::error_code ec)
{
    if (ec) {
        m_handler(*this, HttpClientError("SSL handshake", ec));
        return;
    }

    // send the HTTP request to the remote host
    boost::beast::http::async_write(
        m_stream, request, std::bind(&SslSession::requested, shared_from_this(), std::placeholders::_1, std::placeholders::_2));
}

void SslSession::requested(boost::beast::error_code ec, std::size_t bytesTransferred)
{
    boost::ignore_unused(bytesTransferred);
    if (ec) {
        m_handler(*this, HttpClientError("sending request", ec));
        return;
    }

    // receive the HTTP response
    std::visit(
        [this](auto &&response) {
            http::async_read(
                m_stream, m_buffer, response, std::bind(&SslSession::received, shared_from_this(), std::placeholders::_1, std::placeholders::_2));
        },
        response);
}

void SslSession::received(boost::beast::error_code ec, std::size_t bytesTransferred)
{
    boost::ignore_unused(bytesTransferred);
    if (ec) {
        m_handler(*this, HttpClientError("receiving response", ec));
        return;
    }

    // close the stream gracefully
    m_stream.async_shutdown(std::bind(&SslSession::closed, shared_from_this(), std::placeholders::_1));
}

void SslSession::closed(boost::beast::error_code ec)
{
    if (ec == boost::asio::error::eof) {
        // rationale: http://stackoverflow.com/questions/25587403/boost-asio-ssl-async-shutdown-always-finishes-with-an-error
        ec = {};
    }
    if (ec) {
        m_handler(*this, HttpClientError("closing connection", ec));
        return;
    }
    // if we get here then the connection is closed gracefully
    m_handler(*this, HttpClientError());
}

template <typename SessionType, typename... ArgType>
std::variant<string, std::shared_ptr<Session>, std::shared_ptr<SslSession>> runSession(const std::string &host, const std::string &port,
    const std::string &target, std::function<void(SessionData, const HttpClientError &)> &&handler, std::string &&destinationPath,
    std::string_view userName, std::string_view password, ArgType &&...args)
{
    auto session = make_shared<SessionType>(args..., [handler{ move(handler) }](auto &session, const HttpClientError &error) mutable {
        handler(SessionData{ session.shared_from_this(), session.request, session.response, session.destinationFilePath }, error);
    });
    if (!userName.empty()) {
        const auto authInfo = userName % ":" + password;
        session->request.set(boost::beast::http::field::authorization,
            "Basic " + encodeBase64(reinterpret_cast<const std::uint8_t *>(authInfo.data()), authInfo.size()));
    }
    session->destinationFilePath = move(destinationPath);
    session->run(host.data(), port.data(), http::verb::get, target.data());
    return move(session);
}

std::variant<string, std::shared_ptr<Session>, std::shared_ptr<SslSession>> runSessionFromUrl(boost::asio::io_context &ioContext,
    boost::asio::ssl::context &sslContext, std::string_view url, std::function<void(SessionData, const HttpClientError &)> &&handler,
    std::string &&destinationPath, std::string_view userName, std::string_view password)
{
    string host, port, target;
    auto ssl = false;

    if (startsWith(url, "http:")) {
        url = url.substr(5);
    } else if (startsWith(url, "https:")) {
        url = url.substr(6);
        ssl = true;
    } else {
        return "db mirror for database has unsupported protocol";
    }

    auto urlParts = splitStringSimple<vector<std::string_view>>(url, "/");
    target.reserve(url.size());
    for (const auto &part : urlParts) {
        if (part.empty()) {
            continue;
        }
        if (host.empty()) {
            host = part;
            continue;
        }
        target += '/';
        target += part;
    }

    if (const auto lastColon = host.find_last_of(':'); lastColon != std::string_view::npos) {
        port = host.substr(lastColon + 1);
        host = host.substr(0, lastColon);
    }
    if (port.empty()) {
        port = ssl ? "443" : "80";
    }

    if (ssl) {
        return runSession<SslSession>(host, port, target, move(handler), move(destinationPath), userName, password, ioContext, sslContext);
    } else {
        return runSession<Session>(host, port, target, move(handler), move(destinationPath), userName, password, ioContext);
    }
}

} // namespace WebClient
} // namespace LibRepoMgr
