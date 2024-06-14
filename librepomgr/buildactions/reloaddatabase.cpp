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
    const auto flags = static_cast<ReloadDatabaseFlags>(m_buildAction->flags);
    const auto force = flags & ReloadDatabaseFlags::ForceReload;
    const auto withFiles = m_setup.building.loadFilesDbs;
    vector<LibPkg::Database *> dbsToLoadFromMirror;

    auto configReadLock
        = init(BuildActionAccess::ReadConfig, RequiredDatabases::MaybeDestination | RequiredDatabases::AllowToAur, RequiredParameters::None);
    if (holds_alternative<monostate>(configReadLock)) {
        return;
    }

    auto session
        = WebClient::DatabaseQuerySession::create(m_setup.building.ioContext, [this](WebClient::DatabaseQuerySession::ContainerType &&failedDbs) {
              // save state for "lastUpdate" timestamps
              auto configReadLock2 = m_setup.config.lockToRead();
              m_setup.saveState();
              configReadLock2.unlock();

              // conclude build action
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
            m_setup.building.ioContext.get_executor(), [this, force, session, dbName = db->name, dbArch = db->arch, dbPath = std::move(dbPath)]() mutable {
                try {
                    auto configLock = m_setup.config.lockToRead();
                    auto dbFileLock = m_setup.locks.acquireToRead(m_buildAction->log(), ServiceSetup::Locks::forDatabase(dbName, dbArch));
                    const auto lastModified = LibPkg::lastModified(dbPath);
                    if (!force) {
                        auto configReadLock2 = m_setup.config.lockToRead();
                        auto *const destinationDb = m_setup.config.findDatabase(dbName, dbArch);
                        if (const auto lastUpdate = destinationDb->lastUpdate.load(); lastModified <= lastUpdate) {
                            configReadLock2.unlock();
                            m_buildAction->appendOutput(Phrases::InfoMessage, "Skip loading database \"", dbName, '@', dbArch,
                                "\" from local file \"", dbPath, "\"; last modification time <= last update (", lastModified.toString(), '<', '=',
                                lastUpdate.toString(), ')', '\n');
                            return;
                        }
                    }

                    m_buildAction->appendOutput(
                        Phrases::InfoMessage, "Loading database \"", dbName, '@', dbArch, "\" from local file \"", dbPath, "\"\n");

                    auto *const destinationDb = m_setup.config.findDatabase(dbName, dbArch);
                    if (!destinationDb) {
                        configLock.unlock();
                        m_buildAction->appendOutput(
                            Phrases::ErrorMessage, "Loaded database file for \"", dbName, '@', dbArch, "\" but it no longer exists; discarding\n");
                        session->addResponse(std::move(dbName));
                        return;
                    }

                    auto updater = LibPkg::PackageUpdater(*destinationDb, true);
                    updater.insertFromDatabaseFile(dbPath);
                    dbFileLock.lock().unlock();
                    updater.commit();
                    destinationDb->lastUpdate = lastModified;
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
    WebClient::queryDatabases(m_buildAction->log(), m_setup, std::move(query.queryParamsForDbs), session, force);
    m_preparationFailures = std::move(query.failedDbs);

    // clear AUR cache
    if (m_toAur) {
        auto lock = m_setup.config.lockToRead();
        m_setup.config.aur.clearPackages();
        lock.unlock();
        m_buildAction->log()(Phrases::InfoMessage, "Cleared AUR cache\n");
    }
}

} // namespace LibRepoMgr
