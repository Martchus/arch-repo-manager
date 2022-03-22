#ifndef LIBREPOMGR_MULTI_SESSION_H
#define LIBREPOMGR_MULTI_SESSION_H

#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>

namespace LibRepoMgr {

template <typename SessionResponse> class MultiSession {
public:
    using ContainerType = std::vector<SessionResponse>;
    using HandlerType = std::move_only_function<void(ContainerType &&)>;
    using SharedPointerType = std::shared_ptr<MultiSession<SessionResponse>>;

    explicit MultiSession(boost::asio::io_context &ioContext, HandlerType &&handler);
    ~MultiSession();

    static SharedPointerType create(boost::asio::io_context &ioContext, HandlerType &&handler);
    void addResponses(const ContainerType &);
    void addResponse(SessionResponse &&);

protected:
    boost::asio::io_context &ioContext();
    ContainerType &allResponses();
    const ContainerType &allResponses() const;

private:
    boost::asio::io_context &m_ioContext;
    HandlerType m_handler;
    ContainerType m_allResponses;
    std::mutex m_mutex;
};

template <typename SessionResponse>
inline MultiSession<SessionResponse>::MultiSession(boost::asio::io_context &ioContext, HandlerType &&handler)
    : m_ioContext(ioContext)
    , m_handler(handler)
{
}

template <typename SessionResponse> inline MultiSession<SessionResponse>::~MultiSession()
{
    boost::asio::post(m_ioContext.get_executor(),
        [handler = std::move(m_handler), allResponses = std::move(m_allResponses)]() mutable { handler(std::move(allResponses)); });
}

template <typename SessionResponse> inline void MultiSession<SessionResponse>::addResponses(const ContainerType &responses)
{
    const auto lock = std::unique_lock(m_mutex);
    m_allResponses.insert(m_allResponses.end(), responses.begin(), responses.end());
}

template <typename SessionResponse> inline void MultiSession<SessionResponse>::addResponse(SessionResponse &&response)
{
    const auto lock = std::unique_lock(m_mutex);
    m_allResponses.emplace_back(std::move(response));
}

template <typename SessionResponse> inline boost::asio::io_context &MultiSession<SessionResponse>::ioContext()
{
    return m_ioContext;
}

template <typename SessionResponse> inline typename MultiSession<SessionResponse>::ContainerType &MultiSession<SessionResponse>::allResponses()
{
    return m_allResponses;
}

template <typename SessionResponse>
inline const typename MultiSession<SessionResponse>::ContainerType &MultiSession<SessionResponse>::allResponses() const
{
    return m_allResponses;
}

template <typename SessionResponse>
inline typename MultiSession<SessionResponse>::SharedPointerType MultiSession<SessionResponse>::create(
    boost::asio::io_context &ioContext, MultiSession<SessionResponse>::HandlerType &&handler)
{
    return std::make_shared<MultiSession<SessionResponse>>(ioContext, std::move(handler));
}

template <> class MultiSession<void> {
public:
    using HandlerType = std::function<void(void)>;
    using SharedPointerType = std::shared_ptr<MultiSession<void>>;

    explicit MultiSession(boost::asio::io_context &ioContext, HandlerType &&handler);
    ~MultiSession();

    static std::shared_ptr<MultiSession<void>> create(boost::asio::io_context &ioContext, HandlerType &&handler);

protected:
    boost::asio::io_context &ioContext();

private:
    boost::asio::io_context &m_ioContext;
    HandlerType m_handler;
};

inline MultiSession<void>::MultiSession(boost::asio::io_context &ioContext, HandlerType &&handler)
    : m_ioContext(ioContext)
    , m_handler(handler)
{
}

inline MultiSession<void>::~MultiSession()
{
    boost::asio::post(m_ioContext.get_executor(), std::move(m_handler));
}

inline typename MultiSession<void>::SharedPointerType MultiSession<void>::create(
    boost::asio::io_context &ioContext, MultiSession<void>::HandlerType &&handler)
{
    return std::make_shared<MultiSession<void>>(ioContext, std::move(handler));
}

inline boost::asio::io_context &MultiSession<void>::ioContext()
{
    return m_ioContext;
}

} // namespace LibRepoMgr

#endif // LIBREPOMGR_MULTI_SESSION_H
