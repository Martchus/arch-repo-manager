#ifndef LIBREPOMGR_SERVER_H
#define LIBREPOMGR_SERVER_H

#include "./routeid.h"

#include "../global.h"

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include <boost/asio/bind_executor.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/strand.hpp>

class WebAPITests;

namespace LibRepoMgr {

struct ServiceSetup;

namespace WebAPI {

class LIBREPOMGR_EXPORT Server : public std::enable_shared_from_this<Server> {
    friend WebAPITests;

public:
    Server(ServiceSetup &setup);

    static void serve(ServiceSetup &setup);
    static void stop();
    static const Router &router();
    static boost::asio::io_context *context();

    void run();
    void accept();
    void handleAccepted(boost::system::error_code ec, boost::asio::ip::tcp::socket);

private:
    boost::asio::ip::tcp::acceptor m_acceptor;
    ServiceSetup &m_setup;
    static std::shared_ptr<Server> s_instance;
    static const Router s_router;
};

inline const Router &Server::router()
{
    return s_router;
}

} // namespace WebAPI
} // namespace LibRepoMgr

#endif // LIBREPOMGR_SERVER_H
