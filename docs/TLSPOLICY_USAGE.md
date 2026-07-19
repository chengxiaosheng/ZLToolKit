# TLSPolicy 支持说明

## 概述

ZLToolKit 现在支持使用 `TLSPolicy` 为不同的监听器和客户端连接配置独立的 TLS 设置，包括证书、私钥、CA 证书、验证模式等。

## 主要功能

### 1. 按监听器配置不同证书（服务器端）

不同的 TCP 监听端口可以使用不同的证书，实现虚拟主机功能。

### 2. 客户端双向 TLS 认证

客户端连接可以配置客户端证书，用于服务器验证客户端身份，实现双向 TLS 认证。

### 3. 按目标主机/端口/协议配置策略

同一客户端可以为不同的目标主机、端口或协议配置不同的 TLS 策略。

### 4. 向后兼容

原有基于 `SSL_Initor` 全局单例的方式仍然有效，新老方式可以混合使用。

## 核心类

### TLSSessionFactory

TLS 会话工厂，单例模式，用于管理和创建基于策略的 SSL 上下文。

```cpp
// 获取单例
TLSSessionFactory &factory = TLSSessionFactory::Instance();

// 注册服务器策略
factory.registerServerPolicy("0.0.0.0", 443, policy);

// 注册客户端策略
factory.registerClientPolicy("api.example.com", 443, policy);
factory.registerClientPolicy("api.example.com", 443, "grpc", grpcPolicy);

// 清除所有策略
factory.clear();
```

### SessionWithTLSPolicy<SessionType>

服务器端 Session 包装器模板，自动根据本地地址和端口查找 TLS 策略并创建 SSL_Box。

```cpp
// 使用方式：在 TcpServer::start 中指定
server.start<SessionWithTLSPolicy<EchoSession>>(443, "0.0.0.0");
```

### TcpClientWithTLSPolicy<TcpClientType>

客户端包装器模板，支持多种方式配置 TLS 策略。

```cpp
// 方式1: 自动模式（预先注册策略）
factory.registerClientPolicy("localhost", 8443, clientPolicy);
auto client = std::make_shared<TcpClientWithTLSPolicy<EchoClient>>();
client->startConnect("localhost", 8443);

// 方式2: 手动设置策略
auto client = std::make_shared<TcpClientWithTLSPolicy<EchoClient>>();
client->setTLSPolicy(myPolicy);  // 先设置策略
client->startConnect("localhost", 8443);  // 再连接

// 方式3: 完全自定义 SSL_Box
auto customCtx = SSLUtil::makeSSLContext(customPolicy, false);
auto sslBox = std::make_shared<SSL_Box>(customCtx, false);
auto client = std::make_shared<TcpClientWithTLSPolicy<EchoClient>>();
client->setSSLBox(std::move(sslBox));
client->startConnect("localhost", 8443);
```

## 使用示例

### 服务器端：不同端口使用不同证书

```cpp
#include "Util/SSLBox.h"
#include "trantor/net/TLSPolicy.h"

int main() {
    auto &factory = TLSSessionFactory::Instance();

    // 端口 443 使用证书 A
    auto policy443 = trantor::TLSPolicy::defaultServerPolicy(
        "cert_a.pem", "key_a.pem"
    );
    policy443->setValidate(true)
              .setCaPath("ca.pem");  // 要求客户端证书
    factory.registerServerPolicy("0.0.0.0", 443, policy443);

    // 端口 8443 使用证书 B
    auto policy8443 = trantor::TLSPolicy::defaultServerPolicy(
        "cert_b.pem", "key_b.pem"
    );
    policy8443->setValidate(false);  // 不要求客户端证书
    factory.registerServerPolicy("0.0.0.0", 8443, policy8443);

    // 启动服务器
    TcpServer server;
    server.start<SessionWithTLSPolicy<EchoSession>>(443, "0.0.0.0");
    server.start<SessionWithTLSPolicy<EchoSession>>(8443, "0.0.0.0");

    // ... 事件循环
    return 0;
}
```

### 客户端：双向 TLS 认证

```cpp
#include "Util/SSLBox.h"
#include "trantor/net/TLSPolicy.h"

int main() {
    // 创建带客户端证书的策略
    auto clientPolicy = trantor::TLSPolicy::defaultClientPolicy("server.example.com");
    clientPolicy->setCertPath("client_cert.pem")
                 .setKeyPath("client_key.pem")
                 .setCaPath("server_ca.pem")
                 .setValidate(true);

    // 方式1: 预注册策略
    auto &factory = TLSSessionFactory::Instance();
    factory.registerClientPolicy("server.example.com", 443, clientPolicy);

    auto client = std::make_shared<TcpClientWithTLSPolicy<MyClient>>();
    client->startConnect("server.example.com", 443);

    // 方式2: 直接设置策略
    // auto client = std::make_shared<TcpClientWithTLSPolicy<MyClient>>();
    // client->setTLSPolicy(clientPolicy);
    // client->startConnect("server.example.com", 443);

    // ... 事件循环
    return 0;
}
```

### 同一主机不同端口使用不同策略

