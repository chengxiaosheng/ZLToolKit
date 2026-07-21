#pragma once
#include <string>
#include <memory>

// OpenSSL X509 前置声明 (与 SSLBox.h 中一致)
typedef struct x509_st X509;

namespace trantor
{
struct Certificate
{
    virtual ~Certificate() = default;
    virtual std::string sha1Fingerprint() const = 0;
    virtual std::string sha256Fingerprint() const = 0;
    virtual std::string pem() const = 0;
};
using CertificatePtr = std::shared_ptr<Certificate>;

/**
 * @brief 从 OpenSSL X509 证书构造 Certificate 对象 (内部使用)
 * @param cer 由 SSL_Box::getPeerCertificate() 返回的证书引用，本函数接管引用计数
 * @return Certificate 智能指针，cer 为空时返回 nullptr
 */
CertificatePtr makeCertificate(const std::shared_ptr<X509> &cer);

}  // namespace trantor