#include "./config.h"

#include "../librepomgr/buildactions/buildaction.h"
#include "../librepomgr/buildactions/buildactionmeta.h"
#include "../librepomgr/json.h"
#include "../librepomgr/webapi/params.h"
#include "../librepomgr/webclient/session.h"

#include "../libpkg/data/database.h"
#include "../libpkg/data/package.h"

#include "resources/config.h"

#include <reflective_rapidjson/json/errorformatting.h>

#include <c++utilities/application/argumentparser.h>
#include <c++utilities/application/commandlineutils.h>
#include <c++utilities/conversion/stringbuilder.h>
#include <c++utilities/conversion/stringconversion.h>
#include <c++utilities/io/ansiescapecodes.h>
#include <c++utilities/misc/parseerror.h>

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
#include <ranges>
#include <string_view>

using namespace CppUtilities;
using namespace CppUtilities::EscapeCodes;
using namespace std;

// helpers for formatting output

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
    auto errors = ReflectiveRapidJSON::JsonDeserializationErrors();
    errors.throwOn = ReflectiveRapidJSON::JsonDeserializationErrors::ThrowOn::All;
    const auto packages
        = ReflectiveRapidJSON::JsonReflector::fromJson<std::list<LibPkg::PackageSearchResult>>(jsonData.data(), jsonData.size(), &errors);
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
    auto errors = ReflectiveRapidJSON::JsonDeserializationErrors();
    errors.throwOn = ReflectiveRapidJSON::JsonDeserializationErrors::ThrowOn::All;
    const auto packages = ReflectiveRapidJSON::JsonReflector::fromJson<std::list<LibPkg::Package>>(jsonData.data(), jsonData.size(), &errors);
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
    auto errors = ReflectiveRapidJSON::JsonDeserializationErrors();
    errors.throwOn = ReflectiveRapidJSON::JsonDeserializationErrors::ThrowOn::All;
    auto actions = ReflectiveRapidJSON::JsonReflector::fromJson<std::list<LibRepoMgr::BuildAction>>(jsonData.data(), jsonData.size(), &errors);
    actions.sort([](const auto &lhs, const auto &rhs) { return lhs.created < rhs.created; });
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

static std::string formatTimeStamp(const DateTime timeStamp)
{
    static const auto now = DateTime::gmtNow();
    return (now - timeStamp).toString(TimeSpanOutputFormat::WithMeasures, true) % " ago ("
        % timeStamp.toString(DateTimeOutputFormat::DateAndTime, true)
        + ')';
}

static void printBuildAction(const LibRepoMgr::BuildAction &a, const LibRepoMgr::BuildActionMetaInfo &meta)
{
    constexpr auto unknown = std::string_view("?");
    const auto typeInfo = meta.isTypeIdValid(a.type) ? &meta.typeInfoForId(a.type) : nullptr;
    const auto status = static_cast<std::size_t>(a.status) < meta.states.size() ? meta.states[static_cast<std::size_t>(a.status)].name : unknown;
    const auto result = static_cast<std::size_t>(a.result) < meta.results.size() ? meta.results[static_cast<std::size_t>(a.result)].name : unknown;
    const auto flags = typeInfo ? &typeInfo->flags : nullptr;
    const auto startAfter = a.startAfter | std::views::transform([](auto id) { return numberToString(id); });

    std::cout << TextAttribute::Bold << "Build action " << a.id << TextAttribute::Reset << '\n';
    tabulate::Table t;
    t.format().hide_border();
    t.add_row({ "Task", a.taskName });
    t.add_row({ "Type", (typeInfo ? typeInfo->name : unknown).data() });
    t.add_row({ "Status", status.data() });
    t.add_row({ "Result", result.data() });
    if (std::holds_alternative<std::string>(a.resultData)) {
        t.add_row({ "Result data", std::get<std::string>(a.resultData) });
    } else {
        t.add_row({ "Result data", "(no output formatter for result output type implemented yet)" });
    }
    t.add_row({ "Created", formatTimeStamp(a.created) });
    t.add_row({ "Started", formatTimeStamp(a.started) });
    t.add_row({ "Finished", formatTimeStamp(a.finished) });
    t.add_row({ "Start after", joinStrings<decltype(startAfter), std::string>(startAfter, ", ") });
    t.add_row({ "Directory", a.directory });
    t.add_row({ "Source repo", joinStrings(a.sourceDbs, "\n") });
    t.add_row({ "Destination repo", joinStrings(a.destinationDbs, "\n") });
    t.add_row({ "Packages", joinStrings(a.packageNames, "\n") });
    if (flags) {
        auto presentFlags = std::string();
        presentFlags.reserve(32);
        for (const auto &flag : *flags) {
            if (a.flags & flag.id) {
                if (!presentFlags.empty()) {
                    presentFlags += ", ";
                }
                presentFlags += flag.name;
            }
        }
        t.add_row({ "Flags", std::move(presentFlags) });
    } else {
        t.add_row({ "Flags", numberToString(a.flags) });
    }
    t.add_row({ "Output", a.output });
    t.column(0).format().font_align(tabulate::FontAlign::right);
    std::cout << t << '\n';
}

