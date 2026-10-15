#pragma once

#include <QCoreApplication>
#include <QImage>
#include <QSize>
#include <QString>

#include <memory>

namespace colorizing::app {

// A movie clip, read with ffmpeg: the libraries VPX's PUP plugin ships in its folder of the
// plugin folder, loaded when the first clip is opened. Without them (a build without the media
// libraries, or no PUP plugin) isAvailable() says why and nothing opens.
class VideoClip final {
    Q_DECLARE_TR_FUNCTIONS(VideoClip)

public:
    VideoClip();
    ~VideoClip();
    VideoClip(const VideoClip&) = delete;
    VideoClip& operator=(const VideoClip&) = delete;

    // Whether ffmpeg can be loaded; why not otherwise.
    [[nodiscard]] static bool isAvailable(QString* why = nullptr);

    bool open(const QString& path, QString* error);
    void close();
    [[nodiscard]] bool isOpen() const;

    [[nodiscard]] QSize size() const;
    [[nodiscard]] double durationMs() const;
    // Frames per second, as the clip says; 0 when unknown.
    [[nodiscard]] double frameRate() const;

    // The picture shown at the time: the last frame that starts at or before it. Null when
    // there is none (past the end, or a decoding error). Reading forward from the last picture
    // decodes on; anything else seeks first.
    [[nodiscard]] QImage frameAt(double ms);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}  // namespace colorizing::app
