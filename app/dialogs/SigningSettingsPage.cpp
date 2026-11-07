#include "dialogs/SigningSettingsPage.h"

#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QVBoxLayout>

namespace colorizing::app {

SigningPassphraseDialog::SigningPassphraseDialog(Purpose purpose, QWidget* parent)
    : QDialog(parent),
      m_purpose(purpose)
{
    setWindowTitle(purpose == Purpose::Export ? tr("Export Signing Key")
                                              : tr("Import Signing Key"));
    auto* intro = new QLabel(
        purpose == Purpose::Export
            ? tr("The file holds your signing key, protected by a passphrase you choose now. "
                 "You need the passphrase to import it on your other computer; nobody can "
                 "recover it. Keep the file to yourself: with it and the passphrase anyone "
                 "signs as you.")
            : tr("The passphrase chosen when the file was exported."),
        this);
    intro->setWordWrap(true);
    m_passphrase = new QLineEdit(this);
    m_passphrase->setEchoMode(QLineEdit::Password);
    m_repeated = new QLineEdit(this);
    m_repeated->setEchoMode(QLineEdit::Password);
    m_problem = new QLabel(this);
    m_problem->setWordWrap(true);
    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_passphrase, &QLineEdit::textChanged, this, [this]() { updateState(); });
    connect(m_repeated, &QLineEdit::textChanged, this, [this]() { updateState(); });

    auto* form = new QFormLayout;
    form->addRow(tr("Passphrase"), m_passphrase);
    if (purpose == Purpose::Export) {
        form->addRow(tr("Repeat it"), m_repeated);
    } else {
        m_repeated->hide();
    }
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(intro);
    layout->addLayout(form);
    layout->addWidget(m_problem);
    layout->addWidget(m_buttons);
    updateState();
}

QString SigningPassphraseDialog::passphrase() const
{
    return m_passphrase->text();
}

void SigningPassphraseDialog::setPassphrase(const QString& passphrase, const QString& repeated)
{
    m_passphrase->setText(passphrase);
    m_repeated->setText(repeated);
}

bool SigningPassphraseDialog::canAccept() const
{
    return m_buttons->button(QDialogButtonBox::Ok)->isEnabled();
}

QString SigningPassphraseDialog::problemText() const
{
    return m_problem->text();
}

void SigningPassphraseDialog::updateState()
{
    QString problem;
    if (m_purpose == Purpose::Export) {
        if (m_passphrase->text().size() < kSigningIdentityMinPassphrase) {
            problem = tr("At least %1 characters.").arg(kSigningIdentityMinPassphrase);
        } else if (m_repeated->text() != m_passphrase->text()) {
            problem = tr("The two passphrases differ.");
        }
    } else if (m_passphrase->text().isEmpty()) {
        problem = QStringLiteral(" ");
    }
    m_problem->setText(problem.trimmed());
    m_buttons->button(QDialogButtonBox::Ok)->setEnabled(problem.isEmpty());
}

