#include "./buildactionprivate.h"

#include "./logging.h"

#include "../webapi/params.h"
#include "../webapi/render.h"
#include "../webapi/session.h"

using namespace std;
using namespace CppUtilities;
using namespace CppUtilities::EscapeCodes;

namespace LibRepoMgr {

void BuildProcessSession::BuffersToWrite::clear()
{
    currentlySentBuffers.clear();
    currentlySentBufferRefs.clear();
    outstandingBuffersToSend.clear();
}

void BuildProcessSession::DataForWebSession::streamFile(const std::string &filePath, const std::shared_ptr<BuildProcessSession> &processSession,
    const std::shared_ptr<WebAPI::Session> &webSession, std::unique_lock<std::mutex> &&lock)
{
    error = false;

#ifdef BOOST_ASIO_HAS_FILE
    auto ec = boost::system::error_code();
    m_fileStream.open(filePath, boost::asio::stream_file::read_only, ec);
#else
    auto ec = boost::beast::error_code();
    m_file.open(filePath.data(), boost::beast::file_mode::scan, ec);
#endif
    if (ec) {
        cerr << Phrases::WarningMessage << "Unable to open \"" << filePath << "\": " << ec.message() << Phrases::EndFlush;
        return;
    }
#ifdef BOOST_ASIO_HAS_FILE
    const auto fileSize = m_fileStream.size(ec);
#else
    const auto fileSize = m_file.size(ec);
#endif
    m_bytesToSendFromFile.store(fileSize);
    lock.unlock();
    if (ec) {
        cerr << Phrases::WarningMessage << "Unable to determine size of \"" << filePath << "\": " << ec.message() << Phrases::EndFlush;
        return;
    }
#ifndef BOOST_ASIO_HAS_FILE
    m_fileStream.assign(m_file.native_handle(), ec);
    if (ec) {
        m_bytesToSendFromFile.store(0);
        cerr << Phrases::WarningMessage << "Unable to assign descriptor for \"" << filePath << "\": " << ec.message() << Phrases::EndFlush;
        return;
    }
    m_fileStream.non_blocking(true, ec);
    if (ec) {
        m_bytesToSendFromFile.store(0);
        cerr << Phrases::WarningMessage << "Unable to set descriptor for \"" << filePath << "\" to non-blocking mode: " << ec.message()
             << Phrases::EndFlush;
        return;
    }
#endif
    m_fileBuffer = processSession->m_bufferPool.newBuffer();
    m_fileStream.async_read_some(boost::asio::buffer(*m_fileBuffer, sizeof(std::min(fileSize, processSession->m_bufferPool.bufferSize()))),
        [this, &filePath, processSession, webSession](
            auto &error, auto bytesTransferred) { writeFileData(filePath, processSession, webSession, error, bytesTransferred); });
}

void BuildProcessSession::DataForWebSession::writeFileData(const std::string &filePath, const std::shared_ptr<BuildProcessSession> &processSession,
    const std::shared_ptr<WebAPI::Session> &webSession, const boost::system::error_code &readError, size_t bytesTransferred)
{
    // handle error
    const auto eof = readError == boost::asio::error::eof;
    if (!eof && readError) {
        cerr << Phrases::WarningMessage << "Unable to determine size of \"" << filePath << "\": " << readError.message() << Phrases::EndFlush;
        return;
    } else if (eof) {
        boost::system::error_code ec;
        m_fileStream.close(ec);
    }
    // send file data to web client
    if (bytesTransferred > m_bytesToSendFromFile) {
        bytesTransferred = m_bytesToSendFromFile;
    }
    const auto bytesLeftToRead = m_bytesToSendFromFile - bytesTransferred;
    boost::beast::net::async_write(webSession->socket(), boost::beast::http::make_chunk(boost::asio::buffer(*m_fileBuffer, bytesTransferred)),
        [this, &filePath, processSession, webSession, bytesLeftToRead, moreToRead = !eof && bytesLeftToRead](
            boost::system::error_code ecWebClient, std::size_t bytesTransferredToWebClient) {
            // handle error
            CPP_UTILITIES_UNUSED(bytesTransferredToWebClient)
            if (ecWebClient) {
                cerr << Phrases::WarningMessage << "Error sending \"" << filePath << "\" to client: " << ecWebClient.message() << Phrases::EndFlush;
                std::lock_guard<std::mutex> lock(processSession->m_mutex);
                clear();
                error = true;
                m_bytesToSendFromFile.store(0);
                return;
            }
            m_bytesToSendFromFile.store(bytesLeftToRead);
            // tell the client it's over if there is nothing more to read
            if (!moreToRead) {
                if (processSession->m_exited.load()) {
                    boost::beast::net::async_write(webSession->socket(), boost::beast::http::make_chunk_last(),
                        std::bind(&WebAPI::Session::responded, webSession, std::placeholders::_1, std::placeholders::_2, true));
                }
                return;
            }
            // continue reading if there's more data
            m_fileStream.async_read_some(
                boost::asio::buffer(*m_fileBuffer, sizeof(std::min(bytesLeftToRead, processSession->m_bufferPool.bufferSize()))),
                [this, &filePath, processSession, webSession](auto &readError2, auto bytesTransferred2) {
                    writeFileData(filePath, processSession, webSession, readError2, bytesTransferred2);
                });
        });
}

void BuildProcessSession::registerWebSession(std::shared_ptr<WebAPI::Session> &&webSession)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    auto &sessionInfo = m_registeredWebSessions[webSession];
    if (!sessionInfo) {
        sessionInfo = std::make_unique<DataForWebSession>(m_ioContext);
    }
    sessionInfo->streamFile(m_logFilePath, shared_from_this(), std::move(webSession), std::move(lock));
}

