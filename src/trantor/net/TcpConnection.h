/**
 *
 *  @file TcpConnection.h
 *  @author An Tao
 *
 *  Public header file in trantor lib.
 *
 *  Copyright 2018, An Tao.  All rights reserved.
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the License file.
 *
 *  薄委托实现：基于 toolkit::SocketHelper（Session / TcpClient 均派生自它）
 *  通过 set_session<T>() 绑定传输层，不破坏 Session/TcpClient 继承层级。
 *
 */

#pragma once
#include <toolkit/exports.h>
#include <Poller/EventPoller.h>
#include <Network/Socket.h>   // toolkit::SocketHelper, toolkit::Socket
#include <Network/Buffer.h>   // toolkit::Buffer
#include <Util/SSLBox.h>      // toolkit::SSL_Box
#include <trantor/net/InetAddress.h>
#include <Util/util.h>
#include <trantor/utils/MsgBuffer.h>
#include <trantor/net/callbacks.h>
#include <trantor/net/Certificate.h>
#include <trantor/net/TLSPolicy.h>
#include <trantor/net/AsyncStream.h>
#include <memory>
#include <functional>
#include <string>
#include <atomic>

namespace trantor
{
class BufferNode;
class TimingWheel;

struct SSLContext;
using SSLContextPtr = std::shared_ptr<SSLContext>;

/**
 * @brief This class represents a TCP connection.
 *
 * 薄委托具体类：持 weak_ptr<toolkit::SocketHelper>（服务端为 HttpSession，
 * 客户端为 HttpClientConn）。send 直接走 Socket 自带的安全队列；
 * sendFile/sendStream/sendAsyncStream 经 Socket::onFlush 分块拉取。无 BufferNode
 * 写队列。loop_ 在 set_session 中经 helper->getPoller() 权威赋值（= socket 所在
 * poller），不依赖构造期全局 poller 选择。
 */
class ZLTOOLKIT_EXPORT TcpConnection
    : public std::enable_shared_from_this<TcpConnection>
{
  public:
    TcpConnection() = default;
    ~TcpConnection();

    /**
     * @brief 绑定传输层（toolkit::Session 或 toolkit::TcpClient，均为
     * SocketHelper 派生）。构造后、connectEstablished() 前调用一次。
     * 持 weak_ptr 破环；loop_ 在此权威取 socket 所属 poller。
     */
    template <typename SessionType>
    void set_session(const std::shared_ptr<SessionType> &session)
    {
        auto helper =
            std::static_pointer_cast<toolkit::SocketHelper>(session);

        helper_ = helper;
        loop_ = helper->getPoller();

        if (auto sock = helper->getSock())
        {
            localAddr_ =
                InetAddress(sock->get_local_ip(), sock->get_local_port());
            peerAddr_ =
                InetAddress(sock->get_peer_ip(), sock->get_peer_port());
        }
        // 接线 SSL 错误回调（若为 TLS 包装的传输）
        if (helper)
        {
            if (auto *box = helper->getSSLBox())
            {
                std::weak_ptr<TcpConnection> weakSelf = weak_from_this();
                box->setOnErr([weakSelf](SSLError err) {
                    if (auto self = weakSelf.lock())
                    {
                        if (self->sslErrorCallback_)
                            self->sslErrorCallback_(err);
                    }
                });
            }
        }
    }

    /// @brief Send some data to the peer.
    void send(const char *msg, size_t len);
    void send(const void *msg, size_t len);
    void send(const std::string &msg);
    void send(std::string &&msg);
    void send(const MsgBuffer &buffer);
    void send(MsgBuffer &&buffer);
    void send(const std::shared_ptr<toolkit::Buffer> &buffer);
    void sendFile(std::shared_ptr<BufferNode> &&fileNode);

    /// @brief Send a file (UTF-8 path).
    void sendFile(const char *fileName,
                  long long offset = 0,
                  long long length = 0);
    /// @brief Send a file (wide path, Windows UCS-2).
    void sendFile(const wchar_t *fileName,
                  long long offset = 0,
                  long long length = 0);
    /// @brief Send a stream via pull callback (returns 0 = end).
    void sendStream(std::function<std::size_t(char *, std::size_t)> callback);
    /// @brief Send an async stream.
    AsyncStreamPtr sendAsyncStream(bool disableKickoff = false);

    const InetAddress &localAddr() const { return localAddr_; }
    const InetAddress &peerAddr() const { return peerAddr_; }
    bool connected() const { return connected_; }
    bool disconnected() const { return !connected_; }

    /// @brief Shutdown the writing direction (half-close).
    void shutdown();
    /// @brief Close the connection forcefully.
    void forceClose();

    std::shared_ptr<toolkit::EventPoller> getLoop() { return loop_; }

    /// @brief Set/get custom context.
    void setContext(const std::shared_ptr<void> &context)
    {
        contextPtr_ = context;
    }
    void setContext(std::shared_ptr<void> &&context)
    {
        contextPtr_ = std::move(context);
    }
    template <typename T>
    std::shared_ptr<T> getContext() const
    {
        return std::static_pointer_cast<T>(contextPtr_);
    }
    bool hasContext() const { return (bool)contextPtr_; }
    void clearContext() { contextPtr_.reset(); }

    std::string applicationProtocol() const;
    void keepAlive();
    bool isKeepAlive();
    size_t bytesSent() const;
    size_t bytesReceived() const;
    bool isSSLConnection() const;
    CertificatePtr peerCertificate() const;
    std::string sniName() const;

    // ---- callback setters ----
    void setRecvMsgCallback(const RecvMessageCallback &cb)
    {
        recvMsgCallback_ = cb;
    }
    void setRecvMsgCallback(RecvMessageCallback &&cb)
    {
        recvMsgCallback_ = std::move(cb);
    }
    void setConnectionCallback(const ConnectionCallback &cb)
    {
        connectionCallback_ = cb;
    }
    void setConnectionCallback(ConnectionCallback &&cb)
    {
        connectionCallback_ = std::move(cb);
    }
    void setWriteCompleteCallback(const WriteCompleteCallback &cb)
    {
        writeCompleteCallback_ = cb;
    }
    void setWriteCompleteCallback(WriteCompleteCallback &&cb)
    {
        writeCompleteCallback_ = std::move(cb);
    }
    void setCloseCallback(const CloseCallback &cb)
    {
        closeCallback_ = cb;
    }
    void setCloseCallback(CloseCallback &&cb)
    {
        closeCallback_ = std::move(cb);
    }
    CloseCallback getCloseCallback() const { return closeCallback_; }
    void setSSLErrorCallback(const SSLErrorCallback &cb)
    {
        sslErrorCallback_ = cb;
    }
    void setSSLErrorCallback(SSLErrorCallback &&cb)
    {
        sslErrorCallback_ = std::move(cb);
    }

    // ---- 由传输层（HttpSession / HttpClientConn）调用 ----
    void connectEstablished();
    void connectDestroyed();
    void enableKickingOff(size_t timeout,
                          const std::shared_ptr<TimingWheel> &timingWheel =
                              nullptr);

    // ---- 由传输层转发 ----
    void handleRecv(const toolkit::Buffer::Ptr &buf);
    void handleClose(const toolkit::SockException &ex);
    void handleWriteComplete();
    void handleManagerTick();

  private:
    // 锁 helper_ 返回 SSL_Box（可能为 nullptr）
    toolkit::SSL_Box *sslBox() const;

    // 触发关闭流程（在当前 poller 线程内联执行；命名沿用旧称，非投递 loop）
    void fireClose(const toolkit::SockException &ex);

    std::weak_ptr<toolkit::SocketHelper> helper_;
    std::shared_ptr<toolkit::EventPoller> loop_;
    InetAddress localAddr_;
    InetAddress peerAddr_;
    trantor::MsgBuffer readBuffer_;
    std::atomic_bool connected_{false};
    std::atomic_bool closed_{false};

    // idle kickoff
    size_t idleTimeout_{60 * 1000};
    toolkit::Ticker lastActivityTick_{};
    bool keepAlive_{false};
    bool disableKickoff_{false};

    // 挂起的 file/stream/asyncStream 生产者
    struct PendingProducer;
    std::shared_ptr<PendingProducer> pendingProducer_;

  protected:
    // callbacks
    RecvMessageCallback recvMsgCallback_;
    ConnectionCallback connectionCallback_;
    CloseCallback closeCallback_;
    WriteCompleteCallback writeCompleteCallback_;
    SSLErrorCallback sslErrorCallback_;

  private:
    std::shared_ptr<void> contextPtr_;
};
ZLTOOLKIT_EXPORT SSLContextPtr newSSLContext(const TLSPolicy &policy,
                                             bool server);

}  // namespace trantor
