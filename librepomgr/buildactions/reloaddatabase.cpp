#include "./buildactionprivate.h"

#include "../helper.h"
#include "../logging.h"
#include "../serversetup.h"

#include "../webclient/database.h"

#include "../webapi/params.h"

#include <c++utilities/conversion/stringbuilder.h>
#include <c++utilities/io/ansiescapecodes.h>

#include <boost/asio/post.hpp>

#include <iostream>

using namespace std;
using namespace CppUtilities;
using namespace CppUtilities::EscapeCodes;

namespace LibRepoMgr {

ReloadDatabase::ReloadDatabase(ServiceSetup &setup, const std::shared_ptr<BuildAction> &buildAction)
    : InternalBuildAction(setup, buildAction)
{
}

void ReloadDatabase::run()
{
    const auto withFiles = m_setup.building.loadFilesDbs;
    vector<LibPkg::Database *> dbsToLoadFromMirror;

    auto configReadLock
        = init(BuildActionAccess::ReadConfig, RequiredDatabases::MaybeDestination | RequiredDatabases::AllowToAur, RequiredParameters::None);
    if (holds_alternative<monostate>(configReadLock)) {
        return;
    }

    auto session
        = WebClient::DatabaseQuerySession::create(m_setup.building.ioContext, [this](WebClient::DatabaseQuerySession::ContainerType &&failedDbs) {
              if (!m_preparationFailures.empty()) {
                  mergeSecondVectorIntoFirstVector(failedDbs, m_preparationFailures);
              }
              if (!failedDbs.empty()) {
                  m_hasError = true;
                  reportError("Failed to reload the following databases, see logs for details: " + joinStrings(failedDbs, ", "));
                  return;
              }
              if (m_hasError) {
                  reportError("Errors occurred, see output log for details.");
                  return;
              }
              auto buildActionWriteLock = m_setup.building.lockToWrite();
              reportSuccess();
          });

    for (const auto &db : m_destinationDbs) {
        // add database for syncing with mirror
        if (db->syncFromMirror) {
            dbsToLoadFromMirror.emplace_back(db);
            continue;
        }
        // post job to reload database
        auto dbPath = withFiles ? db->filesPath : db->path;
        if (dbPath.empty()) {
            m_hasError = true;
            m_buildAction->appendOutput(
                Phrases::ErrorMessage, "Unable to reload database database ", db->name, '@', db->arch, ": no path configured\n");
            continue;
        }
        boost::asio::post(
            m_setup.building.ioContext.get_executor(), [this, session, dbName = db->name, dbArch = db->arch, dbPath = move(dbPath)]() mutable {
                m_buildAction->appendOutput(
                    Phrases::InfoMessage, "Loading database \"", dbName, '@', dbArch, "\" from local file \"", dbPath, "\"\n");
                try {
                    const auto lastModified = LibPkg::lastModified(dbPath);
                    auto dbFile = LibPkg::extractFiles(dbPath, &LibPkg::Database::isFileRelevant);
                    auto packages = LibPkg::Package::fromDatabaseFile(move(dbFile));
                    auto lock = m_setup.config.lockToWrite();
                    auto db = m_setup.config.findDatabase(dbName, dbArch);
                    if (!db) {
                        m_buildAction->appendOutput(
                            Phrases::ErrorMessage, "Loaded database file for \"", dbName, '@', dbArch, "\" but it no longer exists; discarding\n");
                        session->addResponse(std::move(dbName));
                        return;
                    }
                    db->replacePackages(packages, lastModified);
                } catch (const std::runtime_error &e) {
                    m_buildAction->appendOutput(Phrases::ErrorMessage, "An error occurred when reloading database \"", dbName, '@', dbArch,
                        "\" from local file \"", dbPath, "\": ", e.what(), '\n');
                    session->addResponse(std::move(dbName));
                }
            });
    }

    // query databases
    auto query = WebClient::prepareDatabaseQuery(m_buildAction->log(), dbsToLoadFromMirror, withFiles);
    std::get<std::shared_lock<std::shared_mutex>>(configReadLock).unlock();
    WebClient::queryDatabases(m_buildAction->log(), m_setup, std::move(query.queryParamsForDbs), session);
    m_preparationFailures = std::move(query.failedDbs);

    // clear AUR cache
    if (m_toAur) {
        auto lock = m_setup.config.lockToWrite();
        m_setup.config.aur.clearPackages();
        lock.unlock();
        m_buildAction->log()(Phrases::InfoMessage, "Cleared AUR cache\n");
    }
}

} // namespace LibRepoMgr
