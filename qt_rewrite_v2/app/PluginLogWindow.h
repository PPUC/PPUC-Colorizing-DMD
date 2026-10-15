#pragma once

#include <QCoreApplication>
#include <QWidget>

#include <cstdint>

class QComboBox;
class QPlainTextEdit;
class QTimer;

namespace colorizing::app {

// Help -> Plugin Log: what the VPX plugins (DMDUtil, and libdmdutil through it) and the plugin
// bus logged (vpx::Host::logLines()), to find out why a display stays dark. It follows new
// lines while it is shown.
class PluginLogWindow final : public QWidget {
    Q_DECLARE_TR_FUNCTIONS(PluginLogWindow)

public:
    explicit PluginLogWindow(QWidget* parent = nullptr);

    // Shows the lines logged so far, at the level chosen.
    void refresh();
    // The text shown, for Copy All.
    [[nodiscard]] QString text() const;

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    QComboBox* m_levelComboBox = nullptr;
    QPlainTextEdit* m_text = nullptr;
    QTimer* m_timer = nullptr;
    uint64_t m_shownCount = UINT64_MAX;
};

}  // namespace colorizing::app
