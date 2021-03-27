#include "resources/config.h"

#include "../librepomgr/buildactions/buildaction.h"
#include "../librepomgr/buildactions/buildactionmeta.h"
#include "../librepomgr/json.h"
#include "../librepomgr/webapi/params.h"
#include "../librepomgr/webclient/session.h"

#include "../libpkg/data/database.h"
#include "../libpkg/data/package.h"

#include <c++utilities/application/argumentparser.h>
#include <c++utilities/application/commandlineutils.h>
#include <c++utilities/conversion/stringbuilder.h>
#include <c++utilities/conversion/stringconversion.h>
#include <c++utilities/io/ansiescapecodes.h>
#include <c++utilities/io/inifile.h>
#include <c++utilities/io/nativefilestream.h>

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wshadow=compatible-local"
#endif
#include <tabulate/table.hpp>
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

#include <fstream>
#include <functional>
#include <iostream>
#include <string_view>

using namespace CppUtilities;
using namespace CppUtilities::EscapeCodes;
using namespace std;

struct ClientConfig {
    void parse(const Argument &configFileArg, const Argument &instanceArg);

    const char *path = nullptr;
    std::string instance;
    std::string url;
    std::string userName;
    std::string password;
};

void ClientConfig::parse(const Argument &configFileArg, const Argument &instanceArg)
{
    // parse connfig file
    path = configFileArg.firstValue();
    if (!path || !*path) {
        path = "/etc/buildservice" PROJECT_CONFIG_SUFFIX "/client.conf";
    }
    auto configFile = NativeFileStream();
    configFile.exceptions(std::ios_base::badbit | std::ios_base::failbit);
    configFile.open(path, std::ios_base::in);
    auto configIni = AdvancedIniFile();
    configIni.parse(configFile);
    configFile.close();

    // read innstance
    if (instanceArg.isPresent()) {
        instance = instanceArg.values().front();
    }
    for (const auto &section : configIni.sections) {
        if (!section.name.starts_with("instance/")) {
            continue;
        }
        if (!instance.empty() && instance != std::string_view(section.name.data() + 9, section.name.size() - 9)) {
            continue;
        }
        instance = section.name;
        if (const auto url = section.findField("url"); url != section.fieldEnd()) {
            this->url = std::move(url->value);
        } else {
            throw std::runtime_error("Config is invalid: No \"url\" specified within \"" % section.name + "\".");
        }
        if (const auto user = section.findField("user"); user != section.fieldEnd()) {
            this->userName = std::move(user->value);
        }
        break;
    }
    if (url.empty()) {
        throw std::runtime_error("Config is invalid: Instance configuration insufficient.");
    }

    // read user data
    if (userName.empty()) {
        return;
    }
    if (const auto userSection = configIni.findSection("user/" + userName); userSection != configIni.sectionEnd()) {
        if (const auto password = userSection->findField("password"); password != userSection->fieldEnd()) {
            this->password = std::move(password->value);
        } else {
            throw std::runtime_error("Config is invalid: No \"password\" specified within \"" % userSection->name + "\".");
        }
    } else {
        throw std::runtime_error("Config is invalid: User \"" % userName + "\" referenced in instance configuration not found.");
    }
}

static void configureColumnWidths(tabulate::Table &table)
{
    const auto terminalSize = determineTerminalSize();
    if (!terminalSize.columns) {
        return;
    }
    struct ColumnStats {
        std::size_t maxSize = 0;
        std::size_t totalSize = 0;
        std::size_t rows = 0;
        double averageSize = 0.0;
        double averagePercentage = 0.0;
        std::size_t width = 0;
    };
    auto columnStats = std::vector<ColumnStats>();
    for (const auto &row : table) {
        const auto columnCount = row.size();
        if (columnStats.size() < columnCount) {
            columnStats.resize(columnCount);
        }
        auto column = columnStats.begin();
        for (const auto &cell : row.cells()) {
            const auto size = cell->size();
            column->maxSize = std::max(column->maxSize, size);
            column->totalSize += std::max<std::size_t>(10, size);
            column->rows += 1;
            ++column;
        }
    }
    auto totalAverageSize = 0.0;
    for (auto &column : columnStats) {
        totalAverageSize += (column.averageSize = static_cast<double>(column.totalSize) / static_cast<double>(column.rows));
    }
    for (auto &column : columnStats) {
        column.averagePercentage = column.averageSize / totalAverageSize;
        column.width = std::max<std::size_t>(static_cast<std::size_t>(static_cast<double>(terminalSize.columns) * column.averagePercentage),
            std::min<std::size_t>(column.maxSize, 10u));
    }
    for (std::size_t columnIndex = 0; columnIndex != columnStats.size(); ++columnIndex) {
        table.column(columnIndex).format().width(columnStats[columnIndex].width);
    }
}