```cpp
// 443 端口使用 HTTPS 策略
auto httpsPolicy = trantor::TLSPolicy::defaultClientPolicy("api.example.com");
httpsPolicy->setAlpnProtocols({"h2", "http/1.1"});
factory.registerClientPolicy("api.example.com", 443, httpsPolicy);

// 8883 端口使用 MQTT 策略
auto mqttPolicy = trantor::TLSPolicy::defaultClientPolicy("api.example.com");
mqttPolicy->setCertPath("mqtt_client.pem");
mqttPolicy->setKeyPath("mqtt_key.pem");
factory.registerClientPolicy("api.example.com", 8883, mqttPolicy);

// 使用协议区分同一端口的不同策略
auto grpcPolicy = trantor::TLSPolicy::defaultClientPolicy("api.example.com");
grpcPolicy->setCertPath("grpc_client.pem");
factory.registerClientPolicy("api.example.com", 443, "grpc", grpcPolicy);

// 使用带协议的策略
auto grpcClient = std::make_shared<TcpClientWithTLSPolicy<GrpcClient>>();
grpcClient->setProtocol("grpc");  // 设置协议标识
grpcClient->startConnect("api.example.com", 443);
```

## TLSPolicy 配置选项

| 方法 | 说明 | 默认值 |
|------|------|--------|
| `setCertPath(path)` | 证书文件路径 (PEM格式) | 空 |
| `setKeyPath(path)` | 私钥文件路径 (PEM格式) | 空 |
| `setCaPath(path)` | CA证书路径，用于验证对等方 | 空 |
| `setValidate(bool)` | 是否启用证书验证 | 客户端:true, 服务器:false |
| `setAllowBrokenChain(bool)` | 允许自签名证书等不完整链 | false |
| `setUseOldTLS(bool)` | 允许使用旧版TLS(<1.2) | false |
| `setUseSystemCertStore(bool)` | 使用系统CA证书库 | 客户端:true |
| `setHostname(hostname)` | SNI主机名和证书验证主机名 | 空 |
| `setAlpnProtocols(list)` | ALPN协议列表 | 空 |
| `setConfCmds(list)` | OpenSSL配置命令列表 | 空 |

## 便捷工厂方法

```cpp
// 创建默认服务器策略（不验证客户端，禁用旧版TLS）
auto serverPolicy = trantor::TLSPolicy::defaultServerPolicy(
    "cert.pem", "key.pem"
);

// 创建默认客户端策略（验证服务器，使用系统CA，禁用旧版TLS）
auto clientPolicy = trantor::TLSPolicy::defaultClientPolicy(
    "api.example.com"
);
```

## SSLUtil 新增方法

```cpp
// 从 TLSPolicy 创建 SSL_CTX
static std::shared_ptr<SSL_CTX> makeSSLContext(
    const trantor::TLSPolicy &policy,
    bool serverMode
);

static std::shared_ptr<SSL_CTX> makeSSLContext(
    const trantor::TLSPolicyPtr &policy,
    bool serverMode
);
```

## SSL_Box 新增方法

```cpp
// 使用自定义 SSL_CTX 创建 SSL_Box
SSL_Box(std::shared_ptr<SSL_CTX> ctx, bool server_mode, int buff_size = 32 * 1024);

// 检查是否使用自定义上下文
bool hasCustomCtx() const;
```

## 向后兼容性

### 原有代码继续工作

```cpp
// 原有全局方式仍然有效
SSL_Initor::Instance().loadCertificate("default.pem");

// 使用 SessionWithSSL 和 TcpClientWithSSL 仍然工作
server.start<SessionWithSSL<EchoSession>>(443, "0.0.0.0");
auto client = std::make_shared<TcpClientWithSSL<EchoClient>>();
```

### 混合使用

可以在同一程序中混合使用新旧两种方式：

```cpp
// 端口 443 使用 TLSPolicy 方式
auto policy = trantor::TLSPolicy::defaultServerPolicy("cert_a.pem", "key_a.pem");
TLSSessionFactory::Instance().registerServerPolicy("0.0.0.0", 443, policy);
server.start<SessionWithTLSPolicy<EchoSession>>(443, "0.0.0.0");

// 端口 8443 使用传统全局方式
SSL_Initor::Instance().loadCertificate("cert_b.pem");
server.start<SessionWithSSL<EchoSession>>(8443, "0.0.0.0");
```

## 文件变更清单

| 文件 | 变更类型 | 说明 |
|------|---------|------|
| `src/Util/SSLBox.h` | 修改 | 1. SSL_Box 新增构造函数<br>2. 新增 TLSSessionFactory 类<br>3. 新增 SessionWithTLSPolicy 模板<br>4. 新增 TcpClientWithTLSPolicy 模板 |
| `src/Util/SSLBox.cpp` | 修改 | 1. SSL_Box 新增构造函数实现<br>2. TLSSessionFactory 实现 |
| `src/Util/SSLUtil.h` | 修改 | 新增 TLSPolicy-based makeSSLContext 重载 |
| `src/Util/SSLUtil.cpp` | 修改 | 实现 TLSPolicy → SSL_CTX 转换 |
| `tests/test_tlspolicy.cpp` | 新增 | TLSPolicy 功能测试 |
| `docs/TLSPOLICY_USAGE.md` | 新增 | 本文档 |

## 注意事项

1. **策略注册时机**：策略应在服务器启动或客户端连接之前注册。
2. **线程安全**：TLSSessionFactory 内部使用 mutex 保护，是线程安全的。
3. **证书文件**：证书和私钥文件必须存在且格式正确（PEM格式）。
4. **SSL_CTX 缓存**：创建的 SSL_CTX 会在工厂内部缓存，相同策略不会重复创建。
5. **清除策略**：调用 `clear()` 会清除所有已注册的策略和缓存的 SSL_CTX，谨慎使用。