void BuildProcessSession::registerNewDataHandler(std::function<void(BuildProcessSession::BufferType, std::size_t)> &&handler)
{
    const auto lock = std::lock_guard<std::mutex>(m_mutex);
    m_newDataHandlers.emplace_back(std::move(handler));
}

void BuildProcessSession::writeData(std::string_view data)
{
    const auto lock = std::lock_guard<std::mutex>(m_mutex);
    while (const auto bufferSize = std::min<std::size_t>(data.size(), m_bufferPool.bufferSize())) {
        m_buffer = m_bufferPool.newBuffer();
        data.copy(m_buffer->data(), bufferSize);
        writeCurrentBuffer(bufferSize);
        data = data.substr(bufferSize);
    }
}

void BuildProcessSession::writeEnd()
{
    m_exited = true;
    close();
}

void BuildProcessSession::prepareLogFile()
{
    // ensure directory exists
    auto path = std::filesystem::path(m_logFilePath);
    if (path.has_parent_path()) {
        auto ec = std::error_code();
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            result.errorCode = std::move(ec);
            result.error = CppUtilities::argsToString("unable to create directory \"", path.parent_path(), ": ", ec.message());
            return;
        }
    }
    // open logfile and a "file descriptor" for writing in a non-blocking way
#ifdef BOOST_ASIO_HAS_FILE
    auto ec = boost::system::error_code();
    m_logFileStream.open(
        m_logFilePath, boost::asio::stream_file::write_only | boost::asio::stream_file::create | boost::asio::stream_file::truncate, ec);
    if (ec) {
        result.errorCode = std::error_code(ec.value(), ec.category());
        result.error = CppUtilities::argsToString("unable to open \"", m_logFilePath, ": ", ec.message());
        return;
    }
#else
    auto ec = boost::beast::error_code();
    m_logFile.open(m_logFilePath.data(), boost::beast::file_mode::write, ec);
    if (ec) {
        result.errorCode = std::error_code(ec.value(), ec.category());
        result.error = CppUtilities::argsToString("unable to open \"", m_logFilePath, ": ", ec.message());
        return;
    }
    try {
        m_logFileStream.assign(m_logFile.native_handle());
        m_logFileStream.non_blocking(true);
        m_logFileStream.native_non_blocking(true);
    } catch (const boost::system::system_error &e) {
        result.errorCode = e.code();
        result.error = CppUtilities::argsToString("unable to prepare descriptor for \"", m_logFilePath, ": ", e.what());
        return;
    }
#endif
}

