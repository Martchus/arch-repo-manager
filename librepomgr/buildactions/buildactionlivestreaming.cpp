#include "./buildactionprivate.h"

#include "./logging.h"

#include "../webapi/params.h"
#include "../webapi/render.h"
#include "../webapi/session.h"

using namespace std;
using namespace CppUtilities;
using namespace CppUtilities::EscapeCodes;

namespace LibRepoMgr {

static OutputBufferingForSession::BufferPoolType outputStreamingBufferPool(OutputBufferingForSession::bufferSize);

void BuildProcessSession::BuffersToWrite::clear()
{
    currentlySentBuffers.clear();
    currentlySentBufferRefs.clear();
    outstandingBuffersToSend.clear();
}

void BuildProcessSession::DataForWebSession::streamFile(
    const std::string &filePath, std::shared_ptr<WebAPI::Session> &&session, std::unique_lock<std::mutex> &&lock)
{
    error = false;

    boost::beast::error_code error;
    m_file.open(filePath.data(), boost::beast::file_mode::scan, error);
    if (error) {
        cerr << Phrases::WarningMessage << "Unable to open \"" << filePath << "\": " << error.message() << Phrases::EndFlush;
        return;
    }
    const auto fileSize = m_file.size(error);
    m_bytesToSendFromFile.store(fileSize);
    lock.unlock();
    if (error) {
        cerr << Phrases::WarningMessage << "Unable to determine size of \"" << filePath << "\": " << error.message() << Phrases::EndFlush;
        return;
    }
    m_descriptor.assign(m_file.native_handle(), error);
    if (error) {
        m_bytesToSendFromFile.store(0);
        cerr << Phrases::WarningMessage << "Unable to assign descriptor for \"" << filePath << "\": " << error.message() << Phrases::EndFlush;
        return;
    }
    m_descriptor.non_blocking(true, error);
    if (error) {
        m_bytesToSendFromFile.store(0);
        cerr << Phrases::WarningMessage << "Unable to set descriptor for \"" << filePath << "\" to non-blocking mode: " << error.message()
             << Phrases::EndFlush;
        return;
    }
    m_fileBuffer = m_session.m_bufferPool.newBuffer();
    m_descriptor.async_read_some(boost::asio::buffer(*m_fileBuffer, sizeof(std::min(fileSize, m_session.m_bufferPool.bufferSize()))),
        std::bind(&DataForWebSession::writeFileData, this, std::ref(filePath), std::move(session), std::placeholders::_1, std::placeholders::_2));
}

void BuildProcessSession::DataForWebSession::writeFileData(
    const std::string &filePath, std::shared_ptr<WebAPI::Session> session, const boost::system::error_code &readError, size_t bytesTransferred)
{
    // handle error
    const auto eof = readError == boost::asio::error::eof;
    if (!eof && readError) {
        cerr << Phrases::WarningMessage << "Unable to determine size of \"" << filePath << "\": " << readError.message() << Phrases::EndFlush;
        return;
    } else if (eof) {
        boost::system::error_code ec;
        m_descriptor.close(ec);
    }
    // send file data to web client
    const auto bytesLeftToRead = m_bytesToSendFromFile - bytesTransferred;
    boost::beast::net::async_write(session->socket(), boost::beast::http::make_chunk(boost::asio::buffer(*m_fileBuffer, bytesTransferred)),
        [this, &filePath, session, bytesLeftToRead, moreToRead = !eof && bytesLeftToRead](
            boost::system::error_code ec, std::size_t bytesTransferred) {
            // handle error
            CPP_UTILITIES_UNUSED(bytesTransferred)
            if (ec) {
                cerr << Phrases::WarningMessage << "Error sending \"" << filePath << "\" to client: " << ec.message() << Phrases::EndFlush;
                std::lock_guard<std::mutex> lock(m_session.m_mutex);
                clear();
                error = true;
                m_bytesToSendFromFile.store(0);
                return;
            }
            m_bytesToSendFromFile.store(bytesLeftToRead);
            // tell the client it's over if there is nothing more to read
            if (!moreToRead) {
                if (m_session.m_exited.load()) {
                    boost::beast::net::async_write(session->socket(), boost::beast::http::make_chunk_last(),
                        std::bind(&WebAPI::Session::responded, session, std::placeholders::_1, std::placeholders::_2, true));
                }
                return;
            }
            // continue reading if there's more data
            m_descriptor.async_read_some(boost::asio::buffer(*m_fileBuffer, sizeof(std::min(bytesLeftToRead, m_session.m_bufferPool.bufferSize()))),
                std::bind(
                    &DataForWebSession::writeFileData, this, std::ref(filePath), std::move(session), std::placeholders::_1, std::placeholders::_2));
        });
}

void BuildProcessSession::registerWebSession(std::shared_ptr<WebAPI::Session> &&webSession)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    auto &sessionInfo = m_registeredWebSessions[webSession];
    if (!sessionInfo) {
        sessionInfo = std::make_unique<DataForWebSession>(*this);
    }
    sessionInfo->streamFile(m_logFilePath, std::move(webSession), std::move(lock));
}