static void printPackageSearchResults(const LibRepoMgr::WebClient::Response::body_type::value_type &jsonData)
{
    const auto packages = ReflectiveRapidJSON::JsonReflector::fromJson<std::list<LibPkg::PackageSearchResult>>(jsonData.data(), jsonData.size());
    tabulate::Table t;
    t.format().hide_border();
    t.add_row({ "Arch", "Repo", "Name", "Version", "Description", "Build date" });
    for (const auto &[db, package] : packages) {
        const auto &dbInfo = std::get<LibPkg::DatabaseInfo>(db);
        t.add_row(
            { package->packageInfo ? package->packageInfo->arch : dbInfo.arch, dbInfo.name, package->name, package->version, package->description,
                package->packageInfo && !package->packageInfo->buildDate.isNull() ? package->packageInfo->buildDate.toString() : "?" });
    }
    t.row(0).format().font_align(tabulate::FontAlign::center).font_style({ tabulate::FontStyle::bold });
    configureColumnWidths(t);
    std::cout << t << std::endl;
}

template <typename List> inline std::string formatList(const List &list)
{
    return joinStrings(list, ", ");
}

static std::string formatDependencies(const std::vector<LibPkg::Dependency> &deps)
{
    auto asStrings = std::vector<std::string>();
    asStrings.reserve(deps.size());
    for (const auto &dep : deps) {
        asStrings.emplace_back(dep.toString());
    }
    return formatList(asStrings);
}

static void printPackageDetails(const LibRepoMgr::WebClient::Response::body_type::value_type &jsonData)
{
    const auto packages = ReflectiveRapidJSON::JsonReflector::fromJson<std::list<LibPkg::Package>>(jsonData.data(), jsonData.size());
    for (const auto &package : packages) {
        const auto *const pkg = &package;
        std::cout << TextAttribute::Bold << pkg->name << ' ' << pkg->version << TextAttribute::Reset << '\n';
        tabulate::Table t;
        t.format().hide_border();
        if (pkg->packageInfo) {
            t.add_row({ "Arch", pkg->packageInfo->arch });
        } else if (pkg->sourceInfo) {
            t.add_row({ "Archs", formatList(pkg->sourceInfo->archs) });
        }
        t.add_row({ "Description", pkg->description });
        t.add_row({ "Upstream URL", pkg->upstreamUrl });
        t.add_row({ "License(s)", formatList(pkg->licenses) });
        t.add_row({ "Groups", formatList(pkg->groups) });
        if (pkg->packageInfo && pkg->packageInfo->size) {
            t.add_row({ "Package size", dataSizeToString(pkg->packageInfo->size, true) });
        }
        if (pkg->installInfo) {
            t.add_row({ "Installed size", dataSizeToString(pkg->installInfo->installedSize, true) });
        }
        if (pkg->packageInfo) {
            if (!pkg->packageInfo->packager.empty()) {
                t.add_row({ "Packager", pkg->packageInfo->packager });
            }
            if (!pkg->packageInfo->buildDate.isNull()) {
                t.add_row({ "Packager", pkg->packageInfo->buildDate.toString() });
            }
        }
        t.add_row({ "Dependencies", formatDependencies(pkg->dependencies) });
        t.add_row({ "Optional dependencies", formatDependencies(pkg->optionalDependencies) });
        if (pkg->sourceInfo) {
            t.add_row({ "Make dependencies", formatDependencies(pkg->sourceInfo->makeDependencies) });
            t.add_row({ "Check dependencies", formatDependencies(pkg->sourceInfo->checkDependencies) });
        }
        t.add_row({ "Provides", formatDependencies(pkg->provides) });
        t.add_row({ "Replaces", formatDependencies(pkg->replaces) });
        t.add_row({ "Conflicts", formatDependencies(pkg->conflicts) });
        t.add_row({ "Contained libraries", formatList(pkg->libprovides) });
        t.add_row({ "Needed libraries", formatList(pkg->libdepends) });
        t.column(0).format().font_align(tabulate::FontAlign::right);
        configureColumnWidths(t);
        std::cout << t << '\n';
    }
    std::cout.flush();
}