void BuildProcessSession::readMoreFromPipe()
{
    m_buffer = m_bufferPool.newBuffer();
    m_pipe.async_read_some(boost::asio::buffer(m_buffer.get(), m_bufferPool.bufferSize()),
        std::bind(&BuildProcessSession::writeDataFromPipe, shared_from_this(), std::placeholders::_1, std::placeholders::_2));
}

void BuildProcessSession::writeDataFromPipe(boost::system::error_code ec, std::size_t bytesTransferred)
{
    // handle error
    if (ec && ec != boost::asio::stream_errc::eof) {
        cerr << Phrases::ErrorMessage << "Error reading from pipe for \"" << m_logFilePath << "\": " << ec.message() << Phrases::EndFlush;
    }
    // write bytes to log file and web clients
    if (bytesTransferred) {
        auto lock = std::lock_guard<std::mutex>(m_mutex);
        writeCurrentBuffer(bytesTransferred);
    }
    // continue reading from the pipe unless there was an error
    if (!ec) {
        readMoreFromPipe();
        return;
    }
    // stop reading from the pipe if there was an error; close the log file and tell web clients that it's over
    if (!bytesTransferred) {
        close();
    }
}

void BuildProcessSession::writeCurrentBuffer(std::size_t bytesTransferred)
{
    // write bytesTransferred bytes from m_buffer to log file
    if (!m_logFileBuffers.error) {
        if (m_logFileBuffers.currentlySentBuffers.empty()) {
            m_logFileBuffers.currentlySentBuffers.emplace_back(std::pair(m_buffer, bytesTransferred));
            boost::asio::async_write(m_logFileStream, boost::asio::buffer(m_buffer.get(), bytesTransferred),
                std::bind(&BuildProcessSession::writeNextBufferToLogFile, shared_from_this(), std::placeholders::_1, std::placeholders::_2));
        } else {
            m_logFileBuffers.outstandingBuffersToSend.emplace_back(std::pair(m_buffer, bytesTransferred));
        }
    }
    // write bytesTransferred bytes from m_buffer to web sessions
    for (auto &[session, sessionInfo] : m_registeredWebSessions) {
        if (sessionInfo->error) {
            continue;
        }
        if (sessionInfo->currentlySentBuffers.empty() && !sessionInfo->bytesToSendFromFile()) {
            sessionInfo->currentlySentBuffers.swap(sessionInfo->outstandingBuffersToSend);
            sessionInfo->currentlySentBuffers.emplace_back(std::pair(m_buffer, bytesTransferred));
            sessionInfo->currentlySentBufferRefs.clear();
            for (const auto &buffer : sessionInfo->currentlySentBuffers) {
                sessionInfo->currentlySentBufferRefs.emplace_back(boost::asio::buffer(buffer.first.get(), buffer.second));
            }
            boost::beast::net::async_write(session->socket(), boost::beast::http::make_chunk(sessionInfo->currentlySentBufferRefs),
                std::bind(&BuildProcessSession::writeNextBufferToWebSession, shared_from_this(), std::placeholders::_1, std::placeholders::_2,
                    std::ref(*session), std::ref(*sessionInfo)));

        } else {
            sessionInfo->outstandingBuffersToSend.emplace_back(std::pair(m_buffer, bytesTransferred));
        }
    }
    // invoke new data handlers
    for (const auto &handler : m_newDataHandlers) {
        if (handler) {
            handler(m_buffer, bytesTransferred);
        }
    }
}

void BuildProcessSession::writeNextBufferToLogFile(const boost::system::error_code &error, std::size_t bytesTransferred)
{
    // handle error
    CPP_UTILITIES_UNUSED(bytesTransferred)
    if (error) {
        cerr << Phrases::ErrorMessage << "Error writing to \"" << m_logFilePath << "\": " << error.message() << Phrases::EndFlush;
        auto lock = std::lock_guard<std::mutex>(m_mutex);
        m_logFileBuffers.clear();
        m_logFileBuffers.error = true;
        return;
    }
    // write more data to the logfile if there's more
    {
        auto lock = std::lock_guard<std::mutex>(m_mutex);
        m_logFileBuffers.currentlySentBuffers.clear();
        // close the logfile when the process exited and we've written all the output
        if (m_logFileBuffers.outstandingBuffersToSend.empty() && m_exited.load()) {
            closeLogFile();
            return;
        }
        m_logFileBuffers.currentlySentBuffers.swap(m_logFileBuffers.outstandingBuffersToSend);
        m_logFileBuffers.currentlySentBufferRefs.clear();
        for (const auto &buffer : m_logFileBuffers.currentlySentBuffers) {
            m_logFileBuffers.currentlySentBufferRefs.emplace_back(boost::asio::buffer(buffer.first.get(), buffer.second));
        }
    }
    if (!m_logFileBuffers.currentlySentBufferRefs.empty()) {
        boost::asio::async_write(m_logFileStream, m_logFileBuffers.currentlySentBufferRefs,
            std::bind(&BuildProcessSession::writeNextBufferToLogFile, shared_from_this(), std::placeholders::_1, std::placeholders::_2));
    }
}

