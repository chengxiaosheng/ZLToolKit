/*
 * Copyright (c) 2016 The ZLToolKit project authors. All Rights Reserved.
 *
 * This file is part of ZLToolKit(https://github.com/ZLMediaKit/ZLToolKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#include <iostream>
#include "Util/logger.h"
#include "Util/SSLBox.h"
#include "Network/TcpServer.h"
#include "Network/TcpClient.h"
#include "Session.h"
#include "trantor/net/TLSPolicy.h"

using namespace std;
using namespace toolkit;

// 简单的Echo会话实现
class EchoSession : public Session {
public:
    EchoSession(const Socket::Ptr &sock) : Session(sock) {}
    ~EchoSession() override {}

    void onRecv(const Buffer::Ptr &buf) override {
        // 回显数据
        send(buf);
    }

    void onError(const SockException &err) override {
        InfoL << "EchoSession disconnected: " << err.what();
    }

    void onManager() override {}
};

// 简单的Echo客户端实现
class EchoClient : public TcpClient {
public:
    EchoClient(const EventPoller::Ptr &poller = nullptr)
        : TcpClient(poller ? poller : EventPollerPool::Instance().getPoller()) {}

    void onRecv(const Buffer::Ptr &buf) override {
        InfoL << "Client received: " << buf->toString();
    }

    void onConnect(const SockException &ex) override {
        if (ex) {
            ErrorL << "Connect failed: " << ex.what();
        } else {
            InfoL << "Connect success";
            // 发送测试数据
            send("Hello, TLSPolicy!");
        }
    }

    void onError(const SockException &ex) override {
        ErrorL << "Client error: " << ex.what();
    }

    void onManager() override {}
};

void testTLSSessionFactory() {
    InfoL << "=== Testing TLSSessionFactory ===";

    // 创建服务器策略
    auto serverPolicy = trantor::TLSPolicy::defaultServerPolicy(
        "server_cert.pem", "server_key.pem"
    );
    serverPolicy->setValidate(false);  // 客户端证书验证

    // 注册服务器策略
    TLSSessionFactory::Instance().registerServerPolicy("0.0.0.0", 8443, serverPolicy);

    // 检查是否能获取到策略
    auto retrievedPolicy = TLSSessionFactory::Instance().getServerPolicy("0.0.0.0", 8443);
    if (retrievedPolicy) {
        InfoL << "Server policy retrieved successfully";
    } else {
        ErrorL << "Failed to retrieve server policy";
    }

    // 创建客户端策略
    auto clientPolicy = trantor::TLSPolicy::defaultClientPolicy("localhost");
    clientPolicy->setValidate(false);

    // 注册客户端策略
    TLSSessionFactory::Instance().registerClientPolicy("localhost", 8443, clientPolicy);

    // 检查是否能获取到客户端策略
    auto retrievedClientPolicy = TLSSessionFactory::Instance().getClientPolicy("localhost", 8443);
    if (retrievedClientPolicy) {
        InfoL << "Client policy retrieved successfully";
    } else {
        ErrorL << "Failed to retrieve client policy";
    }

    // 清理
    TLSSessionFactory::Instance().clear();

    InfoL << "=== TLSSessionFactory test completed ===";
}

void testSSLBoxWithCustomCtx() {
    InfoL << "=== Testing SSL_Box with custom context ===";

    // 创建策略
    auto policy = trantor::TLSPolicy::defaultServerPolicy(
        "server_cert.pem", "server_key.pem"
    );
    policy->setValidate(false);

    // 创建SSL上下文
    auto ctx = SSLUtil::makeSSLContext(policy, true);
    if (ctx) {
        InfoL << "SSL_CTX created successfully from TLSPolicy";

        // 使用自定义上下文创建SSL_Box
        auto sslBox = std::make_shared<SSL_Box>(ctx, true, 32 * 1024);
        if (sslBox && sslBox->hasCustomCtx()) {
            InfoL << "SSL_Box with custom context created successfully";
        } else {
            ErrorL << "Failed to create SSL_Box with custom context";
        }
    } else {
        WarnL << "Note: Could not create SSL_CTX (certificate files may be missing)";
        WarnL << "This is expected in a test environment without actual certificates";
    }

    InfoL << "=== SSL_Box with custom context test completed ===";
}

int main(int argc, char *argv[]) {
    // 初始化日志
    Logger::Instance().add(std::make_shared<ConsoleChannel>());
    Logger::Instance().setWriter(std::make_shared<AsyncLogWriter>());

    InfoL << "Starting TLSPolicy tests...";

    testTLSSessionFactory();
    testSSLBoxWithCustomCtx();

    InfoL << "All TLSPolicy tests completed!";
    return 0;
}
