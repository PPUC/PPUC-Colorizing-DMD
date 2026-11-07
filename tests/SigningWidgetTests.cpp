// Settings -> Signing and the project's choice to sign.

#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>

#include <QCheckBox>
#include <QComboBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryDir>

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

// A signing identity file brings the key and its certificate back with the passphrase, and
// refuses a wrong passphrase, a changed file and what is no such file.
void testSigningIdentityFile()
{
    app::AuthorSigningKey key;
    std::array<uint8_t, 32> seed{};
    for (std::size_t i = 0; i < seed.size(); ++i) {
        seed[i] = static_cast<uint8_t>(i * 7 + 3);
    }
    key.seed = seed;
    key.certificate = QStringLiteral("SERUM-CERTIFICATE-1:AAAA");
    const QString passphrase = QStringLiteral("day and night");
    const std::optional<QByteArray> file = app::signingIdentityFile(key, passphrase, 1000);
    expect(file.has_value(), "a key is exported");
    expect(!file->contains(QByteArray(reinterpret_cast<const char*>(seed.data()), 32).toHex()) &&
               !file->contains(
                   QByteArray(reinterpret_cast<const char*>(seed.data()), 32).toBase64()),
           "the secret key is not in the file in the clear");
    expect(app::signingIdentityFile(key, passphrase, 1000) != file,
           "every export has its own salt");
    expect(!app::signingIdentityFile(app::AuthorSigningKey{}, passphrase, 1000),
           "without a key there is nothing to export");

    const app::SigningIdentityRead read = app::readSigningIdentityFile(*file, passphrase);
    expect(read.error == app::SigningIdentityError::None && read.key && *read.key == key,
           "the passphrase brings the key and the certificate back");
    expect(app::readSigningIdentityFile(*file, QStringLiteral("day and nighT")).error ==
               app::SigningIdentityError::WrongPassphraseOrChanged,
           "a wrong passphrase is refused");

    const QJsonObject root = QJsonDocument::fromJson(*file).object();
    const auto changed = [&](const std::function<void(QJsonObject&)>& change) {
        QJsonObject copy = root;
        change(copy);
        return app::readSigningIdentityFile(QJsonDocument(copy).toJson(), passphrase).error;
    };
    expect(changed([](QJsonObject& o) {
               o.insert(QStringLiteral("certificate"), QStringLiteral("SERUM-CERTIFICATE-1:BBBB"));
           }) == app::SigningIdentityError::WrongPassphraseOrChanged,
           "a changed certificate is refused");
    expect(changed([](QJsonObject& o) {
               QByteArray bytes = QByteArray::fromBase64(
                   o.value(QStringLiteral("encryptedKey")).toString().toLatin1());
               bytes[5] = static_cast<char>(bytes[5] ^ 1);
               o.insert(QStringLiteral("encryptedKey"), QString::fromLatin1(bytes.toBase64()));
           }) == app::SigningIdentityError::WrongPassphraseOrChanged,
           "a changed key is refused");
    expect(changed([&](QJsonObject& o) {
               std::array<uint8_t, 32> other = seed;
               other[0] ^= 1;
               o.insert(QStringLiteral("publicKey"), app::publicKeyText(other));
           }) == app::SigningIdentityError::WrongPassphraseOrChanged,
           "a changed public key is refused");
    expect(changed([](QJsonObject& o) {
               QJsonObject kdf = o.value(QStringLiteral("kdf")).toObject();
               kdf.insert(QStringLiteral("iterations"), 999);
               o.insert(QStringLiteral("kdf"), kdf);
           }) == app::SigningIdentityError::WrongPassphraseOrChanged,
           "fewer rounds are refused");
    expect(changed([](QJsonObject& o) {
               QJsonObject kdf = o.value(QStringLiteral("kdf")).toObject();
               kdf.insert(QStringLiteral("iterations"), app::kSigningIdentityMaxIterations + 1);
               o.insert(QStringLiteral("kdf"), kdf);
           }) == app::SigningIdentityError::NotAnIdentity,
           "too many rounds are not even tried");
    expect(changed([](QJsonObject& o) { o.remove(QStringLiteral("mac")); }) ==
               app::SigningIdentityError::NotAnIdentity,
           "a file without its check is none");
    expect(changed([](QJsonObject& o) { o.insert(QStringLiteral("version"), 2); }) ==
               app::SigningIdentityError::NewerFormat,
           "a newer format is named");
    expect(app::readSigningIdentityFile(file->left(file->size() / 2), passphrase).error ==
               app::SigningIdentityError::NotAnIdentity,
           "a cut file is refused");
    expect(app::readSigningIdentityFile(QByteArray("{}"), passphrase).error ==
               app::SigningIdentityError::NotAnIdentity,
           "other JSON is refused");
}

