#include <QApplication>

#include "MainWindow.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    colorizing::app::MainWindow window;
    window.show();
    return app.exec();
}

