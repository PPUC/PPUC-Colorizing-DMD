#pragma once

#include <QCoreApplication>
#include <QDialog>
#include <QPointer>
#include <QString>
#include <QStringList>

#include <optional>
#include <vector>

#include "document/ProjectDocument.h"
#include "team/GitHub.h"
#include "team/TeamMerge.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QTimer;

namespace colorizing::app {

// Team -> Sign In To GitHub: GitHub's device flow. It shows the code to enter on GitHub's page
// (the browser opens it) and asks for the token until the user has entered it; then it knows
// the user's login. The editor never sees the password.
class SignInDialog final : public QDialog {
    Q_DECLARE_TR_FUNCTIONS(SignInDialog)

public:
    SignInDialog(GitHubClient& client, QWidget* parent = nullptr);

    // After it was accepted.
    [[nodiscard]] const QString& login() const { return m_login; }
    [[nodiscard]] QString token() const;

private:
    void start();
    void poll();
    void fail(const QString& error);

    GitHubClient& m_client;
    std::optional<GitHubClient::DeviceCode> m_code;
    QLabel* m_codeLabel = nullptr;
    QLabel* m_statusLabel = nullptr;
    QTimer* m_timer = nullptr;
    QString m_login;
    int m_intervalSeconds = 5;
};

// Team -> Start Team Project: where the team's repository goes and who the partners are.
class StartTeamDialog final : public QDialog {
    Q_DECLARE_TR_FUNCTIONS(StartTeamDialog)

public:
    StartTeamDialog(const QString& romName, const QString& login, QWidget* parent = nullptr);

    // Empty: the user's own account.
    [[nodiscard]] QString owner() const;
    [[nodiscard]] QString repositoryName() const;
    // GitHub logins, without the user's own.
    [[nodiscard]] QStringList partners() const;

private:
    QLineEdit* m_owner = nullptr;
    QLineEdit* m_name = nullptr;
    QLineEdit* m_partners = nullptr;
    QString m_login;
};

// The conflicts of a merge: for each, this copy's version, the partner's and the common one
// (what both started from), and which of the two to keep.
class MergeDialog final : public QDialog {
    Q_DECLARE_TR_FUNCTIONS(MergeDialog)

public:
    // projectPath: the project file, which keeps the common versions of changed frames.
    MergeDialog(const document::ProjectDocument& document,
                const team::ChangePackage& package,
                const team::MergePlan& plan,
                const QString& projectPath,
                QWidget* parent = nullptr);

    [[nodiscard]] team::MergeChoices choices() const;

private:
    struct Row {
        document::ComponentId id;
        QComboBox* choice = nullptr;
    };
    std::vector<Row> m_rows;
    QComboBox* m_settings = nullptr;
};

}  // namespace colorizing::app
