#define CPP_UTILITIES_PATHHELPER_STRING_VIEW

#include "./params.h"
#include "./render.h"
#include "./routes.h"

#include "../serversetup.h"

#include <c++utilities/conversion/stringbuilder.h>
#include <c++utilities/io/path.h>

#include <algorithm>
#include <variant>

using namespace std;
using namespace CppUtilities;
using namespace LibRepoMgr::WebAPI::Render;
using namespace LibPkg;

namespace LibRepoMgr {
namespace WebAPI {
namespace Routes {

void getBuildActions(const Params &params, ResponseHandler &&handler)
{
    RAPIDJSON_NAMESPACE::Document jsonDoc(RAPIDJSON_NAMESPACE::kArrayType);
    RAPIDJSON_NAMESPACE::Value::Array array(jsonDoc.GetArray());

    const auto start = params.target.asNumber<std::size_t>("start", std::numeric_limits<std::size_t>::max());
    const auto serverLimit = params.setup.webServer.buildActionsResponseLimit.load();
    auto limit = params.target.asNumber<std::size_t>("limit");
    if (!limit || limit > serverLimit) {
        limit = serverLimit;
    }

    auto buildActionLock = params.setup.building.lockToRead();
    params.setup.building.forEachBuildAction(
        [&array, &jsonDoc](std::size_t count) { array.Reserve(ReflectiveRapidJSON::JsonReflector::rapidJsonSize(count), jsonDoc.GetAllocator()); },
        [&array, &jsonDoc, limit](LibPkg::StorageID, BuildActionBase &&buildActionBase) {
            ReflectiveRapidJSON::JsonReflector::push(buildActionBase, array, jsonDoc.GetAllocator());
            return array.Size() >= limit;
        },
        limit, start);
    buildActionLock.unlock();

    handler(makeJson(params.request(), jsonDoc, params.target.hasPrettyFlag()));
}

struct BuildActionSearchResult {
    std::vector<BuildActionIdType> specifiedIds;
    std::vector<std::shared_ptr<BuildAction>> actions;
    std::variant<std::monostate, std::shared_lock<std::shared_mutex>, std::unique_lock<std::shared_mutex>> lock;
    bool ok = false;
};

BuildActionSearchResult findBuildActions(const Params &params, ResponseHandler &&handler, bool acquireWriteLock = false, std::size_t maxIDs = 100)
{
    auto result = BuildActionSearchResult();
    const auto idParams = params.target.decodeValues("id");
    if (idParams.empty()) {
        handler(makeBadRequest(params.request(), "need at least one build action ID"));
        return result;
    }
    if (idParams.size() > maxIDs) {
        handler(makeBadRequest(params.request(), "number of specified IDs exceeds limit"));
        return result;
    }

    result.specifiedIds.reserve(idParams.size());
    for (const auto &idString : idParams) {
        try {
            result.specifiedIds.emplace_back(stringToNumber<BuildActionIdType>(idString));
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

    result.actions.reserve(result.specifiedIds.size());
    for (const auto id : result.specifiedIds) {
        auto action = params.setup.building.getBuildAction(id);
        if (!action) {
            result.lock = std::monostate{};
            handler(makeBadRequest(params.request(), argsToString("no build action with specified ID \"", id, "\" exists")));
            return result;
        }
        result.actions.emplace_back(std::move(action));
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
    for (const auto &action : buildActionsSearchResult.actions) {
        ReflectiveRapidJSON::JsonReflector::push(*action, array, jsonDoc.GetAllocator());
    }
    buildActionsSearchResult.lock = std::monostate{};
    handler(makeJson(params.request(), jsonDoc, params.target.hasPrettyFlag()));
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

static void getBuildActionFile(
    const Params &params, std::vector<std::string> BuildAction::*fileList, ResponseHandler &&handler, bool determineMimeType)
{
    const auto name = readNameParam(params, std::move(handler));
    if (name.empty()) {
        return;
    }
    auto mimeType = boost::beast::string_view("application/octet-stream");
    auto contentDisposition = std::string();
    if (determineMimeType) {
        if (const auto detectedMimeType = Session::determineMimeType(name, boost::beast::string_view()); !detectedMimeType.empty()) {
            mimeType = detectedMimeType;
        } else {
            auto fileName = CppUtilities::fileName(name);
            findAndReplace(fileName, "\"", "");
            contentDisposition = "attachment; filename=\"" % fileName + "\"";
        }
    }
    auto buildActionsSearchResult = findBuildActions(params, std::move(handler), false, 1);
    if (!buildActionsSearchResult.ok) {
        return;
    }

    auto &buildAction = buildActionsSearchResult.actions.front();
    for (const auto &logFile : (*buildAction).*fileList) {
        if (name == logFile) {
            buildAction->streamFile(params, name, mimeType, contentDisposition);
            return;
        }
    }
    handler(makeNotFound(params.request(), name));
}

void getBuildActionLogFile(const Params &params, ResponseHandler &&handler)
{
    getBuildActionFile(params, &BuildAction::logfiles, std::move(handler), false);
}

void getBuildActionArtefact(const Params &params, ResponseHandler &&handler)
{
    getBuildActionFile(params, &BuildAction::artefacts, std::move(handler), true);
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
            startAfterIds.emplace_back(stringToNumber<LibPkg::StorageID>(startAfterIdValue));
        } catch (const ConversionException &) {
            handler(makeBadRequest(params.request(), "the specified ID to start after is not a valid build action ID"));
            return;
        }
    }
    if (startAfterAnotherAction) {
        if (startAfterIds.empty()) {
            handler(makeBadRequest(params.request(), "start condition is \"after\" but not at least one \"start-after-id\" specified"));
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
    buildLock.unlock();
    auto buildAction = std::make_shared<BuildAction>(id, &params.setup);
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

    // start build action immediately or just add to setup-global list for now
    auto buildLock2 = params.setup.building.lockToWrite();
    if (startImmediately) {
        buildAction->start(params.setup, params.session.secrets());
    } else {
        params.setup.building.storeBuildAction(buildAction);
    }
    buildLock2.unlock();

    handler(makeJson(params.request(), response));
}

struct SequencedBuildActions {
    std::string_view name;
    std::vector<std::variant<std::shared_ptr<BuildAction>, SequencedBuildActions>> actions;
    bool concurrent = false;
};

static std::string allocateNewBuildAction(const BuildActionMetaInfo &metaInfo, const std::string &taskName,
    const std::vector<std::string> &packageNames, const std::string &directory,
    const std::unordered_map<std::string, BuildActionTemplate> &actionTemplates, SequencedBuildActions &newActionSequence,
    std::vector<std::shared_ptr<BuildAction>> &allocatedActions, const std::string &actionTemplateToAllocate, bool asFirstActions)
{
    const auto actionTemplateIterator = actionTemplates.find(actionTemplateToAllocate);
    if (actionTemplateIterator == actionTemplates.end()) {
        return "the action \"" % actionTemplateToAllocate + "\" of the specified task is not configured";
    }
    const auto &actionTemplate = actionTemplateIterator->second;
    const auto buildActionType = actionTemplate.type;
    if (!metaInfo.isTypeIdValid(buildActionType)) {
        return argsToString("the type \"", static_cast<std::size_t>(buildActionType), "\" of action \"", actionTemplateToAllocate,
            "\" of the specified task is invalid");
    }
    const auto &typeInfo = metaInfo.typeInfoForId(actionTemplate.type);
    auto &allocatedBuildAction = allocatedActions.emplace_back(std::make_shared<BuildAction>());
    auto *const buildAction = std::get<std::shared_ptr<BuildAction>>(newActionSequence.actions.emplace_back(allocatedBuildAction)).get();
    buildAction->taskName = taskName;
    buildAction->templateName = actionTemplateToAllocate;
    buildAction->directory = !typeInfo.directory || directory.empty() ? actionTemplate.directory : directory;
    buildAction->type = buildActionType;
    buildAction->sourceDbs = actionTemplate.sourceDbs;
    buildAction->destinationDbs = actionTemplate.destinationDbs;
    if (asFirstActions || !typeInfo.implyPackagesFromPrevAction) {
        buildAction->packageNames = !typeInfo.packageNames || packageNames.empty() ? actionTemplate.packageNames : packageNames;
    }
    buildAction->flags = actionTemplate.flags;
    buildAction->settings = actionTemplate.settings;
    return std::string();
}

static std::string allocateNewBuildActionSequence(const BuildActionMetaInfo &metaInfo, const std::string &taskName,
    const std::vector<std::string> &packageNames, const std::string &directory,
    const std::unordered_map<std::string, BuildActionTemplate> &actionTemplates, SequencedBuildActions &newActionSequence,
    std::vector<std::shared_ptr<BuildAction>> &allocatedActions, const BuildActionSequence &actionSequenceToAllocate, bool asFirstActions = true)
{
    auto error = std::string();
    newActionSequence.name = actionSequenceToAllocate.name;
    newActionSequence.concurrent = actionSequenceToAllocate.concurrent;
    for (const auto &actionNode : actionSequenceToAllocate.actions) {
        if (const auto *const actionTemplateName = std::get_if<std::string>(&actionNode)) {
            error = allocateNewBuildAction(metaInfo, taskName, packageNames, directory, actionTemplates, newActionSequence, allocatedActions,
                *actionTemplateName, newActionSequence.concurrent ? asFirstActions : newActionSequence.actions.empty());
        } else if (const auto *const actionSequence = std::get_if<BuildActionSequence>(&actionNode)) {
            error = allocateNewBuildActionSequence(metaInfo, taskName, packageNames, directory, actionTemplates,
                std::get<SequencedBuildActions>(newActionSequence.actions.emplace_back(SequencedBuildActions())), allocatedActions, *actionSequence,
                newActionSequence.concurrent ? asFirstActions : false);
        }
        if (!error.empty()) {
            return error;
        }
    }
    return error;
}

static std::vector<std::shared_ptr<BuildAction>> allocateBuildActionIDs(ServiceSetup &setup,
    const std::vector<std::shared_ptr<BuildAction>> &startAfterActions, const std::vector<std::shared_ptr<BuildAction>> &parentLevelActions,
    SequencedBuildActions &newActionSequence)
{
    auto previousActions = std::vector<std::shared_ptr<BuildAction>>();
    for (auto &sequencedAction : newActionSequence.actions) {
        // make concurrent actions depend on the parent-level actions (or actions specified via start after ID parameters on top-level)
        // make the first action within a sequence depend on the parent-level actions (or actions specified via start after ID parameters on top-level)
        // make further actions within a sequence depend on the previous action within the sequence
        const auto &relevantLastBuildActions = newActionSequence.concurrent || previousActions.empty() ? parentLevelActions : previousActions;
        if (auto *const action = std::get_if<std::shared_ptr<BuildAction>>(&sequencedAction)) {
            (*action)->id = setup.building.allocateBuildActionID();
            (*action)->assignStartAfter(relevantLastBuildActions.empty() ? startAfterActions : relevantLastBuildActions);
            if (!newActionSequence.concurrent) {
                previousActions.clear(); // only the last action within the sequence is relevant
            }
            previousActions.emplace_back(*action);
        } else if (auto *const subSequence = std::get_if<SequencedBuildActions>(&sequencedAction)) {
            auto newSubActions = allocateBuildActionIDs(setup, startAfterActions, relevantLastBuildActions, *subSequence);
            if (!newSubActions.empty()) {
                if (!newActionSequence.concurrent) {
                    previousActions.clear(); // only the last action within the sequence is relevant
                }
                if (subSequence->concurrent) {
                    // consider all new sub-actions from last level relevant when they're concurrent
                    previousActions.insert(previousActions.end(), newSubActions.begin(), newSubActions.end());
                } else {
                    previousActions.emplace_back(newSubActions.back());
                }
            }
        }
    }
    return previousActions;
}

static bool startFirstBuildActions(const Params &params, SequencedBuildActions &newActionSequence)
{
    auto handledFirstAction = false;
    for (auto &sequencedAction : newActionSequence.actions) {
        if (auto *const maybeAction = std::get_if<std::shared_ptr<BuildAction>>(&sequencedAction)) {
            auto &action = *maybeAction;
            handledFirstAction = true;
            if (action->isScheduled()) {
                action->start(params.setup, params.session.secrets());
            }
            if (!newActionSequence.concurrent) {
                return true;
            }
        } else if (auto *const subSequence = std::get_if<SequencedBuildActions>(&sequencedAction)) {
            if (startFirstBuildActions(params, *subSequence) && !newActionSequence.concurrent) {
                return true;
            }
        }
    }
    return handledFirstAction;
}

void postBuildActionsFromTask(const Params &params, ResponseHandler &&handler, const std::string &taskName, const std::string &directory,
    const std::vector<BuildActionIdType> &startAfterIds, bool startImmediately)
{
    static_assert(std::is_same_v<std::size_t, BuildAction::IdType>);

    // show error when parameters are specified which are only supposed to be specified when starting a single build action
    if (!params.target.value("source-repo").empty()) {
        handler(makeBadRequest(params.request(), "the source repo can not be explicitly specified when a task is specified"));
        return;
    }
    if (!params.target.value("destination-repo").empty()) {
        handler(makeBadRequest(params.request(), "the destination repo can not be explicitly specified when a task is specified"));
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
    auto newActionSequence = SequencedBuildActions();
    auto allocatedActions = std::vector<std::shared_ptr<BuildAction>>();
    auto parentLevelActions = std::vector<std::shared_ptr<BuildAction>>();
    newActionSequence.actions.reserve(task.actions.size());
    allocatedActions.reserve(task.actions.size());

    // copy data from templates into new build actions
    auto &metaInfo = params.setup.building.metaInfo;
    auto metaInfoLock = metaInfo.lockToRead();
    auto error
        = allocateNewBuildActionSequence(metaInfo, taskName, packageNames, directory, actionTemplates, newActionSequence, allocatedActions, task);
    metaInfoLock.unlock();
    setupLock.unlock();
    if (!error.empty()) {
        handler(makeBadRequest(params.request(), std::move(error)));
        return;
    }

    // allocate build action IDs and populate "start after ID" and follow-up actions
    auto &building = params.setup.building;
    auto buildLock = building.lockToWrite();
    auto startsAfterBuildActions = building.getBuildActions(startAfterIds);
    allocateBuildActionIDs(params.setup, startsAfterBuildActions, parentLevelActions, newActionSequence);
    buildLock.unlock();

    // serialize build actions
    auto buildReadLock = building.lockToRead();
    const auto startNow = startImmediately || (!startsAfterBuildActions.empty() && BuildAction::haveSucceeded(startsAfterBuildActions));
    const auto response = ReflectiveRapidJSON::JsonReflector::toJsonDocument(allocatedActions);

    // start first build action immediately (read-lock sufficient because build action not part of setup-global list yet)
    if (startNow) {
        startFirstBuildActions(params, newActionSequence);
    }

    // add build actions to setup-global list
    buildLock = building.lockToWrite(buildReadLock);
    for (auto &newAction : allocatedActions) {
        building.storeBuildAction(newAction);
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
    for (const auto &action : buildActionsSearchResult.actions) {
        if (!action->isExecuting()) {
            continue;
        }
        buildActionsSearchResult.lock = std::monostate{};
        handler(
            makeBadRequest(params.request(), argsToString("can not delete \"", action->id, "\"; it is still being executed; no actions altered")));
        return;
    }
    params.setup.building.deleteBuildAction(buildActionsSearchResult.actions);
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
    for (const auto &action : buildActionsSearchResult.actions) {
        if (action->isDone()) {
            continue;
        }
        buildActionsSearchResult.lock = std::monostate{};
        handler(makeBadRequest(
            params.request(), argsToString("can not clone \"", action->id, "\"; it is still scheduled or executed; no actions altered")));
        return;
    }
    auto cloneIds = std::vector<BuildAction::IdType>();
    cloneIds.reserve(buildActionsSearchResult.actions.size());
    for (const auto &orig : buildActionsSearchResult.actions) {
        const auto id = params.setup.building.allocateBuildActionID();
        auto clone = make_shared<BuildAction>(id);
        clone->taskName = orig->taskName.empty() ? "Clone" : (orig->taskName.starts_with("Clone") ? orig->taskName : "Clone of " + orig->taskName);
        clone->directory = orig->directory;
        clone->packageNames = orig->packageNames;
        clone->sourceDbs = orig->sourceDbs;
        clone->destinationDbs = orig->destinationDbs;
        clone->settings = orig->settings;
        clone->flags = orig->flags;
        clone->type = orig->type;
        clone->startAfter = orig->startAfter;
        if (startImmediately) {
            clone->start(params.setup, params.session.secrets());
        }
        params.setup.building.storeBuildAction(std::move(clone));
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
    for (const auto &action : buildActionsSearchResult.actions) {
        if (action->isScheduled()) {
            continue;
        }
        buildActionsSearchResult.lock = std::monostate{};
        handler(
            makeBadRequest(params.request(), argsToString("can not start \"", action->id, "\"; it has already been started; no actions altered")));
        return;
    }
    for (auto &action : buildActionsSearchResult.actions) {
        action->start(params.setup, params.session.secrets());
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
    for (const auto &action : buildActionsSearchResult.actions) {
        if (action->isExecuting()) {
            continue;
        }
        buildActionsSearchResult.lock = std::monostate{};
        handler(makeBadRequest(
            params.request(), argsToString("can not stop/decline \"", action->id, "\"; it is not being executed; no actions altered")));
        return;
    }
    for (auto &action : buildActionsSearchResult.actions) {
        action->abort(true);
        if (action->status == BuildActionStatus::Running) {
            // can not immediately stop a running action; the action needs to terminate itself acknowledging the aborted flag
            continue;
        }
        if (action->status == BuildActionStatus::AwaitingConfirmation) {
            action->result = BuildActionResult::ConfirmationDeclined;
        } else {
            action->result = BuildActionResult::Aborted;
        }
        action->status = BuildActionStatus::Finished;
        params.setup.building.storeBuildAction(action);
    }
    buildActionsSearchResult.lock = std::monostate{};
    handler(makeText(params.request(), "ok"));
}

} // namespace Routes
} // namespace WebAPI
} // namespace LibRepoMgr
