#include "SpeakerLightView.h"

#include <QPainter>
#include <QRadialGradient>
#include <QSettings>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <numbers>

#include "runtime/SpeakerLightPlayer.h"

namespace colorizing::app {

namespace {

const QString kVisibleKey = QStringLiteral("speakerLights/visible");
const QString kShapeKey = QStringLiteral("speakerLights/shape");
const QString kLedsKey = QStringLiteral("speakerLights/leds");
const QString kLeftKey = QStringLiteral("speakerLights/leftChannel");
const QString kRightKey = QStringLiteral("speakerLights/rightChannel");

constexpr int kFrameMs = 33;

// Where the LEDs sit: evenly on the ellipse or the rectangle's border inside `area`, from the
// top centre clockwise, as a ring of a strip.
std::vector<QPointF> ledPositions(SpeakerShape shape, const QRectF& area, std::size_t count)
{
    std::vector<QPointF> points;
    points.reserve(count);
    if (count == 0) {
        return points;
    }
    if (shape == SpeakerShape::Circle) {
        for (std::size_t i = 0; i < count; ++i) {
            const double angle = 2.0 * std::numbers::pi * static_cast<double>(i) / static_cast<double>(count);
            points.emplace_back(area.center().x() + area.width() / 2.0 * std::sin(angle),
                                area.center().y() - area.height() / 2.0 * std::cos(angle));
        }
        return points;
    }
    const double perimeter = 2.0 * (area.width() + area.height());
    for (std::size_t i = 0; i < count; ++i) {
        // From the top centre clockwise along the border.
        double d = std::fmod(area.width() / 2.0 +
                                 perimeter * static_cast<double>(i) / static_cast<double>(count),
                             perimeter);
        if (d < area.width()) {
            points.emplace_back(area.left() + d, area.top());
            continue;
        }
        d -= area.width();
        if (d < area.height()) {
            points.emplace_back(area.right(), area.top() + d);
            continue;
        }
        d -= area.height();
        if (d < area.width()) {
            points.emplace_back(area.right() - d, area.bottom());
            continue;
        }
        d -= area.width();
        points.emplace_back(area.left(), area.bottom() - d);
    }
    return points;
}

}  // namespace

SpeakerLightPreviewOptions loadSpeakerLightPreviewOptions()
{
    const QSettings settings;
    SpeakerLightPreviewOptions options;
    options.visible = settings.value(kVisibleKey, false).toBool();
    options.shape = settings.value(kShapeKey).toString() == QStringLiteral("rectangle")
                        ? SpeakerShape::Rectangle
                        : SpeakerShape::Circle;
    options.ledCount = std::clamp(settings.value(kLedsKey, 16).toInt(), 1, 256);
    options.leftChannel = std::clamp(settings.value(kLeftKey, 0).toInt(), 0, 3);
    options.rightChannel = std::clamp(settings.value(kRightKey, 1).toInt(), 0, 3);
    return options;
}

void saveSpeakerLightPreviewOptions(const SpeakerLightPreviewOptions& options)
{
    QSettings settings;
    settings.setValue(kVisibleKey, options.visible);
    settings.setValue(kShapeKey, options.shape == SpeakerShape::Rectangle
                                     ? QStringLiteral("rectangle")
                                     : QStringLiteral("circle"));
    settings.setValue(kLedsKey, options.ledCount);
    settings.setValue(kLeftKey, options.leftChannel);
    settings.setValue(kRightKey, options.rightChannel);
}

SpeakerLightView::SpeakerLightView(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_OpaquePaintEvent);
}

void SpeakerLightView::setShape(SpeakerShape shape)
{
    if (shape != m_shape) {
        m_shape = shape;
        updateGeometry();
        update();
    }
}

void SpeakerLightView::setColors(const std::vector<uint32_t>& colors)
{
    if (colors != m_colors) {
        m_colors = colors;
        update();
    }
}

QSize SpeakerLightView::sizeHint() const
{
    return m_shape == SpeakerShape::Circle ? QSize(96, 96) : QSize(64, 128);
}

QSize SpeakerLightView::minimumSizeHint() const
{
    return QSize(32, 32);
}

void SpeakerLightView::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), Qt::black);
    // The speaker: square for a circle, two by one standing for a rectangle, centred.
    QRectF area = rect();
    if (m_shape == SpeakerShape::Circle) {
        const double side = std::min(area.width(), area.height());
        area = QRectF(area.center().x() - side / 2, area.center().y() - side / 2, side, side);
    } else {
        const double width = std::min(area.width(), area.height() / 2.0);
        area = QRectF(area.center().x() - width / 2, area.center().y() - width, width, 2 * width);
    }
    const std::size_t count = std::max<std::size_t>(m_colors.size(), 1);
    const double perimeter = m_shape == SpeakerShape::Circle
                                 ? std::numbers::pi * area.width()
                                 : 2.0 * (area.width() + area.height());
    const double led = std::clamp(perimeter / static_cast<double>(count) * 0.6, 2.0, 14.0);
    area.adjust(led, led, -led, -led);
    // The grille.
    painter.setPen(QPen(QColor(40, 40, 40), 1));
    painter.setBrush(QColor(18, 18, 18));
    if (m_shape == SpeakerShape::Circle) {
        painter.drawEllipse(area);
    } else {
        painter.drawRoundedRect(area, 4, 4);
    }
    const std::vector<QPointF> points = ledPositions(m_shape, area, m_colors.size());
    painter.setPen(Qt::NoPen);
    for (std::size_t i = 0; i < points.size(); ++i) {
        const uint32_t value = m_colors[i];
        const QColor color((value >> 16) & 0xff, (value >> 8) & 0xff, value & 0xff);
        // A glow around the LED, then the LED.
        QRadialGradient glow(points[i], led * 1.6);
        QColor halo = color;
        halo.setAlpha(110);
        glow.setColorAt(0.0, halo);
        glow.setColorAt(1.0, QColor(0, 0, 0, 0));
        painter.setBrush(glow);
        painter.drawEllipse(points[i], led * 1.6, led * 1.6);
        painter.setBrush(value == 0 ? QColor(35, 35, 35) : color);
        painter.drawEllipse(points[i], led / 2, led / 2);
    }
}

