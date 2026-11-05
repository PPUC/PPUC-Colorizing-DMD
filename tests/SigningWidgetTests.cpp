// Settings -> Signing and the project's choice to sign.

#include <cstdlib>
#include <iostream>
#include <string>

#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>

#include "RuntimeFixtures.h"
#include "dialogs/CromcHeaderDialog.h"
#include "dialogs/SigningSettingsPage.h"
#include "panels/ProjectSettingsPanel.h"

namespace {

using namespace colorizing;

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: signing widgets: " << message << '\n';
        std::exit(1);
    }
}

// The page says what is missing, shows the public key of the key it is given, and checks the
// certificate as it is pasted.
void testSigningPage()
{
    app::SigningSettingsPage page;
    page.setKey({});
    expect(page.statusText().contains(QStringLiteral("No signing key")), "no key yet");
    expect(!page.key().seed, "and gives none back");

    app::AuthorSigningKey key;
    std::array<uint8_t, 32> seed{};
    seed.fill(0x01);
    key.seed = seed;
    page.setKey(key);
    expect(page.statusText().contains(QStringLiteral("Waiting for the certificate")),
           "a key without a certificate");
    auto* publicKey = page.findChild<QLineEdit*>();
    const QString expected =
        QStringLiteral("8a88e3dd7409f195fd52db2d3cba5d72ca6709bf1d94121bf3748801b40f6f5c");
    expect(publicKey && publicKey->text() == expected,
           "the public key is shown");
    auto* certificate = page.findChild<QPlainTextEdit*>();
    expect(certificate != nullptr, "there is the certificate box");
    certificate->setPlainText(QStringLiteral("SERUM-CERTIFICATE-1:AAAA"));
    expect(page.statusText().contains(QStringLiteral("cannot be used")),
           "a certificate that is none is refused");
    expect(page.key().seed == seed &&
               page.key().certificate == QStringLiteral("SERUM-CERTIFICATE-1:AAAA"),
           "the page gives back what it has");
    expect(!app::canSign(page.key()), "and it cannot sign");
}

// The project settings show whether the project signs, and say so when it is changed.
void testProjectChoice()
{
    document::ProjectDocument document = document::ProjectDocument::fromSnapshot(
        runtime_fixtures::makeProject(render::PlaneLayout::Ordinary, 1));
    app::ProjectSettingsPanel panel;
    panel.setProject(&document);
    expect(panel.signCromc() && panel.signCromcCheckBox()->isChecked(),
           "a project signs by default");
    document::ColorizationSettings settings;
    settings.signCromc = false;
    document.setColorizationSettings(settings);
    panel.setProject(&document);
    expect(!panel.signCromc(), "a project that does not sign");
    panel.signCromcCheckBox()->setChecked(true);
    expect(panel.signCromc(), "is turned on");
}

// Request Certificate sends the public key; while the request waits it becomes the check, and
// a certificate the answer brings goes into the box.
void testRequestButton()
{
    app::SigningSettingsPage page;
    QString sent;
    bool checked = false;
    page.onRequestCertificate = [&](const QString& publicKey) { sent = publicKey; };
    page.onCheckCertificate = [&]() { checked = true; };
    app::AuthorSigningKey key;
    std::array<uint8_t, 32> seed{};
    seed.fill(0x01);
    key.seed = seed;
    page.setKey(key);
    QPushButton* button = nullptr;
    for (QPushButton* candidate : page.findChildren<QPushButton*>()) {
        if (candidate->text() == QStringLiteral("Request Certificate")) {
            button = candidate;
        }
    }
    expect(button && button->isEnabled(), "a key without a certificate can be requested");
    button->click();
    expect(sent == app::publicKeyText(seed), "the request names the public key");
    page.setRequestState(true, QStringLiteral("Waiting"));
    expect(button->text() == QStringLiteral("Check for the Certificate") &&
               page.requestText() == QStringLiteral("Waiting"),
           "a waiting request is checked");
    button->click();
    expect(checked, "by the same button");
    page.takeCertificate(QStringLiteral("SERUM-CERTIFICATE-1:AAAA"));
    expect(page.key().certificate == QStringLiteral("SERUM-CERTIFICATE-1:AAAA"),
           "the answer's certificate goes into the box");
}

