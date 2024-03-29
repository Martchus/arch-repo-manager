#include "./server.h"
#include "./routes.h"
#include "./session.h"

#include "../serversetup.h"

#include <c++utilities/conversion/stringbuilder.h>
#include <c++utilities/io/ansiescapecodes.h>

#include <boost/asio/signal_set.hpp>

#ifdef USE_LIBSYSTEMD
#include <systemd/sd-daemon.h>
#endif

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
    { { http::verb::get, "/api/v0/build-action/logfile" }, Route{&Routes::getBuildActionLogFile, UserPermissions::ReadBuildActionsDetails} },
    { { http::verb::get, "/api/v0/build-action/artefact" }, Route{&Routes::getBuildActionArtefact, UserPermissions::DownloadArtefacts} },
    { { http::verb::post, "/api/v0/build-action" }, Route{&Routes::postBuildAction, UserPermissions::ModifyBuildActions | UserPermissions::AccessSecrets} },
    { { http::verb::post, "/api/v0/build-action/clone" }, Route{&Routes::postCloneBuildActions, UserPermissions::ModifyBuildActions | UserPermissions::AccessSecrets} },
    { { http::verb::post, "/api/v0/build-action/start" }, Route{&Routes::postStartBuildActions, UserPermissions::ModifyBuildActions | UserPermissions::AccessSecrets} },
    { { http::verb::post, "/api/v0/build-action/stop" }, Route{&Routes::postStopBuildActions, UserPermissions::ModifyBuildActions} },
    { { http::verb::post, "/api/v0/quit" }, Route{&Routes::postQuit, UserPermissions::PerformAdminActions} },
};
// clang-format on

Server::Server(ServiceSetup &setup)
    : m_acceptor(setup.webServer.ioContext)
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

    // signal systemd that the service is ready
#ifdef USE_LIBSYSTEMD
    sd_notify(0, argsToString("READY=1\nSTATUS=Listening on http://", setup.webServer.address.to_string(), ':', setup.webServer.port).data());
#endif

    // run the IO service on the requested number of threads
    const auto additionalThreads = ThreadPool("Web server", setup.webServer.ioContext, setup.webServer.threadCount - 1);
    setup.webServer.ioContext.run();

    // signal systemd that the service is stopping
#ifdef USE_LIBSYSTEMD
    sd_notify(0, "STOPPING=1");
#endif
}

void Server::stop()
{
    if (!s_instance) {
        return;
    }
    boost::asio::post(s_instance->m_setup.webServer.ioContext.get_executor(), [] {
        if (!s_instance) {
            return;
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
    m_acceptor.async_accept(
        boost::asio::make_strand(m_setup.webServer.ioContext), boost::beast::bind_front_handler(&Server::handleAccepted, shared_from_this()));
}

void Server::handleAccepted(boost::system::error_code ec, boost::asio::ip::tcp::socket socket)
{
    if (ec) {
        cerr << argsToString(
            formattedPhraseString(Phrases::WarningMessage), "Failed to accept new connection: ", ec.message(), formattedPhraseString(Phrases::End));
    } else {
        // create session and run it
        std::make_shared<Session>(std::move(socket), m_setup)->receive();
    }
    // accept next connection
    accept();
}

} // namespace WebAPI
} // namespace LibRepoMgr