SpeakerLightPair::SpeakerLightPair(QWidget* viewParent)
    : QObject(viewParent)
{
    for (std::size_t speaker = 0; speaker < 2; ++speaker) {
        m_views[speaker] = new SpeakerLightView(viewParent);
        m_views[speaker]->setToolTip(
            QCoreApplication::translate("SpeakerLightPair",
                                        "Speaker light preview: channel %1 (Settings)")
                .arg(channelOf(speaker) + 1));
        m_players[speaker] = std::make_unique<runtime::SpeakerLightPlayer>(16);
    }
    m_clock.start();
    m_timer = new QTimer(this);
    m_timer->setInterval(kFrameMs);
    connect(m_timer, &QTimer::timeout, this, [this]() { tick(); });
    setOptions(m_options);
}

SpeakerLightPair::~SpeakerLightPair() = default;

uint32_t SpeakerLightPair::now() const
{
    return static_cast<uint32_t>(m_clock.elapsed()) + m_offsetMs;
}

int SpeakerLightPair::channelOf(std::size_t speaker) const
{
    return speaker == 0 ? m_options.leftChannel : m_options.rightChannel;
}

void SpeakerLightPair::setOptions(const SpeakerLightPreviewOptions& options)
{
    const bool channelsChanged = options.leftChannel != m_options.leftChannel ||
                                 options.rightChannel != m_options.rightChannel;
    m_options = options;
    for (std::size_t speaker = 0; speaker < 2; ++speaker) {
        m_views[speaker]->setShape(options.shape);
        m_views[speaker]->setVisible(options.visible);
        m_views[speaker]->setToolTip(
            QCoreApplication::translate("SpeakerLightPair",
                                        "Speaker light preview: channel %1 (Settings)")
                .arg(channelOf(speaker) + 1));
        m_players[speaker]->setLedCount(static_cast<std::size_t>(options.ledCount));
        if (channelsChanged) {
            m_hasShown[speaker] = false;
        }
    }
    if (options.visible) {
        m_timer->start();
    } else {
        m_timer->stop();
    }
    tick();
}

void SpeakerLightPair::startSpeaker(std::size_t speaker,
                                    const std::optional<document::LightEffect>& effect)
{
    m_shown[speaker] = effect;
    m_hasShown[speaker] = true;
    if (effect) {
        m_players[speaker]->play(*effect, now());
    } else {
        m_players[speaker]->playDefault(now());
    }
}

void SpeakerLightPair::showFrame(const document::ProjectDocument* document,
                                 std::optional<std::size_t> frame)
{
    if (!document || !frame || *frame >= document->frameCount()) {
        for (std::size_t speaker = 0; speaker < 2; ++speaker) {
            m_players[speaker]->setDefault(std::nullopt);
            if (!m_hasShown[speaker] || m_shown[speaker]) {
                startSpeaker(speaker, std::nullopt);
            }
        }
        tick();
        return;
    }
    const document::SpeakerLightSettings& settings = document->speakerLights();
    const document::LightActions shown = runtime::lightsShownAt(*document, *frame);
    for (std::size_t speaker = 0; speaker < 2; ++speaker) {
        const std::size_t channel = static_cast<std::size_t>(channelOf(speaker));
        document::LightAction fallback = settings.defaults[channel];
        if (fallback.kind == document::LightActionKind::Unchanged) {
            fallback.kind = document::LightActionKind::Off;
        }
        m_players[speaker]->setDefault(runtime::lightEffectForAction(settings, fallback));
        document::LightAction action = shown[channel];
        if (action.kind == document::LightActionKind::Unchanged) {
            action = fallback;
        }
        const std::optional<document::LightEffect> effect =
            runtime::lightEffectForAction(settings, action);
        // An effect that plays goes on, unless it ran out.
        if (m_hasShown[speaker] && m_shown[speaker] == effect &&
            m_players[speaker]->playing() == effect) {
            continue;
        }
        startSpeaker(speaker, effect);
    }
    tick();
}

void SpeakerLightPair::setDefaults(
    const std::array<std::optional<document::LightEffect>, document::kLightChannels>& defaults)
{
    for (std::size_t speaker = 0; speaker < 2; ++speaker) {
        m_players[speaker]->setDefault(defaults[static_cast<std::size_t>(channelOf(speaker))]);
        startSpeaker(speaker, defaults[static_cast<std::size_t>(channelOf(speaker))]);
    }
    tick();
}

void SpeakerLightPair::start(
    const std::array<std::optional<document::LightEffect>, document::kLightChannels>& effects)
{
    for (std::size_t speaker = 0; speaker < 2; ++speaker) {
        const std::optional<document::LightEffect>& effect =
            effects[static_cast<std::size_t>(channelOf(speaker))];
        if (effect) {
            startSpeaker(speaker, effect);
        }
    }
}

void SpeakerLightPair::advance(uint32_t ms)
{
    m_offsetMs += ms;
    tick();
}

void SpeakerLightPair::tick()
{
    if (!m_options.visible) {
        return;
    }
    for (std::size_t speaker = 0; speaker < 2; ++speaker) {
        m_players[speaker]->update(now());
        m_views[speaker]->setColors(m_players[speaker]->colors());
    }
}

}  // namespace colorizing::app
