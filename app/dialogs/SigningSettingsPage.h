#pragma once

#include <QCoreApplication>
#include <QWidget>

#include <functional>

#include "Signing.h"

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;

namespace colorizing::app {

// Settings -> Signing: the author's signing key (Signing.h). Creates a key, shows its public
// key to send to the PPUC signing team, and takes the certificate the team sends back. It only
// shows and collects the key; the caller stores it.
class SigningSettingsPage final : public QWidget {
    Q_DECLARE_TR_FUNCTIONS(SigningSettingsPage)

public:
    explicit SigningSettingsPage(QWidget* parent = nullptr);

    void setKey(const AuthorSigningKey& key);
    [[nodiscard]] AuthorSigningKey key() const;

    // For tests: what the status line says.
    [[nodiscard]] QString statusText() const;

    // Request Certificate: asks the PPUC signing team for a certificate of the public key
    // (team/CertificateRequest.h). The owner sends it and answers with setRequestState().
    std::function<void(const QString& publicKey)> onRequestCertificate;
    // Check for the Certificate, while a request waits for its answer.
    std::function<void()> onCheckCertificate;
    // Whether a request waits for its answer, and what to say about it.
    void setRequestState(bool waiting, const QString& text);
    // The certificate the answer brought, put into the certificate box.
    void takeCertificate(const QString& certificate);
    [[nodiscard]] QString requestText() const;

private:
    void createKey();
    void updateState();

    std::optional<std::array<uint8_t, 32>> m_seed;
    QLabel* m_status = nullptr;
    QPushButton* m_createButton = nullptr;
    QLineEdit* m_publicKey = nullptr;
    QPushButton* m_copyButton = nullptr;
    QPlainTextEdit* m_certificate = nullptr;
    QPushButton* m_requestButton = nullptr;
    QLabel* m_requestStatus = nullptr;
    bool m_requestWaiting = false;
};

}  // namespace colorizing::app
