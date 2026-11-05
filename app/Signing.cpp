#include "Signing.h"

#include <QByteArray>
#include <QRandomGenerator>
#include <QSettings>

#include "runtime/CromcExport.h"

namespace colorizing::app {

namespace {

const QString kSeedKey = QStringLiteral("signing/key");
const QString kCertificateKey = QStringLiteral("signing/certificate");
const QString kRequestIssueKey = QStringLiteral("signing/requestIssue");
const QString kRequestKeyKey = QStringLiteral("signing/requestKey");

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
