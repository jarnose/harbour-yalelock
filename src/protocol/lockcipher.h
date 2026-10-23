#pragma once

#include <QByteArray>
#include <QString>

// AES primitives for the two BLE channels in LockProtocol - see
// lockprotocol.h for where each is used.
//
// DataChannelCipher: AES-128-CBC, IV = 16 zero bytes at session start, no
// padding - but *stateful*: the same OpenSSL cipher context is reused for
// every 16-byte block for the life of the connection, so block N+1's IV is
// block N's ciphertext (ordinary CBC chaining), not a fresh zero IV each
// time. This matches the single long-lived CipherContext yalexs-ble's
// Session keeps for its whole connection.
//
// SecureChannelCipher: AES-128-ECB, no padding, used only for the initial
// handshake on the secure channel (first with the lock's offline key, then
// with the freshly derived session key) - stateless, so a fresh one-shot
// encrypt/decrypt is correct here.
class DataChannelCipher
{
public:
    DataChannelCipher();
    ~DataChannelCipher();
    DataChannelCipher(const DataChannelCipher &) = delete;
    DataChannelCipher &operator=(const DataChannelCipher &) = delete;

    // (Re)starts the chain: sets the key and resets the running IV to zero.
    // Call once, right after the handshake derives the session key.
    bool setKey(const QByteArray &key16, QString *error = nullptr);

    bool hasKey() const { return m_keyed; }

    // Encrypts/decrypts exactly one 16-byte block, advancing the chain.
    bool encryptBlock(const QByteArray &plain16, QByteArray *cipher16, QString *error = nullptr);
    bool decryptBlock(const QByteArray &cipher16, QByteArray *plain16, QString *error = nullptr);

private:
    struct Impl;
    Impl *m_impl;
    bool m_keyed = false;
};

// Cryptographically random bytes (OpenSSL RAND_bytes), for the handshake's
// random challenge. Returns an empty array on failure.
QByteArray secureRandomBytes(int count);

class SecureChannelCipher
{
public:
    void setKey(const QByteArray &key16) { m_key = key16; }
    bool hasKey() const { return m_key.size() == 16; }

    bool encryptBlock(const QByteArray &plain16, QByteArray *cipher16, QString *error = nullptr) const;
    bool decryptBlock(const QByteArray &cipher16, QByteArray *plain16, QString *error = nullptr) const;

private:
    QByteArray m_key;
};