void BuildProcessSession::writeNextBufferToWebSession(
    const boost::system::error_code &error, std::size_t bytesTransferred, WebAPI::Session &session, BuildProcessSession::BuffersToWrite &sessionInfo)
{
    // handle error
    CPP_UTILITIES_UNUSED(bytesTransferred)
    if (error) {
        cerr << Phrases::WarningMessage << "Error sending \"" << m_logFilePath << "\" to client: " << error.message() << Phrases::EndFlush;
        std::lock_guard<std::mutex> lock(m_mutex);
        sessionInfo.clear();
        sessionInfo.error = true;
        return;
    }
    // send more data to the client if there's more
    {
        auto lock = std::lock_guard<std::mutex>(m_mutex);
        sessionInfo.currentlySentBuffers.clear();
        // tell the client it's over when the process exited and we've sent all the output
        if (sessionInfo.outstandingBuffersToSend.empty() && m_exited.load()) {
            boost::beast::net::async_write(session.socket(), boost::beast::http::make_chunk_last(),
                std::bind(&WebAPI::Session::responded, session.shared_from_this(), std::placeholders::_1, std::placeholders::_2, true));
            return;
        }
        sessionInfo.currentlySentBuffers.swap(sessionInfo.outstandingBuffersToSend);
        sessionInfo.currentlySentBufferRefs.clear();
        for (const auto &buffer : sessionInfo.currentlySentBuffers) {
            sessionInfo.currentlySentBufferRefs.emplace_back(boost::asio::buffer(buffer.first.get(), buffer.second));
        }
    }
    if (!sessionInfo.currentlySentBufferRefs.empty()) {
        boost::beast::net::async_write(session.socket(), boost::beast::http::make_chunk(sessionInfo.currentlySentBufferRefs),
            std::bind(&BuildProcessSession::writeNextBufferToWebSession, shared_from_this(), std::placeholders::_1, std::placeholders::_2,
                std::ref(session), std::ref(sessionInfo)));
    }
}

void BuildProcessSession::closeLogFile()
{
    auto ec = boost::system::error_code();
#ifdef BOOST_ASIO_HAS_FILE
    m_logFileStream.close(ec);
#else
    m_logFile.close(ec);
#endif
    if (ec) {
        cerr << Phrases::WarningMessage << "Error closing \"" << m_logFilePath << "\": " << ec.message() << Phrases::EndFlush;
    }
}

void BuildProcessSession::close()
{
    auto lock = std::lock_guard<std::mutex>(m_mutex);
    if (m_logFileBuffers.outstandingBuffersToSend.empty()) {
        closeLogFile();
    }
    for (auto &[session, sessionInfo] : m_registeredWebSessions) {
        if (!sessionInfo->outstandingBuffersToSend.empty()) {
            continue;
        }
        boost::beast::net::async_write(session->socket(), boost::beast::http::make_chunk_last(),
            std::bind(&WebAPI::Session::responded, session, std::placeholders::_1, std::placeholders::_2, true));
    }
}

void BuildProcessSession::conclude()
{
    // set the exited flag so all async operations know there's no more data to expect
    m_exited = true;

    // detach from build action
    if (!m_buildAction) {
        return;
    }
    if (const auto outputLock = std::lock_guard<std::mutex>(m_buildAction->m_outputSessionMutex); m_buildAction->m_outputSession.get() == this) {
        m_buildAction->m_outputSession.reset();
    } else {
        const auto processesLock = std::lock_guard<std::mutex>(m_buildAction->m_processesMutex);
        m_buildAction->m_ongoingProcesses.erase(m_logFilePath);
    }
}

