#ifndef LIBREPOMGR_SESSION_H
#define LIBREPOMGR_SESSION_H

#include "./typedefs.h"

#include "../global.h"

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include <boost/asio/bind_executor.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/strand.hpp>

#include <memory>

namespace Io {
class PasswordFile;
}

namespace LibRepoMgr {

struct ServiceSetup;

namespace WebAPI {

class LIBREPOMGR_EXPORT Session : public std::enable_shared_from_this<Session> {
public:
    Session(boost::asio::ip::tcp::socket &&socket, ServiceSetup &config);
    ~Session();

    void receive();
    void respond(std::shared_ptr<Response> &&response);
    void respond(
        const char *localFilePath, boost::beast::string_view mimeType, boost::beast::string_view contentDisposition, std::string_view urlPath);
    void close();
    const Request &request() const;
    void assignEmptyRequest();
    boost::asio::ip::tcp::socket &socket();
    void received(boost::system::error_code ec, std::size_t bytesTransferred);
    void responded(boost::system::error_code ec, std::size_t bytesTransferred, bool shouldClose);
    static boost::beast::string_view determineMimeType(std::string_view path, boost::beast::string_view fallback = "text/plain");
    std::unique_ptr<Io::PasswordFile> &&secrets();

private:
    boost::asio::ip::tcp::socket m_socket;
    boost::asio::strand<boost::asio::ip::tcp::socket::executor_type> m_strand;
    boost::beast::flat_buffer m_buffer;
    std::unique_ptr<RequestParser> m_parser;
    ServiceSetup &m_setup;
    std::shared_ptr<void> m_res;
    std::unique_ptr<Io::PasswordFile> m_secrets;
};

inline const Request &Session::request() const
{
    return m_parser->get();
}

inline void Session::assignEmptyRequest()
{
    m_parser = std::make_unique<RequestParser>();
}

inline boost::asio::ip::tcp::socket &Session::socket()
{
    return m_socket;
}

inline std::unique_ptr<Io::PasswordFile> &&Session::secrets()
{
    return std::move(m_secrets);
}

} // namespace WebAPI
} // namespace LibRepoMgr

#endif // LIBREPOMGR_SESSION_H
