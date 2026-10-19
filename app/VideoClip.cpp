#include "VideoClip.h"

#include <QDir>
#include <QLibrary>

#include "vpx/VpxHost.h"

#ifdef COLORIZINGDMD_HAS_VIDEO_IMPORT

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include <cmath>
#include <type_traits>

namespace colorizing::app {

namespace {

// ffmpeg's functions the clip uses, from the PUP plugin's libraries.
struct LibAv {
    decltype(&avformat_open_input) openInput = nullptr;
    decltype(&avformat_find_stream_info) findStreamInfo = nullptr;
    decltype(&av_find_best_stream) findBestStream = nullptr;
    decltype(&avformat_close_input) closeInput = nullptr;
    decltype(&av_read_frame) readFrame = nullptr;
    decltype(&av_seek_frame) seekFrame = nullptr;
    decltype(&avcodec_alloc_context3) allocContext = nullptr;
    decltype(&avcodec_parameters_to_context) parametersToContext = nullptr;
    decltype(&avcodec_open2) openCodec = nullptr;
    decltype(&avcodec_send_packet) sendPacket = nullptr;
    decltype(&avcodec_receive_frame) receiveFrame = nullptr;
    decltype(&avcodec_flush_buffers) flushBuffers = nullptr;
    decltype(&avcodec_free_context) freeContext = nullptr;
    decltype(&av_packet_alloc) packetAlloc = nullptr;
    decltype(&av_packet_free) packetFree = nullptr;
    decltype(&av_packet_unref) packetUnref = nullptr;
    decltype(&av_frame_alloc) frameAlloc = nullptr;
    decltype(&av_frame_free) frameFree = nullptr;
    decltype(&sws_getCachedContext) getScaler = nullptr;
    decltype(&sws_scale) scale = nullptr;
    decltype(&sws_freeContext) freeScaler = nullptr;
    QString error;

    [[nodiscard]] bool loaded() const { return error.isEmpty(); }
};

// The file of an ffmpeg library in the PUP plugin's folder, as the media libraries name it.
QString libraryFile(const QString& folder, const char* name, int major)
{
#if defined(Q_OS_WIN)
    return QDir(folder).filePath(QStringLiteral("%1%2-%3.dll")
                                     .arg(QLatin1String(name))
                                     .arg(sizeof(void*) == 8 ? QStringLiteral("64") : QString())
                                     .arg(major));
#elif defined(Q_OS_MACOS)
    return QDir(folder).filePath(
        QStringLiteral("lib%1.%2.dylib").arg(QLatin1String(name)).arg(major));
#else
    return QDir(folder).filePath(
        QStringLiteral("lib%1.so.%2").arg(QLatin1String(name)).arg(major));
#endif
}

const LibAv& libAv()
{
    static const LibAv instance = []() {
        LibAv av;
        const QString folder =
            QDir(QString::fromStdString(vpx::Host::pluginDirectory())).filePath(QStringLiteral("pup"));
        // In the order they need each other: each finds the ones it links among those loaded
        // already (their search paths are the PUP plugin's, not the editor's). Kept loaded for
        // the rest of the run.
        const auto load = [&](const char* name, int major) -> QLibrary* {
            auto* library = new QLibrary(libraryFile(folder, name, major));
            if (!library->load() && av.error.isEmpty()) {
                av.error = VideoClip::tr("ffmpeg's %1 does not load from %2: %3")
                               .arg(QLatin1String(name), folder, library->errorString());
            }
            return library;
        };
        QLibrary* avutil = load("avutil", LIBAVUTIL_VERSION_MAJOR);
        load("swresample", LIBSWRESAMPLE_VERSION_MAJOR);
        QLibrary* swscale = load("swscale", LIBSWSCALE_VERSION_MAJOR);
        QLibrary* avcodec = load("avcodec", LIBAVCODEC_VERSION_MAJOR);
        QLibrary* avformat = load("avformat", LIBAVFORMAT_VERSION_MAJOR);
        if (!av.error.isEmpty()) {
            return av;
        }
        const auto resolve = [&](auto& function, QLibrary* library, const char* name) {
            function = reinterpret_cast<std::remove_reference_t<decltype(function)>>(
                library->resolve(name));
            if (!function && av.error.isEmpty()) {
                av.error = VideoClip::tr("ffmpeg has no %1").arg(QLatin1String(name));
            }
        };
        resolve(av.openInput, avformat, "avformat_open_input");
        resolve(av.findStreamInfo, avformat, "avformat_find_stream_info");
        resolve(av.findBestStream, avformat, "av_find_best_stream");
        resolve(av.closeInput, avformat, "avformat_close_input");
        resolve(av.readFrame, avformat, "av_read_frame");
        resolve(av.seekFrame, avformat, "av_seek_frame");
        resolve(av.allocContext, avcodec, "avcodec_alloc_context3");
        resolve(av.parametersToContext, avcodec, "avcodec_parameters_to_context");
        resolve(av.openCodec, avcodec, "avcodec_open2");
        resolve(av.sendPacket, avcodec, "avcodec_send_packet");
        resolve(av.receiveFrame, avcodec, "avcodec_receive_frame");
        resolve(av.flushBuffers, avcodec, "avcodec_flush_buffers");
        resolve(av.freeContext, avcodec, "avcodec_free_context");
        resolve(av.packetAlloc, avcodec, "av_packet_alloc");
        resolve(av.packetFree, avcodec, "av_packet_free");
        resolve(av.packetUnref, avcodec, "av_packet_unref");
        resolve(av.frameAlloc, avutil, "av_frame_alloc");
        resolve(av.frameFree, avutil, "av_frame_free");
        resolve(av.getScaler, swscale, "sws_getCachedContext");
        resolve(av.scale, swscale, "sws_scale");
        resolve(av.freeScaler, swscale, "sws_freeContext");
        return av;
    }();
    return instance;
}

}  // namespace

struct VideoClip::Impl {
    AVFormatContext* format = nullptr;
    AVCodecContext* codec = nullptr;
    AVPacket* packet = nullptr;
    AVFrame* frame = nullptr;
    SwsContext* scaler = nullptr;
    int stream = -1;
    AVRational timeBase{1, 1000};
    double startMs = 0.0;
    double durationMs = 0.0;
    double frameRate = 0.0;
    // The picture decoded last and the time it starts; -1 when there is none.
    QImage picture;
    double pictureMs = -1.0;
    // The frame after it, read already while looking for the right one.
    QImage pending;
    double pendingMs = -1.0;

