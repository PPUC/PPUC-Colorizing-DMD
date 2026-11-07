#include "Signing.h"

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageAuthenticationCode>
#include <QRandomGenerator>
#include <QSettings>
#include <QtEndian>

#include "runtime/CromcExport.h"

namespace colorizing::app {

namespace {

const QString kSeedKey = QStringLiteral("signing/key");
const QString kCertificateKey = QStringLiteral("signing/certificate");
const QString kRequestIssueKey = QStringLiteral("signing/requestIssue");
const QString kRequestKeyKey = QStringLiteral("signing/requestKey");

const QString kIdentityFormat = QStringLiteral("PPUC-SIGNING-IDENTITY");
constexpr int kIdentityVersion = 1;
const QString kIdentityKdf = QStringLiteral("PBKDF2-HMAC-SHA256");
constexpr qsizetype kSaltBytes = 16;

// PBKDF2 (RFC 8018) with HMAC-SHA256, 64 bytes: two blocks. The passphrase in Unicode's
// composed form, so that it is the same bytes whichever system it was typed on.
QByteArray deriveKeys(const QString& passphrase, const QByteArray& salt, int iterations)
{
    QMessageAuthenticationCode mac(QCryptographicHash::Sha256,
                                   passphrase.normalized(QString::NormalizationForm_C).toUtf8());
    QByteArray derived;
    for (quint32 block = 1; block <= 2; ++block) {
        char index[4];
        qToBigEndian(block, index);
        mac.reset();
        mac.addData(salt);
        mac.addData(index, 4);
        QByteArray u = mac.result();
        QByteArray t = u;
        for (int round = 1; round < iterations; ++round) {
            mac.reset();
            mac.addData(u);
            u = mac.result();
            for (qsizetype i = 0; i < t.size(); ++i) {
                t[i] = static_cast<char>(t[i] ^ u[i]);
            }
        }
        derived += t;
    }
    return derived;
}

void addField(QByteArray& bytes, const QByteArray& field)
{
    char length[4];
    qToBigEndian(static_cast<quint32>(field.size()), length);
    bytes.append(length, 4);
    bytes += field;
}

// What the HMAC covers: every field of the file but the HMAC, each with its length.
QByteArray authenticated(const QString& publicKey,
                         const QString& certificate,
                         int iterations,
                         const QByteArray& salt,
                         const QByteArray& encrypted)
{
    QByteArray bytes;
    addField(bytes, kIdentityFormat.toUtf8());
    addField(bytes, QByteArray::number(kIdentityVersion));
    addField(bytes, publicKey.toUtf8());
    addField(bytes, certificate.toUtf8());
    addField(bytes, kIdentityKdf.toUtf8());
    addField(bytes, QByteArray::number(iterations));
    addField(bytes, salt);
    addField(bytes, encrypted);
    return bytes;
}

QByteArray xorBytes(const QByteArray& a, const QByteArray& b)
{
    QByteArray result(a.size(), '\0');
    for (qsizetype i = 0; i < a.size(); ++i) {
        result[i] = static_cast<char>(a[i] ^ b[i]);
    }
    return result;
}

// Compares without stopping at the first difference, so that the time says nothing.
bool sameBytes(const QByteArray& a, const QByteArray& b)
{
    if (a.size() != b.size()) {
        return false;
    }
    unsigned char difference = 0;
    for (qsizetype i = 0; i < a.size(); ++i) {
        difference |= static_cast<unsigned char>(a[i] ^ b[i]);
    }
    return difference == 0;
}

}  // namespace

AuthorSigningKey loadAuthorSigningKey()
{
    QSettings settings;
    AuthorSigningKey key;
    const QByteArray seed = QByteArray::fromHex(settings.value(kSeedKey).toByteArray());
    if (seed.size() == 32) {
        std::array<uint8_t, 32> bytes{};
        std::copy(seed.begin(), seed.end(), bytes.begin());
        key.seed = bytes;
    }
    key.certificate = settings.value(kCertificateKey).toString();
    return key;
}

void saveAuthorSigningKey(const AuthorSigningKey& key)
{
    QSettings settings;
    if (key.seed) {
        settings.setValue(kSeedKey,
                          QByteArray(reinterpret_cast<const char*>(key.seed->data()), 32)
                              .toHex());
    } else {
        settings.remove(kSeedKey);
    }
    if (key.certificate.trimmed().isEmpty()) {
        settings.remove(kCertificateKey);
    } else {
        settings.setValue(kCertificateKey, key.certificate.trimmed());
    }
}

std::array<uint8_t, 32> newSigningSeed()
{
    std::array<uint8_t, 32> seed{};
    QRandomGenerator* random = QRandomGenerator::system();
    for (uint8_t& byte : seed) {
        byte = static_cast<uint8_t>(random->bounded(256));
    }
    return seed;
}

QString publicKeyText(const std::array<uint8_t, 32>& seed)
{
    const std::array<uint8_t, 32> key = runtime::signingPublicKey(seed);
    return QString::fromLatin1(
        QByteArray(reinterpret_cast<const char*>(key.data()), 32).toHex());
}

CertificateCheck checkCertificate(const QString& certificate,
                                  const std::optional<std::array<uint8_t, 32>>& seed)
{
    const runtime::SignatureCheck result =
        runtime::checkSigningCertificate(certificate.trimmed().toStdString(), seed);
    CertificateCheck check;
    check.trusted = result.status == runtime::SignatureCheck::Status::Signed;
    check.name = QString::fromStdString(result.name);
    check.identity = QString::fromStdString(result.identity);
    check.reason = QString::fromStdString(result.reason);
    return check;
}

bool canSign(const AuthorSigningKey& key)
{
    return key.seed && !key.certificate.trimmed().isEmpty() &&
           checkCertificate(key.certificate, key.seed).trusted;
}

std::optional<QByteArray> signingIdentityFile(const AuthorSigningKey& key,
                                              const QString& passphrase,
                                              int iterations)
{
    if (!key.seed || iterations < 1) {
        return std::nullopt;
    }
    QByteArray salt(kSaltBytes, '\0');
    QRandomGenerator* random = QRandomGenerator::system();
    for (char& byte : salt) {
        byte = static_cast<char>(random->bounded(256));
    }
    const QByteArray derived = deriveKeys(passphrase, salt, iterations);
    const QByteArray seed(reinterpret_cast<const char*>(key.seed->data()), 32);
    const QByteArray encrypted = xorBytes(seed, derived.left(32));
    const QString publicKey = publicKeyText(*key.seed);
    const QString certificate = key.certificate.trimmed();
    const QByteArray tag = QMessageAuthenticationCode::hash(
        authenticated(publicKey, certificate, iterations, salt, encrypted), derived.mid(32),
        QCryptographicHash::Sha256);

    QJsonObject kdf;
    kdf.insert(QStringLiteral("name"), kIdentityKdf);
    kdf.insert(QStringLiteral("iterations"), iterations);
    kdf.insert(QStringLiteral("salt"), QString::fromLatin1(salt.toBase64()));
    QJsonObject root;
    root.insert(QStringLiteral("format"), kIdentityFormat);
    root.insert(QStringLiteral("version"), kIdentityVersion);
    root.insert(QStringLiteral("publicKey"), publicKey);
    root.insert(QStringLiteral("certificate"), certificate);
    root.insert(QStringLiteral("kdf"), kdf);
    root.insert(QStringLiteral("encryptedKey"), QString::fromLatin1(encrypted.toBase64()));
    root.insert(QStringLiteral("mac"), QString::fromLatin1(tag.toBase64()));
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

SigningIdentityRead readSigningIdentityFile(const QByteArray& file, const QString& passphrase)
{
    SigningIdentityRead read;
    read.error = SigningIdentityError::NotAnIdentity;
    QJsonParseError parseError;
    const QJsonDocument json = QJsonDocument::fromJson(file, &parseError);
    if (parseError.error != QJsonParseError::NoError || !json.isObject()) {
        return read;
    }
    const QJsonObject root = json.object();
    if (root.value(QStringLiteral("format")).toString() != kIdentityFormat ||
        !root.value(QStringLiteral("version")).isDouble()) {
        return read;
    }
    if (root.value(QStringLiteral("version")).toInt() != kIdentityVersion) {
        read.error = SigningIdentityError::NewerFormat;
        return read;
    }
    const QJsonObject kdf = root.value(QStringLiteral("kdf")).toObject();
    const QString publicKey = root.value(QStringLiteral("publicKey")).toString();
    const QJsonValue certificateValue = root.value(QStringLiteral("certificate"));
    const QJsonValue iterationsValue = kdf.value(QStringLiteral("iterations"));
    const QByteArray salt = QByteArray::fromBase64(
        kdf.value(QStringLiteral("salt")).toString().toLatin1(),
        QByteArray::AbortOnBase64DecodingErrors);
    const QByteArray encrypted = QByteArray::fromBase64(
        root.value(QStringLiteral("encryptedKey")).toString().toLatin1(),
        QByteArray::AbortOnBase64DecodingErrors);
    const QByteArray tag = QByteArray::fromBase64(
        root.value(QStringLiteral("mac")).toString().toLatin1(),
        QByteArray::AbortOnBase64DecodingErrors);
    const double iterationsNumber = iterationsValue.toDouble(-1);
    if (kdf.value(QStringLiteral("name")).toString() != kIdentityKdf ||
        !iterationsValue.isDouble() || iterationsNumber < 1 ||
        iterationsNumber > kSigningIdentityMaxIterations ||
        iterationsNumber != static_cast<double>(static_cast<int>(iterationsNumber)) ||
        !certificateValue.isString() || publicKey.size() != 64 || salt.size() != kSaltBytes ||
        encrypted.size() != 32 || tag.size() != 32) {
        return read;
    }
    const int iterations = static_cast<int>(iterationsNumber);
    const QString certificate = certificateValue.toString();
    const QByteArray derived = deriveKeys(passphrase, salt, iterations);
    const QByteArray expected = QMessageAuthenticationCode::hash(
        authenticated(publicKey, certificate, iterations, salt, encrypted), derived.mid(32),
        QCryptographicHash::Sha256);
    if (!sameBytes(tag, expected)) {
        read.error = SigningIdentityError::WrongPassphraseOrChanged;
        return read;
    }
    const QByteArray seed = xorBytes(encrypted, derived.left(32));
    std::array<uint8_t, 32> bytes{};
    std::copy(seed.begin(), seed.end(), bytes.begin());
    if (publicKeyText(bytes) != publicKey) {
        read.error = SigningIdentityError::KeyMismatch;
        return read;
    }
    AuthorSigningKey key;
    key.seed = bytes;
    key.certificate = certificate;
    read.key = key;
    read.error = SigningIdentityError::None;
    return read;
}

std::optional<PendingCertificateRequest> loadPendingCertificateRequest()
{
    QSettings settings;
    const int issue = settings.value(kRequestIssueKey).toInt();
    if (issue <= 0) {
        return std::nullopt;
    }
    return PendingCertificateRequest{issue, settings.value(kRequestKeyKey).toString()};
}

void savePendingCertificateRequest(const std::optional<PendingCertificateRequest>& request)
{
    QSettings settings;
    if (!request) {
        settings.remove(kRequestIssueKey);
        settings.remove(kRequestKeyKey);
        return;
    }
    settings.setValue(kRequestIssueKey, request->issue);
    settings.setValue(kRequestKeyKey, request->publicKey);
}

}  // namespace colorizing::app
