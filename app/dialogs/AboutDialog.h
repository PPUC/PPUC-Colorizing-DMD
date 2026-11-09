#pragma once

#include <QCoreApplication>
#include <QDialog>

namespace colorizing::app {

// Help -> About: the application's name, version and icon, the versions of libserum and Qt it
// was built with, the licence and where the project lives. Its "Third-Party Licenses" button
// shows the notices of the software the packages contain (THIRD-PARTY-NOTICES.md).
class AboutDialog final : public QDialog {
    Q_DECLARE_TR_FUNCTIONS(AboutDialog)

public:
    explicit AboutDialog(QWidget* parent = nullptr);

    // The text of the version line, for tests: "Version 3.99.38".
    [[nodiscard]] static QString versionText();

    // THIRD-PARTY-NOTICES.md as built in: Markdown, empty if the resource is missing.
    [[nodiscard]] static QString thirdPartyNotices();

private:
    // Shows the third-party notices in a window of their own, beside this dialog.
    void showThirdPartyNotices();
};

}  // namespace colorizing::app
