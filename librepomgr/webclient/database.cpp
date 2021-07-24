#include "./database.h"
#include "./logging.h"

#include <c++utilities/conversion/stringbuilder.h>
#include <c++utilities/io/ansiescapecodes.h>

#include <filesystem>
#include <iostream>
#include <regex>

using namespace std;
using namespace CppUtilities;
using namespace CppUtilities::EscapeCodes;
using namespace LibPkg;

namespace LibRepoMgr {

namespace WebClient {

DatabaseQuery prepareDatabaseQuery(LogContext &log, const std::vector<Database *> &dbs, bool withFiles)
{
    DatabaseQuery query;
    query.queryParamsForDbs.reserve(dbs.size());

    for (auto *const db : dbs) {
        const auto &destinationFilePath = withFiles ? db->filesPath : db->path;
        if (destinationFilePath.empty()) {
            log(Phrases::ErrorMessage, "No local path configured database \"", db->name, '@', db->arch, "\" (for ",
                withFiles ? "files" : "regular db", ')', '\n');
            continue;
        }
        if (db->mirrors.empty()) {
            log(Phrases::ErrorMessage, "No mirrors configured for database \"", db->name, '@', db->arch, '\"', '\n');
            continue;
        }
        const auto destinationDirPath = std::filesystem::path(destinationFilePath).parent_path();
        try {
            if (!std::filesystem::exists(destinationDirPath)) {
                std::filesystem::create_directories(destinationDirPath);
            }
        } catch (const std::filesystem::filesystem_error &e) {
            log(Phrases::ErrorMessage, "Unable to create directory \"", destinationDirPath, "\" for database \"", db->name, '@', db->arch,
                "\": ", e.what(), '\n');
            query.failedDbs.emplace_back(db->name);
            continue;
        }
        query.queryParamsForDbs.emplace_back(
            DatabaseQueryParams{ db->name, db->arch, (db->mirrors.front() % '/' % db->name) + (withFiles ? ".files" : ".db"), destinationFilePath });
    }
    return query;
}

static int matchToInt(const std::sub_match<const char *> &match)
{
    const auto sv = std::string_view(match.first, static_cast<std::size_t>(match.length()));
    return stringToNumber<int>(sv);
}

void queryDatabases(
    LogContext &log, ServiceSetup &setup, std::vector<DatabaseQueryParams> &&dbQueries, std::shared_ptr<DatabaseQuerySession> &dbQuerySession)
{
    for (auto &query : dbQueries) {
        log(Phrases::InfoMessage, "Retrieving \"", query.databaseName, "\" from mirror: ", query.url, '\n');
        auto session = runSessionFromUrl(
            setup.building.ioContext, setup.webServer.sslContext, query.url,
            [&log, &setup, dbName = std::move(query.databaseName), dbArch = std::move(query.databaseArch), dbQuerySession](
                Session &session2, const WebClient::HttpClientError &error) mutable {
                if (error.errorCode != boost::beast::errc::success && error.errorCode != boost::asio::ssl::error::stream_truncated) {
                    log(Phrases::ErrorMessage, "Error retrieving database file \"", session2.destinationFilePath, "\" for ", dbName, ": ",
                        error.what(), '\n');
                    dbQuerySession->addResponse(std::move(dbName));
                    return;
                }

                const auto &response = get<FileResponse>(session2.response);
                const auto &message = response.get();
                if (message.result() != boost::beast::http::status::ok) {
                    log(Phrases::ErrorMessage, "Error retrieving database file \"", session2.destinationFilePath, "\" for ", dbName,
                        ": mirror returned ", message.result_int(), " response\n");
                    dbQuerySession->addResponse(std::move(dbName));
                    return;
                }

                // find last modification time
                auto lastModified = DateTime();
                const auto lastModifiedHeader = message.find(boost::beast::http::field::last_modified);
                if (lastModifiedHeader != message.cend()) {
                    // parse "Last-Modified" header which should be something like "<day-name>, <day> <month> <year> <hour>:<minute>:<second> GMT"
                    const auto lastModifiedStr = lastModifiedHeader->value();
                    static const auto lastModifiedPattern = regex("..., (\\d\\d) (...) (\\d\\d\\d\\d) (\\d\\d):(\\d\\d):(\\d\\d) GMT");
                    static const auto months = unordered_map<string, int>{ { "Jan", 1 }, { "Feb", 2 }, { "Mar", 3 }, { "Apr", 4 }, { "May", 5 },
                        { "Jun", 6 }, { "Jul", 7 }, { "Aug", 8 }, { "Sep", 9 }, { "Oct", 10 }, { "Nov", 11 }, { "Dec", 12 } };
                    try {
                        cmatch match;
                        if (!regex_search(lastModifiedStr.cbegin(), lastModifiedStr.cend(), match, lastModifiedPattern)) {
                            throw ConversionException("date/time denotation not in expected format");
                        }
                        const auto month = months.find(match[2]);
                        if (month == months.cend()) {
                            throw ConversionException("month is invalid");
                        }
                        lastModified = DateTime::fromDateAndTime(matchToInt(match[3]), month->second, matchToInt(match[1]), matchToInt(match[4]),
                            matchToInt(match[5]), matchToInt(match[6]));
                    } catch (const ConversionException &e) {
                        log(Phrases::WarningMessage, "Unable to parse \"Last-Modified\" header for database ", dbName, ": ", e.what(),
                            " (last modification time was \"", string_view(lastModifiedStr.data(), lastModifiedStr.size()), "\")\n");
                    }
                }

                // log
                if (lastModified.isNull()) {
                    log(Phrases::InfoMessage, "Loading database \"", dbName, "\" from mirror response\n");
                    lastModified = DateTime::gmtNow();
                } else {
                    log(Phrases::InfoMessage, "Loading database \"", dbName,
                        "\" from mirror response; last modification time: ", lastModified.toString(), '\n');
                }

                try {
                    // load packages
                    auto files = extractFiles(session2.destinationFilePath, &Database::isFileRelevant);
                    auto packages = Package::fromDatabaseFile(move(files));

                    // insert packages
                    auto lock = setup.config.lockToWrite();
                    auto db = setup.config.findDatabase(dbName, dbArch);
                    if (!db) {
                        log(Phrases::ErrorMessage, "Retrieved database file for \"", dbName, '@', dbArch, "\" but it no longer exists; discarding\n");
                        return;
                    }
                    db->replacePackages(packages, lastModified);
                } catch (const std::runtime_error &e) {
                    log(Phrases::ErrorMessage, "Unable to parse retrieved database file for \"", dbName, '@', dbArch, "\": ", e.what(), '\n');
                    dbQuerySession->addResponse(std::move(dbName));
                }
            },
            move(query.destinationFilePath));
    }
}

std::shared_ptr<DatabaseQuerySession> queryDatabases(
    LogContext &log, ServiceSetup &setup, std::vector<DatabaseQueryParams> &&urls, DatabaseQuerySession::HandlerType &&handler)
{
    auto dbQuerySession = DatabaseQuerySession::create(setup.building.ioContext, move(handler));
    queryDatabases(log, setup, move(urls), dbQuerySession);
    return dbQuerySession;
}

std::shared_ptr<DatabaseQuerySession> queryDatabases(LogContext &log, ServiceSetup &setup, std::shared_lock<shared_mutex> *configReadLock,
    const std::vector<Database *> &dbs, DatabaseQuerySession::HandlerType &&handler)
{
    shared_lock<shared_mutex> ownConfigReadLock;
    auto query = prepareDatabaseQuery(log, dbs, setup.building.loadFilesDbs);
    configReadLock->unlock();
    return queryDatabases(log, setup, std::move(query.queryParamsForDbs), move(handler));
}

PackageCachingSession::PackageCachingSession(PackageCachingDataForSession &data, boost::asio::io_context &ioContext,
    boost::asio::ssl::context &sslContext, MultiSession::HandlerType &&handler)
    : MultiSession<void>(ioContext, std::move(handler))
    , m_sslContext(sslContext)
    , m_data(data)
    , m_dbIterator(data.begin())
    , m_dbEnd(data.end())
    , m_packageIterator(m_dbIterator != m_dbEnd ? m_dbIterator->second.begin() : decltype(m_packageIterator)())
    , m_packageEnd(m_dbIterator != m_dbEnd ? m_dbIterator->second.end() : decltype(m_packageEnd)())
{
}

void PackageCachingSession::selectNextPackage()
{
    if (++m_packageIterator != m_packageEnd) {
        return;
    }
    if (++m_dbIterator == m_dbEnd) {
        return;
    }
    m_packageIterator = m_dbIterator->second.begin();
    m_packageEnd = m_dbIterator->second.end();
}

PackageCachingDataForPackage *PackageCachingSession::getCurrentDataAndSelectNext()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_packageIterator == m_packageEnd) {
        return nullptr;
    }
    auto *const data = &m_packageIterator->second;
    selectNextPackage();
    return data;
}

