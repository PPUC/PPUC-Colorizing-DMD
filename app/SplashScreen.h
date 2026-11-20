#pragma once

#include <memory>

#include <QPixmap>
#include <QSize>

class QSplashScreen;

namespace colorizing::app {

// The picture shown while the editor starts, built in as :/splash/splash.png.
inline constexpr int kSplashScreenMs = 4000;
// Whether the editor shows it at all; off for now, the picture and the code stay.
inline constexpr bool kSplashScreenShown = false;

// The picture scaled down to fit into the available screen area (at most 80% of its height and
// width), its proportions kept; a null pixmap when the picture is missing.
[[nodiscard]] QPixmap splashPixmap(QSize available);

// Shows the picture on the primary screen, above the windows; a click closes it early. Nothing
// without a screen or a picture.
[[nodiscard]] std::unique_ptr<QSplashScreen> showSplashScreen();

}  // namespace colorizing::app