    [[nodiscard]] double msOf(int64_t pts) const
    {
        return static_cast<double>(pts) * 1000.0 * timeBase.num / timeBase.den - startMs;
    }

    QImage toImage(const AVFrame* source)
    {
        const LibAv& av = libAv();
        scaler = av.getScaler(scaler, source->width, source->height,
                              static_cast<AVPixelFormat>(source->format), source->width,
                              source->height, AV_PIX_FMT_BGRA, SWS_BILINEAR, nullptr, nullptr,
                              nullptr);
        if (!scaler) {
            return {};
        }
        // BGRA in memory is QImage's 32-bit RGB on every platform Qt runs on (little-endian).
        QImage image(source->width, source->height, QImage::Format_RGB32);
        uint8_t* destination[4] = {image.bits(), nullptr, nullptr, nullptr};
        const int strides[4] = {static_cast<int>(image.bytesPerLine()), 0, 0, 0};
        av.scale(scaler, source->data, source->linesize, 0, source->height, destination, strides);
        return image;
    }

    // The next decoded frame: its picture and start; false at the end or on an error.
    bool decodeNext(QImage& image, double& ms)
    {
        const LibAv& av = libAv();
        for (;;) {
            const int received = av.receiveFrame(codec, frame);
            if (received == 0) {
                const int64_t pts = frame->best_effort_timestamp != AV_NOPTS_VALUE
                                        ? frame->best_effort_timestamp
                                        : frame->pts;
                ms = pts == AV_NOPTS_VALUE ? pictureMs + 1.0 : msOf(pts);
                image = toImage(frame);
                return !image.isNull();
            }
            if (received != AVERROR(EAGAIN)) {
                return false;
            }
            // The decoder needs more: the next packet of the stream, or the end.
            bool sent = false;
            while (!sent) {
                if (av.readFrame(format, packet) < 0) {
                    av.sendPacket(codec, nullptr);
                    sent = true;
                    break;
                }
                if (packet->stream_index == stream) {
                    av.sendPacket(codec, packet);
                    sent = true;
                }
                av.packetUnref(packet);
            }
        }
    }

