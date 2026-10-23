#include "lockcipher.h"

#include <openssl/evp.h>
#include <openssl/err.h>
#include <openssl/rand.h>

namespace {

QString opensslError()
{
    unsigned long code = ERR_get_error();
    if (code == 0)
        return QStringLiteral("unknown OpenSSL error");
    char buf[256];
    ERR_error_string_n(code, buf, sizeof(buf));
    return QString::fromLatin1(buf);
}

bool ecbOneShot(bool encrypt, const QByteArray &key16, const QByteArray &in16, QByteArray *out16,
                 QString *error)
{
    if (key16.size() != 16 || in16.size() != 16) {
        if (error)
            *error = QStringLiteral("AES-ECB needs a 16-byte key and a 16-byte block");
        return false;
    }
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        if (error)
            *error = opensslError();
        return false;
    }
    bool ok = false;
    const auto *key = reinterpret_cast<const unsigned char *>(key16.constData());
    int rc = encrypt ? EVP_EncryptInit_ex(ctx, EVP_aes_128_ecb(), nullptr, key, nullptr)
                      : EVP_DecryptInit_ex(ctx, EVP_aes_128_ecb(), nullptr, key, nullptr);
    if (rc == 1)
        EVP_CIPHER_CTX_set_padding(ctx, 0);
    if (rc == 1) {
        out16->resize(16);
        int outLen = 0;
        auto *out = reinterpret_cast<unsigned char *>(out16->data());
        const auto *in = reinterpret_cast<const unsigned char *>(in16.constData());
        rc = encrypt ? EVP_EncryptUpdate(ctx, out, &outLen, in, 16)
                     : EVP_DecryptUpdate(ctx, out, &outLen, in, 16);
        ok = (rc == 1 && outLen == 16);
    }
    if (!ok && error)
        *error = opensslError();
    EVP_CIPHER_CTX_free(ctx);
    return ok;
}

} // namespace

struct DataChannelCipher::Impl {
    EVP_CIPHER_CTX *enc = nullptr;
    EVP_CIPHER_CTX *dec = nullptr;
};

DataChannelCipher::DataChannelCipher() : m_impl(new Impl) {}

DataChannelCipher::~DataChannelCipher()
{
    if (m_impl->enc)
        EVP_CIPHER_CTX_free(m_impl->enc);
    if (m_impl->dec)
        EVP_CIPHER_CTX_free(m_impl->dec);
    delete m_impl;
}

bool DataChannelCipher::setKey(const QByteArray &key16, QString *error)
{
    m_keyed = false;
    if (key16.size() != 16) {
        if (error)
            *error = QStringLiteral("session key must be 16 bytes, got %1").arg(key16.size());
        return false;
    }
    if (m_impl->enc)
        EVP_CIPHER_CTX_free(m_impl->enc);
    if (m_impl->dec)
        EVP_CIPHER_CTX_free(m_impl->dec);
    m_impl->enc = EVP_CIPHER_CTX_new();
    m_impl->dec = EVP_CIPHER_CTX_new();
    if (!m_impl->enc || !m_impl->dec) {
        if (error)
            *error = opensslError();
        return false;
    }
    const auto *key = reinterpret_cast<const unsigned char *>(key16.constData());
    unsigned char zeroIv[16] = {0};
    const bool okEnc = EVP_EncryptInit_ex(m_impl->enc, EVP_aes_128_cbc(), nullptr, key, zeroIv) == 1;
    const bool okDec = EVP_DecryptInit_ex(m_impl->dec, EVP_aes_128_cbc(), nullptr, key, zeroIv) == 1;
    if (okEnc)
        EVP_CIPHER_CTX_set_padding(m_impl->enc, 0);
    if (okDec)
        EVP_CIPHER_CTX_set_padding(m_impl->dec, 0);
    if (!okEnc || !okDec) {
        if (error)
            *error = opensslError();
        return false;
    }
    m_keyed = true;
    return true;
}

bool DataChannelCipher::encryptBlock(const QByteArray &plain16, QByteArray *cipher16, QString *error)
{
    if (!m_keyed || plain16.size() != 16) {
        if (error)
            *error = QStringLiteral("DataChannelCipher: not keyed, or input is not 16 bytes");
        return false;
    }
    cipher16->resize(16);
    int outLen = 0;
    const auto *in = reinterpret_cast<const unsigned char *>(plain16.constData());
    auto *out = reinterpret_cast<unsigned char *>(cipher16->data());
    // The context's running IV (block N's ciphertext feeds block N+1) is
    // exactly the CBC chaining this channel relies on across its whole
    // connection lifetime - do not reset or recreate the context per block.
    const bool ok = EVP_EncryptUpdate(m_impl->enc, out, &outLen, in, 16) == 1 && outLen == 16;
    if (!ok && error)
        *error = opensslError();
    return ok;
}

bool DataChannelCipher::decryptBlock(const QByteArray &cipher16, QByteArray *plain16, QString *error)
{
    if (!m_keyed || cipher16.size() != 16) {
        if (error)
            *error = QStringLiteral("DataChannelCipher: not keyed, or input is not 16 bytes");
        return false;
    }
    plain16->resize(16);
    int outLen = 0;
    const auto *in = reinterpret_cast<const unsigned char *>(cipher16.constData());
    auto *out = reinterpret_cast<unsigned char *>(plain16->data());
    const bool ok = EVP_DecryptUpdate(m_impl->dec, out, &outLen, in, 16) == 1 && outLen == 16;
    if (!ok && error)
        *error = opensslError();
    return ok;
}

QByteArray secureRandomBytes(int count)
{
    QByteArray out(count, char(0));
    if (RAND_bytes(reinterpret_cast<unsigned char *>(out.data()), count) != 1)
        return QByteArray();
    return out;
}

bool SecureChannelCipher::encryptBlock(const QByteArray &plain16, QByteArray *cipher16, QString *error) const
{
    return ecbOneShot(true, m_key, plain16, cipher16, error);
}

bool SecureChannelCipher::decryptBlock(const QByteArray &cipher16, QByteArray *plain16, QString *error) const
{
    return ecbOneShot(false, m_key, cipher16, plain16, error);
}
