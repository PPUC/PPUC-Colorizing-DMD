#pragma once

#include <QByteArray>
#include <QString>

#include <array>
#include <cstdint>
#include <optional>

namespace colorizing::app {

// The author's signing key (Settings -> Signing): the editor signs the .cROMc it exports with
// it, so that libserum can tell players whether a file is still the one its author released
// (libserum's Serum_ConcentrateFromSourceSigned()). The key is a 32-byte secret made on this
// computer; a certificate for its public key, issued by the PPUC signing team for the
// author's GitHub account, says whose key it is. Both are kept in the editor's settings.
struct AuthorSigningKey {
    std::optional<std::array<uint8_t, 32>> seed;
    // The certificate's text form ("SERUM-CERTIFICATE-1:..."); empty without one.
    QString certificate;

    friend bool operator==(const AuthorSigningKey&, const AuthorSigningKey&) = default;
};

[[nodiscard]] AuthorSigningKey loadAuthorSigningKey();
void saveAuthorSigningKey(const AuthorSigningKey& key);

// A new secret key, from the system's random source.
[[nodiscard]] std::array<uint8_t, 32> newSigningSeed();
// The public key of a secret key, as 64 hex digits.
[[nodiscard]] QString publicKeyText(const std::array<uint8_t, 32>& seed);

struct CertificateCheck {
    // A trusted key vouches for the certificate, and it is one of the key.
    bool trusted = false;
    QString name;
    QString identity;
    // Why it is not trusted.
    QString reason;
};
// The certificate as libserum sees it, for the key (no key: any key).
[[nodiscard]] CertificateCheck checkCertificate(
    const QString& certificate,
    const std::optional<std::array<uint8_t, 32>>& seed);

// Whether the key can sign: a secret key and a trusted certificate for it.
[[nodiscard]] bool canSign(const AuthorSigningKey& key);

// A signing identity file (.cdmdsigning): the key and its certificate, to sign as the same
// author on another computer (Settings -> Signing -> Export/Import). JSON with the public key
// and the certificate in the clear and the secret key encrypted with the author's passphrase:
// PBKDF2-HMAC-SHA256 of the passphrase and a random salt gives 64 bytes, the first 32 are
// XORed with the secret key (a key stream used once, as every file has its own salt), the
// others key an HMAC-SHA256 over everything else (encrypt, then authenticate). A wrong
// passphrase and a changed file fail the same check, so neither tells anything of the key.
inline constexpr int kSigningIdentityIterations = 600000;
// More rounds than any file of ours has: refused, so that a file cannot keep the editor busy.
inline constexpr int kSigningIdentityMaxIterations = 10000000;
inline constexpr int kSigningIdentityMinPassphrase = 8;

// The file; nothing without a secret key. iterations: for tests only.
[[nodiscard]] std::optional<QByteArray> signingIdentityFile(
    const AuthorSigningKey& key,
    const QString& passphrase,
    int iterations = kSigningIdentityIterations);

enum class SigningIdentityError {
    None,
    // Not a signing identity file, or one that lacks what it needs.
    NotAnIdentity,
    // Written by a newer editor.
    NewerFormat,
    // The passphrase is wrong, or the file was changed or damaged.
    WrongPassphraseOrChanged,
    // It checks out, but its key is not the one it names (a file made to deceive).
    KeyMismatch,
};
struct SigningIdentityRead {
    std::optional<AuthorSigningKey> key;
    SigningIdentityError error = SigningIdentityError::None;
};
[[nodiscard]] SigningIdentityRead readSigningIdentityFile(const QByteArray& file,
                                                          const QString& passphrase);

// The certificate request sent and not answered yet (team/CertificateRequest.h): its issue and
// the public key it asks for. Kept until the certificate is taken, the request is refused or a
// new key is made.
struct PendingCertificateRequest {
    int issue = 0;
    QString publicKey;
};
[[nodiscard]] std::optional<PendingCertificateRequest> loadPendingCertificateRequest();
void savePendingCertificateRequest(const std::optional<PendingCertificateRequest>& request);

}  // namespace colorizing::app