    void seek(double ms)
    {
        const LibAv& av = libAv();
        const int64_t target = static_cast<int64_t>(
            std::floor((ms + startMs) / 1000.0 * timeBase.den / timeBase.num));
        av.seekFrame(format, stream, target, AVSEEK_FLAG_BACKWARD);
        av.flushBuffers(codec);
        picture = QImage();
        pictureMs = -1.0;
        pending = QImage();
        pendingMs = -1.0;
    }
};

VideoClip::VideoClip()
    : m_impl(std::make_unique<Impl>())
{
}

VideoClip::~VideoClip()
{
    close();
}

bool VideoClip::isAvailable(QString* why)
{
    const LibAv& av = libAv();
    if (!av.loaded() && why) {
        *why = av.error;
    }
    return av.loaded();
}

bool VideoClip::open(const QString& path, QString* error)
{
    close();
    const LibAv& av = libAv();
    const auto fail = [&](const QString& message) {
        if (error) {
            *error = message;
        }
        close();
        return false;
    };
    if (!av.loaded()) {
        return fail(av.error);
    }
    Impl& impl = *m_impl;
    if (av.openInput(&impl.format, path.toUtf8().constData(), nullptr, nullptr) < 0) {
        impl.format = nullptr;
        return fail(tr("%1 is no movie ffmpeg can read").arg(QDir::toNativeSeparators(path)));
    }
    av.findStreamInfo(impl.format, nullptr);
    const AVCodec* decoder = nullptr;
    impl.stream = av.findBestStream(impl.format, AVMEDIA_TYPE_VIDEO, -1, -1, &decoder, 0);
    if (impl.stream < 0 || !decoder) {
        return fail(tr("%1 has no video ffmpeg can decode").arg(QDir::toNativeSeparators(path)));
    }
    const AVStream* stream = impl.format->streams[impl.stream];
    impl.codec = av.allocContext(decoder);
    if (!impl.codec || av.parametersToContext(impl.codec, stream->codecpar) < 0 ||
        av.openCodec(impl.codec, decoder, nullptr) < 0) {
        return fail(tr("The video of %1 does not decode").arg(QDir::toNativeSeparators(path)));
    }
    impl.packet = av.packetAlloc();
    impl.frame = av.frameAlloc();
    impl.timeBase = stream->time_base;
    impl.startMs = stream->start_time != AV_NOPTS_VALUE
                       ? static_cast<double>(stream->start_time) * 1000.0 * impl.timeBase.num /
                             impl.timeBase.den
                       : 0.0;
    if (stream->duration != AV_NOPTS_VALUE && stream->duration > 0) {
        impl.durationMs = static_cast<double>(stream->duration) * 1000.0 * impl.timeBase.num /
                          impl.timeBase.den;
    } else if (impl.format->duration > 0) {
        impl.durationMs = static_cast<double>(impl.format->duration) / 1000.0;
    }
    const AVRational rate = stream->avg_frame_rate.num ? stream->avg_frame_rate
                                                       : stream->r_frame_rate;
    impl.frameRate = rate.den ? static_cast<double>(rate.num) / rate.den : 0.0;
    return true;
}

void VideoClip::close()
{
    Impl& impl = *m_impl;
    const LibAv& av = libAv();
    if (impl.scaler) {
        av.freeScaler(impl.scaler);
    }
    if (impl.frame) {
        av.frameFree(&impl.frame);
    }
    if (impl.packet) {
        av.packetFree(&impl.packet);
    }
    if (impl.codec) {
        av.freeContext(&impl.codec);
    }
    if (impl.format) {
        av.closeInput(&impl.format);
    }
    m_impl = std::make_unique<Impl>();
}

bool VideoClip::isOpen() const
{
    return m_impl->codec != nullptr;
}

QSize VideoClip::size() const
{
    return isOpen() ? QSize(m_impl->codec->width, m_impl->codec->height) : QSize();
}

double VideoClip::durationMs() const
{
    return m_impl->durationMs;
}

double VideoClip::frameRate() const
{
    return m_impl->frameRate;
}

QImage VideoClip::frameAt(double ms)
{
    if (!isOpen() || ms < 0.0) {
        return {};
    }
    Impl& impl = *m_impl;
    // Forward from the picture there is, within a few seconds: decode on. Else seek.
    if (impl.pictureMs < 0.0 || ms < impl.pictureMs || ms > impl.pictureMs + 3000.0) {
        impl.seek(ms);
    }
    for (;;) {
        if (impl.pendingMs < 0.0) {
            if (!impl.decodeNext(impl.pending, impl.pendingMs)) {
                // The end: the last picture stays until the clip ends.
                impl.pendingMs = -1.0;
                return ms <= impl.durationMs ? impl.picture : QImage();
            }
        }
        if (impl.pendingMs > ms && !impl.picture.isNull()) {
            return impl.picture;
        }
        impl.picture = impl.pending;
        impl.pictureMs = impl.pendingMs;
        impl.pendingMs = -1.0;
        if (impl.pictureMs >= ms) {
            return impl.picture;
        }
    }
}

}  // namespace colorizing::app

#else  // no media libraries in this build

namespace colorizing::app {

struct VideoClip::Impl {
};

VideoClip::VideoClip()
    : m_impl(std::make_unique<Impl>())
{
}

VideoClip::~VideoClip() = default;

bool VideoClip::isAvailable(QString* why)
{
    if (why) {
        *why = tr("This build has no media libraries (ffmpeg).");
    }
    return false;
}

bool VideoClip::open(const QString&, QString* error)
{
    (void)isAvailable(error);
    return false;
}

void VideoClip::close() {}

bool VideoClip::isOpen() const
{
    return false;
}

QSize VideoClip::size() const
{
    return {};
}

double VideoClip::durationMs() const
{
    return 0.0;
}

double VideoClip::frameRate() const
{
    return 0.0;
}

QImage VideoClip::frameAt(double)
{
    return {};
}

}  // namespace colorizing::app

#endif
