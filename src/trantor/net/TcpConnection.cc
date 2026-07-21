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
#include <Network/sockutil.h>    // SockUtil::setNoDelay
#include <Poller/EventPoller.h>
#include <Util/SSLBox.h>
#include <Util/logger.h>
#include <Util/util.h>

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
namespace
{
// 跨平台 UTF-8 / 宽字符文件打开
FILE *openFileUtf8(const char *name)
{
#ifdef _WIN32
    int wlen = MultiByteToWideChar(CP_UTF8, 0, name, -1, nullptr, 0);
    if (wlen <= 0)
        return nullptr;
    std::wstring w(wlen, 0);
    MultiByteToWideChar(CP_UTF8, 0, name, -1, &w[0], wlen);
    return _wfopen(w.c_str(), L"rb");
#else
    return std::fopen(name, "rb");
#endif
}

FILE *openFileWide(const wchar_t *name)
{
#ifdef _WIN32
    return _wfopen(name, L"rb");
#else
    (void)name;
    return nullptr;  // 非 Windows 不支持宽字符路径
#endif
}

constexpr size_t kFileChunkSize = 64 * 1024;
}  // namespace

// 挂起的 file/stream 生产者
struct TcpConnection::PendingProducer
{
    std::function<Buffer::Ptr()> next;  // 返回 nullptr 表示结束
    bool done = false;
};

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

void TcpConnection::startEncryption(TLSPolicyPtr /*policy*/,
                                    bool /*isServer*/,
                                    std::function<void(
                                        const TcpConnectionPtr &)> /*cb*/)
{
    // TODO: 支持 STARTTLS 风格的运行时 TLS 升级（drogon 当前未使用）
}

void TcpConnection::forwardToTLSBuffer(MsgBuffer * /*buffer*/)
{
    // TODO: 与 startEncryption 配套（drogon 当前未使用）
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
        fireCloseInLoop(SockException(Err_eof, "send failed"));
        return;  // 传输层已销毁，丢弃
    }

    size_t sz = buffer->size();
    if (0 > h->send(std::move(buffer)))
    {
        // socket 已关闭
        h->safeShutdown(SockException(Err_eof, "send failed"));
        return;
    }
    lastActivityTick_.resetTime();

    // 高水位（best-effort）
    if (highWaterMarkCallback_ && highWaterMark_ > buffer->size())
    {
        highWaterMarkCallback_(shared_from_this(), sz);
    }
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
void TcpConnection::setHighWaterMarkCallback(const HighWaterMarkCallback &cb,
                                             size_t markLen)
{
    highWaterMarkCallback_ = cb;
    highWaterMark_ = markLen;
}

void TcpConnection::setTcpNoDelay(bool on)
{
    if (sock_)
        SockUtil::setNoDelay(sock_->rawFD(), on);
}

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

MsgBuffer *TcpConnection::getRecvBuffer()
{
    return &readBuffer_;
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

    readBuffer_.append(buf->data(), buf->size());
    if (recvMsgCallback_)
        recvMsgCallback_(shared_from_this(), &readBuffer_);
}

void TcpConnection::fireCloseInLoop(const SockException &ex)
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
    fireCloseInLoop(ex);
}

void TcpConnection::handleWriteComplete()
{
    highWaterMarkFired_ = false;
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
            fireCloseInLoop(SockException(Err_shutdown, "idle kickoff"));
        }
    }
}

}  // namespace trantor
