#pragma once

#include <QCoreApplication>
#include <QDialog>
#include <QWidget>

#include <functional>

#include "Signing.h"

class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;

namespace colorizing::app {

// Asks for the passphrase of a signing identity file: for an export twice, and at least
// kSigningIdentityMinPassphrase characters; for an import once.
class SigningPassphraseDialog final : public QDialog {
    Q_DECLARE_TR_FUNCTIONS(SigningPassphraseDialog)

public:
    enum class Purpose { Export, Import };
    SigningPassphraseDialog(Purpose purpose, QWidget* parent = nullptr);

    [[nodiscard]] QString passphrase() const;
    // For tests: the fields, and whether OK can be pressed.
    void setPassphrase(const QString& passphrase, const QString& repeated = QString());
    [[nodiscard]] bool canAccept() const;
    [[nodiscard]] QString problemText() const;

private:
    void updateState();

    Purpose m_purpose;
    QLineEdit* m_passphrase = nullptr;
    QLineEdit* m_repeated = nullptr;
    QLabel* m_problem = nullptr;
    QDialogButtonBox* m_buttons = nullptr;
};

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

    // Export... and Import...: the key and its certificate as a signing identity file
    // (Signing.h), to sign as the same author on another computer. Both ask for the
    // passphrase and the file; these do the rest and return why they failed, empty when they
    // did not. An import replaces the key on the page; the caller stores it as usual.
    [[nodiscard]] QString exportIdentity(const QString& path,
                                         const QString& passphrase,
                                         int iterations = kSigningIdentityIterations) const;
    [[nodiscard]] QString importIdentity(const QString& path, const QString& passphrase);

private:
    void createKey();
    void exportClicked();
    void importClicked();
    void updateState();

    std::optional<std::array<uint8_t, 32>> m_seed;
    QLabel* m_status = nullptr;
    QPushButton* m_createButton = nullptr;
    QPushButton* m_exportButton = nullptr;
    QPushButton* m_importButton = nullptr;
    QLineEdit* m_publicKey = nullptr;
    QPushButton* m_copyButton = nullptr;
    QPlainTextEdit* m_certificate = nullptr;
    QPushButton* m_requestButton = nullptr;
    QLabel* m_requestStatus = nullptr;
    bool m_requestWaiting = false;
};

}  // namespace colorizing::app