static void printBuildAction(const LibRepoMgr::WebClient::Response::body_type::value_type &jsonData)
{
    auto buildAction = LibRepoMgr::BuildAction();
    {
        const auto doc = ReflectiveRapidJSON::JsonReflector::parseJsonDocFromString(jsonData.data(), jsonData.size());
        auto errors = ReflectiveRapidJSON::JsonDeserializationErrors();
        errors.throwOn = ReflectiveRapidJSON::JsonDeserializationErrors::ThrowOn::All;
        ReflectiveRapidJSON::JsonReflector::pull(buildAction, doc.GetObject(), &errors);
    }
    printBuildAction(buildAction, LibRepoMgr::BuildActionMetaInfo());
}

static void printBuildActions(const LibRepoMgr::WebClient::Response::body_type::value_type &jsonData)
{
    auto errors = ReflectiveRapidJSON::JsonDeserializationErrors();
    errors.throwOn = ReflectiveRapidJSON::JsonDeserializationErrors::ThrowOn::All;
    auto buildActions = ReflectiveRapidJSON::JsonReflector::fromJson<std::list<LibRepoMgr::BuildAction>>(jsonData.data(), jsonData.size(), &errors);
    buildActions.sort([](const auto &lhs, const auto &rhs) { return lhs.created < rhs.created; });
    const auto meta = LibRepoMgr::BuildActionMetaInfo();
    for (const auto &a : buildActions) {
        printBuildAction(a, meta);
    }
    std::cout.flush();
}

static void printRawDataForErrorHandling(const LibRepoMgr::WebClient::Response::body_type::value_type &rawData)
{
    if (!rawData.empty()) {
        std::cerr << Phrases::InfoMessage << "Server replied:";
        if (rawData.size() > 50 || rawData.find('\n') != std::string::npos) {
            std::cerr << Phrases::End;
        } else {
            std::cerr << TextAttribute::Reset << ' ';
        }
        std::cerr << rawData << '\n';
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
        std::cerr << Phrases::ErrorMessage << "HTTP request not successful: " << response.result() << " ("
                  << static_cast<std::underlying_type_t<decltype(response.result())>>(response.result()) << " response)" << Phrases::End;
        std::cerr << Phrases::InfoMessage << "URL was: " << url << Phrases::End;
        printRawDataForErrorHandling(body);
        return;
    }
    try {
        std::invoke(printer, body);
    } catch (const ReflectiveRapidJSON::JsonDeserializationError &e) {
        std::cerr << Phrases::ErrorMessage << "Unable to make sense of response: " << ReflectiveRapidJSON::formatJsonDeserializationError(e)
                  << Phrases::End;
        returnCode = 13;
    } catch (const RAPIDJSON_NAMESPACE::ParseResult &e) {
        std::cerr << Phrases::ErrorMessage << "Unable to parse responnse: " << tupleToString(LibRepoMgr::serializeParseError(e)) << Phrases::End;
        returnCode = 11;
    } catch (const std::runtime_error &e) {
        std::cerr << Phrases::ErrorMessage << "Unable to display response: " << e.what() << Phrases::End;
        returnCode = 12;
    }
    if (returnCode) {
        std::cerr << Phrases::InfoMessage << "URL was: " << url << std::endl;
    }
}

