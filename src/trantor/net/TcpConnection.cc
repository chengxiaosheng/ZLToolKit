/**
 *
 *  @file TcpConnection.cc
 *  @author An Tao
 *
 *  薄委托实现：trantor::TcpConnection 基于 toolkit::SocketHelper。
 *
 */

#include <trantor/net/TcpConnection.h>

#include "inner/BufferNode.h"
#include "utils/Utilities.h"

#include <Network/Buffer.h>      // BufferRaw, BufferString, BufferOffset
#include <Poller/EventPoller.h>
#include <Util/SSLBox.h>
#include <Util/logger.h>
#include <Util/util.h>
#include <trantor/net/ParseCursor.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#ifndef SHUT_WR
#define SHUT_WR SD_SEND
#endif
#else
#include <sys/socket.h>
#endif

using namespace toolkit;

namespace trantor
{

// AsyncStream 具体实现：直接转发到 TcpConnection::send（Socket 队列保序）

class AsyncStreamImpl : public AsyncStream
{
public:
    explicit AsyncStreamImpl(std::function<bool(std::shared_ptr<toolkit::Buffer>)> callback)
        : callback_(std::move(callback))
    {
    }
    AsyncStreamImpl() = delete;

    bool send(const std::shared_ptr<toolkit::Buffer> &buffer) override {
        if (callback_) return callback_(buffer);
        return false;
    }
    void close() override
    {
        send(nullptr);
        (void)callback_(nullptr);
        callback_ = nullptr;
    }
    ~AsyncStreamImpl() override
    {
        if (callback_)
            (void)callback_(nullptr);
    }

private:
    std::function<bool(std::shared_ptr<toolkit::Buffer>)> callback_;
};

TcpConnection::~TcpConnection() = default;

// ---------------------------------------------------------------------------
// SSL 辅助
// ---------------------------------------------------------------------------
SSL_Box *TcpConnection::sslBox() const
{
    auto h = helper_.lock();
    return h ? h->getSSLBox() : nullptr;
}

bool TcpConnection::isSSLConnection() const
{
    return sslBox() != nullptr;
}

CertificatePtr TcpConnection::peerCertificate() const
{
    auto *box = sslBox();
    if (!box)
        return nullptr;
    return makeCertificate(box->getPeerCertificate());
}

std::string TcpConnection::sniName() const
{
    auto *box = sslBox();
    return box ? box->getSNIName() : std::string();
}

std::string TcpConnection::applicationProtocol() const
{
    auto *box = sslBox();
    return box ? box->getApplicationProtocol() : std::string();
}

void TcpConnection::send(const char *msg, size_t len)
{
    std::shared_ptr<toolkit::BufferRaw> buffer = nullptr;
    if (msg && len) {
        buffer = toolkit::BufferRaw::create(len);
        buffer->assign(msg, len);
    }
    send(buffer);
}

void TcpConnection::send(const void *msg, size_t len)
{
    send(static_cast<const char *>(msg), len);
}

void TcpConnection::send(const std::string &msg)
{
    auto buffer = toolkit::BufferRaw::create(msg.size());
    buffer->assign(msg.data(), msg.size());
    send(buffer);
}

void TcpConnection::send(std::string &&msg)
{
    send(std::make_shared<toolkit::BufferLikeString>(std::move(msg)));
}

void TcpConnection::send(const MsgBuffer &buffer)
{
    send(buffer.peek(), buffer.readableBytes());
}

void TcpConnection::send(MsgBuffer &&buffer)
{
    send(std::make_shared<MsgBuffer>(std::move(buffer)));
}

void TcpConnection::send(const std::shared_ptr<toolkit::Buffer> &buffer) {
    if (closed_ || !buffer || buffer->size() == 0)
        return;
    auto h = helper_.lock();
    if (!h) {
        fireClose(SockException(Err_eof, "send failed"));
        return;  // 传输层已销毁，丢弃
    }

    if (0 > h->send(std::move(buffer)))
    {
        // socket 已关闭
        h->safeShutdown(SockException(Err_eof, "send failed"));
        return;
    }
    lastActivityTick_.resetTime();
}

void TcpConnection::sendFile(const char *fileName,
                             long long offset,
                             long long length)
{
    assert(fileName);
#ifdef _WIN32
    sendFile(utils::toWidePath(fileName).c_str(), offset, length);
#else   // _WIN32
    auto fileNode = BufferNode::newFileBufferNode(fileName, offset, length);

    if (!fileNode->available())
    {
        ErrorL << fileName << " open error";
        return;
    }

    sendFile(std::move(fileNode));
#endif  // _WIN32
}

void TcpConnection::sendFile(const wchar_t *fileName,
                             long long offset,
                             long long length)
{
    assert(fileName);
#ifndef _WIN32
    sendFile(utils::toNativePath(fileName).c_str(), offset, length);
#else
    auto fileNode = BufferNode::newFileBufferNode(fileName, offset, length);
    if (!fileNode->available())
    {
        ErrorL << fileName << " open error";
        return;
    }
    sendFile(std::move(fileNode));
#endif  // _WIN32
}


void TcpConnection::sendFile(std::shared_ptr<BufferNode> &&fileNode) {
    assert(fileNode->isFile() && fileNode->remainingBytes() > 0);

    auto buffer = std::make_shared<toolkit::BufferOffset<std::shared_ptr<BufferNode>>>(std::move(fileNode));
    this->send(std::move(buffer));
}

void TcpConnection::sendStream(
    std::function<std::size_t(char *, std::size_t)> callback)
{
    if (!callback)
        return;
    size_t len = 0;
    do {
        auto buffer = toolkit::BufferRaw::create(1024 * 64);
        buffer->setSize(callback(buffer->data(), buffer->size()));
        len = buffer->size();
        if (len > 0) {
            this->send(std::move(buffer));
        }
    } while (len > 0);
    // Signal end-of-stream so the producer can clean up (e.g. close a file).
    callback(nullptr, 0);
}

AsyncStreamPtr TcpConnection::sendAsyncStream(bool disableKickoff)
{
    std::weak_ptr<TcpConnection> weak_self = std::dynamic_pointer_cast<TcpConnection>(shared_from_this());
    auto stream = std::make_unique<AsyncStreamImpl>([weak_self](std::shared_ptr<toolkit::Buffer> buffer)-> bool {
        if (buffer == nullptr) return true;
        if (auto strong_self = weak_self.lock()) {
            strong_self->send(std::move(buffer));
            return true;
        }
        return false;
    });

    getLoop()->async([weak_self, disableKickoff]() {
        if (auto strong_self = weak_self.lock()) {
            strong_self->disableKickoff_ =
            strong_self->disableKickoff_ = disableKickoff;
        }
    });


    return stream;
}

// ---------------------------------------------------------------------------
// 地址 / 状态 / 配置
// ---------------------------------------------------------------------------
void TcpConnection::shutdown()
{
    if (auto helper = helper_.lock()) {
        helper->safeShutdown();
    }
}

void TcpConnection::forceClose()
{
    shutdown();
}

size_t TcpConnection::bytesSent() const
{
    if (auto helper = helper_.lock()) {
        if (auto sock = helper->getSock()) {
            return sock->getSendTotalBytes();
        }
    }
    return 0;
}

size_t TcpConnection::bytesReceived() const
{
    if (auto helper = helper_.lock()) {
        if (auto sock = helper->getSock()) {
            return sock->getRecvTotalBytes();
        }
    }
    return 0;
}

void TcpConnection::keepAlive()
{
    keepAlive_ = true;
}

bool TcpConnection::isKeepAlive()
{
    return keepAlive_;
}

void TcpConnection::enableKickingOff(size_t timeout,
                                     const std::shared_ptr<TimingWheel> &)
{
    idleTimeout_ = timeout;
}

// ---------------------------------------------------------------------------
// 由传输层转发
// ---------------------------------------------------------------------------
void TcpConnection::connectEstablished()
{
    connected_ = true;
    closed_ = false;
    lastActivityTick_.resetTime();
    if (connectionCallback_)
        connectionCallback_(shared_from_this());
}

void TcpConnection::connectDestroyed()
{
    connected_ = false;
}

void TcpConnection::handleRecv(const Buffer::Ptr &buf)
{
    if (!buf || buf->size() == 0 || closed_)
        return;
    lastActivityTick_.resetTime();

    if (readBuffer_.readableBytes() == 0)
    {
        // fresh：以零拷贝视图直接解析 buf（主路径；body 阶段全程零拷贝，
        // 消除原 Buffer::Ptr->readBuffer_ 的入站拷贝回归）
        ParseCursor cursor(buf->data(), buf->size());
        if (recvMsgCallback_)
            recvMsgCallback_(shared_from_this(), &cursor);
        // 不完整尾部落地 leftover（仅 header 行跨 recv 截断时偶发；
        // body 阶段消费完即 cursor==size，不触发）
        if (cursor.consumed() < buf->size())
            readBuffer_.append(buf->data() + cursor.consumed(),
                               buf->size() - cursor.consumed());
    }
    else
    {
        // leftover：先拼接新数据，再以 leftover 为连续基底解析
        readBuffer_.append(buf->data(), buf->size());
        ParseCursor cursor(readBuffer_.peek(), readBuffer_.readableBytes());
        if (recvMsgCallback_)
            recvMsgCallback_(shared_from_this(), &cursor);
        if (cursor.consumed() > 0)
            readBuffer_.retrieve(cursor.consumed());
        if (readBuffer_.readableBytes() == 0)
            readBuffer_.retrieveAll();  // 排空则收缩，回 fresh
    }
}

void TcpConnection::fireClose(const SockException &ex)
{
    bool expected = false;
    if (!closed_.compare_exchange_strong(expected, true))
        return;  // 已关闭
    connected_ = false;
    if (connectionCallback_)
        connectionCallback_(shared_from_this());  // drogon 见 disconnected
    if (closeCallback_)
        closeCallback_(shared_from_this());
    (void)ex;
}

void TcpConnection::handleClose(const SockException &ex)
{
    fireClose(ex);
}

void TcpConnection::handleWriteComplete()
{
    if (pendingProducer_)
    {
        return;
    }
    if (writeCompleteCallback_)
        writeCompleteCallback_(shared_from_this());
}

void TcpConnection::handleManagerTick()
{
    if (!idleTimeout_ || disableKickoff_)
        return;
    if (keepAlive_)
    {
        return;
    }
    if (lastActivityTick_.elapsedTime() > idleTimeout_ * 1000)
    {
        TraceL << "kickoff idle connection, idle=" << lastActivityTick_.elapsedTime() << "ms";

        if (auto conn = helper_.lock()) {
            conn->safeShutdown(SockException(Err_shutdown, "idle kickoff"));
        } else {
            fireClose(SockException(Err_shutdown, "idle kickoff"));
        }
    }
}

}  // namespace trantor