void BuildProcessSession::registerNewDataHandler(std::function<void(BuildProcessSession::BufferType, std::size_t)> &&handler)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    m_newDataHandler = std::move(handler);
}

void BuildProcessSession::prpareLogFile()
{
    // open logfile and a "file descriptor" for writing in a non-blocking way
    boost::beast::error_code ec;
    m_logFile.open(m_logFilePath.data(), boost::beast::file_mode::write, ec);
    if (ec) {
        result.errorCode = std::error_code(ec.value(), ec.category());
        result.error = CppUtilities::argsToString("unable to open \"", m_logFilePath, ": ", ec.message());
        return;
    }
    try {
        m_logFileDescriptor.assign(m_logFile.native_handle());
        m_logFileDescriptor.non_blocking(true);
        m_logFileDescriptor.native_non_blocking(true);
    } catch (const boost::system::system_error &e) {
        result.errorCode = e.code();
        result.error = CppUtilities::argsToString("unable to prepare descriptor for \"", m_logFilePath, ": ", e.what());
        return;
    }
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
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_logFileBuffers.error) {
            if (m_logFileBuffers.currentlySentBuffers.empty()) {
                m_logFileBuffers.currentlySentBuffers.emplace_back(std::pair(m_buffer, bytesTransferred));
                boost::asio::async_write(m_logFileDescriptor, boost::asio::buffer(m_buffer.get(), bytesTransferred),
                    std::bind(&BuildProcessSession::writeNextBufferToLogFile, shared_from_this(), std::placeholders::_1, std::placeholders::_2));
            } else {
                m_logFileBuffers.outstandingBuffersToSend.emplace_back(std::pair(m_buffer, bytesTransferred));
            }
        }
        for (auto &[session, sessionInfo] : m_registeredWebSessions) {
            if (sessionInfo->error) {
                continue;
            }
            if (sessionInfo->currentlySentBuffers.empty() && !sessionInfo->bytesToSendFromFile()) {
                sessionInfo->currentlySentBuffers.emplace_back(std::pair(m_buffer, bytesTransferred));
                boost::beast::net::async_write(session->socket(),
                    boost::beast::http::make_chunk(boost::asio::buffer(m_buffer.get(), bytesTransferred)),
                    std::bind(&BuildProcessSession::writeNextBufferToWebSession, shared_from_this(), std::placeholders::_1, std::placeholders::_2,
                        std::ref(*session), std::ref(*sessionInfo)));
            } else {
                sessionInfo->outstandingBuffersToSend.emplace_back(std::pair(m_buffer, bytesTransferred));
            }
        }
        if (m_newDataHandler) {
            m_newDataHandler(m_buffer, bytesTransferred);
        }
    }
    // continue reading from the pipe unless there was an error
    if (!ec) {
        readMoreFromPipe();
        return;
    }
    // stop reading from the pipe if there was an error; close the log file and tell web clients that it's over
    if (bytesTransferred) {
        return;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_logFileBuffers.outstandingBuffersToSend.empty()) {
        boost::system::error_code error;
        m_logFile.close(error);
        if (error) {
            cerr << Phrases::WarningMessage << "Error closing \"" << m_logFilePath << "\": " << error.message() << Phrases::EndFlush;
        }
    }
    for (auto &[session, sessionInfo] : m_registeredWebSessions) {
        if (!sessionInfo->outstandingBuffersToSend.empty()) {
            continue;
        }
        boost::beast::net::async_write(session->socket(), boost::beast::http::make_chunk_last(),
            std::bind(&WebAPI::Session::responded, session, std::placeholders::_1, std::placeholders::_2, true));
    }
}

