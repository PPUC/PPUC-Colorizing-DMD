#include <QApplication>

#include "MainWindow.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("ColorizingDMD"));
    QApplication::setApplicationName(QStringLiteral("ColorizingDMDQtV2"));
    colorizing::app::MainWindow window;
    window.show();
    window.offerUntitledAutosave();
    return app.exec();
}
