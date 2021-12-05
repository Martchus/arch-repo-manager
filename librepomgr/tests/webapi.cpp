#include "../buildactions/buildaction.h"
#include "../serversetup.h"
#include "../webapi/params.h"
#include "../webapi/routes.h"
#include "../webapi/server.h"
#include "../webapi/session.h"
#include "../webclient/session.h"

#include "../../libpkg/data/config.h"

#include "resources/config.h"

#include <c++utilities/conversion/stringbuilder.h>
#include <c++utilities/io/misc.h>
#include <c++utilities/tests/testutils.h>

#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>

#include <boost/asio/ip/tcp.hpp>

#include <iostream>
#include <random>
#include <string>
#include <vector>

using namespace std;
using namespace CPPUNIT_NS;
using namespace CppUtilities;
using namespace CppUtilities::Literals;

using namespace LibRepoMgr;
using namespace LibRepoMgr::WebAPI;

class WebAPITests : public TestFixture {
    CPPUNIT_TEST_SUITE(WebAPITests);
    CPPUNIT_TEST(testBasicNetworking);
    CPPUNIT_TEST(testPostingBuildAction);
    CPPUNIT_TEST(testPostingBuildActionsFromTask);
    CPPUNIT_TEST_SUITE_END();

public:
    WebAPITests();
    void setUp() override;
    void tearDown() override;

    void testRoutes(const std::list<std::pair<string, WebClient::Session::Handler>> &routes);
    void testBasicNetworking();
    std::shared_ptr<WebAPI::Response> invokeRouteHandler(
        void (*handler)(const Params &params, ResponseHandler &&handler), std::vector<std::pair<std::string_view, std::string_view>> &&queryParams);
    void testPostingBuildAction();
    void testPostingBuildActionsFromTask();

private:
    std::string m_dbFile;
    ServiceSetup m_setup;
    boost::beast::error_code m_lastError;
    string m_body;
};

CPPUNIT_TEST_SUITE_REGISTRATION(WebAPITests);

static unsigned short randomPort()
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
    m_dbFile = workingCopyPath("test-webapi.db", WorkingCopyMode::Cleanup);
    m_setup.webServer.port = randomPort();
    m_setup.config.initStorage(m_dbFile.data());
}

void WebAPITests::tearDown()
{
}

/*!
 * \brief Runs the Boost.Asio/Beast server and client to simulte accessing the specified \a routes.
 */
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
            boost::asio::post(server->m_acceptor.get_executor(), stopServer);
            return;
        }
        testNextRoute();
    };

    // start to actually run the tests
    testNextRoute();
    m_setup.webServer.ioContext.run();
}

/*!
 * \brief Checks a few basic routes using the Boost.Beast-based HTTP server and client to test whether basic
 *        networking and HTTP processing works.
 */
void WebAPITests::testBasicNetworking()
{
    testRoutes({
        { "/",
            [](const WebClient::Session &session, const WebClient::HttpClientError &error) {
                const auto &response = get<Response>(session.response);
                CPPUNIT_ASSERT(!error);
                CPPUNIT_ASSERT(!response.body().empty());
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
            } },
    });
}

/*!
 * \brief Invokes the specified route \a handler with the specified \a queryParams and returns the response.
 */
std::shared_ptr<Response> WebAPITests::invokeRouteHandler(
    void (*handler)(const Params &, ResponseHandler &&), std::vector<std::pair<std::string_view, std::string_view>> &&queryParams)
{
    auto &ioc = m_setup.webServer.ioContext;
    auto session = std::make_shared<WebAPI::Session>(boost::asio::ip::tcp::socket(ioc), m_setup);
    auto params = WebAPI::Params(m_setup, *session, WebAPI::Url(std::string_view(), std::string_view(), std::move(queryParams)));
    auto response = std::shared_ptr<WebAPI::Response>();
    session->assignEmptyRequest();
    std::invoke(handler, params, [&response](std::shared_ptr<WebAPI::Response> &&r) { response = r; });
    return response;
}

/*!
 * \brief Parses the specified \a json as build action storing results in \a buildAction.
 */
static void parseBuildAction(BuildAction &buildAction, std::string_view json)
{
    const auto doc = ReflectiveRapidJSON::JsonReflector::parseJsonDocFromString(json.data(), json.size());
    if (!doc.IsObject()) {
        CPPUNIT_FAIL("json document is no object");
    }
    auto errors = ReflectiveRapidJSON::JsonDeserializationErrors();
    errors.throwOn = ReflectiveRapidJSON::JsonDeserializationErrors::ThrowOn::All;
    ReflectiveRapidJSON::JsonReflector::pull(buildAction, doc.GetObject(), &errors);
}

/*!
 * \brief Parses the specified \a json as build actions storing results in \a buildActions.
 */
static auto parseBuildActions(std::string_view json)
{
    auto errors = ReflectiveRapidJSON::JsonDeserializationErrors();
    errors.throwOn = ReflectiveRapidJSON::JsonDeserializationErrors::ThrowOn::All;
    return ReflectiveRapidJSON::JsonReflector::fromJson<std::list<LibRepoMgr::BuildAction>>(json.data(), json.size(), &errors);
}

