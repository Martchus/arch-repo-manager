#include "./routes.h"
#include "./params.h"
#include "./render.h"
#include "./server.h"

#include "../webclient/aur.h"

#include "../serversetup.h"

#include "../../libpkg/data/config.h"
#include "../../libpkg/data/package.h"
#include "../../libpkg/parser/aur.h"

#include "resources/config.h"

#include <c++utilities/conversion/stringbuilder.h>
#include <c++utilities/conversion/stringconversion.h>
#include <c++utilities/io/ansiescapecodes.h>

#include <algorithm>
#include <iostream>
#include <numeric>
#include <regex>

using namespace std;
using namespace CppUtilities;
using namespace CppUtilities::EscapeCodes;
using namespace LibRepoMgr::WebAPI::Render;
using namespace LibPkg;

namespace LibRepoMgr {
namespace WebAPI {
namespace Routes {

void getRoot(const Params &params, ResponseHandler &&handler)
{
    static const auto routes([] {
        stringstream ss;
        ss << "Available routes:\n";
        for (const auto &route : Server::router()) {
            const auto method(boost::beast::http::to_string(route.first.method));
            ss << method;
            for (auto i = method.size(); i < 5; ++i) {
                ss << ' ';
            }
            ss << route.first.path << '\n';
        }
        ss << "\nNote: curl -X GET/POST http://...\n";
        return ss.str();
    }());
    handler(makeText(params.request(), routes));
}

void getVersion(const Params &params, ResponseHandler &&handler)
{
    handler(makeText(params.request(), APP_VERSION));
}

void getStatus(const Params &params, ResponseHandler &&handler)
{
    auto configLock = params.setup.config.lockToRead();
    auto buildLock = params.setup.building.lockToRead();
    auto metaLock = params.setup.building.metaInfo.lockToRead();
    const auto status = params.setup.computeStatus();
    const auto jsonDoc = status.toJsonDocument();
    metaLock.unlock();
    buildLock.unlock();
    configLock.unlock();
    handler(makeJson(params.request(), jsonDoc, params.target.hasPrettyFlag()));
}

void getDatabases(const Params &params, ResponseHandler &&handler)
{
    const auto prettyFlag(params.target.hasPrettyFlag());
    const auto dbNames = params.target.decodeValues("name");
    if (dbNames.empty()) {
        auto lock = params.setup.config.lockToRead();
        const auto jsonDoc = ReflectiveRapidJSON::JsonReflector::toJsonDocument(params.setup.config.databases);
        lock.unlock();
        handler(makeJson(params.request(), jsonDoc, prettyFlag));
        return;
    }

    RAPIDJSON_NAMESPACE::Document document(RAPIDJSON_NAMESPACE::kArrayType);
    RAPIDJSON_NAMESPACE::Document::Array array(document.GetArray());
    auto lock = params.setup.config.lockToRead();
    for (const auto &dbName : dbNames) {
        for (const auto &db : params.setup.config.databases) {
            if (db.name == dbName) {
                ReflectiveRapidJSON::JsonReflector::push(db, array, document.GetAllocator());
                break;
            }
        }
    }
    lock.unlock();
    handler(makeJson(params.request(), document, prettyFlag));
}

void getUnresolved(const Params &params, ResponseHandler &&handler)
{
    const auto names = params.target.decodeValues("name");
    if (names.empty()) {
        throw BadRequest("parameter name missing");
    }

    namespace JR = ReflectiveRapidJSON::JsonReflector;
    RAPIDJSON_NAMESPACE::Document document(RAPIDJSON_NAMESPACE::kObjectType);
    RAPIDJSON_NAMESPACE::Document::Object documentObj(document.GetObject());

    auto lock = params.setup.config.lockToRead();
    const auto newPackages = [&] {
        const auto newPackageNames = params.target.decodeValues("add");
        auto res = std::vector<std::shared_ptr<Package>>();
        res.reserve(newPackageNames.size());
        for (const auto &name : newPackageNames) {
            for (auto &package : params.setup.config.findPackages(name)) {
                res.emplace_back(move(package.pkg));
            }
        }
        return res;
    }();
    const auto removedPackages = [&] {
        DependencySet removedProvides;
        for (const auto &dependencyString : params.target.decodeValues("remove")) {
            auto dependency(Dependency::fromString(dependencyString.data(), dependencyString.size()));
            removedProvides.add(move(dependency.name), DependencyDetail(move(dependency.version), dependency.mode));
        }
        return removedProvides;
    }();

    for (const auto &name : names) {
        for (auto &db : params.setup.config.databases) {
            if (db.name != name) {
                continue;
            }
            const auto unresolvedPackages = db.detectUnresolvedPackages(params.setup.config, newPackages, removedPackages);
            auto value = RAPIDJSON_NAMESPACE::Value(RAPIDJSON_NAMESPACE::Type::kObjectType);
            auto obj = value.GetObject();
            for (const auto &[package, unresolvedDependencies] : unresolvedPackages) {
                JR::push(unresolvedDependencies, package->name.data(), obj, document.GetAllocator());
            }
            obj.AddMember(RAPIDJSON_NAMESPACE::StringRef(db.name.data(), JR::rapidJsonSize(db.name.size())), value, document.GetAllocator());
        }
    }

    lock.unlock();
    handler(makeJson(params.request(), document, params.target.hasPrettyFlag()));
}

void getPackages(const Params &params, ResponseHandler &&handler)
{
    // read mode
    const auto modes(params.target.decodeValues("mode"));
    if (modes.size() > 1) {
        throw BadRequest("more than one mode specified");
    }
    enum class Mode {
        Name,
        NameContains,
        Regex,
        Provides,
        Depends,
        LibProvides,
        LibDepends,
    } mode
        = Mode::Name;
    static const std::unordered_map<std::string_view, Mode> modeByParamValue{
        { "name", Mode::Name },
        { "name-contains", Mode::NameContains },
        { "regex", Mode::Regex },
        { "provides", Mode::Provides },
        { "depends", Mode::Depends },
        { "libprovides", Mode::LibProvides },
        { "libdepends", Mode::LibDepends },
    };
    if (!modes.empty()) {
        const auto modeIterator = modeByParamValue.find(modes.front());
        if (modeIterator == modeByParamValue.end()) {
            throw BadRequest("mode must be \"name\", \"name-contains\", \"regex\", \"provides\", \"depends\", \"libprovides\" or \"libdepends\"");
        }
        mode = modeIterator->second;
    }

    // check for details flag
    const auto details = params.target.hasFlag("details");
    if (details && mode != Mode::Name) {
        throw BadRequest("details flag only supported with mode \"name\"");
    }

    // check dbs
    const auto dbValues = params.target.decodeValues("db");
    const auto canSearchAur = mode == Mode::Name || mode == Mode::NameContains;
    auto dbs = std::unordered_map<std::string_view, std::unordered_set<std::string_view>>();
    auto fromAur = dbValues.empty() && canSearchAur;
    auto onlyFromAur = false;
    for (const auto &dbValue : dbValues) {
        if (dbValue == "any") {
            dbs.clear();
            fromAur = canSearchAur;
            break;
        }
        if (dbValue == "aur") {
            if (!canSearchAur) {
                throw BadRequest("searching AUR is only possible with mode \"name\"");
            }
            fromAur = true;
            onlyFromAur = true;
            continue;
        }
        const auto &[dbName, dbArch] = LibPkg::Config::parseDatabaseDenotation(dbValue);
        dbs[dbName].emplace(dbArch);
    }

    RAPIDJSON_NAMESPACE::Document document(RAPIDJSON_NAMESPACE::kArrayType);
    RAPIDJSON_NAMESPACE::Document::Array array(document.GetArray());

    const auto pushPackages = [&dbs, &document, &array](auto &&packages) {
        for (const auto &package : packages) {
            if (!dbs.empty()) {
                const auto *const db = std::get<Database *>(package.db);
                const auto dbIterator = dbs.find(db->name);
                if (dbIterator == dbs.end() || dbIterator->second.find(db->arch) == dbIterator->second.end()) {
                    continue;
                }
            }
            ReflectiveRapidJSON::JsonReflector::push(package, array, document.GetAllocator());
        }
    };

    std::vector<PackageSearchResult> aurPackages;
    std::vector<std::string> neededAurPackages;
    auto lock = params.setup.config.lockToRead();
    auto &aurDb = params.setup.config.aur;

    // add specified packages to JSON array
    for (const auto &name : params.target.decodeValues("name")) {
        switch (mode) {
        case Mode::Name: {
            const auto packageDenotation
                = LibPkg::Config::parsePackageDenotation(name); // assume names are in the form "repo@arch/pkgname", eg. "core@i686/gcc"
            const auto &[dbName, dbArch, packageName] = packageDenotation;
            const auto isDbAur = dbName == "aur";
            if (fromAur && (dbName.empty() || isDbAur)) {
                auto packageNameStr = std::string(packageName);
                if (auto i = aurDb.packages.find(packageNameStr), end = aurDb.packages.end();
                    i != end && (!details || i->second->origin != PackageOrigin::AurRpcSearch)) {
                    aurPackages.emplace_back(PackageSearchResult{ aurDb, i->second });
                } else {
                    neededAurPackages.emplace_back(std::move(packageNameStr));
                }
            }
            if (!isDbAur && (!dbs.empty() || !onlyFromAur)) {
                auto packages = params.setup.config.findPackages(packageDenotation);
                if (details) {
                    for (const auto &package : packages) {
                        if (dbs.empty() || dbs.find(std::get<LibPkg::Database *>(package.db)->name) != dbs.end()) {
                            ReflectiveRapidJSON::JsonReflector::push(package.pkg, array, document.GetAllocator());
                        }
                    }
                } else {
                    pushPackages(std::move(packages));
                }
            }
            break;
        }
        case Mode::NameContains:
            pushPackages(params.setup.config.findPackages(
                [&dbs, onlyFromAur](const LibPkg::Database &db) { return (dbs.empty() && !onlyFromAur) || dbs.find(db.name) != dbs.end(); },
                [&name](const LibPkg::Database &, const LibPkg::Package &pkg) { return pkg.name.find(name) != std::string::npos; }));
            if (fromAur && !name.empty()) {
                neededAurPackages.emplace_back(std::move(name));
            }
            break;
        case Mode::Regex:
            // assume names are regexes
            pushPackages(params.setup.config.findPackages(std::regex(name.data(), name.size())));
            break;
        case Mode::Provides:
        case Mode::Depends:
            // assume names are dependency notation
            pushPackages(params.setup.config.findPackages(Dependency::fromString(name), mode == Mode::Depends));
            break;
        case Mode::LibProvides:
        case Mode::LibDepends:
            // assume names are "normalized" library names with platform prefix
            pushPackages(params.setup.config.findPackagesProvidingLibrary(name, mode == Mode::LibDepends));
            break;
        default:;
        }
    }

    // add cached AUR packages
    if (details) {
        for (auto &package : aurPackages) {
            ReflectiveRapidJSON::JsonReflector::push(std::move(package.pkg), array, document.GetAllocator());
        }
    } else if (!aurPackages.empty()) {
        for (const auto &package : aurPackages) {
            ReflectiveRapidJSON::JsonReflector::push(package, array, document.GetAllocator());
        }
    }

    // serialize JSON directly if no AUR request required
    lock.unlock();
    if (neededAurPackages.empty()) {
        handler(makeJson(params.request(), document, params.target.hasPrettyFlag()));
        return;
    }

    // retrieve packages from AUR
    auto log = LogContext();
    auto handleAurResponse
        = [handler{ std::move(handler) }, params{ std::move(params) }, document{ make_shared<RAPIDJSON_NAMESPACE::Document>(std::move(document)) },
              details](WebClient::AurQuerySession::ContainerType &&queriedAurPackages) mutable {
              std::vector<PackageSearchResult> aurPackageSearchResults;
              aurPackageSearchResults.reserve(queriedAurPackages.size());
              auto configLock = params.setup.config.lockToRead();
              auto documentArray = document->GetArray();
              if (details) {
                  for (auto &package : queriedAurPackages) {
                      ReflectiveRapidJSON::JsonReflector::push(std::move(package), documentArray, document->GetAllocator());
                  }
              } else if (!queriedAurPackages.empty()) {
                  for (auto &package : queriedAurPackages) {
                      ReflectiveRapidJSON::JsonReflector::push(
                          PackageSearchResult{ params.setup.config.aur, std::move(package) }, documentArray, document->GetAllocator());
                  }
              }
              configLock.unlock();
              handler(makeJson(params.request(), *document, params.target.hasPrettyFlag()));
          };
    if (mode == Mode::Name) {
        WebClient::queryAurPackages(log, params.setup, neededAurPackages, params.setup.webServer.ioContext, std::move(handleAurResponse));
    } else {
        auto session = WebClient::AurQuerySession::create(params.setup.webServer.ioContext, std::move(handleAurResponse));
        for (const auto &searchTerm : neededAurPackages) {
            WebClient::searchAurPackages(log, params.setup, searchTerm, params.setup.webServer.ioContext, session);
        }
    }
}

void postLoadPackages(const Params &params, ResponseHandler &&handler)
{
    auto lock = params.setup.config.lockToWrite();
    params.setup.config.loadAllPackages(params.target.hasFlag("with-files"));
    lock.unlock();
    handler(makeText(params.request(), "packages loaded"));
}

void postDumpCacheFile(const Params &params, ResponseHandler &&handler)
{
    auto configLock = params.setup.config.lockToRead();
    auto buildLock = params.setup.building.lockToRead();
    const auto size = params.setup.saveState();
    buildLock.unlock();
    configLock.unlock();
    handler(makeText(params.request(), "cache file written (" % dataSizeToString(size) + ')'));
}

void postQuit(const Params &params, ResponseHandler &&handler)
{
    cerr << Phrases::SuccessMessage << "Stopping via route /quit" << endl;
    Server::stop();
    handler(makeText(params.request(), "stopping"));
}

void getBuildActions(const Params &params, ResponseHandler &&handler)
{
    RAPIDJSON_NAMESPACE::Document jsonDoc(RAPIDJSON_NAMESPACE::kArrayType);
    RAPIDJSON_NAMESPACE::Value::Array array(jsonDoc.GetArray());

    auto buildActionLock = params.setup.building.lockToRead();
    array.Reserve(ReflectiveRapidJSON::JsonReflector::rapidJsonSize(params.setup.building.actions.size()), jsonDoc.GetAllocator());
    auto configLock = params.setup.config.lockToRead(); // build actions might refer to "config things" like packages
    for (const auto &buildAction : params.setup.building.actions) {
        if (!buildAction) {
            continue;
        }
        ReflectiveRapidJSON::JsonReflector::push(BuildActionBasicInfo(*buildAction), array, jsonDoc.GetAllocator());
    }
    configLock.unlock();
    buildActionLock.unlock();

    handler(makeJson(params.request(), jsonDoc, params.target.hasPrettyFlag()));
}

struct BuildActionSearchResult {
    struct ActionRef {
        std::uint64_t id;
        BuildAction *action = nullptr;
    };
    std::vector<ActionRef> actions;
    std::variant<std::monostate, std::shared_lock<std::shared_mutex>, std::unique_lock<std::shared_mutex>> lock;
    bool ok = false;
};

BuildActionSearchResult findBuildActions(const Params &params, ResponseHandler &&handler, bool acquireWriteLock = false, std::size_t maxIDs = 100)
{
    BuildActionSearchResult result;
    const auto idParams = params.target.decodeValues("id");
    if (idParams.empty()) {
        handler(makeBadRequest(params.request(), "need at least one build action ID"));
        return result;
    }
    if (idParams.size() > maxIDs) {
        handler(makeBadRequest(params.request(), "number of specified IDs exceeds limit"));
        return result;
    }

    result.actions.reserve(idParams.size());
    for (const auto &idString : idParams) {
        try {
            result.actions.emplace_back(BuildActionSearchResult::ActionRef{ .id = stringToNumber<std::size_t>(idString) });
        } catch (const ConversionException &) {
            handler(makeBadRequest(params.request(), "all IDs must be unsigned integers"));
            return result;
        }
    }

    if (acquireWriteLock) {
        result.lock = params.setup.building.lockToWrite();
    } else {
        result.lock = params.setup.building.lockToRead();
    }

    for (auto &actionRef : result.actions) {
        if (actionRef.id >= params.setup.building.actions.size()) {
            result.lock = std::monostate{};
            handler(makeBadRequest(params.request(), argsToString("no build action with specified ID \"", actionRef.id, "\" exists")));
            return result;
        }
        actionRef.action = params.setup.building.actions[actionRef.id].get();
        if (!actionRef.action) {
            handler(makeBadRequest(params.request(), argsToString("no build action with specified ID \"", actionRef.id, "\" exists")));
            return result;
        }
    }
    result.ok = true;
    return result;
}

void getBuildActionDetails(const Params &params, ResponseHandler &&handler)
{
    RAPIDJSON_NAMESPACE::Document jsonDoc(RAPIDJSON_NAMESPACE::kArrayType);
    RAPIDJSON_NAMESPACE::Value::Array array(jsonDoc.GetArray());
    auto buildActionsSearchResult = findBuildActions(params, std::move(handler));
    if (!buildActionsSearchResult.ok) {
        return;
    }
    for (const auto &buildActionRef : buildActionsSearchResult.actions) {
        ReflectiveRapidJSON::JsonReflector::push(*buildActionRef.action, array, jsonDoc.GetAllocator());
    }
    buildActionsSearchResult.lock = std::monostate{};
    handler(makeJson(params.request(), jsonDoc, params.target.hasPrettyFlag()));
}

void getBuildActionOutput(const Params &params, ResponseHandler &&handler)
{
    const auto offsetParams = params.target.decodeValues("offset");
    std::size_t offset = 0;
    if (offsetParams.size() > 1) {
        handler(makeBadRequest(params.request(), "the offset parameter must be specified at most once"));
        return;
    }
    try {
        offset = stringToNumber<std::size_t>(offsetParams.front());
    } catch (const ConversionException &) {
        handler(makeBadRequest(params.request(), "the offset must be an unsigned integer"));
        return;
    }
    auto buildActionsSearchResult = findBuildActions(params, std::move(handler), false, 1);
    if (!buildActionsSearchResult.ok) {
        return;
    }
    auto buildAction = buildActionsSearchResult.actions.front().action;
    if (offset > buildAction->output.size()) {
        buildActionsSearchResult.lock = std::monostate{};
        handler(makeBadRequest(params.request(), "the offset must not exceed the output size"));
        return;
    }
    buildActionsSearchResult.lock = std::monostate{};
    buildAction->streamOutput(params, offset);
}

static std::string readNameParam(const Params &params, ResponseHandler &&handler)
{
    const auto nameParams = params.target.decodeValues("name");
    if (nameParams.size() != 1) {
        handler(makeBadRequest(params.request(), "name must be specified exactly one time"));
        return std::string();
    }
    if (nameParams.front().empty()) {
        handler(makeBadRequest(params.request(), "the name parameter must not be empty"));
        return std::string();
    }
    return nameParams.front();
}

static void getBuildActionFile(const Params &params, std::vector<std::string> BuildAction::*fileList, ResponseHandler &&handler)
{
    const auto name = readNameParam(params, std::move(handler));
    if (name.empty()) {
        return;
    }
    auto buildActionsSearchResult = findBuildActions(params, std::move(handler), false, 1);
    if (!buildActionsSearchResult.ok) {
        return;
    }

    auto *const buildAction = buildActionsSearchResult.actions.front().action;
    for (const auto &logFile : buildAction->*fileList) {
        if (name == logFile) {
            buildAction->streamFile(params, name, "application/octet-stream");
            return;
        }
    }
    handler(makeNotFound(params.request(), name));
}

void getBuildActionLogFile(const Params &params, ResponseHandler &&handler)
{
    getBuildActionFile(params, &BuildAction::logfiles, std::move(handler));
}

void getBuildActionArtefact(const Params &params, ResponseHandler &&handler)
{
    getBuildActionFile(params, &BuildAction::artefacts, std::move(handler));
}

void postBuildAction(const Params &params, ResponseHandler &&handler)
{
    // validate general parameters
    static const auto generalParameters = std::unordered_set<std::string_view>{ "type", "directory", "start-condition", "start-after-id",
        "source-repo", "destination-repo", "package", "pretty" };
    const auto typeParams = params.target.decodeValues("type");
    const auto taskParams = params.target.decodeValues("task");
    if (taskParams.size() + typeParams.size() != 1) {
        handler(makeBadRequest(params.request(), "need exactly either one type or one task parameter"));
        return;
    }
    auto directories = params.target.decodeValues("directory");
    if (directories.size() > 1) {
        handler(makeBadRequest(params.request(), "at most one directory can be specified"));
        return;
    }
    const auto startConditionValues = params.target.decodeValues("start-condition");
    if (startConditionValues.size() > 1) {
        handler(makeBadRequest(params.request(), "at most one start condition can be specified"));
        return;
    }
    const auto startImmediately = startConditionValues.empty() || startConditionValues.front() == "immediately";
    const auto startAfterAnotherAction = !startImmediately && startConditionValues.front() == "after";
    if (!startImmediately && !startAfterAnotherAction && startConditionValues.front() != "manually") {
        handler(makeBadRequest(params.request(), "specified start condition is not supported"));
        return;
    }
    const auto startAfterIdValues = params.target.decodeValues("start-after-id");
    auto startAfterIds = std::vector<BuildActionIdType>();
    startAfterIds.reserve(startAfterIdValues.size());
    for (const auto &startAfterIdValue : startAfterIdValues) {
        if (startAfterIdValue.empty()) {
            continue;
        }
        try {
            startAfterIds.emplace_back(stringToNumber<BuildActionIdType>(startAfterIdValue));
        } catch (const ConversionException &) {
            handler(makeBadRequest(params.request(), "the specified ID to start after is not a valid build action ID"));
            return;
        }
    }
    if (startAfterAnotherAction) {
        if (startAfterIds.empty()) {
            handler(makeBadRequest(params.request(), "start condition is \"after\" but not exactly one \"start-after-id\" specified"));
            return;
        }
    } else if (!startAfterIdValues.empty() && (startAfterIdValues.size() > 1 || !startAfterIdValues.front().empty())) {
        handler(makeBadRequest(params.request(), "start condition is not \"after\" but \"start-after-id\" specified"));
        return;
    }

    // spawn multiple build actions for the specified task
    if (!taskParams.empty()) {
        postBuildActionsFromTask(params, std::move(handler), taskParams.front(), directories.empty() ? std::string() : directories.front(),
            startAfterIds, startImmediately);
        return;
    }

    // read type and type-specific flags and settings from parameters
    auto &metaInfo = params.setup.building.metaInfo;
    auto metaInfoLock = metaInfo.lockToRead();
    const auto &typeInfo = metaInfo.typeInfoForName(typeParams.front());
    const auto type = typeInfo.id;
    if (type == BuildActionType::Invalid) {
        handler(makeBadRequest(params.request(), "specified type is invalid"));
        return;
    }
    const auto &mapping = metaInfo.mappingForId(type);
    BuildActionFlagType buildActionFlags = noBuildActionFlags;
    std::unordered_map<std::string, std::string> buildActionSettings;
    for (const auto &[key, value] : params.target.params) {
        if (generalParameters.find(key) != generalParameters.end()) {
            continue; // skip general parameters here
        }
        if (const auto flag = mapping.flagInfoByName.find(key); flag != mapping.flagInfoByName.end()) {
            if (!value.empty() && value != "0") {
                buildActionFlags += flag->second.get().id;
            }
            continue;
        }
        if (mapping.settingInfoByName.find(key) != mapping.settingInfoByName.end()) {
            buildActionSettings[WebAPI::Url::decodeValue(key)] = WebAPI::Url::decodeValue(value);
            continue;
        }
        handler(makeBadRequest(params.request(), argsToString("parameter \"", key, "\" is not supported (by specified build action type)")));
        return;
    }
    metaInfoLock.unlock();

    // initialize build action
    auto buildLock = params.setup.building.lockToWrite();
    const auto id = params.setup.building.allocateBuildActionID();
    auto startsAfterBuildActions = params.setup.building.getBuildActions(startAfterIds);
    const auto startNow = startImmediately || (!startsAfterBuildActions.empty() && BuildAction::haveSucceeded(startsAfterBuildActions));
    buildLock.unlock();
    auto buildAction = std::make_shared<BuildAction>(id);
    if (!directories.empty()) {
        buildAction->directory = move(directories.front());
    }
    buildAction->type = type;
    buildAction->sourceDbs = params.target.decodeValues("source-repo");
    buildAction->destinationDbs = params.target.decodeValues("destination-repo");
    buildAction->packageNames = params.target.decodeValues("package");
    buildAction->startAfter = startAfterIds;
    buildAction->flags = buildActionFlags;
    buildAction->settings.swap(buildActionSettings);

    // serialize build action
    const auto response = buildAction->toJsonDocument();

    // start build action immediately (no locking required because build action is not part of setup-global list yet)
    if (startNow) {
        buildAction->start(params.setup);
    }

    // add build action to setup-global list and to "follow-up actions" of the build action this one should be started after
    auto buildLock2 = params.setup.building.lockToWrite();
    if (!startNow && !startsAfterBuildActions.empty()) {
        buildAction->startAfterOtherBuildActions(params.setup, startsAfterBuildActions);
    }
    params.setup.building.actions[id] = std::move(buildAction);
    buildLock2.unlock();

    handler(makeJson(params.request(), response));
}

static std::string allocateNewBuildAction(const BuildActionMetaInfo &metaInfo, const std::string &taskName,
    const std::vector<std::string> &packageNames, const std::string &directory,
    const std::unordered_map<std::string, BuildActionTemplate> &actionTemplates, std::vector<std::shared_ptr<BuildAction>> &newBuildActions,
    const std::string &actionName)
{
    const auto actionTemplateIterator = actionTemplates.find(actionName);
    if (actionTemplateIterator == actionTemplates.end()) {
        return "the action \"" % actionName + "\" of the specified task is not configured";
    }
    const auto &actionTemplate = actionTemplateIterator->second;
    const auto buildActionType = actionTemplate.type;
    if (!metaInfo.isTypeIdValid(buildActionType)) {
        return argsToString(
            "the type \"", static_cast<std::size_t>(buildActionType), "\" of action \"", actionName, "\" of the specified task is invalid");
    }
    const auto &typeInfo = metaInfo.typeInfoForId(actionTemplate.type);
    auto &buildAction = newBuildActions.emplace_back(std::make_shared<BuildAction>()); // a real ID is set later
    buildAction->taskName = taskName;
    buildAction->directory = !typeInfo.directory || directory.empty() ? actionTemplate.directory : directory;
    buildAction->type = buildActionType;
    buildAction->sourceDbs = actionTemplate.sourceDbs;
    buildAction->destinationDbs = actionTemplate.destinationDbs;
    buildAction->packageNames = !typeInfo.packageNames || packageNames.empty() ? actionTemplate.packageNames : packageNames;
    buildAction->flags = actionTemplate.flags;
    buildAction->settings = actionTemplate.settings;
    return std::string();
}

static std::string allocateNewBuildActionSequence(const BuildActionMetaInfo &metaInfo, const std::string &taskName,
    const std::vector<std::string> &packageNames, const std::string &directory,
    const std::unordered_map<std::string, BuildActionTemplate> &actionTemplates, std::vector<std::shared_ptr<BuildAction>> &newBuildActions,
    const BuildActionSequence &actionSequence)
{
    auto error = std::string();
    for (const auto &actionNode : actionSequence.actions) {
        if (const auto *const actionName = std::get_if<std::string>(&actionNode)) {
            error = allocateNewBuildAction(metaInfo, taskName, packageNames, directory, actionTemplates, newBuildActions, *actionName);
        } else if (const auto *const actionSequence = std::get_if<BuildActionSequence>(&actionNode)) {
            error = allocateNewBuildActionSequence(metaInfo, taskName, packageNames, directory, actionTemplates, newBuildActions, *actionSequence);
        }
        if (!error.empty()) {
            return error;
        }
    }
    return error;
}

void postBuildActionsFromTask(const Params &params, ResponseHandler &&handler, const std::string &taskName, const std::string &directory,
    const std::vector<BuildActionIdType> &startAfterIds, bool startImmediately)
{
    static_assert(std::is_same_v<std::size_t, BuildAction::IdType>);

    // show error when parameters are specified which are only supposed to be specified when starting a single build action
    if (!params.target.value("source-repo").empty()) {
        handler(makeBadRequest(params.request(), "the source repo can not be explicitely specified when a task is specified"));
        return;
    }
    if (!params.target.value("destination-repo").empty()) {
        handler(makeBadRequest(params.request(), "the destination repo can not be explicitely specified when a task is specified"));
        return;
    }

    // read other parameters
    const auto packageNames = params.target.decodeValues("package");

    // find task info
    auto setupLock = params.setup.lockToRead();
    const auto &presets = params.setup.building.presets;
    const auto taskIterator = presets.tasks.find(taskName);
    if (taskIterator == presets.tasks.end()) {
        setupLock.unlock();
        handler(makeBadRequest(params.request(), "the specified task is not configured"));
        return;
    }
    const auto &task = taskIterator->second;
    const auto &actionTemplates = presets.templates;
    if (task.actions.empty()) {
        setupLock.unlock();
        handler(makeBadRequest(params.request(), "the specified task has no actions configured"));
        return;
    }

    // allocate a vector to store build actions (temporarily) in
    auto newBuildActions = std::vector<std::shared_ptr<BuildAction>>();
    newBuildActions.reserve(task.actions.size());

    // copy data from templates into new build actions
    auto &metaInfo = params.setup.building.metaInfo;
    auto metaInfoLock = metaInfo.lockToRead();
    auto error = allocateNewBuildActionSequence(metaInfo, taskName, packageNames, directory, actionTemplates, newBuildActions, task);
    metaInfoLock.unlock();
    setupLock.unlock();
    if (!error.empty()) {
        handler(makeBadRequest(params.request(), std::move(error)));
        return;
    }

    // allocate build action IDs and populate "start after ID"
    BuildAction *lastBuildAction = nullptr;
    auto &building = params.setup.building;
    auto buildLock = building.lockToWrite();
    auto startsAfterBuildActions = building.getBuildActions(startAfterIds);
    const auto startNow = startImmediately || (!startsAfterBuildActions.empty() && BuildAction::haveSucceeded(startsAfterBuildActions));
    for (auto &newBuildAction : newBuildActions) {
        newBuildAction->id = building.allocateBuildActionID();
        if (lastBuildAction) {
            newBuildAction->startAfter.emplace_back(lastBuildAction->id);
        } else {
            newBuildAction->startAfter = startAfterIds;
        }
        lastBuildAction = newBuildAction.get();
    }
    buildLock.unlock();

    // serialize build actions
    const auto response = ReflectiveRapidJSON::JsonReflector::toJsonDocument(newBuildActions);

    // start first build action immediately
    if (startNow) {
        newBuildActions.front()->start(params.setup);
    }

    // add build actions to setup-global list and populate "follow-up actions"
    buildLock = building.lockToWrite();
    lastBuildAction = nullptr;
    for (auto &newBuildAction : newBuildActions) {
        if (lastBuildAction) {
            if (lastBuildAction->hasSucceeded()) {
                newBuildAction->start(params.setup);
            } else {
                lastBuildAction->m_followUpActions.emplace_back(newBuildAction->weak_from_this());
            }
        } else if (!startsAfterBuildActions.empty()) {
            newBuildAction->startAfterOtherBuildActions(params.setup, startsAfterBuildActions);
        }
        lastBuildAction = newBuildAction.get();
        building.actions[lastBuildAction->id] = std::move(newBuildAction);
    }
    buildLock.unlock();

    handler(makeJson(params.request(), response));
}

void deleteBuildActions(const Params &params, ResponseHandler &&handler)
{
    auto buildActionsSearchResult = findBuildActions(params, std::move(handler), true, std::numeric_limits<std::size_t>::max());
    if (!buildActionsSearchResult.ok) {
        return;
    }
    for (const auto &actionRef : buildActionsSearchResult.actions) {
        if (!actionRef.action->isExecuting()) {
            continue;
        }
        buildActionsSearchResult.lock = std::monostate{};
        handler(
            makeBadRequest(params.request(), argsToString("can not delete \"", actionRef.id, "\"; it is still being executed; no actions altered")));
        return;
    }
    auto &actions = params.setup.building.actions;
    auto &invalidActions = params.setup.building.invalidActions;
    for (auto &actionRef : buildActionsSearchResult.actions) {
        for (auto &maybeFollowUpAction : actionRef.action->m_followUpActions) {
            if (auto followUpAction = maybeFollowUpAction.lock()) {
                auto &startAfter = followUpAction->startAfter;
                startAfter.erase(std::remove(startAfter.begin(), startAfter.end(), actionRef.id));
            }
        }
        actions[actionRef.id] = nullptr;
        invalidActions.emplace(actionRef.id);
    }
    if (!actions.empty()) {
        auto newActionsSize = actions.size();
        for (auto id = newActionsSize - 1;; --id) {
            if (actions[id]) {
                break;
            }
            newActionsSize = id;
            invalidActions.erase(id);
            if (!newActionsSize) {
                break;
            }
        }
        actions.resize(newActionsSize);
    }
    buildActionsSearchResult.lock = std::monostate{};
    handler(makeText(params.request(), "ok"));
}

void postCloneBuildActions(const Params &params, ResponseHandler &&handler)
{
    const auto startConditionValues = params.target.decodeValues("start-condition");
    if (startConditionValues.size() > 1) {
        handler(makeBadRequest(params.request(), "at most one start condition can be specified"));
        return;
    }
    const auto startImmediately = startConditionValues.empty() || startConditionValues.front() == "immediately";
    if (!startImmediately && startConditionValues.front() != "manually") {
        handler(makeBadRequest(params.request(), "specified start condition is not supported"));
        return;
    }
    auto buildActionsSearchResult = findBuildActions(params, std::move(handler), true);
    if (!buildActionsSearchResult.ok) {
        return;
    }
    for (const auto &actionRef : buildActionsSearchResult.actions) {
        if (actionRef.action->isDone()) {
            continue;
        }
        buildActionsSearchResult.lock = std::monostate{};
        handler(makeBadRequest(
            params.request(), argsToString("can not clone \"", actionRef.id, "\"; it is still scheduled or executed; no actions altered")));
        return;
    }
    std::vector<BuildAction::IdType> cloneIds;
    cloneIds.reserve(buildActionsSearchResult.actions.size());
    for (const auto &actionRef : buildActionsSearchResult.actions) {
        const auto orig = actionRef.action;
        const auto id = params.setup.building.allocateBuildActionID();
        auto clone = make_shared<BuildAction>(id);
        clone->directory = orig->directory;
        clone->packageNames = orig->packageNames;
        clone->sourceDbs = orig->sourceDbs;
        clone->destinationDbs = orig->destinationDbs;
        clone->extraParams = orig->extraParams;
        clone->settings = orig->settings;
        clone->flags = orig->flags;
        clone->type = orig->type;
        // transfer any follow-up actions which haven't already started yet from the original build action to the new one
        // TODO: It would be cool to have a "recursive flag" which would allow restarting follow-ups.
        clone->m_followUpActions.reserve(orig->m_followUpActions.size());
        for (auto &maybeOrigFollowUp : orig->m_followUpActions) {
            auto origFollowUp = maybeOrigFollowUp.lock();
            if (!origFollowUp || !origFollowUp->isScheduled()) {
                continue;
            }
            for (auto &startAfterId : origFollowUp->startAfter) {
                if (startAfterId == orig->id) {
                    startAfterId = id;
                }
            }
            clone->m_followUpActions.emplace_back(origFollowUp);
        }
        orig->m_followUpActions.clear();
        if (startImmediately) {
            clone->start(params.setup);
        }
        params.setup.building.actions[id] = move(clone);
        cloneIds.emplace_back(id);
    }
    buildActionsSearchResult.lock = std::monostate{};
    handler(makeJson(params.request(), cloneIds));
}

void postStartBuildActions(const Params &params, ResponseHandler &&handler)
{
    auto buildActionsSearchResult = findBuildActions(params, std::move(handler), true);
    if (!buildActionsSearchResult.ok) {
        return;
    }
    for (const auto &actionRef : buildActionsSearchResult.actions) {
        if (actionRef.action->isScheduled()) {
            continue;
        }
        buildActionsSearchResult.lock = std::monostate{};
        handler(
            makeBadRequest(params.request(), argsToString("can not start \"", actionRef.id, "\"; it has already been started; no actions altered")));
        return;
    }
    for (auto &actionRef : buildActionsSearchResult.actions) {
        actionRef.action->start(params.setup);
    }
    buildActionsSearchResult.lock = std::monostate{};
    handler(makeText(params.request(), "ok"));
}

void postStopBuildActions(const Params &params, ResponseHandler &&handler)
{
    auto buildActionsSearchResult = findBuildActions(params, std::move(handler), true);
    if (!buildActionsSearchResult.ok) {
        return;
    }
    for (const auto &actionRef : buildActionsSearchResult.actions) {
        if (actionRef.action->isExecuting()) {
            continue;
        }
        buildActionsSearchResult.lock = std::monostate{};
        handler(makeBadRequest(
            params.request(), argsToString("can not stop/decline \"", actionRef.id, "\"; it is not being executed; no actions altered")));
        return;
    }
    for (auto &actionRef : buildActionsSearchResult.actions) {
        actionRef.action->abort();
        if (actionRef.action->status == BuildActionStatus::Running) {
            // can not immediately stop a running action; the action needs to terminate itself acknowledging the aborted flag
            continue;
        }
        actionRef.action->status = BuildActionStatus::Finished;
        if (actionRef.action->status == BuildActionStatus::AwaitingConfirmation) {
            actionRef.action->result = BuildActionResult::ConfirmationDeclined;
        } else {
            actionRef.action->result = BuildActionResult::Aborted;
        }
    }
    buildActionsSearchResult.lock = std::monostate{};
    handler(makeText(params.request(), "ok"));
}

} // namespace Routes
} // namespace WebAPI
} // namespace LibRepoMgr