void BuildProcessSession::writeNextBufferToLogFile(const boost::system::error_code &error, std::size_t bytesTransferred)
{
    // handle error
    CPP_UTILITIES_UNUSED(bytesTransferred)
    if (error) {
        cerr << Phrases::ErrorMessage << "Error writing to \"" << m_logFilePath << "\": " << error.message() << Phrases::EndFlush;
        std::lock_guard<std::mutex> lock(m_mutex);
        m_logFileBuffers.clear();
        m_logFileBuffers.error = true;
        return;
    }
    // write more data to the logfile if there's more
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_logFileBuffers.currentlySentBuffers.clear();
        if (m_logFileBuffers.outstandingBuffersToSend.empty()) {
            // close the logfile when the process exited and we've written all the output
            if (m_exited.load()) {
                boost::system::error_code error;
                m_logFile.close(error);
                if (error) {
                    cerr << Phrases::WarningMessage << "Error closing \"" << m_logFilePath << "\": " << error.message() << Phrases::EndFlush;
                }
            }
            return;
        }
        m_logFileBuffers.currentlySentBuffers.swap(m_logFileBuffers.outstandingBuffersToSend);
        m_logFileBuffers.currentlySentBufferRefs.clear();
        for (const auto &buffer : m_logFileBuffers.currentlySentBuffers) {
            m_logFileBuffers.currentlySentBufferRefs.emplace_back(boost::asio::buffer(buffer.first.get(), buffer.second));
        }
    }
    boost::asio::async_write(m_logFileDescriptor, m_logFileBuffers.currentlySentBufferRefs,
        std::bind(&BuildProcessSession::writeNextBufferToLogFile, shared_from_this(), std::placeholders::_1, std::placeholders::_2));
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
        std::lock_guard<std::mutex> lock(m_mutex);
        sessionInfo.currentlySentBuffers.clear();
        // tell the client it's over when the process exited and we've sent all the output
        if (sessionInfo.outstandingBuffersToSend.empty()) {
            if (m_exited.load()) {
                boost::beast::net::async_write(session.socket(), boost::beast::http::make_chunk_last(),
                    std::bind(&WebAPI::Session::responded, session.shared_from_this(), std::placeholders::_1, std::placeholders::_2, true));
            }
            return;
        }
        sessionInfo.currentlySentBuffers.swap(sessionInfo.outstandingBuffersToSend);
        sessionInfo.currentlySentBufferRefs.clear();
        for (const auto &buffer : sessionInfo.currentlySentBuffers) {
            sessionInfo.currentlySentBufferRefs.emplace_back(boost::asio::buffer(buffer.first.get(), buffer.second));
        }
    }
    boost::beast::net::async_write(session.socket(), boost::beast::http::make_chunk(sessionInfo.currentlySentBufferRefs),
        std::bind(&BuildProcessSession::writeNextBufferToWebSession, shared_from_this(), std::placeholders::_1, std::placeholders::_2,
            std::ref(session), std::ref(sessionInfo)));
}

void BuildProcessSession::conclude()
{
    // set the exited flag so all async operations know there's no more data to expect
    m_exited = true;

    // detach from build action
    auto buildAction = m_buildAction.lock();
    if (!buildAction) {
        return;
    }
    const auto processesLock = std::lock_guard<std::mutex>(buildAction->m_processesMutex);
    buildAction->m_ongoingProcesses.erase(m_logFilePath);
}

