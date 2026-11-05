#pragma once

#include <QCoreApplication>
#include <QDialog>
#include <QString>

#include "document/ProjectLicense.h"

class QPlainTextEdit;

namespace colorizing::app {

// File -> Show .cROMc Header: the text the exported .cROMc starts with, readable as it is
// (document::colorizationText()), as the next export writes it.
class CromcHeaderDialog final : public QDialog {
    Q_DECLARE_TR_FUNCTIONS(CromcHeaderDialog)

public:
    CromcHeaderDialog(const QString& text, QWidget* parent = nullptr);

    // The text for the project: its license, with authorName as the author when the license
    // names nobody, as the export does.
    [[nodiscard]] static QString text(const std::string& romName,
                                      document::ProjectLicense license,
                                      const QString& authorName);

    [[nodiscard]] QString shownText() const;

private:
    QPlainTextEdit* m_text = nullptr;
};

}  // namespace colorizing::app
