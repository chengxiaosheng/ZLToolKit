// Copyright (c) 2026 The ZLToolKit project authors.
//
// OpenSSL 实现的 trantor::Certificate 包装。

#include "trantor/net/Certificate.h"

#if defined(ENABLE_OPENSSL)
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <openssl/evp.h>
#include <openssl/sha.h>
#endif // defined(ENABLE_OPENSSL)

#include <memory>
#include <sstream>
#include <iomanip>

namespace trantor
{
namespace
{
#if defined(ENABLE_OPENSSL)
struct X509Deleter
{
    void operator()(X509 *p) const
    {
        if (p)
            X509_free(p);
    }
};

class OpenSSLCertificate : public Certificate
{
  public:
    // 接管 cer 的引用 (SSL_get_peer_certificate 返回新引用)
    explicit OpenSSLCertificate(std::shared_ptr<X509> cer)
        : cer_(std::move(cer))
    {
    }

    std::string sha1Fingerprint() const override
    {
        unsigned char md[SHA_DIGEST_LENGTH];
        unsigned int n = 0;
        if (X509_digest(cer_.get(), EVP_sha1(), md, &n))
            return toHex(md, n);
        return "";
    }

    std::string sha256Fingerprint() const override
    {
        unsigned char md[SHA256_DIGEST_LENGTH];
        unsigned int n = 0;
        if (X509_digest(cer_.get(), EVP_sha256(), md, &n))
            return toHex(md, n);
        return "";
    }

    std::string pem() const override
    {
        BIO *bio = BIO_new(BIO_s_mem());
        if (!bio)
            return "";
        PEM_write_bio_X509(bio, cer_.get());
        BUF_MEM *bm = nullptr;
        BIO_get_mem_ptr(bio, &bm);
        std::string s(bm ? bm->data : "", bm ? bm->length : 0);
        BIO_free(bio);
        return s;
    }

  private:
    static std::string toHex(const unsigned char *d, unsigned int n)
    {
        std::ostringstream os;
        os << std::hex << std::setfill('0');
        for (unsigned int i = 0; i < n; i++)
        {
            os << std::setw(2) << static_cast<int>(d[i]);
            if (i + 1 < n)
                os << ':';
        }
        return os.str();
    }

    std::shared_ptr<X509> cer_;
};
#endif // defined(ENABLE_OPENSSL)
}  // namespace

CertificatePtr makeCertificate(const std::shared_ptr<X509> &cer)
{
#if defined(ENABLE_OPENSSL)
    if (!cer)
        return nullptr;
    return std::make_shared<OpenSSLCertificate>(cer);
#else
    (void)cer;
    return nullptr;
#endif
}

}  // namespace trantor
