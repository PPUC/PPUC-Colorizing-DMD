#include "dialogs/CromcHeaderDialog.h"

#include <QClipboard>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

namespace colorizing::app {

CromcHeaderDialog::CromcHeaderDialog(const QString& text, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr(".cROMc Header"));
    auto* intro = new QLabel(
        tr("The exported .cROMc starts with this text, readable without libserum, before the "
           "colorization itself. It comes from the project's license (File -> Project "
           "Settings) and is part of what the signature covers."),
        this);
    intro->setWordWrap(true);
    m_text = new QPlainTextEdit(this);
    m_text->setReadOnly(true);
    m_text->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    m_text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_text->setPlainText(text);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    QPushButton* copy = buttons->addButton(tr("Copy"), QDialogButtonBox::ActionRole);
    connect(copy, &QPushButton::clicked, this, [this]() {
        QGuiApplication::clipboard()->setText(m_text->toPlainText());
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(intro);
    layout->addWidget(m_text, 1);
    layout->addWidget(buttons);
    resize(640, 480);
}

QString CromcHeaderDialog::text(const std::string& romName,
                                document::ProjectLicense license,
                                const QString& authorName,
                                const std::vector<std::string>& contributors)
{
    if (license.attribution.empty()) {
        license.attribution = authorName.trimmed().toStdString();
    }
    return QString::fromStdString(document::colorizationText(romName, license, contributors));
}

QString CromcHeaderDialog::shownText() const
{
    return m_text->toPlainText();
}

CromcHeaderDialog::Credits CromcHeaderDialog::credits(
    const document::ProjectLicense& license,
    const std::optional<document::TeamState>& team,
    const std::vector<std::string>& componentAuthors,
    const QString& selfName,
    const QString& fallbackAuthor)
{
    Credits credits;
    if (!license.attribution.empty()) {
        credits.author = QString::fromStdString(license.attribution);
    } else if (team && !team->ownerLogin.empty()) {
        credits.author =
            QStringLiteral("%1 (GitHub)").arg(QString::fromStdString(team->ownerLogin));
    } else {
        credits.author = fallbackAuthor.trimmed();
    }
    const std::string author = credits.author.toStdString();
    const auto add = [&credits, &author](const std::string& name) {
        if (!name.empty() && name != author &&
            std::find(credits.contributors.begin(), credits.contributors.end(), name) ==
                credits.contributors.end()) {
            credits.contributors.push_back(name);
        }
    };
    if (team) {
        for (const std::string& member : team->members) {
            if (member != team->ownerLogin) {
                add(member + " (GitHub)");
            }
        }
    } else {
        const std::string self = selfName.trimmed().toStdString();
        for (const std::string& name : componentAuthors) {
            if (name != self) {
                add(name);
            }
        }
    }
    return credits;
}

}  // namespace colorizing::app
