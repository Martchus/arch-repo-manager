#include "./session.h"

#include "./params.h"
#include "./render.h"
#include "./routes.h"
#include "./server.h"

#include "../serversetup.h"

#include <passwordfile/io/passwordfile.h>

#include <c++utilities/conversion/stringbuilder.h>
#include <c++utilities/io/ansiescapecodes.h>
#include <c++utilities/io/misc.h>

#include <iostream>

using namespace std;
using namespace boost::asio;
using namespace boost::beast;
using namespace CppUtilities;
using namespace CppUtilities::EscapeCodes;

namespace LibRepoMgr {
namespace WebAPI {

Session::Session(boost::asio::ip::tcp::socket &&socket, ServiceSetup &setup)
    : m_socket(std::move(socket))
    , m_strand(m_socket.get_executor())
    , m_setup(setup)
{
}

Session::~Session()
{
}

void Session::receive()
{
    m_parser = make_unique<RequestParser>();
    m_parser->header_limit(0x10000);
    boost::beast::http::async_read(m_socket, m_buffer, *m_parser,
        boost::asio::bind_executor(m_strand, std::bind(&Session::received, shared_from_this(), std::placeholders::_1, std::placeholders::_2)));
}

void Session::received(boost::system::error_code ec, size_t bytesTransferred)
{
    boost::ignore_unused(bytesTransferred);

    // this means they closed the connection
    if (ec == http::error::end_of_stream) {
        return close();
    }

    if (ec) {
        cerr << Phrases::WarningMessage << "Failed to read request:" << Phrases::End << "    " << ec.message() << endl;
        return close();
    }

    // parse request
    auto &request = m_parser->get();
    auto params = Params{ m_setup, *this };
    const auto &router = Server::router();
    const auto path = params.target.path;

    // allow overriding method via query parameter (for easier debugging)
    auto method = request.method();
    if (const auto methodStr = params.target.value("method"); !methodStr.empty()) {
        if (methodStr == "get") {
            method = boost::beast::http::verb::get;
        } else if (methodStr == "post") {
            method = boost::beast::http::verb::post;
        } else if (methodStr == "put") {
            method = boost::beast::http::verb::put;
        } else if (methodStr == "delete") {
            method = boost::beast::http::verb::delete_;
        }
    }

    // find route's controller and invoke it
    if (const auto routing(router.find(RouteId{ method, std::string(path) })); routing != router.cend()) {
        const Route &route = routing->second;
        const auto requiredPermissions = route.permissions;
        if (requiredPermissions != UserPermissions::None && requiredPermissions != UserPermissions::DefaultPermissions) {
            const auto authInfo = request.find(boost::beast::http::field::authorization);
            if (authInfo == request.end()) {
                respond(Render::makeAuthRequired(request));
                return;
            }
            const auto userAuth = m_setup.auth.authenticate(std::string_view(authInfo->value().data(), authInfo->value().size()));
            using PermissionFlags = std::underlying_type_t<UserPermissions>;
            if (static_cast<PermissionFlags>(userAuth.permissions) & static_cast<PermissionFlags>(UserPermissions::TryAgain)) {
                // send the 401 response again if credentials are 'try again' to show the password prompt for the XMLHttpRequest again
                // note: This is kind of a hack. Maybe there's a better solution to make XMLHttpRequest forget wrongly entered credentials
                //       and instead show the login prompt again?
                respond(Render::makeAuthRequired(request));
                return;
            }
            if ((static_cast<PermissionFlags>(requiredPermissions) & static_cast<PermissionFlags>(userAuth.permissions))
                != static_cast<PermissionFlags>(requiredPermissions)) {
                respond(Render::makeForbidden(request));
                return;
            }
            // prepare file with secrets for user
            if (!userAuth.name.empty() && !userAuth.password.empty()
                && (static_cast<PermissionFlags>(requiredPermissions) & static_cast<PermissionFlags>(UserPermissions::AccessSecrets))) {
                try {
                    if (m_secrets) {
                        m_secrets->clear();
                    } else {
                        m_secrets = std::make_unique<Io::PasswordFile>();
                    }
                    m_secrets->setPath(argsToString("secrets/"sv, userAuth.name));
                    m_secrets->setPassword(userAuth.password.data(), userAuth.password.size());
                } catch (const std::ios_base::failure &e) {
                    cerr << Phrases::WarningMessage << "Failed to close password file \"" << m_secrets->path()
                         << "\" (before preparing new one): " << e.what() << Phrases::End;
                }
            }
        }

        // invoke the route's handler
        // note: The error handling is in vain if an exception in a deferred handler is thrown.
        try {
            route.handler(move(params),
                std::bind(
                    static_cast<void (Session::*)(std::shared_ptr<Response> &&)>(&Session::respond), shared_from_this(), std::placeholders::_1));
        } catch (const BadRequest &badRequest) {
            respond(Render::makeBadRequest(request, badRequest.what()));
        }

        // discard password; secrets are expected to be read on the immediate call of the route
        try {
            if (m_secrets) {
                m_secrets->clearPassword();
                m_secrets->close();
            }
        } catch (const std::ios_base::failure &e) {
            cerr << Phrases::WarningMessage << "Failed to close password file \"" << m_secrets->path() << "\": " << e.what() << Phrases::End;
        }
        return;
    }

    // handle requests to static files (intended for development only; use NGINX in production)
    if (!m_setup.webServer.staticFilesPath.empty() && (path.find("../") == string::npos || path.find("..\\") == string::npos)) {
        const auto filePath = argsToString(m_setup.webServer.staticFilesPath, params.target.path);
        respond(filePath.data(), determineMimeType(params.target.path).data(), boost::beast::string_view(), params.target.path);
        return;
    }

    // handle requests for non-existent routes
#ifdef CPP_UTILITIES_DEBUG_BUILD
    cerr << Phrases::Info << "Invalid request: " << TextAttribute::Reset << request.method_string() << ' ' << request.target() << '\n';
    if (!request.body().empty()) {
        cerr << "body: " << request.body();
    }
    cerr << flush;
#endif
    respond(Render::makeNotFound(request, "route \"" % request.method_string().to_string() % ' ' % request.target().to_string() + '\"'));
}

void Session::respond(std::shared_ptr<Response> &&response)
{
    // write the response
    http::async_write(m_socket, *response,
        boost::asio::bind_executor(
            m_strand, std::bind(&Session::responded, shared_from_this(), std::placeholders::_1, std::placeholders::_2, response->need_eof())));

    // keep message alive as long as the session exists
    m_res = move(response);
}

void Session::respond(
    const char *localFilePath, boost::beast::string_view mimeType, boost::beast::string_view contentDisposition, std::string_view urlPath)
{
    // make response with file body
    auto ec = boost::beast::error_code{};
    auto response = Render::makeFile(m_parser->get(), localFilePath, mimeType, contentDisposition, ec);
    if (ec.failed()) {
        respond(Render::makeNotFound(m_parser->get(), urlPath));
        return;
    }

    // write the response
    http::async_write(m_socket, *response,
        boost::asio::bind_executor(
            m_strand, std::bind(&Session::responded, shared_from_this(), std::placeholders::_1, std::placeholders::_2, response->need_eof())));

    // keep message alive as long as the session exists
    m_res = response;
}

void Session::responded(boost::system::error_code ec, std::size_t bytesTransferred, bool shouldClose)
{
    boost::ignore_unused(bytesTransferred);
    if (ec) {
        cerr << Phrases::WarningMessage << "Failed to write response:" << Phrases::End << "    " << ec.message() << endl;
    }

    if (shouldClose) {
        // this means we should close the connection, usually because
        // the response indicated the "Connection: close" semantic.
        return close();
    }

    // we're done with the response so delete it
    m_res = nullptr;

    // read another request
    receive();
}

boost::beast::string_view Session::determineMimeType(std::string_view path, boost::beast::string_view fallback)
{
    if (path.ends_with(".html")) {
        return "text/html";
    } else if (path.ends_with(".json")) {
        return "application/json";
    } else if (path.ends_with(".js")) {
        return "text/javascript";
    } else if (path.ends_with(".css")) {
        return "text/css";
    } else if (path.ends_with(".svg")) {
        return "image/svg+xml";
    } else if (path.ends_with(".txt") || path.ends_with(".log") || path.ends_with("PKGBUILD")) {
        return "text/plain";
    } else if (path.ends_with(".md")) {
        return "text/plain"; // using text/markdown leads to download prompt in Firefox
    } else {
        return fallback;
    }
}

void Session::close()
{
    // send a TCP shutdown
    boost::system::error_code ec;
    m_socket.shutdown(ip::tcp::socket::shutdown_send, ec);
    // at this point the connection is closed gracefully
}

} // namespace WebAPI
} // namespace LibRepoMgr
