#pragma once

#include <QDialog>

#include "Appearance.h"
#include "DmdOutput.h"

class QComboBox;
class QTabWidget;

namespace colorizing::app {

class DmdOutputSettingsPage;

// The editor's settings, one page per topic: Appearance (the theme) and DMD Output. It only
// shows and collects them; the caller stores and applies what it returns.
class SettingsDialog final : public QDialog {
public:
    enum class Page {
        Appearance,
        DmdOutput,
    };

    SettingsDialog(Theme theme,
                   const DmdOutputSettings& dmdOutput,
                   bool dmdOutputAvailable,
                   QWidget* parent = nullptr);

    void showPage(Page page);

    [[nodiscard]] Theme theme() const;
    [[nodiscard]] DmdOutputSettings dmdOutputSettings() const;

private:
    QTabWidget* m_tabs = nullptr;
    QComboBox* m_themeComboBox = nullptr;
    DmdOutputSettingsPage* m_dmdOutputPage = nullptr;
};

}  // namespace colorizing::app
