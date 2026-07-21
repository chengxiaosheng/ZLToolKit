// taken from muduo
// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//
// Copyright 2016, Tao An.  All rights reserved.
//
// 重写为具体类：trantor::TcpClient = HttpClientConn。
// 继承 toolkit::TcpClient，在 onConnect 成功时创建 trantor::TcpConnection
// 并经 set_session 绑定自身。TLS 由 toolkit::TcpClientWithTLSPolicy<trantor::TcpClient>
// 包装（wrapper IS-A trantor::TcpClient），enableSSL 经虚 setTLSPolicy 派发。

#pragma once
#include <Poller/EventPoller.h>
#include <Network/TcpClient.h>     // toolkit::TcpClient
#include <trantor/net/InetAddress.h>
#include <trantor/net/TcpConnection.h>
#include <trantor/net/callbacks.h>
#include <functional>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

namespace trantor
{
/**
 * @brief This class represents a TCP client.
 *
 * 具体类：继承 toolkit::TcpClient，连接成功后创建 trantor::TcpConnection。
 * TLS：实例化 toolkit::TcpClientWithTLSPolicy<trantor::TcpClient>（一类型，
 * 非 TLS 时 box 为空，明文直通）。
 */
class ZLTOOLKIT_EXPORT TcpClient : public toolkit::TcpClient
{
  public:
    TcpClient(const std::shared_ptr<toolkit::EventPoller> &loop,
              const InetAddress &serverAddr,
              const std::string &nameArg);
    ~TcpClient() override;

    /// @brief Connect to the server.
    void connect();

    /// @brief Disconnect (half-close the connection).
    void disconnect();

    /// @brief Stop connecting / tear down.
    void stop();

    /// @brief Get the TCP connection (nullptr until connected).
    TcpConnectionPtr connection() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return connection_;
    }

    std::shared_ptr<toolkit::EventPoller> getLoop() const
    {
        return getPoller();
    }

    bool retry() const { return retry_; }
    void enableRetry() { retry_ = true; }

    const std::string &name() const { return name_; }

    // ---- 回调设置 ----
    void setConnectionCallback(const ConnectionCallback &cb)
    {
        connectionCallback_ = cb;
    }
    void setConnectionCallback(ConnectionCallback &&cb)
    {
        connectionCallback_ = std::move(cb);
    }
    void setConnectionErrorCallback(const ConnectionErrorCallback &cb)
    {
        connectionErrorCallback_ = cb;
    }
    void setMessageCallback(const RecvMessageCallback &cb)
    {
        messageCallback_ = cb;
    }
    void setMessageCallback(RecvMessageCallback &&cb)
    {
        messageCallback_ = std::move(cb);
    }
    void setWriteCompleteCallback(const WriteCompleteCallback &cb)
    {
        writeCompleteCallback_ = cb;
    }
    void setWriteCompleteCallback(WriteCompleteCallback &&cb)
    {
        writeCompleteCallback_ = std::move(cb);
    }
    void setSSLErrorCallback(const SSLErrorCallback &cb)
    {
        sslErrorCallback_ = cb;
    }
    void setSSLErrorCallback(SSLErrorCallback &&cb)
    {
        sslErrorCallback_ = std::move(cb);
    }
    void setSockOptCallback(const SockOptCallback &cb)
    {
        sockOptCallback_ = cb;
    }
    void setSockOptCallback(SockOptCallback &&cb)
    {
        sockOptCallback_ = std::move(cb);
    }

    /**
     * @brief TLS 策略派发（base 无操作；TcpClientWithTLSPolicy 模板 override）。
     * enableSSL(TLSPolicyPtr) 经此虚函数把策略传给 TLS 包装器。
     */
    virtual void setTLSPolicy(TLSPolicyPtr policy)
    {
        (void)policy;
    }

    [[deprecated("Use enableSSL(TLSPolicyPtr policy) instead")]] void enableSSL(
        bool useOldTLS = false,
        bool validateCert = true,
        std::string hostname = "",
        const std::vector<std::pair<std::string, std::string>> &sslConfCmds =
            {},
        const std::string &certPath = "",
        const std::string &keyPath = "",
        const std::string &caPath = "");

    /// @brief Enable SSL encryption with a TLSPolicy.
    void enableSSL(TLSPolicyPtr policy)
    {
        tlsPolicyPtr_ = std::move(policy);
        setTLSPolicy(tlsPolicyPtr_);  // 虚派发 -> TLS 包装器
    }

  protected:
    // ---- toolkit::TcpClient 回调 ----
    void onConnect(const toolkit::SockException &ex) override;
    void onRecv(const toolkit::Buffer::Ptr &buf) override
    {
        if (connection_)
            connection_->handleRecv(buf);
    }
    void onError(const toolkit::SockException &ex) override;
    void onFlush() override
    {
        if (connection_)
            connection_->handleWriteComplete();
    }

  private:
    void removeConnection(const TcpConnectionPtr &conn);
    void retryInLoop();

    std::shared_ptr<toolkit::EventPoller> loop_;
    InetAddress serverAddr_;
    const std::string name_;

    ConnectionCallback connectionCallback_;
    ConnectionErrorCallback connectionErrorCallback_;
    RecvMessageCallback messageCallback_;
    WriteCompleteCallback writeCompleteCallback_;
    SSLErrorCallback sslErrorCallback_;
    SockOptCallback sockOptCallback_;

    std::atomic_bool retry_{false};
    std::atomic_bool connect_{false};
    float connectTimeout_{30.0f};
    int retryDelayMs_{3000};

    mutable std::mutex mutex_;
    TcpConnectionPtr connection_;  // @GuardedBy mutex_
    TLSPolicyPtr tlsPolicyPtr_;
};

}  // namespace trantor
