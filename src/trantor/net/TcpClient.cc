// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// 重写：trantor::TcpClient = HttpClientConn（具体类，继承 toolkit::TcpClient）。

#include <trantor/net/TcpClient.h>

#include <Network/Socket.h>
#include <Poller/EventPoller.h>
#include <Util/logger.h>
#include <trantor/net/TcpConnection.h>
#include <trantor/net/TLSPolicy.h>

#include <algorithm>
#include <memory>
#include <string>

using namespace toolkit;

namespace trantor
{

TcpClient::TcpClient(const std::shared_ptr<EventPoller> &loop,
                     const InetAddress &serverAddr,
                     const std::string &nameArg)
    : toolkit::TcpClient(loop),
      loop_(loop),
      serverAddr_(serverAddr),
      name_(nameArg)
{
    TraceL << "TcpClient::TcpClient[" << name_ << "] -> "
           << serverAddr_.toIpPort();
}

TcpClient::~TcpClient()
{
    stop();
}

void TcpClient::connect()
{
    connect_ = true;
    TraceL << "TcpClient::connect[" << name_ << "] -> "
           << serverAddr_.toIpPort();
    // toolkit::TcpClient::startConnect（若为 TLS 包装器，其 override 会记录 host 用于 SNI）
    this->startConnect(serverAddr_.toIp(),
                       serverAddr_.toPort(),
                       connectTimeout_);
}

void TcpClient::disconnect()
{
    connect_ = false;
    std::lock_guard<std::mutex> lock(mutex_);
    if (connection_)
        connection_->shutdown();
}

void TcpClient::stop()
{
    connect_ = false;
    // toolkit::TcpClient::shutdown 关闭 socket / 取消连接
    this->toolkit::TcpClient::shutdown(
        SockException(Err_shutdown, "TcpClient::stop"));
}

void TcpClient::onConnect(const SockException &ex)
{
    if (ex)
    {
        // 连接失败
        TraceL << "TcpClient[" << name_ << "] connect failed: " << ex.what();
        if (connectionErrorCallback_)
            connectionErrorCallback_();
        if (retry_ && connect_)
            retryInLoop();
        return;
    }
    // 连接成功：应用 sockopt，创建 TcpConnection
    if (sockOptCallback_ && this->getSock())
        sockOptCallback_(this->getSock()->rawFD());

    auto conn = std::make_shared<TcpConnection>();
    conn->set_session(shared_from_this());  // shared_from_this -> SocketHelper
    conn->setConnectionCallback(connectionCallback_);
    conn->setRecvMsgCallback(messageCallback_);
    conn->setWriteCompleteCallback(writeCompleteCallback_);
    // SSL 错误回调已在 set_session 内经 getSSLBox() 接线（TLS 时）
    if (sslErrorCallback_)
        conn->setSSLErrorCallback(sslErrorCallback_);

    std::weak_ptr<TcpClient> weakSelf(
        std::static_pointer_cast<TcpClient>(shared_from_this()));
    conn->setCloseCallback([weakSelf](const TcpConnectionPtr &c) {
        if (auto self = weakSelf.lock())
            self->removeConnection(c);
        else
            c->getLoop()->async([c] { c->connectDestroyed(); }, false);
    });

    {
        std::lock_guard<std::mutex> lock(mutex_);
        connection_ = conn;
    }
    conn->connectEstablished();  // 触发 connectionCallback_(connected)
}

void TcpClient::onError(const SockException &ex)
{
    TraceL << "TcpClient[" << name_ << "] onError: " << ex.what();
    TcpConnectionPtr conn;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        conn = connection_;
    }
    if (conn)
    {
        // handleClose -> fireClose -> closeCallback_(removeConnection)
        conn->handleClose(ex);
    }
    if (retry_ && connect_)
        retryInLoop();
}

void TcpClient::removeConnection(const TcpConnectionPtr &conn)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (connection_ == conn)
            connection_.reset();
    }
    if (conn)
        conn->getLoop()->async([conn] { conn->connectDestroyed(); }, false);
}

void TcpClient::retryInLoop()
{
    auto weakSelf = std::weak_ptr<TcpClient>(
        std::static_pointer_cast<TcpClient>(shared_from_this()));
    loop_->doDelayTask(retryDelayMs_, [weakSelf]() -> uint64_t {
        auto self = weakSelf.lock();
        if (!self || !self->connect_)
            return 0;
        self->connect();
        return 0;
    });
}

void TcpClient::enableSSL(bool useOldTLS,
                          bool validateCert,
                          std::string hostname,
                          const std::vector<std::pair<std::string, std::string>>
                              &sslConfCmds,
                          const std::string &certPath,
                          const std::string &keyPath,
                          const std::string &caPath)
{
    if (!hostname.empty())
    {
        std::transform(hostname.begin(),
                       hostname.end(),
                       hostname.begin(),
                       [](unsigned char c) { return std::tolower(c); });
    }
    auto policy = TLSPolicy::defaultClientPolicy();
    policy->setValidate(validateCert)
        .setUseOldTLS(useOldTLS)
        .setConfCmds(sslConfCmds)
        .setCertPath(certPath)
        .setKeyPath(keyPath)
        .setHostname(hostname)
        .setCaPath(caPath);
    enableSSL(std::move(policy));
}

}  // namespace trantor