// The license box offers Creative Commons without changes, its building blocks deciding the
// kind, and the PPUC license with its choices; a kind that allows changes shows only for a
// project that has it.
void testLicenseChoices()
{
    document::ProjectDocument document = document::ProjectDocument::fromSnapshot(
        runtime_fixtures::makeProject(render::PlaneLayout::Ordinary, 1));
    app::ProjectSettingsPanel panel;
    panel.setProject(&document);
    QComboBox* box = panel.licenseComboBox();
    for (const document::LicenseKind kind :
         {document::LicenseKind::Cc0, document::LicenseKind::CcBy,
          document::LicenseKind::CcBySa, document::LicenseKind::CcByNc,
          document::LicenseKind::CcByNcSa}) {
        expect(box->findData(static_cast<int>(kind)) < 0, "no kind that allows changes");
    }
    box->setCurrentIndex(box->findData(app::ProjectSettingsPanel::kCreativeCommonsChoice));
    expect(panel.license().kind == document::LicenseKind::CcByNcNd,
           "Creative Commons without commercial use is BY-NC-ND");
    panel.ppucCommercialCheckBox()->setChecked(true);
    expect(panel.license().commercialWithPpucDmd, "with the CC+ permission for a PPUC DMD");
    panel.commercialCheckBox()->setChecked(true);
    expect(panel.license().kind == document::LicenseKind::CcByNd &&
               !panel.license().commercialWithPpucDmd &&
               !panel.ppucCommercialCheckBox()->isEnabled(),
           "with commercial use BY-ND, which needs no permission");

    box->setCurrentIndex(
        box->findData(static_cast<int>(document::LicenseKind::PpucColorization)));
    panel.ppucOnlyCheckBox()->setChecked(true);
    const document::ProjectLicense ppuc = panel.license();
    expect(ppuc.kind == document::LicenseKind::PpucColorization &&
               ppuc.commercialWithPpucDmd && ppuc.realMachinesOnlyPpucDmd,
           "the PPUC license with both choices");

    document::ProjectLicense old;
    old.kind = document::LicenseKind::CcBySa;
    document.setLicense(old);
    panel.setProject(&document);
    expect(panel.license().kind == document::LicenseKind::CcBySa &&
               panel.licenseLabel()->text().contains(QStringLiteral("choose another")),
           "a project's old kind shows, with the advice to change it");
}

// File -> Show .cROMc Header shows the text the export writes, the author taken from the
// settings when the license names nobody.
void testCromcHeader()
{
    document::ProjectLicense license;
    license.kind = document::LicenseKind::CcByNcNd;
    const QString text =
        app::CromcHeaderDialog::text("afm_113b", license, QStringLiteral(" Kraken "));
    expect(text.startsWith(QStringLiteral("Serum colorization\nROM: afm_113b\nAuthor: Kraken\n"
                                          "License: CC BY-NC-ND 4.0\n")),
           "the author comes from the settings: " + text.toStdString());
    license.attribution = "Team Kraken";
    expect(app::CromcHeaderDialog::text("afm_113b", license, QStringLiteral("Kraken"))
               .contains(QStringLiteral("Author: Team Kraken\n")),
           "the license's attribution comes first");
    app::CromcHeaderDialog dialog(text);
    expect(dialog.shownText() == text, "the dialog shows it");
}

}  // namespace

void runSigningWidgetTests()
{
    testSigningPage();
    testProjectChoice();
    testRequestButton();
    testLicenseChoices();
    testCromcHeader();
}