void BufferSearch::operator()(const BuildProcessSession::BufferType &buffer, std::size_t bufferSize)
{
    if (m_hasResult || (!m_giveUpTerm.empty() && m_giveUpTermIterator == m_giveUpTerm.end())) {
        return;
    }
    for (auto i = buffer->data(), end = buffer->data() + bufferSize; i != end; ++i) {
        const auto currentChar = *i;
        if (m_searchTermIterator == m_searchTerm.end()) {
            for (const auto &terminationChar : m_terminationChars) {
                if (currentChar == terminationChar) {
                    m_hasResult = true;
                    break;
                }
            }
            if (m_hasResult) {
                m_callback(std::move(m_result));
                return;
            }
            m_result += currentChar;
            continue;
        }
        if (currentChar == *m_searchTermIterator) {
            ++m_searchTermIterator;
        } else {
            m_searchTermIterator = m_searchTerm.begin();
        }
        if (m_giveUpTerm.empty()) {
            continue;
        }
        if (currentChar == *m_giveUpTermIterator) {
            ++m_giveUpTermIterator;
        } else {
            m_giveUpTermIterator = m_giveUpTerm.begin();
        }
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
    auto processesLock = std::unique_lock<std::mutex>(m_processesMutex);
    auto buildProcess = findBuildProcess(filePath);
    processesLock.unlock();
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

void BuildAction::streamOutput(const WebAPI::Params &params, std::size_t offset)
{
    if (!m_setup) {
        m_setup = &params.setup;
    }
    auto session = params.session.shared_from_this();
    auto chunkResponse = WebAPI::Render::makeChunkResponse(params.request(), "application/octet-stream");
    auto outputStreamingLock = std::unique_lock<std::mutex>(m_outputStreamingMutex);
    auto &buffersForSession = m_bufferingForSession[session];
    if (buffersForSession) {
        return; // skip when already streaming to that session
    }
    buffersForSession = std::make_unique<OutputBufferingForSession>();
    auto buildLock = params.setup.building.lockToRead();
    buffersForSession->existingOutputSize = output.size();
    buffersForSession->bytesSent = offset;
    buildLock.unlock();
    outputStreamingLock.unlock();
    boost::beast::http::async_write_header(params.session.socket(), chunkResponse->serializer,
        [buildAction = shared_from_this(), session = std::move(session), &buffering = *buffersForSession, chunkResponse](
            const boost::system::error_code &error, std::size_t bytesTransferred) {
            CPP_UTILITIES_UNUSED(bytesTransferred)
            buildAction->continueStreamingExistingOutputToSession(std::move(session), buffering, error, 0);
        });
}

void BuildAction::continueStreamingExistingOutputToSession(std::shared_ptr<WebAPI::Session> session, OutputBufferingForSession &buffering,
    const boost::system::error_code &error, std::size_t bytesTransferred)
{
    auto outputStreamingLock = std::unique_lock<std::mutex>(m_outputStreamingMutex);
    if (error) {
        m_bufferingForSession.erase(session);
        return;
    }
    const auto bytesSent = buffering.bytesSent += bytesTransferred;
    if (bytesSent >= buffering.existingOutputSize) {
        buffering.currentlySentBuffers.clear();
        buffering.existingOutputSent = true;
        if (!buffering.outstandingBuffersToSend.empty()) {
            outputStreamingLock.unlock();
            continueStreamingNewOutputToSession(std::move(session), buffering, error, 0);
            return;
        }
        if (isDone()) {
            m_bufferingForSession.erase(session);
            outputStreamingLock.unlock();
            boost::beast::net::async_write(session->socket(), boost::beast::http::make_chunk_last(),
                std::bind(&WebAPI::Session::responded, session, std::placeholders::_1, std::placeholders::_2, true));
        }
        return;
    }
    auto buffer = buffering.currentlySentBuffers.empty() ? outputStreamingBufferPool.newBuffer() : buffering.currentlySentBuffers.front().first;
    const auto bytesToCopy = std::min(output.size() - bytesSent, outputStreamingBufferPool.bufferSize());
    if (buffering.currentlySentBuffers.empty()) {
        buffering.currentlySentBuffers.emplace_back(std::pair(buffer, bytesToCopy));
    }
    outputStreamingLock.unlock();

    auto buildLock = m_setup->building.lockToRead();
    output.copy(buffer->data(), bytesToCopy, bytesSent);
    buildLock.unlock();
    boost::beast::net::async_write(session->socket(), boost::beast::http::make_chunk(boost::asio::buffer(buffer->data(), bytesToCopy)),
        std::bind(&BuildAction::continueStreamingExistingOutputToSession, shared_from_this(), session, std::ref(buffering), std::placeholders::_1,
            std::placeholders::_2));
}

void BuildAction::continueStreamingNewOutputToSession(std::shared_ptr<WebAPI::Session> session, OutputBufferingForSession &buffering,
    const boost::system::error_code &error, std::size_t bytesTransferred)
{
    auto outputStreamingLock = std::unique_lock<std::mutex>(m_outputStreamingMutex);
    buffering.bytesSent += bytesTransferred;
    buffering.currentlySentBuffers.clear();
    buffering.currentlySentBufferRefs.clear();
    if (error) {
        m_bufferingForSession.erase(session);
        return;
    }
    if (buffering.outstandingBuffersToSend.empty()) {
        if (isDone()) {
            m_bufferingForSession.erase(session);
            outputStreamingLock.unlock();
            boost::beast::net::async_write(session->socket(), boost::beast::http::make_chunk_last(),
                std::bind(&WebAPI::Session::responded, session, std::placeholders::_1, std::placeholders::_2, true));
        }
        return;
    }
    buffering.outstandingBuffersToSend.swap(buffering.currentlySentBuffers);
    buffering.currentlySentBufferRefs.reserve(buffering.currentlySentBuffers.size());
    for (const auto &currentBuffer : buffering.currentlySentBuffers) {
        buffering.currentlySentBufferRefs.emplace_back(boost::asio::buffer(*currentBuffer.first, currentBuffer.second));
    }
    boost::beast::net::async_write(session->socket(), boost::beast::http::make_chunk(buffering.currentlySentBufferRefs),
        std::bind(&BuildAction::continueStreamingNewOutputToSession, shared_from_this(), session, std::ref(buffering), std::placeholders::_1,
            std::placeholders::_2));
}

template <typename OutputType> void BuildAction::appendOutput(OutputType &&output)
{
    if (output.empty() || !m_setup) {
        return;
    }

    auto lock = m_setup->building.lockToWrite();
    this->output.append(output);
    lock.unlock();

    OutputBufferingForSession::BufferPile buffers;
    for (std::size_t offset = 0; offset < output.size(); offset += buffers.back().second) {
        const auto bytesToBuffer = std::min(output.size() - offset, outputStreamingBufferPool.bufferSize());
        auto buffer = buffers.emplace_back(std::pair(outputStreamingBufferPool.newBuffer(), bytesToBuffer));
        output.copy(buffer.first->data(), bytesToBuffer, offset);
    }

    auto outputStreamingLock = std::unique_lock<std::mutex>(m_outputStreamingMutex);
    for (auto &bufferingForSession : m_bufferingForSession) {
        auto &buffering = bufferingForSession.second;
        auto &currentlySentBuffers = buffering->currentlySentBuffers;
        if (currentlySentBuffers.empty() && buffering->existingOutputSent) {
            auto &session = bufferingForSession.first;
            auto &currentlySentBufferRefs = buffering->currentlySentBufferRefs;
            currentlySentBuffers.insert(currentlySentBuffers.end(), buffers.begin(), buffers.end());
            for (const auto &buffer : buffers) {
                currentlySentBufferRefs.emplace_back(boost::asio::buffer(buffer.first->data(), buffer.second));
            }
            boost::beast::net::async_write(session->socket(), boost::beast::http::make_chunk(currentlySentBufferRefs),
                std::bind(&BuildAction::continueStreamingNewOutputToSession, shared_from_this(), session, std::ref(*buffering), std::placeholders::_1,
                    std::placeholders::_2));
        } else {
            auto &outstandingBuffersToSend = buffering->outstandingBuffersToSend;
            outstandingBuffersToSend.insert(outstandingBuffersToSend.end(), buffers.begin(), buffers.end());
        }
    }
}

/*!
 * \brief Internally called to append output and spread it to all waiting sessions.
 */
void BuildAction::appendOutput(std::string &&output)
{
    appendOutput<std::string>(std::move(output));
}

/*!
 * \brief Internally called to append output and spread it to all waiting sessions.
 */
void BuildAction::appendOutput(std::string_view output)
{
    appendOutput<std::string_view>(std::forward<std::string_view>(output));
}

} // namespace LibRepoMgr
