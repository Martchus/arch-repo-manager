#ifndef LIBREPOMGR_SUB_PROCESS_H
#define LIBREPOMGR_SUB_PROCESS_H

#include "./subprocessfwd.h"

#include <c++utilities/application/global.h>
#include <c++utilities/conversion/stringbuilder.h>

#include <boost/asio/buffers_iterator.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/streambuf.hpp>

#include <boost/filesystem/path.hpp>

#include <boost/process/v1/async.hpp>
#include <boost/process/v1/async_pipe.hpp>
#include <boost/process/v1/child.hpp>
#include <boost/process/v1/extend.hpp>
#include <boost/process/v1/group.hpp>
#include <boost/process/v1/io.hpp>
#include <boost/process/v1/search_path.hpp>

#include <memory>

namespace LibRepoMgr {

/// \brief The ProcessResult struct holds data about a concluded BaseProcessSession/ProcessSession/BuildProcessSession.
struct ProcessResult {
    ProcessResult() = default;
    ProcessResult(const ProcessResult &other) = delete;
    ProcessResult(ProcessResult &&other) = default;
    std::string output, error;
    std::error_code errorCode;
    int exitCode = -1;
};

/// \brief The BaseProcessSession class is the base for ProcessSession and BuildProcessSession.
class BaseProcessSession {
public:
    using Handler = ProcessHandler;

    explicit BaseProcessSession(boost::asio::io_context &ioContext, Handler &&handler);
    ~BaseProcessSession();

    boost::process::group group;
    boost::process::child child;
    ProcessResult result;

protected:
    boost::asio::io_context &m_ioContext;
    Handler m_handler;
};

inline BaseProcessSession::BaseProcessSession(boost::asio::io_context &ioContext, Handler &&handler)
    : child()
    , m_ioContext(ioContext)
    , m_handler(std::move(handler))
{
}

inline BaseProcessSession::~BaseProcessSession()
{
    if (!m_handler) {
        return;
    }
    boost::asio::post(m_ioContext, [child = std::move(this->child), result = std::move(this->result), handler = std::move(m_handler)]() mutable {
        handler(std::move(child), std::move(result));
    });
}

class BasicProcessSession : public std::enable_shared_from_this<BasicProcessSession>, public BaseProcessSession {
public:
    explicit BasicProcessSession(boost::asio::io_context &ioContext, Handler &&handler);
    template <typename... ChildArgs> void launch(ChildArgs &&...childArgs);
};

inline BasicProcessSession::BasicProcessSession(boost::asio::io_context &ioContext, Handler &&handler)
    : BaseProcessSession(ioContext, std::move(handler))
{
}

template <typename... ChildArgs> void BasicProcessSession::launch(ChildArgs &&...childArgs)
{
    try {
        child = boost::process::child(
            m_ioContext, group, std::forward<ChildArgs>(childArgs)...,
            boost::process::on_exit =
                [session = shared_from_this()](int exitCode, const std::error_code &errorCode) {
                    session->result.exitCode = exitCode;
                    session->result.errorCode = errorCode;
                },
            boost::process::extend::on_error
            = [session = shared_from_this()](auto &, const std::error_code &errorCode) { session->result.errorCode = errorCode; });
    } catch (const boost::process::process_error &e) {
        result.errorCode = e.code();
        result.error = CppUtilities::argsToString("unable to launch: ", e.what());
        return;
    }
}

/// \brief The ProcessSession class is used to spawn a process, e.g. from a build action, capturing stdout/stderr in-memory.
class ProcessSession : public std::enable_shared_from_this<ProcessSession>, public BaseProcessSession {
public:
    explicit ProcessSession(boost::asio::io_context &ioContext, Handler &&handler);
    template <typename... ChildArgs> void launch(ChildArgs &&...childArgs);

    boost::process::async_pipe outputPipe, errorPipe;

private:
    static std::string streambufToString(boost::asio::streambuf &buf);

    boost::asio::streambuf m_outputBuffer, m_errorBuffer;
};

inline ProcessSession::ProcessSession(boost::asio::io_context &ioContext, Handler &&handler)
    : BaseProcessSession(ioContext, std::move(handler))
    , outputPipe(ioContext)
    , errorPipe(ioContext)
{
}

inline std::string ProcessSession::streambufToString(boost::asio::streambuf &buf)
{
    const auto begin = boost::asio::buffers_begin(buf.data());
    return std::string(begin, begin + static_cast<std::ptrdiff_t>(buf.size()));
}

template <typename... ChildArgs> void ProcessSession::launch(ChildArgs &&...childArgs)
{
    try {
        child = boost::process::child(
            m_ioContext, group, std::forward<ChildArgs>(childArgs)..., boost::process::std_out > outputPipe, boost::process::std_err > errorPipe,
            boost::process::on_exit =
                [session = shared_from_this()](int exitCode, const std::error_code &errorCode) {
                    session->result.exitCode = exitCode;
                    session->result.errorCode = errorCode;
                },
            boost::process::extend::on_error
            = [session = shared_from_this()](auto &, const std::error_code &errorCode) { session->result.errorCode = errorCode; });
    } catch (const boost::process::process_error &e) {
        result.errorCode = e.code();
        result.error = CppUtilities::argsToString("unable to launch: ", e.what());
        return;
    }
    boost::asio::async_read(outputPipe, m_outputBuffer, [session = shared_from_this()](const auto &ec, auto bytesTransferred) {
        CPP_UTILITIES_UNUSED(ec)
        CPP_UTILITIES_UNUSED(bytesTransferred)
        session->result.output = streambufToString(session->m_outputBuffer);
    });
    boost::asio::async_read(errorPipe, m_errorBuffer, [session = shared_from_this()](const auto &ec, auto bytesTransferred) {
        CPP_UTILITIES_UNUSED(ec)
        CPP_UTILITIES_UNUSED(bytesTransferred)
        session->result.error = streambufToString(session->m_errorBuffer);
    });
}

inline boost::filesystem::path findExecutable(const std::string &nameOrPath)
{
    return nameOrPath.find('/') == std::string::npos ? boost::process::search_path(nameOrPath) : boost::filesystem::path(nameOrPath);
}

inline bool checkExecutable(const boost::filesystem::path &path)
{
    auto ec = boost::system::error_code();
    return !path.empty() && boost::filesystem::exists(path, ec);
}

} // namespace LibRepoMgr

#endif // LIBREPOMGR_SUB_PROCESS_H