SigningSettingsPage::SigningSettingsPage(QWidget* parent)
    : QWidget(parent)
{
    auto* intro = new QLabel(
        tr("The editor signs every .cROMc it exports with your signing key. libserum then "
           "tells players who made the colorization, and shows a notice on the DMD when "
           "someone changed the file after you signed it. A project can turn signing off in "
           "its settings."),
        this);
    intro->setWordWrap(true);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);

    m_createButton = new QPushButton(this);
    connect(m_createButton, &QPushButton::clicked, this, [this]() {
        createKey();
    });

    m_exportButton = new QPushButton(tr("Export..."), this);
    m_exportButton->setToolTip(
        tr("Saves your key and certificate in a file protected by a passphrase, to sign as "
           "the same author on another computer"));
    connect(m_exportButton, &QPushButton::clicked, this, [this]() {
        exportClicked();
    });
    m_importButton = new QPushButton(tr("Import..."), this);
    m_importButton->setToolTip(
        tr("Takes the key and certificate exported on another computer"));
    connect(m_importButton, &QPushButton::clicked, this, [this]() {
        importClicked();
    });
    auto* keyButtons = new QHBoxLayout;
    keyButtons->addWidget(m_createButton);
    keyButtons->addWidget(m_exportButton);
    keyButtons->addWidget(m_importButton);
    keyButtons->addStretch(1);

    m_publicKey = new QLineEdit(this);
    m_publicKey->setReadOnly(true);
    m_copyButton = new QPushButton(tr("Copy"), this);
    connect(m_copyButton, &QPushButton::clicked, this, [this]() {
        QGuiApplication::clipboard()->setText(m_publicKey->text());
    });
    auto* keyRow = new QHBoxLayout;
    keyRow->addWidget(m_publicKey, 1);
    keyRow->addWidget(m_copyButton);

    auto* request = new QLabel(
        tr("Request Certificate asks the PPUC signing team for a certificate of your key, as "
           "your GitHub account: the editor opens the request on GitHub, and once a trusted "
           "member approved it, Check for the Certificate takes it in. You can also paste a "
           "certificate below."),
        this);
    request->setWordWrap(true);
    m_requestButton = new QPushButton(this);
    connect(m_requestButton, &QPushButton::clicked, this, [this]() {
        if (m_requestWaiting) {
            if (onCheckCertificate) {
                onCheckCertificate();
            }
        } else if (onRequestCertificate && m_seed) {
            onRequestCertificate(publicKeyText(*m_seed));
        }
    });
    m_requestStatus = new QLabel(this);
    m_requestStatus->setWordWrap(true);
    m_requestStatus->setTextInteractionFlags(Qt::TextBrowserInteraction);
    m_requestStatus->setOpenExternalLinks(true);
    auto* requestRow = new QHBoxLayout;
    requestRow->addWidget(m_requestButton);
    requestRow->addWidget(m_requestStatus, 1);

    m_certificate = new QPlainTextEdit(this);
    m_certificate->setPlaceholderText(tr("SERUM-CERTIFICATE-1:..."));
    m_certificate->setMaximumHeight(100);
    connect(m_certificate, &QPlainTextEdit::textChanged, this, [this]() {
        updateState();
    });

    auto* form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->addRow(tr("Public key"), keyRow);
    form->addRow(request);
    form->addRow(requestRow);
    form->addRow(tr("Certificate"), m_certificate);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(intro);
    layout->addWidget(m_status);
    layout->addLayout(keyButtons);
    layout->addLayout(form);
    layout->addStretch(1);
    updateState();
}

void SigningSettingsPage::setKey(const AuthorSigningKey& key)
{
    m_seed = key.seed;
    m_certificate->setPlainText(key.certificate);
    updateState();
}

AuthorSigningKey SigningSettingsPage::key() const
{
    AuthorSigningKey key;
    key.seed = m_seed;
    key.certificate = m_certificate->toPlainText().trimmed();
    return key;
}

QString SigningSettingsPage::statusText() const
{
    return m_status->text();
}

void SigningSettingsPage::setRequestState(bool waiting, const QString& text)
{
    m_requestWaiting = waiting;
    m_requestStatus->setText(text);
    updateState();
}

void SigningSettingsPage::takeCertificate(const QString& certificate)
{
    m_certificate->setPlainText(certificate);
}

QString SigningSettingsPage::requestText() const
{
    return m_requestStatus->text();
}

QString SigningSettingsPage::exportIdentity(const QString& path,
                                           const QString& passphrase,
                                           int iterations) const
{
    const std::optional<QByteArray> bytes = signingIdentityFile(key(), passphrase, iterations);
    if (!bytes) {
        return tr("There is no signing key to export.");
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(*bytes) != bytes->size() ||
        !file.commit()) {
        return tr("%1 could not be written: %2").arg(path, file.errorString());
    }
    return QString();
}

QString SigningSettingsPage::importIdentity(const QString& path, const QString& passphrase)
{
    QFile file(path);
    // A signing identity file is a few kilobytes; a large file is none.
    if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024) {
        return tr("%1 could not be read: %2").arg(path, file.errorString());
    }
    const SigningIdentityRead read = readSigningIdentityFile(file.readAll(), passphrase);
    switch (read.error) {
        case SigningIdentityError::None:
            break;
        case SigningIdentityError::NotAnIdentity:
            return tr("The file is no signing key exported by the editor, or it is damaged.");
        case SigningIdentityError::NewerFormat:
            return tr("The file was exported by a newer editor. Update the editor first.");
        case SigningIdentityError::WrongPassphraseOrChanged:
            return tr("The passphrase is wrong, or the file was changed after the export.");
        case SigningIdentityError::KeyMismatch:
            return tr("The file was changed after the export: its key is not the one it "
                      "names.");
    }
    const bool otherKey = m_seed != read.key->seed;
    m_seed = read.key->seed;
    m_certificate->setPlainText(read.key->certificate);
    if (otherKey) {
        // A request for the old key no longer helps.
        m_requestWaiting = false;
        m_requestStatus->clear();
    }
    updateState();
    return QString();
}

