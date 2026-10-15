// The PuP tester's host (vpx::PupHost) with VPX's PUP plugin from the build: a PuP pack of one
// trigger, D5, that shows a red picture on the backglass. The editor announces the game, sends
// trigger 5 as a machine's colorization does, and draws the backglass.

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QLineEdit>
#include <QListWidget>
#include <QTemporaryDir>

#include "PupTesterWindow.h"
#include "vpx/PupHost.h"

namespace {

using namespace colorizing;

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "FAIL: PuP host: " << message << '\n';
        std::exit(1);
    }
}

void writeText(const QString& path, const std::string& text)
{
    std::ofstream(path.toStdString()) << text;
}

// Runs the bus and draws the backglass until its middle has the color, or 5 s have passed.
bool backglassTurns(vpx::PupHost& host, const QColor& color)
{
    QImage image(160, 90, QImage::Format_ARGB32);
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 5000) {
        host.process();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        if (host.render(vpx::PupHost::Screen::Backglass, image)) {
            const QColor middle = image.pixelColor(80, 45);
            if (std::abs(middle.red() - color.red()) < 20 &&
                std::abs(middle.green() - color.green()) < 20 &&
                std::abs(middle.blue() - color.blue()) < 20) {
                return true;
            }
        }
    }
    return false;
}

}  // namespace

void runPupHostWidgetTests()
{
    if (!vpx::PupHost::isAvailable()) {
        std::cerr << "PuP host: skipped, no PUP plugin in this build\n";
        return;
    }
    QTemporaryDir folder;
    const QDir pack(folder.filePath(QStringLiteral("pupvideos/testgame")));
    QDir().mkpath(pack.filePath(QStringLiteral("red")));
    QImage red(64, 36, QImage::Format_RGB32);
    red.fill(Qt::red);
    expect(red.save(pack.filePath(QStringLiteral("red/red.png"))), "the pack's picture is written");
    writeText(pack.filePath(QStringLiteral("screens.pup")),
              "ScreenNum,ScreenDes,PlayList,PlayFile,Loopit,Active,Priority,CustomPos\n"
              "2,\"Backglass\",\"\",,0,Show,100,\n");
    writeText(pack.filePath(QStringLiteral("playlists.pup")),
              "ScreenNum,Folder,Des,AlphaSort,RestSeconds,Volume,Priority\n"
              "2,red,red,0,0,100,1\n");
    writeText(pack.filePath(QStringLiteral("triggers.pup")),
              "ID,Active,Descript,Trigger,ScreenNum,PlayList,PlayFile,Volume,Priority,Length,"
              "Counter,RestSeconds,Loop,Defaults\n"
              "1,1,red,D5,2,red,red.png,100,1,0,0,0,Loop,\n");

    vpx::PupHost host;
    std::string error;
    expect(host.start(folder.path().toStdString(), "testgame", &error) && host.isRunning(),
           "PUP loads for the game: " + error);
    expect(!backglassTurns(host, Qt::red), "nothing red before the trigger");
    host.trigger(5);
    expect(backglassTurns(host, Qt::red), "trigger 5 shows the pack's red picture on the backglass");
    host.stop();
    expect(!host.isRunning(), "stopped");

    // The PuP Tester window on the same pack.
    app::PupTesterWindow window;
    QLineEdit* folderEdit = nullptr;
    QLineEdit* gameEdit = nullptr;
    for (QLineEdit* edit : window.findChildren<QLineEdit*>()) {
        if (edit->toolTip().startsWith(QStringLiteral("PUP looks"))) {
            folderEdit = edit;
        } else if (edit->toolTip().startsWith(QStringLiteral("The pack's folder"))) {
            gameEdit = edit;
        }
    }
    expect(folderEdit && gameEdit, "the window asks for the PUP folder and the game");
    folderEdit->setText(folder.path());
    gameEdit->setText(QStringLiteral("testgame"));
    expect(window.packFound() && window.start() && window.isRunning(),
           "the window finds the pack and starts PUP");
    auto* sent = window.findChild<QListWidget*>();
    window.frameShown(std::nullopt);
    expect(sent && sent->count() == 0, "a frame without a trigger sends nothing");
    window.frameShown(5u);
    expect(sent->count() == 1 && sent->item(0)->text().endsWith(QStringLiteral("D5")),
           "a shown frame's trigger goes to PUP");
    window.stop();
    expect(!window.isRunning(), "and the window stops it");
    folderEdit->setText(QString());
}
