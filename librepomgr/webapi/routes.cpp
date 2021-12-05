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
            for (const auto &[packageSpec, unresolvedDependencies] : unresolvedPackages) {
                JR::push(unresolvedDependencies, packageSpec.pkg->name.data(), obj, document.GetAllocator());
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
                if (const auto [aurPackageID, aurPackage] = aurDb.findPackageWithID(packageNameStr);
                    aurPackage && (!details || aurPackage->origin != PackageOrigin::AurRpcSearch)) {
                    aurPackages.emplace_back(aurDb, aurPackage, aurPackageID);
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
            try {
                pushPackages(params.setup.config.findPackages(std::regex(name.data(), name.size())));
            } catch (const std::regex_error &e) {
                throw BadRequest(argsToString("regex is invalid: ", e.what()));
            }
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
                  for (auto &[packageID, package] : queriedAurPackages) {
                      ReflectiveRapidJSON::JsonReflector::push(
                          PackageSearchResult{ params.setup.config.aur, std::move(package), packageID }, documentArray, document->GetAllocator());
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

} // namespace Routes
} // namespace WebAPI
} // namespace LibRepoMgr