std::shared_ptr<BuildProcessSession> BuildAction::makeBuildProcess(
    std::string &&displayName, std::string &&logFilePath, ProcessHandler &&handler, AssociatedLocks &&locks)
{
    const auto processesLock = std::lock_guard<std::mutex>(m_processesMutex);
    auto &process = m_ongoingProcesses[logFilePath];
    if (process) {
        // prevent multiple ongoing processes for the same log file
        // note: The build action implementations are supposed to avoid this condition but let's make this function generic.
        return nullptr;
    }
    auto buildLock = m_setup->building.lockToWrite();
    if (find(logfiles.cbegin(), logfiles.cend(), logFilePath) == logfiles.cend()) {
        logfiles.emplace_back(logFilePath);
    }
    buildLock.unlock();
    return process = make_shared<BuildProcessSession>(
               this, m_setup->building.ioContext, std::move(displayName), std::move(logFilePath), std::move(handler), std::move(locks));
}

void BuildAction::terminateOngoingBuildProcesses()
{
    const auto processesLock = std::lock_guard<std::mutex>(m_processesMutex);
    for (auto &[logFilePath, process] : m_ongoingProcesses) {
        if (process->hasExited()) {
            continue;
        }
        std::error_code ec;
        process->group.terminate(ec);
        if (ec) {
            log()(Phrases::ErrorMessage, "Unable to stop process group (main PID ", process->child.id(), ") for \"", logFilePath,
                "\": ", ec.message(), '\n');
        }
    }
}

void BuildAction::streamFile(const WebAPI::Params &params, const std::string &filePath, std::string_view fileMimeType)
{
    auto buildProcess = std::shared_ptr<BuildProcessSession>();
    if (const auto outputLock = std::unique_lock<std::mutex>(m_outputSessionMutex); m_outputSession && m_outputSession->logFilePath() == filePath) {
        buildProcess = m_outputSession;
    } else {
        const auto processesLock = std::unique_lock<std::mutex>(m_processesMutex);
        buildProcess = findBuildProcess(filePath);
    }
    if (!buildProcess) {
        // simply send the file if there's no ongoing process writing to it anymore
        params.session.respond(filePath.data(), fileMimeType.data(), params.target.path);
        return;
    }

    // stream the output of the ongoing process
    auto chunkResponse = WebAPI::Render::makeChunkResponse(params.request(), fileMimeType.data());
    boost::beast::http::async_write_header(params.session.socket(), chunkResponse->serializer,
        [chunkResponse, filePath, buildProcess, session = params.session.shared_from_this()](
            const boost::system::error_code &error, std::size_t) mutable {
            if (error) {
                cerr << Phrases::WarningMessage << "Error sending header for \"" << filePath << "\" to client: " << error.message()
                     << Phrases::EndFlush;
                return;
            }
            buildProcess->registerWebSession(std::move(session));
        });
}

/*!
 * \brief Internally called to append output and spread it to all waiting sessions.
 */
void BuildAction::appendOutput(std::string_view output)
{
    if (output.empty() || !m_setup) {
        return;
    }

    auto outputLock = std::unique_lock<std::mutex>(m_outputSessionMutex);
    if (!m_outputSession) {
        m_outputSession = std::make_shared<BuildProcessSession>(
            this, m_setup->building.ioContext, argsToString("Output of build action ", id), argsToString("logs/build-action-", id, ".log"));
        m_outputSession->prepareLogFile();
        if (m_outputSession->result.errorCode) {
            std::cerr << Phrases::ErrorMessage << "Unable to open output logfile for build action " << id << ": " << m_outputSession->result.error
                      << Phrases::EndFlush;
            return;
        }
        const auto buildingLock = m_setup->building.lockToWrite();
        logfiles.emplace_back(m_outputSession->logFilePath());
    }
    outputLock.unlock();
    if (!m_outputSession->result.errorCode) {
        m_outputSession->writeData(output);
    }
}

} // namespace LibRepoMgr