void SigningSettingsPage::exportClicked()
{
    if (!m_seed) {
        return;
    }
    SigningPassphraseDialog ask(SigningPassphraseDialog::Purpose::Export, this);
    if (ask.exec() != QDialog::Accepted) {
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Signing Key"), QStringLiteral("signing-key.cdmdsigning"),
        tr("Signing keys (*.cdmdsigning)"));
    if (path.isEmpty()) {
        return;
    }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const QString error = exportIdentity(path, ask.passphrase());
    QApplication::restoreOverrideCursor();
    if (!error.isEmpty()) {
        QMessageBox::warning(this, tr("Export Signing Key"), error);
        return;
    }
    QMessageBox::information(
        this, tr("Export Signing Key"),
        tr("The key is in %1. On the other computer, use Import... on this page with the "
           "passphrase, then delete the file or keep it somewhere safe.")
            .arg(path));
}

void SigningSettingsPage::importClicked()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Import Signing Key"), QString(), tr("Signing keys (*.cdmdsigning)"));
    if (path.isEmpty()) {
        return;
    }
    if (m_seed &&
        QMessageBox::question(
            this, tr("Import Signing Key"),
            tr("The imported key replaces the key of this computer. Files you signed with "
               "it stay signed. Import the key?")) != QMessageBox::Yes) {
        return;
    }
    SigningPassphraseDialog ask(SigningPassphraseDialog::Purpose::Import, this);
    if (ask.exec() != QDialog::Accepted) {
        return;
    }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const QString error = importIdentity(path, ask.passphrase());
    QApplication::restoreOverrideCursor();
    if (!error.isEmpty()) {
        QMessageBox::warning(this, tr("Import Signing Key"), error);
    }
}

void SigningSettingsPage::createKey()
{
    if (m_seed &&
        QMessageBox::question(
            this, tr("New Signing Key"),
            tr("A new key needs a new certificate. Files you signed with the old key stay "
               "signed. Make a new key?")) != QMessageBox::Yes) {
        return;
    }
    m_seed = newSigningSeed();
    m_certificate->clear();
    // A request for the old key no longer helps.
    m_requestWaiting = false;
    m_requestStatus->clear();
    updateState();
}

void SigningSettingsPage::updateState()
{
    m_publicKey->setText(m_seed ? publicKeyText(*m_seed) : QString());
    m_publicKey->setEnabled(m_seed.has_value());
    m_copyButton->setEnabled(m_seed.has_value());
    m_exportButton->setEnabled(m_seed.has_value());
    m_certificate->setEnabled(m_seed.has_value());
    m_createButton->setText(m_seed ? tr("Make a New Key...") : tr("Make a Signing Key"));
    const QString certificate = m_certificate->toPlainText().trimmed();
    const bool trusted =
        m_seed && !certificate.isEmpty() && checkCertificate(certificate, m_seed).trusted;
    m_requestButton->setText(m_requestWaiting ? tr("Check for the Certificate")
                                              : tr("Request Certificate"));
    m_requestButton->setEnabled(m_seed.has_value() && !trusted &&
                                (m_requestWaiting ? onCheckCertificate != nullptr
                                                  : onRequestCertificate != nullptr));
    if (!m_seed) {
        m_status->setText(tr("No signing key yet: the editor exports unsigned files."));
    } else if (certificate.isEmpty()) {
        m_status->setText(
            tr("Waiting for the certificate: the editor exports unsigned files until then."));
    } else if (const CertificateCheck check = checkCertificate(certificate, m_seed);
               check.trusted) {
        m_status->setText(tr("Exports are signed as %1 (%2).").arg(check.name, check.identity));
    } else {
        m_status->setText(tr("The certificate cannot be used: %1.").arg(check.reason));
    }
}

}  // namespace colorizing::app
