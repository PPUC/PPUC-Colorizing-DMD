#pragma once

#include <QCoreApplication>
#include <QWidget>

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

private:
    void createKey();
    void updateState();

    std::optional<std::array<uint8_t, 32>> m_seed;
    QLabel* m_status = nullptr;
    QPushButton* m_createButton = nullptr;
    QLineEdit* m_publicKey = nullptr;
    QPushButton* m_copyButton = nullptr;
    QPlainTextEdit* m_certificate = nullptr;
};

}  // namespace colorizing::app
