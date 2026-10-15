#include "PupTesterWindow.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QDir>
#include <QFileDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTime>
#include <QTimer>
#include <QVBoxLayout>

#include <string>

#include "document/ProjectDocument.h"
#include "vpx/PupHost.h"

namespace colorizing::app {

namespace {

const QString kFolderKey = QStringLiteral("pup/folder");

// The PuP pack name the project names (the cRP's, UTF-16 as the Win32 editor writes it), when
// it names one.
QString projectPackName(const document::ProjectDocument& document)
{
    const document::EditorMetadata& metadata = document.editorMetadata();
    if (!metadata.hasPupPack || metadata.pupPackName.size() < 2) {
        return {};
    }
    QString name;
    for (std::size_t index = 0; index + 1 < metadata.pupPackName.size(); index += 2) {
        const char16_t unit = static_cast<char16_t>(metadata.pupPackName[index] |
                                                    (metadata.pupPackName[index + 1] << 8));
        if (unit == 0) {
            break;
        }
        name += QChar(unit);
    }
    return name.trimmed();
}

}  // namespace

// One of PUP's screens: drawn by the host into a picture of the widget's size.
class PupScreenView final : public QWidget {
public:
    PupScreenView(vpx::PupHost::Screen screen, double aspect, QWidget* parent)
        : QWidget(parent)
        , m_screen(screen)
        , m_aspect(aspect)
    {
        setMinimumSize(160, static_cast<int>(160 / aspect));
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

    // Draws PUP's picture of the screen; nothing shown when PUP has none.
    void refresh(vpx::PupHost& host)
    {
        const QSize size = pictureSize();
        if (size.isEmpty()) {
            return;
        }
        if (m_picture.size() != size) {
            m_picture = QImage(size, QImage::Format_ARGB32);
        }
        m_shown = host.render(m_screen, m_picture);
        update();
    }

    void clear()
    {
        m_shown = false;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), Qt::black);
        if (m_shown) {
            const QSize size = pictureSize();
            painter.drawImage(QPoint((width() - size.width()) / 2, (height() - size.height()) / 2),
                              m_picture);
        }
    }

private:
    // The screen's proportions, as large as the widget allows.
    [[nodiscard]] QSize pictureSize() const
    {
        return QSizeF(m_aspect * 1000.0, 1000.0).scaled(QSizeF(size()), Qt::KeepAspectRatio).toSize();
    }

    vpx::PupHost::Screen m_screen;
    double m_aspect;
    QImage m_picture;
    bool m_shown = false;
};

PupTesterWindow::PupTesterWindow(QWidget* parent)
    : QWidget(parent, Qt::Window)
    , m_host(std::make_unique<vpx::PupHost>())
{
    setWindowTitle(tr("PuP Tester"));
    resize(900, 760);

    m_folderEdit = new QLineEdit(QSettings().value(kFolderKey).toString(), this);
    m_folderEdit->setPlaceholderText(tr("The PinUP System folder, which holds pupvideos"));
    m_folderEdit->setToolTip(tr("PUP looks for the pack in <this folder>/pupvideos/<game>"));
    auto* browseButton = new QPushButton(tr("Browse..."), this);
    m_gameEdit = new QLineEdit(this);
    m_gameEdit->setToolTip(tr("The pack's folder name in pupvideos: the project's PuP pack "
                              "name, or its ROM name"));
    m_packLabel = new QLabel(this);
    m_startButton = new QPushButton(this);
    auto* setup = new QGridLayout;
    setup->addWidget(new QLabel(tr("PUP folder"), this), 0, 0);
    setup->addWidget(m_folderEdit, 0, 1);
    setup->addWidget(browseButton, 0, 2);
    setup->addWidget(new QLabel(tr("Game"), this), 1, 0);
    setup->addWidget(m_gameEdit, 1, 1);
    setup->addWidget(m_startButton, 1, 2);
    setup->addWidget(m_packLabel, 2, 1, 1, 2);
    setup->setColumnStretch(1, 1);

    m_topper = new PupScreenView(vpx::PupHost::Screen::Topper, 4.0, this);
    m_backglass = new PupScreenView(vpx::PupHost::Screen::Backglass, 16.0 / 9.0, this);
    m_dmd = new PupScreenView(vpx::PupHost::Screen::Dmd, 4.0, this);
    m_topper->setToolTip(tr("PUP's topper screen"));
    m_backglass->setToolTip(tr("PUP's backglass screen"));
    m_dmd->setToolTip(tr("PUP's DMD screen"));
    auto* screens = new QVBoxLayout;
    screens->addWidget(m_topper, 1);
    screens->addWidget(m_backglass, 4);
    screens->addWidget(m_dmd, 1);

    m_followCheckBox = new QCheckBox(tr("Follow the editor"), this);
    m_followCheckBox->setChecked(true);
    m_followCheckBox->setToolTip(tr("Send the trigger ID of each frame the editor shows, as a "
                                    "machine does when it identifies the frame (playback too)"));
    m_triggerSpinBox = new QSpinBox(this);
    m_triggerSpinBox->setRange(0, 65535);
    m_sendButton = new QPushButton(tr("Send Trigger"), this);
    m_sentList = new QListWidget(this);
    m_sentList->setToolTip(tr("The triggers sent to PUP, the last one first"));
    auto* triggerRow = new QHBoxLayout;
    triggerRow->addWidget(m_followCheckBox);
    triggerRow->addStretch(1);
    triggerRow->addWidget(new QLabel(tr("Trigger"), this));
    triggerRow->addWidget(m_triggerSpinBox);
    triggerRow->addWidget(m_sendButton);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(setup);
    layout->addLayout(screens, 1);
    layout->addLayout(triggerRow);
    layout->addWidget(m_sentList);
    m_sentList->setMaximumHeight(110);

    m_timer = new QTimer(this);
    m_timer->setInterval(33);
    connect(m_timer, &QTimer::timeout, this, [this]() { renderScreens(); });
    connect(browseButton, &QPushButton::clicked, this, [this]() {
        const QString folder = QFileDialog::getExistingDirectory(
            this, tr("PUP Folder"), m_folderEdit->text().isEmpty() ? QDir::homePath()
                                                                     : m_folderEdit->text());
        if (!folder.isEmpty()) {
            m_folderEdit->setText(QDir::toNativeSeparators(folder));
        }
    });
    connect(m_folderEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        QSettings().setValue(kFolderKey, text);
        updateState();
    });
    connect(m_gameEdit, &QLineEdit::textChanged, this, [this](const QString&) { updateState(); });
    connect(m_startButton, &QPushButton::clicked, this, [this]() {
        if (isRunning()) {
            stop();
        } else {
            start();
        }
    });
    connect(m_sendButton, &QPushButton::clicked, this,
            [this]() { sendTrigger(static_cast<uint32_t>(m_triggerSpinBox->value())); });
    updateState();
}