// Settings -> Signing exports the key to a file and imports it on another computer; the
// passphrase is asked twice for an export.
void testSigningIdentityPage()
{
    QTemporaryDir folder;
    expect(folder.isValid(), "a temporary folder");
    const QString path = folder.filePath(QStringLiteral("key.cdmdsigning"));

    app::SigningSettingsPage day;
    std::array<uint8_t, 32> seed{};
    seed.fill(0x01);
    app::AuthorSigningKey key;
    key.seed = seed;
    key.certificate = QStringLiteral("SERUM-CERTIFICATE-1:AAAA");
    day.setKey(key);
    QPushButton* exportButton = nullptr;
    QPushButton* importButton = nullptr;
    for (QPushButton* candidate : day.findChildren<QPushButton*>()) {
        if (candidate->text() == QStringLiteral("Export...")) {
            exportButton = candidate;
        } else if (candidate->text() == QStringLiteral("Import...")) {
            importButton = candidate;
        }
    }
    expect(exportButton && exportButton->isEnabled() && importButton &&
               importButton->isEnabled(),
           "a key can be exported, and another imported");
    expect(day.exportIdentity(path, QStringLiteral("evening key"), 1000).isEmpty(),
           "the key is exported");

    app::SigningSettingsPage evening;
    evening.setKey({});
    QPushButton* eveningExport = nullptr;
    for (QPushButton* candidate : evening.findChildren<QPushButton*>()) {
        if (candidate->text() == QStringLiteral("Export...")) {
            eveningExport = candidate;
        }
    }
    expect(eveningExport && !eveningExport->isEnabled(), "without a key nothing is exported");
    expect(evening.importIdentity(path, QStringLiteral("evening kez")).contains(
               QStringLiteral("passphrase is wrong")),
           "a wrong passphrase says so");
    expect(!evening.key().seed, "and takes nothing");
    expect(evening.importIdentity(path, QStringLiteral("evening key")).isEmpty() &&
               evening.key() == key,
           "the other computer signs with the same key and certificate");
    expect(evening.importIdentity(folder.filePath(QStringLiteral("none")),
                                  QStringLiteral("evening key"))
               .contains(QStringLiteral("could not be read")),
           "a missing file is named");

    app::SigningPassphraseDialog ask(app::SigningPassphraseDialog::Purpose::Export);
    ask.setPassphrase(QStringLiteral("short"), QStringLiteral("short"));
    expect(!ask.canAccept() && ask.problemText().contains(QStringLiteral("At least 8")),
           "an export's passphrase is long enough");
    ask.setPassphrase(QStringLiteral("long enough"), QStringLiteral("long enougj"));
    expect(!ask.canAccept() && ask.problemText().contains(QStringLiteral("differ")),
           "and typed twice the same");
    ask.setPassphrase(QStringLiteral("long enough"), QStringLiteral("long enough"));
    expect(ask.canAccept() && ask.passphrase() == QStringLiteral("long enough"),
           "then it is taken");
    app::SigningPassphraseDialog askImport(app::SigningPassphraseDialog::Purpose::Import);
    expect(!askImport.canAccept(), "an import needs a passphrase");
    askImport.setPassphrase(QStringLiteral("x"));
    expect(askImport.canAccept(), "of any length, once");
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
    expect(panel.license().commercialWithPpucDmd &&
               panel.licenseLabel()->text().contains(QStringLiteral("ppuc.org")) &&
               panel.licenseLabel()->text().contains(QStringLiteral("PIN2DMD MMI")),
           "with the CC+ permission for a PPUC/DMD, explained");
    panel.commercialCheckBox()->setChecked(true);
    expect(panel.license().kind == document::LicenseKind::CcByNd &&
               !panel.license().commercialWithPpucDmd &&
               !panel.ppucCommercialCheckBox()->isEnabled(),
           "with commercial use BY-ND, which needs no permission");

    box->setCurrentIndex(
        box->findData(static_cast<int>(document::LicenseKind::PpucColorization)));
    panel.ppucOnlyCheckBox()->setChecked(true);
    const document::ProjectLicense ppuc = panel.license();
    panel.contactEdit()->setText(QStringLiteral(" kraken@example.org "));
    expect(panel.license().contact == "kraken@example.org", "the contact, trimmed");
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

// A team project names its owner as the author, whoever exports, and its other members as
// contributors; a solo project the one who exports and the other authors of its components.
// The license's attribution comes first.
void testCredits()
{
    document::TeamState team;
    team.ownerLogin = "mkalkbrenner";
    team.members = {"mkalkbrenner", "PastorL69", "Kraken74"};
    using Credits = app::CromcHeaderDialog::Credits;
    const Credits shared = app::CromcHeaderDialog::credits(
        {}, team, {}, QStringLiteral("Kraken"), QStringLiteral("Kraken74 (github:9)"));
    expect(shared.author == QStringLiteral("mkalkbrenner (GitHub)") &&
               shared.contributors ==
                   std::vector<std::string>{"PastorL69 (GitHub)", "Kraken74 (GitHub)"},
           "a team project keeps its owner as the author");

    const Credits solo = app::CromcHeaderDialog::credits(
        {}, std::nullopt, {"Markus", "Kraken", "Markus"}, QStringLiteral(" Markus "),
        QStringLiteral("Markus"));
    expect(solo.author == QStringLiteral("Markus") &&
               solo.contributors == std::vector<std::string>{"Kraken"},
           "a solo project names the other authors of its components");

    document::ProjectLicense license;
    license.attribution = "Team Kraken";
    expect(app::CromcHeaderDialog::credits(license, team, {}, {}, {}).author ==
               QStringLiteral("Team Kraken"),
           "the attribution comes first");

    const QString text = app::CromcHeaderDialog::text("afm_113b", {}, shared.author,
                                                      shared.contributors);
    expect(text.contains(QStringLiteral("Author: mkalkbrenner (GitHub)\n")) &&
               text.contains(
                   QStringLiteral("Contributors: PastorL69 (GitHub), Kraken74 (GitHub)\n")),
           "the header names them: " + text.toStdString());
}

}  // namespace

void runSigningWidgetTests()
{
    testSigningPage();
    testProjectChoice();
    testRequestButton();
    testSigningIdentityFile();
    testSigningIdentityPage();
    testLicenseChoices();
    testCromcHeader();
    testCredits();
}