static void printListOfBuildActions(const LibRepoMgr::WebClient::Response::body_type::value_type &jsonData)
{
    const auto actions = ReflectiveRapidJSON::JsonReflector::fromJson<std::list<LibRepoMgr::BuildAction>>(jsonData.data(), jsonData.size());
    const auto meta = LibRepoMgr::BuildActionMetaInfo();
    const auto unknown = std::string_view("?");
    auto t = tabulate::Table();
    t.format().hide_border();
    t.add_row(
        { "ID", "Task", "Type", "Status", "Result", "Created", "Started", "Runtime", "Directory", "Source repo", "Destination repo", "Packages" });
    for (const auto &a : actions) {
        const LibRepoMgr::BuildActionTypeMetaInfo *typeInfo = nullptr;
        if (meta.isTypeIdValid(a.type)) {
            typeInfo = &meta.typeInfoForId(a.type);
        }
        const auto status = static_cast<std::size_t>(a.status) < meta.states.size() ? meta.states[static_cast<std::size_t>(a.status)].name : unknown;
        const auto result
            = static_cast<std::size_t>(a.result) < meta.results.size() ? meta.results[static_cast<std::size_t>(a.result)].name : unknown;
        t.add_row({ numberToString(a.id), a.taskName, (typeInfo ? typeInfo->name : unknown).data(), status.data(), result.data(),
            a.created.toString(DateTimeOutputFormat::DateAndTime, true), a.started.toString(DateTimeOutputFormat::DateAndTime, true),
            (a.finished - a.started).toString(TimeSpanOutputFormat::WithMeasures, true), a.directory, joinStrings(a.sourceDbs, ", "),
            joinStrings(a.destinationDbs, ", "), joinStrings(a.packageNames, ", ") });
    }
    t.row(0).format().font_align(tabulate::FontAlign::center).font_style({ tabulate::FontStyle::bold });
    configureColumnWidths(t);
    std::cout << t << std::endl;
}

static void printRawDataForErrorHandling(const LibRepoMgr::WebClient::Response::body_type::value_type &rawData)
{
    if (!rawData.empty()) {
        std::cerr << Phrases::InfoMessage << "Server replied:" << Phrases::End << rawData << '\n';
    }
}

static void printRawData(const LibRepoMgr::WebClient::Response::body_type::value_type &rawData)
{
    std::cout << rawData << '\n';
}

static void handleResponse(const std::string &url, const LibRepoMgr::WebClient::SessionData &data,
    const LibRepoMgr::WebClient::HttpClientError &error, void (*printer)(const LibRepoMgr::WebClient::Response::body_type::value_type &jsonData),
    int &returnCode)
{
    const auto &response = std::get<LibRepoMgr::WebClient::Response>(data.response);
    const auto &body = response.body();
    if (error.errorCode != boost::beast::errc::success && error.errorCode != boost::asio::ssl::error::stream_truncated) {
        std::cerr << Phrases::ErrorMessage << "Unable to connect: " << error.what() << Phrases::End;
        std::cerr << Phrases::InfoMessage << "URL was: " << url << Phrases::End;
        printRawDataForErrorHandling(body);
        return;
    }
    if (response.result() != boost::beast::http::status::ok) {
        std::cerr << Phrases::ErrorMessage << "HTTP request not successful: " << error.what() << Phrases::End;
        std::cerr << Phrases::InfoMessage << "URL was: " << url << Phrases::End;
        printRawDataForErrorHandling(body);
        return;
    }
    try {
        std::invoke(printer, body);
    } catch (const RAPIDJSON_NAMESPACE::ParseResult &e) {
        std::cerr << Phrases::ErrorMessage << "Unable to parse responnse: " << tupleToString(LibRepoMgr::serializeParseError(e)) << Phrases::End;
        std::cerr << Phrases::InfoMessage << "URL was: " << url << std::endl;
        returnCode = 11;
    } catch (const std::runtime_error &e) {
        std::cerr << Phrases::ErrorMessage << "Unable to display response: " << e.what() << Phrases::End;
        std::cerr << Phrases::InfoMessage << "URL was: " << url << std::endl;
        returnCode = 12;
    }
}

