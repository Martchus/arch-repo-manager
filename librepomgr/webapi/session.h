#ifndef LIBREPOMGR_SESSION_H
#define LIBREPOMGR_SESSION_H

#include "./typedefs.h"

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include <boost/asio/bind_executor.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/strand.hpp>

namespace LibRepoMgr {

struct ServiceSetup;

namespace WebAPI {

class Session : public std::enable_shared_from_this<Session> {
public:
    Session(boost::asio::ip::tcp::socket socket, ServiceSetup &config);

    void receive();
    void respond(std::shared_ptr<Response> &&response);
    void respond(const char *localFilePath, const char *mimeType, std::string_view urlPath);
    void close();
    const Request &request() const;
    boost::asio::ip::tcp::socket &socket();
    void received(boost::system::error_code ec, std::size_t bytesTransferred);
    void responded(boost::system::error_code ec, std::size_t bytesTransferred, bool shouldClose);

private:
    boost::asio::ip::tcp::socket m_socket;
    boost::asio::strand<boost::asio::ip::tcp::socket::executor_type> m_strand;
    boost::beast::flat_buffer m_buffer;
    std::unique_ptr<RequestParser> m_parser;
    ServiceSetup &m_setup;
    std::shared_ptr<void> m_res;
};

inline Session::Session(boost::asio::ip::tcp::socket socket, ServiceSetup &setup)
    : m_socket(std::move(socket))
    , m_strand(m_socket.get_executor())
    , m_setup(setup)
{
}

inline const Request &Session::request() const
{
    return m_parser->get();
}

inline boost::asio::ip::tcp::socket &Session::socket()
{
    return m_socket;
}

} // namespace WebAPI
} // namespace LibRepoMgr

#endif // LIBREPOMGR_SESSION_H
