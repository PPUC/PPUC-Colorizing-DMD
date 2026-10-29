#pragma once

#include <QCoreApplication>
#include <QDialog>

namespace colorizing::app {

// Help -> About: the application's name, version and icon, the versions of libserum and Qt it
// was built with, the licence and where the project lives.
class AboutDialog final : public QDialog {
    Q_DECLARE_TR_FUNCTIONS(AboutDialog)

public:
    explicit AboutDialog(QWidget* parent = nullptr);

    // The text of the version line, for tests: "Version 3.99.22".
    [[nodiscard]] static QString versionText();
};

}  // namespace colorizing::app