/*!
 * \brief Tests the handler to post a build action.
 * \remarks Only covers a very basic use so far; tasks are handled in the next test function.
 */
void WebAPITests::testPostingBuildAction()
{
    {
        const auto response = invokeRouteHandler(&WebAPI::Routes::postBuildAction, {});
        CPPUNIT_ASSERT_MESSAGE("got response", response);
        CPPUNIT_ASSERT_EQUAL_MESSAGE("response body", "need exactly either one type or one task parameter"s, response->body());
        CPPUNIT_ASSERT_EQUAL_MESSAGE("response ok", boost::beast::http::status::bad_request, response->result());
    }
    {
        const auto response = invokeRouteHandler(&WebAPI::Routes::postBuildAction,
            {
                { "type"sv, "prepare-build"sv },
                { "start-condition"sv, "manually"sv },
            });
        CPPUNIT_ASSERT_MESSAGE("got response", response);

        auto buildAction = BuildAction();
        parseBuildAction(buildAction, response->body());
        CPPUNIT_ASSERT_EQUAL_MESSAGE("expected build action type returned", BuildActionType::PrepareBuild, buildAction.type);

        const auto createdBuildAction = m_setup.building.getBuildAction(buildAction.id);
        CPPUNIT_ASSERT_MESSAGE("build action actually created", createdBuildAction);
        CPPUNIT_ASSERT_EQUAL_MESSAGE("build action not started yet", BuildActionStatus::Created, createdBuildAction->status);
        CPPUNIT_ASSERT_EQUAL_MESSAGE("build action has no result yet", BuildActionResult::None, createdBuildAction->result);
        CPPUNIT_ASSERT_EQUAL_MESSAGE("response ok", boost::beast::http::status::ok, response->result());
    }
}

/*!
 * \brief Tests the handler to post build actions from a pre-defined task.
 */
void WebAPITests::testPostingBuildActionsFromTask()
{
    auto &building = m_setup.building;
    building.presets = decltype(building.presets)::fromJson(readFile(testFilePath("test-config/presets.json")));
    CPPUNIT_ASSERT_MESSAGE("templates parsed from JSON", !building.presets.templates.empty());
    CPPUNIT_ASSERT_MESSAGE("task parsed from JSON", building.presets.tasks.contains("foobarbaz"));
    CPPUNIT_ASSERT_MESSAGE("no build actions present before", building.actions.empty());

    const auto response = invokeRouteHandler(&WebAPI::Routes::postBuildAction,
        {
            { "task"sv, "foobarbaz"sv },
            { "start-condition"sv, "manually"sv },
        });
    CPPUNIT_ASSERT_MESSAGE("got response", response);

    const auto buildActions = parseBuildActions(response->body());
    CPPUNIT_ASSERT_EQUAL_MESSAGE("expected number of build actions created", 5_st, buildActions.size());

    CPPUNIT_ASSERT_EQUAL_MESSAGE("build actions actually present", 5_st, building.actions.size());
    for (const auto &action : building.actions) {
        CPPUNIT_ASSERT_EQUAL_MESSAGE(argsToString("build action ", action->id, " not started yet"), BuildActionStatus::Created, action->status);
        CPPUNIT_ASSERT_EQUAL_MESSAGE(argsToString("build action ", action->id, " has no result yet"), BuildActionResult::None, action->result);
        CPPUNIT_ASSERT_EQUAL_MESSAGE(argsToString("build action ", action->id, " has task name assigned"), "foobarbaz"s, action->taskName);
    }
    CPPUNIT_ASSERT_EQUAL_MESSAGE("foo is 1st action", "foo"s, building.actions[0]->templateName);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("foo has dir assigned", "foo"s, building.actions[0]->directory);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("foo has correct deps", std::vector<BuildAction::IdType>{}, building.actions[0]->startAfter);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("bar-1 is 2nd action", "bar-1"s, building.actions[1]->templateName);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("bar-1 has dir assigned", "bar"s, building.actions[1]->directory);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("bar-1 has correct deps", std::vector<BuildAction::IdType>{ 0 }, building.actions[1]->startAfter);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("bar-2 is 3rd action", "bar-2"s, building.actions[2]->templateName);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("bar-2 has dir assigned", "bar"s, building.actions[2]->directory);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("bar-2 has correct deps", std::vector<BuildAction::IdType>{ 1 }, building.actions[2]->startAfter);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("baz is 4th action", "baz"s, building.actions[3]->templateName);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("baz has dir assigned", "baz"s, building.actions[3]->directory);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("baz has correct deps", std::vector<BuildAction::IdType>{ 0 }, building.actions[3]->startAfter);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("buz is 5th action", "buz"s, building.actions[4]->templateName);
    CPPUNIT_ASSERT_EQUAL_MESSAGE("buz has dir assigned", "buz"s, building.actions[4]->directory);
    CPPUNIT_ASSERT_EQUAL_MESSAGE(
        "buz has correct deps", std::vector<BuildAction::IdType>{ 2 CPP_UTILITIES_PP_COMMA 3 }, building.actions[4]->startAfter);

    CPPUNIT_ASSERT_EQUAL_MESSAGE("response ok", boost::beast::http::status::ok, response->result());
}
