#pragma once

#include <QCoreApplication>
#include <QDialog>
#include <QString>

#include <string>
#include <vector>

#include "document/ProjectLicense.h"
#include "document/TeamState.h"

#include <optional>

class QPlainTextEdit;

namespace colorizing::app {

// File -> Show .cROMc Header: the text the exported .cROMc starts with, readable as it is
// (document::colorizationText()), as the next export writes it.
class CromcHeaderDialog final : public QDialog {
    Q_DECLARE_TR_FUNCTIONS(CromcHeaderDialog)

public:
    CromcHeaderDialog(const QString& text, QWidget* parent = nullptr);

    // The text for the project: its license, with authorName as the author when the license
    // names nobody, and the contributors, as the export does (MainWindow::exportCredits()).
    [[nodiscard]] static QString text(const std::string& romName,
                                      document::ProjectLicense license,
                                      const QString& authorName,
                                      const std::vector<std::string>& contributors = {});

    [[nodiscard]] QString shownText() const;

    // Who the exported cROMc names. The author: the license's attribution; else a team
    // project's owner, whoever exports it; else fallbackAuthor (the name in Settings ->
    // Projects or the GitHub account of the signing certificate). The contributors: a team
    // project's other members; else the authors of the components (componentAuthors) other
    // than selfName, the one who works with this editor.
    struct Credits {
        QString author;
        std::vector<std::string> contributors;
    };
    [[nodiscard]] static Credits credits(const document::ProjectLicense& license,
                                         const std::optional<document::TeamState>& team,
                                         const std::vector<std::string>& componentAuthors,
                                         const QString& selfName,
                                         const QString& fallbackAuthor);

private:
    QPlainTextEdit* m_text = nullptr;
};

}  // namespace colorizing::app