// helper for turning CLI args into URL query parameters

static std::string asQueryParam(const Argument &cliArg, std::string_view paramName = std::string_view())
{
    if (!cliArg.isPresent()) {
        return std::string();
    }
    const auto argValues
        = cliArg.values(0) | std::views::transform([](const char *argValue) { return LibRepoMgr::WebAPI::Url::encodeValue(argValue); });
    return joinStrings<decltype(argValues), std::string>(
        argValues, "&", true, argsToString(paramName.empty() ? std::string_view(cliArg.name()) : paramName, '='));
}

static void appendAsQueryParam(std::string &path, const Argument &cliArg, std::string_view paramName = std::string_view())
{
    auto asParam = asQueryParam(cliArg, paramName);
    if (asParam.empty()) {
        return;
    }
    if (!(path.empty() || path.ends_with('?'))) {
        path += '&';
    }
    path += std::move(asParam);
}

static void appendAsQueryParam(std::string &path, std::initializer_list<const Argument *> args)
{
    for (const auto *arg : args) {
        appendAsQueryParam(path, *arg, arg->name());
    }
}

int main(int argc, const char *argv[])
{
    // define command-specific parameters
    auto verb = boost::beast::http::verb::get;
    auto path = std::string();
    void (*printer)(const LibRepoMgr::WebClient::Response::body_type::value_type &jsonData) = nullptr;

    // read CLI args
    auto parser = ArgumentParser();
    auto configFileArg = ConfigValueArgument("config-file", 'c', "specifies the path of the config file", { "path" });
    configFileArg.setEnvironmentVariable(PROJECT_VARNAME_UPPER "_CONFIG_FILE");
    auto instanceArg = ConfigValueArgument("instance", 'i', "specifies the instance to connect to", { "instance" });
    auto rawArg = ConfigValueArgument("raw", 'r', "print the raw output from the server");
    auto verboseArg = ConfigValueArgument("verbose", 'v', "prints debugging output");
    auto packageArg = OperationArgument("package", 'p', "Package-related operations:");
    auto searchArg = OperationArgument("search", 's', "searches for packages");
    auto searchTermArg = ConfigValueArgument("term", 't', "specifies the search term", { "term" });
    searchTermArg.setImplicit(true);
    searchTermArg.setRequired(true);
    auto searchModeArg
        = ConfigValueArgument("mode", 'm', "specifies the mode", { "name/name-contains/regex/provides/depends/libprovides/libdepends" });
    searchModeArg.setPreDefinedCompletionValues("name name-contains regex provides depends libprovides libdepends");
    searchArg.setSubArguments({ &searchTermArg, &searchModeArg });
    searchArg.setCallback([&path, &printer, &searchTermArg, &searchModeArg](const ArgumentOccurrence &) {
        path = "/api/v0/packages?mode=" + LibRepoMgr::WebAPI::Url::encodeValue(searchModeArg.firstValueOr("name-contains"));
        printer = printPackageSearchResults;
        appendAsQueryParam(path, searchTermArg, "name");
    });
    auto packageNameArg = ConfigValueArgument("name", 'n', "specifies the package name", { "name" });
    packageNameArg.setImplicit(true);
    packageNameArg.setRequired(true);
    auto packageShowArg = OperationArgument("show", 'd', "shows details about a package");
    packageShowArg.setSubArguments({ &packageNameArg });
    packageShowArg.setCallback([&path, &printer, &packageNameArg](const ArgumentOccurrence &) {
        path = "/api/v0/packages?mode=name&details=1";
        printer = printPackageDetails;
        appendAsQueryParam(path, packageNameArg, "name");
    });
    packageArg.setSubArguments({ &searchArg, &packageShowArg });
    auto actionArg = OperationArgument("action", 'a', "Build-action-related operations:");
    auto listActionsArg = OperationArgument("list", 'l', "list build actions");
    listActionsArg.setCallback([&path, &printer](const ArgumentOccurrence &) {
        path = "/api/v0/build-action";
        printer = printListOfBuildActions;
    });
    auto buildActionIdArg = ConfigValueArgument("id", '\0', "specifies the build action IDs", { "ID" });
    buildActionIdArg.setImplicit(true);
    buildActionIdArg.setRequired(true);
    buildActionIdArg.setRequiredValueCount(Argument::varValueCount);
    auto showBuildActionArg = OperationArgument("show", 'd', "show details about a build action");
    showBuildActionArg.setCallback([&path, &printer, &buildActionIdArg](const ArgumentOccurrence &) {
        path = "/api/v0/build-action/details?";
        printer = printBuildActions;
        appendAsQueryParam(path, buildActionIdArg, "id");
    });
    showBuildActionArg.setSubArguments({ &buildActionIdArg });
    auto createBuildActionArg = OperationArgument("create", '\0', "creates and starts a new build action (or pre-defined task)");
    auto taskArg = ConfigValueArgument("task", '\0', "specifies the pre-defined task to run", { "task" });
    auto typeArg = ConfigValueArgument("type", '\0', "specifies the action type", { "type" });
    auto directoryArg = ConfigValueArgument("directory", '\0', "specifies the directory", { "path" });
    auto startConditionArg = ConfigValueArgument("start-condition", '\0', "specifies the start condition", { "immediately/after/manually" });
    auto startAfterIdArg
        = ConfigValueArgument("start-after-id", '\0', "specifies the IDs of existing build actions to start the new action after", { "ID" });
    auto sourceRepoArg = ConfigValueArgument("source-repo", '\0', "specifies the source repositories", { "database-name" });
    auto destinationRepoArg = ConfigValueArgument("destination-repo", '\0', "specifies the destination repositories", { "database-name" });
    auto packagesArg = ConfigValueArgument("package", '\0', "specifies the packages", { "package-name" });
    startConditionArg.setPreDefinedCompletionValues("immediately after manually");
    startAfterIdArg.setRequiredValueCount(Argument::varValueCount);
    sourceRepoArg.setRequiredValueCount(Argument::varValueCount);
    destinationRepoArg.setRequiredValueCount(Argument::varValueCount);
    packagesArg.setRequiredValueCount(Argument::varValueCount);
    createBuildActionArg.setCallback([&verb, &path, &printer, &taskArg, &typeArg, &directoryArg, &startConditionArg, &startAfterIdArg, &sourceRepoArg,
                                         &destinationRepoArg, &packagesArg](const ArgumentOccurrence &) {
        verb = boost::beast::http::verb::post;
        path = "/api/v0/build-action?";
        printer = printBuildAction;
        if (taskArg.isPresent() && typeArg.isPresent()) {
            throw ParseError("The arguments --task and --type can not be combined.");
        }
        appendAsQueryParam(path,
            { &(taskArg.isPresent() ? taskArg : typeArg), &directoryArg, &startConditionArg, &startAfterIdArg, &sourceRepoArg, &destinationRepoArg,
                &packagesArg });
    });
    createBuildActionArg.setSubArguments(
        { &typeArg, &taskArg, &directoryArg, &startConditionArg, &startAfterIdArg, &sourceRepoArg, &destinationRepoArg, &packagesArg });
    auto deleteBuildActionArg = OperationArgument("delete", '\0', "deletes a build action");
    deleteBuildActionArg.setCallback([&verb, &path, &printer, &buildActionIdArg](const ArgumentOccurrence &) {
        verb = boost::beast::http::verb::delete_;
        path = "/api/v0/build-action?";
        printer = printRawData;
        appendAsQueryParam(path, buildActionIdArg, "id");
    });
    deleteBuildActionArg.setSubArguments({ &buildActionIdArg });
    auto cloneBuildActionArg = OperationArgument("clone", '\0', "clones a build action");
    cloneBuildActionArg.setCallback([&verb, &path, &printer, &buildActionIdArg](const ArgumentOccurrence &) {
        verb = boost::beast::http::verb::post;
        path = "/api/v0/build-action/clone?";
        printer = printRawData;
        appendAsQueryParam(path, buildActionIdArg, "id");
    });
    cloneBuildActionArg.setSubArguments({ &buildActionIdArg });
    auto startBuildActionArg = OperationArgument("start", '\0', "starts a build action");
    startBuildActionArg.setCallback([&verb, &path, &printer, &buildActionIdArg](const ArgumentOccurrence &) {
        verb = boost::beast::http::verb::post;
        path = "/api/v0/build-action/start?";
        printer = printRawData;
        appendAsQueryParam(path, buildActionIdArg, "id");
    });
    startBuildActionArg.setSubArguments({ &buildActionIdArg });
    auto stopBuildActionArg = OperationArgument("stop", '\0', "stops a build action");
    stopBuildActionArg.setCallback([&verb, &path, &printer, &buildActionIdArg](const ArgumentOccurrence &) {
        verb = boost::beast::http::verb::post;
        path = "/api/v0/build-action/stop?";
        printer = printRawData;
        appendAsQueryParam(path, buildActionIdArg, "id");
    });
    stopBuildActionArg.setSubArguments({ &buildActionIdArg });
    actionArg.setSubArguments({ &listActionsArg, &showBuildActionArg, &createBuildActionArg, &deleteBuildActionArg, &cloneBuildActionArg,
        &startBuildActionArg, &stopBuildActionArg });
    auto apiArg = OperationArgument("api", '\0', "Invoke a generic API request:");
    auto pathArg = ConfigValueArgument("path", '\0', "specifies the route's path without prefix", { "path/of/route?foo=bar&bar=foo" });
    pathArg.setImplicit(true);
    pathArg.setRequired(true);
    auto methodArg = ConfigValueArgument("method", 'x', "specifies the method", { "GET/POST/PUT/DELETE" });
    methodArg.setPreDefinedCompletionValues("GET POST PUT DELETE");
    apiArg.setCallback([&verb, &path, &printer, &pathArg, &methodArg](const ArgumentOccurrence &) {
        const auto *rawVerb = methodArg.firstValueOr("GET");
        verb = boost::beast::http::string_to_verb(rawVerb);
        if (verb == boost::beast::http::verb::unknown) {
            throw ParseError(argsToString('\"', rawVerb, "\" is not a valid method."));
        }
        path = argsToString("/api/v0/", pathArg.values(0).front());
        printer = printRawData;
    });
    apiArg.setSubArguments({ &pathArg, &methodArg });
    auto helpArg = HelpArgument(parser);
    auto noColorArg = NoColorArgument();
    parser.setMainArguments({ &packageArg, &actionArg, &apiArg, &instanceArg, &configFileArg, &rawArg, &verboseArg, &noColorArg, &helpArg });
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
        if (verboseArg.isPresent()) {
            std::cerr << Phrases::InfoMessage << "Read config from: " << config.path << std::endl;
        }
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
    if (verboseArg.isPresent()) {
        std::cerr << Phrases::InfoMessage << verb << ':' << ' ' << url << std::endl;
    }
    LibRepoMgr::WebClient::runSessionFromUrl(ioContext, sslContext, url,
        std::bind(&handleResponse, std::ref(url), std::placeholders::_1, std::placeholders::_2, rawArg.isPresent() ? printRawData : printer,
            std::ref(returnCode)),
        std::string(), config.userName, config.password, verb);
    ioContext.run();
    return returnCode;
}
