#include "PluginLogWindow.h"

#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>

#include "vpx/VpxHost.h"

namespace colorizing::app {

namespace {

// The plugin SDK's levels (LPI_LVL_*).
constexpr unsigned int kInfo = 0x10;
constexpr unsigned int kWarning = 0x20;
constexpr unsigned int kError = 0x40;

QString levelText(unsigned int level)
{
    if (level >= kError) {
        return QStringLiteral("ERROR");
    }
    if (level >= kWarning) {
        return QStringLiteral("WARN ");
    }
    if (level >= kInfo) {
        return QStringLiteral("INFO ");
    }
    return QStringLiteral("DEBUG");
}

}  // namespace

PluginLogWindow::PluginLogWindow(QWidget* parent)
    : QWidget(parent, Qt::Window)
{
    setWindowTitle(tr("Plugin Log"));
    resize(900, 500);

    m_levelComboBox = new QComboBox(this);
    m_levelComboBox->addItem(tr("Everything"), 0u);
    m_levelComboBox->addItem(tr("Information and above"), kInfo);
    m_levelComboBox->addItem(tr("Warnings and errors"), kWarning);
    m_levelComboBox->addItem(tr("Errors"), kError);
    auto* copyButton = new QPushButton(tr("Copy All"), this);
    copyButton->setToolTip(tr("Copy the lines shown, to paste them into a report"));
    auto* clearButton = new QPushButton(tr("Clear"), this);
    m_text = new QPlainTextEdit(this);
    m_text->setReadOnly(true);
    m_text->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_text->setMaximumBlockCount(static_cast<int>(vpx::Host::kMaxLogLines));
    auto* note = new QLabel(
        tr("What the VPX plugins log, among them DMDUtil and libdmdutil for the DMD output. "
           "For more detail from a ZeDMD, switch on \"ZeDMD debug output\" in Settings -> DMD "
           "Output and restart the output."),
        this);
    note->setWordWrap(true);

    auto* bar = new QHBoxLayout;
    bar->addWidget(new QLabel(tr("Show"), this));
    bar->addWidget(m_levelComboBox);
    bar->addStretch(1);
    bar->addWidget(copyButton);
    bar->addWidget(clearButton);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(note);
    layout->addLayout(bar);
    layout->addWidget(m_text, 1);

    connect(m_levelComboBox, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this, [this](int) {
                m_shownCount = UINT64_MAX;
                refresh();
            });
    connect(copyButton, &QPushButton::clicked, this, [this]() {
        if (QClipboard* clipboard = QGuiApplication::clipboard()) {
            clipboard->setText(text());
        }
    });
    connect(clearButton, &QPushButton::clicked, this, [this]() {
        vpx::Host::instance().clearLog();
        m_shownCount = UINT64_MAX;
        refresh();
    });
    // Plugins log from their own threads: the window looks for new lines twice a second.
    m_timer = new QTimer(this);
    m_timer->setInterval(500);
    connect(m_timer, &QTimer::timeout, this, [this]() { refresh(); });
}

void PluginLogWindow::refresh()
{
    const vpx::Host& host = vpx::Host::instance();
    const uint64_t count = host.logCount();
    if (count == m_shownCount) {
        return;
    }
    m_shownCount = count;
    const unsigned int minimum = m_levelComboBox->currentData().toUInt();
    QStringList lines;
    for (const vpx::Host::LogLine& line : host.logLines()) {
        if (line.level < minimum) {
            continue;
        }
        lines << QStringLiteral("%1 %2 [%3] %4")
                     .arg(QDateTime::fromMSecsSinceEpoch(line.timeMs).toString(
                              QStringLiteral("HH:mm:ss.zzz")),
                          levelText(line.level),
                          line.source.empty() ? QStringLiteral("bus")
                                              : QString::fromStdString(line.source),
                          QString::fromStdString(line.message));
    }
    QScrollBar* bar = m_text->verticalScrollBar();
    const bool atEnd = bar->value() == bar->maximum();
    m_text->setPlainText(lines.isEmpty() ? tr("Nothing logged yet.") : lines.join(QChar('\n')));
    if (atEnd) {
        bar->setValue(bar->maximum());
    }
}

QString PluginLogWindow::text() const
{
    return m_text->toPlainText();
}

void PluginLogWindow::showEvent(QShowEvent* event)
{
    m_shownCount = UINT64_MAX;
    refresh();
    m_timer->start();
    QWidget::showEvent(event);
}

void PluginLogWindow::hideEvent(QHideEvent* event)
{
    m_timer->stop();
    QWidget::hideEvent(event);
}

}  // namespace colorizing::app
