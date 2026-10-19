#include "dialogs/AboutDialog.h"

#include <QApplication>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

#include "AppInfo.h"
#include "runtime/CromcExport.h"

namespace colorizing::app {

AboutDialog::AboutDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("About %1").arg(kApplicationName));
    auto* icon = new QLabel(this);
    icon->setPixmap(QApplication::windowIcon().pixmap(128, 128));
    auto* title = new QLabel(QStringLiteral("<h2>%1</h2>").arg(kApplicationName.toHtmlEscaped()),
                             this);
    auto* version = new QLabel(versionText(), this);
    version->setObjectName(QStringLiteral("aboutVersion"));
    version->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* details = new QLabel(
        tr("An editor for Serum colorizations of pinball DMD frames.<br><br>"
           "libserum %1, Qt %2<br><br>"
           "Licensed under the GNU General Public License, version 3 or later.<br>"
           "<a href=\"%3\">%3</a><br><a href=\"%4\">%4</a>")
            .arg(QString::fromStdString(runtime::libserumVersion()),
                 QString::fromLatin1(qVersion()), kProjectUrl, kOrganizationUrl),
        this);
    details->setOpenExternalLinks(true);
    details->setTextInteractionFlags(Qt::TextBrowserInteraction);
    details->setWordWrap(true);

    auto* text = new QVBoxLayout;
    text->addWidget(title);
    text->addWidget(version);
    text->addWidget(details);
    text->addStretch(1);
    auto* row = new QHBoxLayout;
    row->addWidget(icon, 0, Qt::AlignTop);
    row->addSpacing(12);
    row->addLayout(text, 1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto* layout = new QVBoxLayout(this);
    layout->addLayout(row);
    layout->addWidget(buttons);
}

QString AboutDialog::versionText()
{
    return tr("Version %1").arg(kApplicationVersion);
}

}  // namespace colorizing::app