PupTesterWindow::~PupTesterWindow() = default;

void PupTesterWindow::setProject(const document::ProjectDocument* document)
{
    if (!document) {
        m_gameEdit->clear();
        return;
    }
    const QString pack = projectPackName(*document);
    m_gameEdit->setText(pack.isEmpty() ? QString::fromStdString(document->romName()) : pack);
}

void PupTesterWindow::frameShown(std::optional<uint32_t> triggerId)
{
    if (triggerId && isRunning() && m_followCheckBox->isChecked()) {
        sendTrigger(*triggerId);
    }
}

bool PupTesterWindow::isRunning() const
{
    return m_host->isRunning();
}

QString PupTesterWindow::game() const
{
    return m_gameEdit->text().trimmed();
}

bool PupTesterWindow::packFound() const
{
    const QString folder = m_folderEdit->text().trimmed();
    return !folder.isEmpty() && !game().isEmpty() &&
           QDir(QDir(folder).filePath(QStringLiteral("pupvideos"))).exists(game());
}

bool PupTesterWindow::start()
{
    std::string error;
    if (!m_host->start(QDir::fromNativeSeparators(m_folderEdit->text().trimmed()).toStdString(),
                       game().toStdString(), &error)) {
        QMessageBox::warning(this, tr("PuP Tester"),
                             tr("PUP does not start: %1").arg(QString::fromStdString(error)));
        updateState();
        return false;
    }
    m_sentList->clear();
    m_timer->start();
    updateState();
    return true;
}

void PupTesterWindow::stop()
{
    m_timer->stop();
    m_host->stop();
    for (PupScreenView* view : {m_topper, m_backglass, m_dmd}) {
        view->clear();
    }
    updateState();
}

void PupTesterWindow::sendTrigger(uint32_t id)
{
    if (!isRunning()) {
        return;
    }
    m_host->trigger(id);
    m_sentList->insertItem(0, tr("%1  D%2").arg(QTime::currentTime().toString(
                                                    QStringLiteral("HH:mm:ss.zzz")))
                                  .arg(id));
    while (m_sentList->count() > 200) {
        delete m_sentList->takeItem(m_sentList->count() - 1);
    }
}

void PupTesterWindow::closeEvent(QCloseEvent* event)
{
    stop();
    QWidget::closeEvent(event);
}

void PupTesterWindow::updateState()
{
    const bool running = isRunning();
    const bool available = vpx::PupHost::isAvailable();
    m_startButton->setText(running ? tr("Stop") : tr("Start"));
    m_startButton->setEnabled(running || (available && !game().isEmpty()));
    m_folderEdit->setEnabled(!running);
    m_gameEdit->setEnabled(!running);
    m_sendButton->setEnabled(running);
    if (!available) {
        m_packLabel->setText(tr("This build has no PUP plugin."));
    } else if (game().isEmpty()) {
        m_packLabel->setText(tr("Open a project, or name the game."));
    } else if (packFound()) {
        m_packLabel->setText(running ? tr("Playing the pack %1. PUP's messages are in Help -> "
                                          "Plugin Log.")
                                           .arg(game())
                                     : tr("The pack %1 is there.").arg(game()));
    } else {
        m_packLabel->setText(tr("No pack %1 in %2: PUP shows nothing.")
                                 .arg(game(),
                                      QDir::toNativeSeparators(
                                          QDir(m_folderEdit->text()).filePath(QStringLiteral("pupvideos")))));
    }
}

void PupTesterWindow::renderScreens()
{
    m_host->process();
    for (PupScreenView* view : {m_topper, m_backglass, m_dmd}) {
        view->refresh(*m_host);
    }
}

}  // namespace colorizing::app