void cachePackages(LogContext &log, std::shared_ptr<PackageCachingSession> &&packageCachingSession, std::optional<std::uint64_t> bodyLimit,
    std::size_t maxParallelDownloads)
{
    for (std::size_t startedDownloads = 0; startedDownloads < maxParallelDownloads; ++startedDownloads) {
        auto *const cachingData = packageCachingSession->getCurrentDataAndSelectNext();
        if (!cachingData) {
            return;
        }
        log(Phrases::InfoMessage, "Downloading \"", cachingData->url, "\" to \"", cachingData->destinationFilePath, "\"\n");
        runSessionFromUrl(
            packageCachingSession->ioContext(), packageCachingSession->m_sslContext, cachingData->url,
            [&log, bodyLimit, packageCachingSession, cachingData](Session &session, const WebClient::HttpClientError &error) mutable {
                if (error.errorCode != boost::beast::errc::success && error.errorCode != boost::asio::ssl::error::stream_truncated) {
                    const auto msg = std::make_tuple(
                        "Error downloading \"", cachingData->url, "\" to \"", cachingData->destinationFilePath, "\": ", error.what());
                    cachingData->error = tupleToString(msg);
                    log(Phrases::ErrorMessage, msg, '\n');
                }
                const auto &response = get<FileResponse>(session.response);
                const auto &message = response.get();
                if (message.result() != boost::beast::http::status::ok) {
                    const auto msg = std::make_tuple("Error downloading \"", cachingData->url, "\" to \"", cachingData->destinationFilePath,
                        "\": mirror returned ", message.result_int(), " response");
                    cachingData->error = tupleToString(msg);
                    log(Phrases::ErrorMessage, msg, '\n');
                }
                cachePackages(log, std::move(packageCachingSession), bodyLimit, 1);
            },
            std::string(cachingData->destinationFilePath), std::string_view(), std::string_view(), boost::beast::http::verb::get, bodyLimit);
    }
}

} // namespace WebClient

} // namespace LibRepoMgr