int main(int argc, const char *argv[])
{
    // define command-specific parameters
    auto verb = boost::beast::http::verb::get;
    auto path = std::string();
    void (*printer)(const LibRepoMgr::WebClient::Response::body_type::value_type &jsonData) = nullptr;

    // read CLI args
    ArgumentParser parser;
    ConfigValueArgument configFileArg("config-file", 'c', "specifies the path of the config file", { "path" });
    configFileArg.setEnvironmentVariable(PROJECT_VARNAME_UPPER "_CONFIG_FILE");
    ConfigValueArgument instanceArg("instance", 'i', "specifies the instance to connect to", { "instance" });
    OperationArgument packageArg("package", 'p', "Package-related operations:");
    OperationArgument searchArg("search", 's', "searches for packages");
    ConfigValueArgument searchTermArg("term", 't', "specifies the search term", { "term" });
    searchTermArg.setImplicit(true);
    searchTermArg.setRequired(true);
    ConfigValueArgument searchModeArg("mode", 'm', "specifies the mode", { "name/name-contains/regex/provides/depends/libprovides/libdepends" });
    searchModeArg.setPreDefinedCompletionValues("name name-contains regex provides depends libprovides libdepends");
    searchArg.setSubArguments({ &searchTermArg, &searchModeArg });
    searchArg.setCallback([&path, &printer, &searchTermArg, &searchModeArg](const ArgumentOccurrence &) {
        path = "/api/v0/packages?mode=" % LibRepoMgr::WebAPI::Url::encodeValue(searchModeArg.firstValueOr("name-contains")) % "&name="
            + LibRepoMgr::WebAPI::Url::encodeValue(searchTermArg.values().front());
        printer = printPackageSearchResults;
    });
    ConfigValueArgument packageNameArg("name", 'n', "specifies the package name", { "name" });
    packageNameArg.setImplicit(true);
    packageNameArg.setRequired(true);
    OperationArgument packageShowArg("show", 'd', "shows details about a package");
    packageShowArg.setSubArguments({ &packageNameArg });
    packageShowArg.setCallback([&path, &printer, &packageNameArg](const ArgumentOccurrence &) {
        path = "/api/v0/packages?mode=name&details=1&name=" + LibRepoMgr::WebAPI::Url::encodeValue(packageNameArg.values().front());
        printer = printPackageDetails;
    });
    packageArg.setSubArguments({ &searchArg, &packageShowArg });
    OperationArgument actionArg("action", 'a', "Build-action-related operations:");
    OperationArgument listActionsArg("list", 'l', "list build actions");
    listActionsArg.setCallback([&path, &printer](const ArgumentOccurrence &) {
        path = "/api/v0/build-action";
        printer = printListOfBuildActions;
    });
    actionArg.setSubArguments({ &listActionsArg });
    HelpArgument helpArg(parser);
    NoColorArgument noColorArg;
    parser.setMainArguments({ &packageArg, &actionArg, &instanceArg, &configFileArg, &noColorArg, &helpArg });
    parser.parseArgs(argc, argv);

    // return early if no operation specified
    if (!printer) {
        if (!helpArg.isPresent()) {
            std::cerr << "No command specified; use --help to list available commands.\n";
        }
        return 0;
    }

    // parse config
    auto config = ClientConfig();
    try {
        config.parse(configFileArg, instanceArg);
    } catch (const std::runtime_error &e) {
        std::cerr << Phrases::ErrorMessage << "Unable to parse config: " << e.what() << Phrases::End;
        std::cerr << Phrases::InfoMessage << "Path of config file was: " << (config.path ? config.path : "[none]") << Phrases::End;
        return 10;
    }

    // make HTTP request and show response
    const auto url = config.url + path;
    auto ioContext = boost::asio::io_context();
    auto sslContext = boost::asio::ssl::context{ boost::asio::ssl::context::sslv23_client };
    auto returnCode = 0;
    sslContext.set_verify_mode(boost::asio::ssl::verify_peer);
    sslContext.set_default_verify_paths();
    LibRepoMgr::WebClient::runSessionFromUrl(ioContext, sslContext, url,
        std::bind(&handleResponse, std::ref(url), std::placeholders::_1, std::placeholders::_2, printer, std::ref(returnCode)), std::string(),
        config.userName, config.password, verb);
    ioContext.run();

    return 0;
}
