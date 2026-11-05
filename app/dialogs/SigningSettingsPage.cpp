#include "dialogs/SigningSettingsPage.h"

#include <QClipboard>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace colorizing::app {

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
    layout->addWidget(m_createButton, 0, Qt::AlignLeft);
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
