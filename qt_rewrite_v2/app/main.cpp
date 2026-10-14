#include <QApplication>
#include <QStringList>

#include <cstdio>
#include <string>

#include "MainWindow.h"
#include "vpx/VpxHost.h"

namespace {

// --check-plugins, for the packaging scripts: loads and unloads every VPX plugin the package
// has, from the folder the app uses, and fails when there is none or one does not load.
int checkPlugins()
{
    colorizing::vpx::Host& host = colorizing::vpx::Host::instance();
    const std::vector<std::string> ids = host.pluginIds();
    std::printf("VPX plugins in %s\n", colorizing::vpx::Host::pluginDirectory().c_str());
    bool ok = !ids.empty();
    for (const std::string& id : ids) {
        std::string error;
        const bool loaded = host.load(id, {}, &error);
        std::printf("  %s: %s\n", id.c_str(), loaded ? "loads" : error.c_str());
        host.unload(id);
        ok = ok && loaded;
    }
    std::fflush(stdout);
    return ok ? 0 : 1;
}

}  // namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("ColorizingDMD"));
    QApplication::setApplicationName(QStringLiteral("ColorizingDMDQtV2"));
    if (QApplication::arguments().contains(QStringLiteral("--check-plugins"))) {
        return checkPlugins();
    }
    colorizing::app::MainWindow window;
    window.show();
    window.offerUntitledAutosave();
    return app.exec();
}
