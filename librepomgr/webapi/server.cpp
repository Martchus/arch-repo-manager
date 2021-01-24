#include "./server.h"
#include "./routes.h"
#include "./session.h"

#include "../serversetup.h"

#include <c++utilities/conversion/stringbuilder.h>
#include <c++utilities/io/ansiescapecodes.h>

#include <boost/asio/signal_set.hpp>

#include <iostream>
#include <thread>

using namespace std;
using namespace boost::asio;
using namespace boost::beast;
using namespace CppUtilities;
using namespace CppUtilities::EscapeCodes;

namespace LibRepoMgr {
namespace WebAPI {

// clang-format off
const Router Server::s_router = {
    { { http::verb::get, "/" }, Route{&Routes::getRoot} },
    { { http::verb::get, "/api/v0/databases" }, Route{&Routes::getDatabases} },
    { { http::verb::get, "/api/v0/unresolved" }, Route{&Routes::getUnresolved} },
    { { http::verb::get, "/api/v0/packages" }, Route{&Routes::getPackages} },
    { { http::verb::get, "/api/v0/version" }, Route{&Routes::getVersion} },
    { { http::verb::get, "/api/v0/status" }, Route{&Routes::getStatus} },
    { { http::verb::post, "/api/v0/load/packages" }, Route{&Routes::postLoadPackages, UserPermissions::PerformAdminActions} },
    { { http::verb::get, "/api/v0/build-action" }, Route{&Routes::getBuildActions} },
    { { http::verb::delete_, "/api/v0/build-action" }, Route{&Routes::deleteBuildActions, UserPermissions::ModifyBuildActions} },
    { { http::verb::get, "/api/v0/build-action/details" }, Route{&Routes::getBuildActionDetails, UserPermissions::ReadBuildActionsDetails} },
    { { http::verb::get, "/api/v0/build-action/output" }, Route{&Routes::getBuildActionOutput, UserPermissions::ReadBuildActionsDetails} },
    { { http::verb::get, "/api/v0/build-action/logfile" }, Route{&Routes::getBuildActionLogFile, UserPermissions::ReadBuildActionsDetails} },
    { { http::verb::get, "/api/v0/build-action/artefact" }, Route{&Routes::getBuildActionArtefact, UserPermissions::ReadBuildActionsDetails} },
    { { http::verb::post, "/api/v0/build-action" }, Route{&Routes::postBuildAction, UserPermissions::ModifyBuildActions} },
    { { http::verb::post, "/api/v0/build-action/clone" }, Route{&Routes::postCloneBuildActions, UserPermissions::ModifyBuildActions} },
    { { http::verb::post, "/api/v0/build-action/start" }, Route{&Routes::postStartBuildActions, UserPermissions::ModifyBuildActions} },
    { { http::verb::post, "/api/v0/build-action/stop" }, Route{&Routes::postStopBuildActions, UserPermissions::ModifyBuildActions} },
    { { http::verb::post, "/api/v0/dump/cache-file" }, Route{&Routes::postDumpCacheFile, UserPermissions::PerformAdminActions} },
    { { http::verb::post, "/api/v0/quit" }, Route{&Routes::postQuit, UserPermissions::PerformAdminActions} },
};
// clang-format on

Server::Server(ServiceSetup &setup)
    : m_acceptor(setup.webServer.ioContext)
    , m_socket(setup.webServer.ioContext)
    , m_setup(setup)
{
    // open the acceptor
    const auto endpoint = boost::asio::ip::tcp::endpoint{ setup.webServer.address, setup.webServer.port };
    m_acceptor.open(endpoint.protocol());
    // allow to reuse the address immediately (TODO: make this configurable)
    m_acceptor.set_option(socket_base::reuse_address(true));
    m_acceptor.set_option(ip::tcp::acceptor::reuse_address(true));
    // bind to the server address
    m_acceptor.bind(endpoint);
    // start listening for connections
    m_acceptor.listen(boost::asio::socket_base::max_listen_connections);
}

std::shared_ptr<Server> Server::s_instance = nullptr;

void Server::serve(ServiceSetup &setup)
{
    // create and launch listener
    s_instance = std::make_shared<Server>(setup);
    s_instance->run();

    // stop gracefully when receiving SIGINT or SIGTERM
    boost::asio::signal_set signalSet(setup.webServer.ioContext, SIGINT, SIGTERM, SIGQUIT);
    signalSet.async_wait([](const boost::system::error_code &error, int signalNumber) {
        CPP_UTILITIES_UNUSED(signalNumber)
        if (!error) {
            Server::stop();
        }
    });

    // run the IO service on the requested number of threads
    const auto additionalThreads = ThreadPool("Additional web server thread", setup.webServer.ioContext, setup.webServer.threadCount - 1);
    setup.webServer.ioContext.run();
}

void Server::stop()
{
    if (!s_instance) {
        return;
    }
    boost::asio::post(s_instance->m_socket.get_executor(), [] {
        if (!s_instance) {
            return;
        }
        if (s_instance->m_socket.is_open()) {
            s_instance->m_socket.cancel();
        }
        if (s_instance->m_acceptor.is_open()) {
            s_instance->m_acceptor.cancel();
        }
        s_instance->m_setup.webServer.ioContext.stop();
    });
}

void Server::run()
{
    if (!m_acceptor.is_open()) {
        return;
    }
    accept();
}

void Server::accept()
{
    m_acceptor.async_accept(m_socket, bind(&Server::handleAccepted, shared_from_this(), placeholders::_1));
}

void Server::handleAccepted(boost::system::error_code ec)
{
    if (ec) {
        cerr << Phrases::WarningMessage << "Failed to accept new connection: " << ec.message() << Phrases::EndFlush;
    } else {
        // create session and run it
        std::make_shared<Session>(move(m_socket), m_setup)->receive();
    }
    // accept next connection
    accept();
}

} // namespace WebAPI
} // namespace LibRepoMgr
