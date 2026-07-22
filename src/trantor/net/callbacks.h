/**
 *
 *  callbacks.h
 *  An Tao
 *
 *  Public header file in trantor lib.
 *
 *  Copyright 2018, An Tao.  All rights reserved.
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the License file.
 *
 *
 */

#pragma once

#include <functional>
#include <memory>
#include <trantor/net/ParseCursor.h>
namespace trantor
{
enum class SSLError
{
    kSSLHandshakeError,
    kSSLInvalidCertificate,
    kSSLProtocolError
};
using TimerCallback = std::function<void()>;

// the data has been read to (buf, len)
class TcpConnection;
using TcpConnectionPtr = std::shared_ptr<TcpConnection>;
// tcp server and connection callback
using RecvMessageCallback =
    std::function<void(const TcpConnectionPtr &, ParseCursor *)>;
using ConnectionErrorCallback = std::function<void()>;
using ConnectionCallback = std::function<void(const TcpConnectionPtr &)>;
using CloseCallback = std::function<void(const TcpConnectionPtr &)>;
using WriteCompleteCallback = std::function<void(const TcpConnectionPtr &)>;
using SSLErrorCallback = std::function<void(SSLError)>;
using SockOptCallback = std::function<void(int)>;

}  // namespace trantor
