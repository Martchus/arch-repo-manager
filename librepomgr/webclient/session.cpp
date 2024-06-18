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
#include <limits>

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

void Session::setChunkHandler(ChunkHandler &&handler)
{
    m_chunkProcessing = std::make_unique<ChunkProcessing>();
    m_chunkProcessing->onChunkHeader
        = std::bind(&Session::onChunkHeader, shared_from_this(), std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
    m_chunkProcessing->onChunkBody
        = std::bind(&Session::onChunkBody, shared_from_this(), std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
    m_chunkProcessing->handler = std::move(handler);
}

bool LibRepoMgr::WebClient::Session::openDestinationFile()
{
    auto &fileResponse = response.emplace<FileResponse>();
    boost::beast::error_code errorCode;
    fileResponse.body_limit(m_bodyLimit);
    fileResponse.get().body().open(destinationFilePath.data(), file_mode::write, errorCode);
    if (errorCode != boost::beast::errc::success) {
        m_handler(*this, HttpClientError("opening output file", errorCode));
        m_handler = decltype(m_handler)();
        return false;
    }
    return true;
}

bool Session::closeDestinationFile(bool skipHandler)
{
    if (!std::holds_alternative<FileResponse>(response)) {
        return true;
    }
    auto &file = std::get<FileResponse>(response).get().body().file();
    if (!file.is_open()) {
        return true;
    }
    auto ec = boost::system::error_code();
    file.close(ec);
    if (ec == boost::beast::errc::success) {
        return true;
    }
    if (!skipHandler) {
        m_handler(*this, HttpClientError("closing output file", ec));
        m_handler = decltype(m_handler)();
    }
    return false;
}

void Session::run(
    const char *host, const char *port, http::verb verb, const char *target, std::optional<std::uint64_t> bodyLimit, unsigned int version)
{
    // set SNI Hostname (many hosts need this to handshake successfully)
    auto *const sslStream = std::get_if<SslStream>(&m_stream);
    if (sslStream
        && !SSL_ctrl(sslStream->native_handle(), SSL_CTRL_SET_TLSEXT_HOSTNAME, TLSEXT_NAMETYPE_host_name,
            reinterpret_cast<void *>(const_cast<char *>(host)))) {
        invokeHandler(HttpClientError(
            "setting SNI hostname", boost::beast::error_code{ static_cast<int>(::ERR_get_error()), boost::asio::error::get_ssl_category() }));
        return;
    }

    // set up an HTTP request message
    request.version(version);
    request.target(target);
    request.set(http::field::host, host);
    request.set(http::field::user_agent, APP_NAME " " APP_VERSION);
    method = verb;
    m_bodyLimit = bodyLimit.value_or(500 * 1024 * 1024);

    // setup a file response
    if (!destinationFilePath.empty()) {
        if (!m_headHandler && !openDestinationFile()) {
            return;
        }
    } else if (m_chunkProcessing) {
        auto &emptyResponse = response.emplace<StringResponse>();
        emptyResponse.on_chunk_header(m_chunkProcessing->onChunkHeader);
        emptyResponse.on_chunk_body(m_chunkProcessing->onChunkBody);
    }

    // look up the domain name
    m_resolver.async_resolve(host, port,
        boost::asio::ip::tcp::resolver::canonical_name | boost::asio::ip::tcp::resolver::passive | boost::asio::ip::tcp::resolver::all_matching,
        std::bind(&Session::resolved, shared_from_this(), std::placeholders::_1, std::placeholders::_2));
}

inline Session::RawSocket &Session::socket()
{
    auto *socket = std::get_if<RawSocket>(&m_stream);
    if (!socket) {
        socket = &std::get<SslStream>(m_stream).next_layer();
    }
    return *socket;
}

void Session::resolved(boost::beast::error_code ec, ip::tcp::resolver::results_type results)
{
    if (ec) {
        invokeHandler(HttpClientError("resolving", ec));
        return;
    }

    // make the connection on the IP address we get from a lookup
    boost::asio::async_connect(socket(), results.begin(), results.end(), std::bind(&Session::connected, shared_from_this(), std::placeholders::_1));
}

void Session::connected(boost::beast::error_code ec)
{
    if (ec) {
        invokeHandler(HttpClientError("connecting", ec));
        return;
    }

    if (auto *const sslStream = std::get_if<SslStream>(&m_stream)) {
        // perform the SSL handshake
        sslStream->async_handshake(ssl::stream_base::client, std::bind(&Session::handshakeDone, shared_from_this(), std::placeholders::_1));
    } else {
        sendRequest();
    }
}

void Session::handshakeDone(boost::beast::error_code ec)
{
    if (ec) {
        invokeHandler(HttpClientError("SSL handshake", ec));
        return;
    }
    sendRequest();
}

void Session::sendRequest()
{
    // send the HTTP request to the remote host
    if (m_headHandler) {
        request.method(boost::beast::http::verb::head);
        std::visit(
            [this](auto &&stream) {
                boost::beast::http::async_write(
                    stream, request, std::bind(&Session::headRequested, shared_from_this(), std::placeholders::_1, std::placeholders::_2));
            },
            m_stream);
        return;
    }
    request.method(method);
    std::visit(
        [this](auto &&stream) {
            boost::beast::http::async_write(
                stream, request, std::bind(&Session::requested, shared_from_this(), std::placeholders::_1, std::placeholders::_2));
        },
        m_stream);
}

void Session::headRequested(boost::beast::error_code ec, std::size_t bytesTransferred)
{
    boost::ignore_unused(bytesTransferred);
    if (ec) {
        invokeHandler(HttpClientError("sending HEAD request", ec));
        return;
    }

    // receive the HTTP response
    headResponse.skip(true);
    std::visit(
        [this](auto &stream) {
            http::async_read(
                stream, m_buffer, headResponse, std::bind(&Session::headReceived, shared_from_this(), std::placeholders::_1, std::placeholders::_2));
        },
        m_stream);
}

void Session::requested(boost::beast::error_code ec, std::size_t bytesTransferred)
{
    boost::ignore_unused(bytesTransferred);
    if (ec) {
        invokeHandler(HttpClientError("sending request", ec));
        return;
    }

    // receive the HTTP response
    std::visit(
        [this](auto &stream, auto &&response) {
            if constexpr (std::is_same_v<std::decay_t<decltype(response)>, StringResponse>) {
                http::async_read_header(
                    stream, m_buffer, response, std::bind(&Session::chunkReceived, shared_from_this(), std::placeholders::_1, std::placeholders::_2));
            } else {
                http::async_read(
                    stream, m_buffer, response, std::bind(&Session::received, shared_from_this(), std::placeholders::_1, std::placeholders::_2));
            }
        },
        m_stream, response);
}

void Session::onChunkHeader(std::uint64_t chunkSize, boost::beast::string_view extensions, boost::beast::error_code &ec)
{
    // parse the chunk extensions so we can access them easily
    m_chunkProcessing->chunkExtensions.parse(extensions, ec);
    if (ec) {
        return;
    }

    if (chunkSize > std::numeric_limits<std::size_t>::max()) {
        ec = boost::beast::http::error::body_limit;
        return;
    }

    // make sure we have enough storage, and reset the container for the upcoming chunk
    m_chunkProcessing->currentChunk.reserve(static_cast<std::size_t>(chunkSize));
    m_chunkProcessing->currentChunk.clear();
}

std::size_t Session::onChunkBody(std::uint64_t bytesLeftInThisChunk, boost::beast::string_view chunkBodyData, boost::beast::error_code &ec)
{
    // set the error so that the call to `read` returns if this is the last piece of the chunk body and we can process the chunk
    if (bytesLeftInThisChunk == chunkBodyData.size()) {
        ec = boost::beast::http::error::end_of_chunk;
    }

    // append this piece to our container
    m_chunkProcessing->currentChunk.append(chunkBodyData.data(), chunkBodyData.size());

    return chunkBodyData.size();
    // note: The return value informs the parser of how much of the body we
    //       consumed. We will indicate that we consumed everything passed in.
}

void Session::chunkReceived(boost::beast::error_code ec, std::size_t bytesTransferred)
{
    if (ec == boost::beast::http::error::end_of_chunk) {
        m_chunkProcessing->handler(m_chunkProcessing->chunkExtensions, m_chunkProcessing->currentChunk);
    } else if (ec) {
        invokeHandler(HttpClientError("receiving chunk response", ec));
        return;
    }
    if (!continueReadingChunks()) {
        received(ec, bytesTransferred);
    }
}

bool Session::continueReadingChunks()
{
    auto &parser = std::get<StringResponse>(response);
    if (parser.is_done()) {
        return false;
    }
    std::visit(
        [this, &parser](auto &stream) {
            boost::beast::http::async_read(
                stream, m_buffer, parser, std::bind(&Session::chunkReceived, shared_from_this(), std::placeholders::_1, std::placeholders::_2));
        },
        m_stream);
    return true;
}

void Session::headReceived(boost::beast::error_code ec, std::size_t bytesTransferred)
{
    boost::ignore_unused(bytesTransferred);
    if (ec) {
        invokeHandler(HttpClientError("receiving HEAD response", ec));
        return;
    }
    m_headHandler(*this);
    m_headHandler = HeadHandler();
    if (!skip && (destinationFilePath.empty() || openDestinationFile())) {
        sendRequest();
    } else {
        closeGracefully();
    }
}

void Session::received(boost::beast::error_code ec, std::size_t bytesTransferred)
{
    boost::ignore_unused(bytesTransferred);
    if (ec) {
        invokeHandler(HttpClientError("receiving response", ec));
        return;
    }
    closeGracefully();
}

void Session::closeGracefully()
{
    auto ec = boost::beast::error_code();
    if (auto *const sslStream = std::get_if<SslStream>(&m_stream)) {
        // perform the SSL handshake
        sslStream->async_shutdown(std::bind(&Session::closed, shared_from_this(), std::placeholders::_1));
    } else if (auto *const socket = std::get_if<RawSocket>(&m_stream)) {
        socket->shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
        invokeHandler(ec && ec != boost::beast::errc::not_connected ? HttpClientError("closing connection", ec) : HttpClientError());
    }
}

void Session::closed(boost::beast::error_code ec)
{
    // rationale regarding boost::asio::error::eof: http://stackoverflow.com/questions/25587403/boost-asio-ssl-async-shutdown-always-finishes-with-an-error
    invokeHandler(ec && ec != boost::asio::error::eof ? HttpClientError("closing connection", ec) : HttpClientError());
}

void Session::invokeHandler(const HttpClientError &error)
{
    closeDestinationFile(error);
    if (m_handler) {
        m_handler(*this, error);
        m_handler = decltype(m_handler)();
    }
}

std::variant<std::string, std::shared_ptr<Session>> runSessionFromUrl(boost::asio::io_context &ioContext, boost::asio::ssl::context &sslContext,
    std::string_view url, Session::Handler &&handler, std::string &&destinationPath, std::string_view userName, std::string_view password,
    boost::beast::http::verb verb, std::optional<std::uint64_t> bodyLimit, Session::ChunkHandler &&chunkHandler)
{
    return runSessionFromUrl(ioContext, sslContext, url, std::move(handler), Session::HeadHandler(), std::move(destinationPath), userName, password,
        verb, bodyLimit, std::move(chunkHandler));
}

std::variant<std::string, std::shared_ptr<Session>> runSessionFromUrl(boost::asio::io_context &ioContext, boost::asio::ssl::context &sslContext,
    std::string_view url, Session::Handler &&handler, Session::HeadHandler &&headHandler, std::string &&destinationPath, std::string_view userName,
    std::string_view password, boost::beast::http::verb verb, std::optional<std::uint64_t> bodyLimit, Session::ChunkHandler &&chunkHandler)
{
    std::string host, port, target;
    auto ssl = false;

    if (startsWith(url, "http:")) {
        url = url.substr(5);
    } else if (startsWith(url, "https:")) {
        url = url.substr(6);
        ssl = true;
    } else {
        return std::string("unsupported protocol");
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

    auto session = ssl ? std::make_shared<Session>(ioContext, sslContext, std::move(handler), std::move(headHandler))
                       : std::make_shared<Session>(ioContext, std::move(handler), std::move(headHandler));
    if (!userName.empty()) {
        const auto authInfo = userName % ":" + password;
        session->request.set(boost::beast::http::field::authorization,
            "Basic " + encodeBase64(reinterpret_cast<const std::uint8_t *>(authInfo.data()), static_cast<std::uint32_t>(authInfo.size())));
    }
    session->destinationFilePath = std::move(destinationPath);
    if (chunkHandler) {
        session->setChunkHandler(std::move(chunkHandler));
    }
    session->run(host.data(), port.data(), verb, target.data(), bodyLimit);
    return std::variant<std::string, std::shared_ptr<Session>>(std::move(session));
}

} // namespace WebClient
} // namespace LibRepoMgr
