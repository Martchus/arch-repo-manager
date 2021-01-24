#include "../serversetup.h"
#include "../webapi/server.h"
#include "../webclient/session.h"

#include "../../libpkg/data/config.h"

#include "resources/config.h"

#include <c++utilities/conversion/stringbuilder.h>
#include <c++utilities/io/misc.h>
#include <c++utilities/tests/testutils.h>

#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>

#include <iostream>
#include <random>
#include <string>
#include <vector>

using namespace std;
using namespace CPPUNIT_NS;
using namespace CppUtilities;

using namespace LibRepoMgr;
using namespace LibRepoMgr::WebAPI;

class WebAPITests : public TestFixture {
    CPPUNIT_TEST_SUITE(WebAPITests);
    CPPUNIT_TEST(testBasicNetworking);
    CPPUNIT_TEST_SUITE_END();

public:
    WebAPITests();
    void setUp() override;
    void tearDown() override;

    void testRoutes(const std::list<std::pair<string, WebClient::Session::Handler>> &routes);
    void testBasicNetworking();

private:
    ServiceSetup m_setup;
    boost::beast::error_code m_lastError;
    string m_body;
};

CPPUNIT_TEST_SUITE_REGISTRATION(WebAPITests);

unsigned short randomPort()
{
    random_device dev;
    default_random_engine engine(dev());
    uniform_int_distribution<unsigned short> distri(5000, 25000);
    return distri(engine);
}

WebAPITests::WebAPITests()
{
}

void WebAPITests::setUp()
{
    applicationInfo.version = APP_VERSION;
    m_setup.webServer.port = randomPort();
}

void WebAPITests::tearDown()
{
}

void WebAPITests::testRoutes(const std::list<std::pair<std::string, WebClient::Session::Handler>> &routes)
{
    // get first route
    auto currentRoute = routes.begin();
    if (currentRoute == routes.end()) {
        return;
    }

    // start server
    auto server = std::make_shared<Server>(m_setup);
    server->run();
    cout << "Test server running under http://" << m_setup.webServer.address << ':' << m_setup.webServer.port << endl;

    // define function to stop server
    const auto stopServer = [&] {
        if (server->m_socket.is_open()) {
            server->m_socket.cancel();
        }
        if (server->m_acceptor.is_open()) {
            server->m_acceptor.cancel();
        }
        m_setup.webServer.ioContext.stop();
    };

    // define function to request the next route to test
    WebClient::Session::Handler handleResponse;
    const auto testNextRoute = [&] {
        std::make_shared<WebClient::Session>(m_setup.webServer.ioContext, handleResponse)
            ->run(m_setup.webServer.address.to_string().data(), numberToString(m_setup.webServer.port).data(), boost::beast::http::verb::get,
                currentRoute->first.data(), 11);
    };

    // define function to respond
    handleResponse = [&](WebClient::Session &session, const WebClient::HttpClientError &error) {
        currentRoute->second(session, error);
        if (++currentRoute == routes.end()) {
            boost::asio::post(server->m_socket.get_executor(), stopServer);
            return;
        }
        testNextRoute();
    };

    // start to actually run the tests
    testNextRoute();
    m_setup.webServer.ioContext.run();
}

void WebAPITests::testBasicNetworking()
{
    testRoutes({
        { "/",
            [](const WebClient::Session &session, const WebClient::HttpClientError &error) {
                const auto &response = get<Response>(session.response);
                CPPUNIT_ASSERT(!error);
                CPPUNIT_ASSERT(!response.body().empty());
                cout << "index: " << response.body() << endl;
            } },
        { "/foo",
            [](const WebClient::Session &session, const WebClient::HttpClientError &error) {
                const auto &response = get<Response>(session.response);
                CPPUNIT_ASSERT(!error);
                CPPUNIT_ASSERT_EQUAL("text/plain"s, response[boost::beast::http::field::content_type].to_string());
                CPPUNIT_ASSERT_EQUAL("The resource 'route \"GET /foo\"' was not found."s, response.body());
            } },
        { "/api/v0/version",
            [](const WebClient::Session &session, const WebClient::HttpClientError &error) {
                const auto &response = get<Response>(session.response);
                CPPUNIT_ASSERT(!error);
                CPPUNIT_ASSERT_EQUAL("text/plain"s, response[boost::beast::http::field::content_type].to_string());
                CPPUNIT_ASSERT_EQUAL(string(APP_VERSION), response.body());
            } },
        { "/api/v0/status",
            [](const WebClient::Session &session, const WebClient::HttpClientError &error) {
                const auto &response = get<Response>(session.response);
                CPPUNIT_ASSERT(!error);
                CPPUNIT_ASSERT(!response.body().empty());
                CPPUNIT_ASSERT_EQUAL("application/json"s, response[boost::beast::http::field::content_type].to_string());
                cout << "status: " << response.body() << endl;
            } },
    });
}
