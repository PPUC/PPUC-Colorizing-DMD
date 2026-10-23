#include "panels/ProjectSettingsPanel.h"

#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QStringList>

#include <algorithm>

#include "document/ProjectDocument.h"
#include "panels/FrameLightsPanel.h"
#include "panels/InspectorForm.h"
#include "panels/SpritePanel.h"

namespace colorizing::app {

ProjectSettingsPanel::ProjectSettingsPanel(QWidget* parent)
    : QWidget(parent)
{
    m_targetComboBox = new QComboBox(this);
    m_targetComboBox->addItem(tr("libserum 2.8"),
                              static_cast<int>(document::LibserumTarget::Libserum28));
    m_targetComboBox->addItem(tr("libserum 2.9"),
                              static_cast<int>(document::LibserumTarget::Libserum29));
    m_targetComboBox->setToolTip(
        tr("The libserum the colorization is made for. libserum 2.8 allows 32 sprites per "
           "frame and 255 sprites and backgrounds; libserum 2.9 allows 128 sprites per frame "
           "and needs libserum 2.9 or later to play."));
    m_targetLabel = new QLabel(this);
    m_targetLabel->setWordWrap(true);
    m_scalingComboBox = new QComboBox(this);
    m_scalingComboBox->addItem(tr("Scale2x"), document::kScalingScale2x);
    m_scalingComboBox->addItem(tr("Line doubling"), document::kScalingLineDoubling);
    m_scalingComboBox->setToolTip(
        tr("How libserum makes the 256x64 picture of a frame that has none"));
    m_shadowComboBox = new QComboBox(this);
    m_shadowComboBox->addItem(tr("1 pixel (native)"), document::kShadowOffsetNative);
    m_shadowComboBox->addItem(tr("2 pixels (proportional)"),
                              document::kShadowOffsetProportional);
    m_shadowComboBox->setToolTip(tr("How far a dynamic shadow falls on the 256x64 plane"));
    m_licenseComboBox = new QComboBox(this);
    m_licenseComboBox->addItem(tr("Not chosen"),
                               static_cast<int>(document::LicenseKind::Unspecified));
    m_licenseComboBox->addItem(tr("All rights reserved"),
                               static_cast<int>(document::LicenseKind::AllRightsReserved));
    for (const document::LicenseKind kind :
         {document::LicenseKind::Cc0, document::LicenseKind::CcBy, document::LicenseKind::CcBySa,
          document::LicenseKind::CcByNc, document::LicenseKind::CcByNcSa}) {
        m_licenseComboBox->addItem(QString::fromStdString(document::licenseShortName(kind)),
                                   static_cast<int>(kind));
    }
    m_licenseComboBox->addItem(tr("Custom"), static_cast<int>(document::LicenseKind::Custom));
    m_licenseComboBox->setToolTip(
        tr("What others may do with the colorization: share it, change it, use it "
           "commercially. Saved with the project."));
    m_licenseLabel = new QLabel(this);
    m_licenseLabel->setWordWrap(true);
    m_licenseLabel->setOpenExternalLinks(true);
    m_licenseLabel->setTextFormat(Qt::RichText);
    m_licenseTextEdit = new QPlainTextEdit(this);
    m_licenseTextEdit->setPlaceholderText(tr("The terms of the license"));
    m_licenseTextEdit->setTabChangesFocus(true);
    m_licenseTextEdit->setFixedHeight(m_licenseTextEdit->fontMetrics().lineSpacing() * 5);
    m_attributionEdit = new QLineEdit(this);
    m_attributionEdit->setPlaceholderText(tr("For example the authors' names"));
    m_attributionEdit->setToolTip(tr("Who is to be named when the colorization is used"));
    m_licenseUrlEdit = new QLineEdit(this);
    m_licenseUrlEdit->setPlaceholderText(QStringLiteral("https://..."));
    m_licenseUrlEdit->setToolTip(tr("Where the colorization or its license can be found"));
    // The boxes can be narrower than their longest entry, see makeInspectorForm().
    for (QComboBox* box : {m_targetComboBox, m_scalingComboBox, m_shadowComboBox,
                           m_licenseComboBox}) {
        box->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        box->setMinimumContentsLength(8);
    }
    QFormLayout* layout = makeInspectorForm(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addRow(tr("Made for"), m_targetComboBox);
    layout->addRow(m_targetLabel);
    layout->addRow(tr("Upscaling"), m_scalingComboBox);
    layout->addRow(tr("Upscaled shadow offset"), m_shadowComboBox);
    for (std::size_t channel = 0; channel < m_lightDefaults.size(); ++channel) {
        m_lightDefaults[channel] = new LightActionEditor(true, this);
        m_lightDefaults[channel]->setToolTip(
            tr("What speaker light channel %1 shows while no frame has started anything, and "
               "after an effect with a duration")
                .arg(channel + 1));
        layout->addRow(tr("Speaker light %1").arg(channel + 1), m_lightDefaults[channel]);
    }
    layout->addRow(tr("License"), m_licenseComboBox);
    layout->addRow(m_licenseLabel);
    layout->addRow(m_licenseTextEdit);
    layout->addRow(tr("Attribution"), m_attributionEdit);
    layout->addRow(tr("Link"), m_licenseUrlEdit);

    const auto changed = [this](int) {
        if (!m_filling && onScalingChanged) {
            onScalingChanged(static_cast<uint8_t>(m_scalingComboBox->currentData().toInt()),
                             static_cast<uint8_t>(m_shadowComboBox->currentData().toInt()));
        }
    };
    connect(m_scalingComboBox,
            static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this,
            changed);
    connect(m_shadowComboBox,
            static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this,
            changed);
    connect(m_targetComboBox,
            static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this,
            [this](int) {
                updateTargetProblems();
                if (!m_filling && onTargetChanged) {
                    onTargetChanged(libserumTarget());
                }
            });
    connect(m_licenseComboBox,
            static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this,
            [this](int) { updateLicenseLabel(); });
    setProject(nullptr);
}

void ProjectSettingsPanel::setProject(const document::ProjectDocument* document)
{
    const document::ColorizationSettings settings =
        document ? document->colorizationSettings().value_or(document::ColorizationSettings{})
                 : document::ColorizationSettings{};
    m_document = document;
    m_filling = true;
    m_targetComboBox->setCurrentIndex(std::max(
        0,
        m_targetComboBox->findData(static_cast<int>(
            document ? document->libserumTarget() : document::LibserumTarget::Libserum28))));
    m_scalingComboBox->setCurrentIndex(
        std::max(0, m_scalingComboBox->findData(settings.scalingAlgorithm)));
    m_shadowComboBox->setCurrentIndex(
        std::max(0, m_shadowComboBox->findData(settings.shadowOffset)));
    m_filling = false;
    updateTargetProblems();
    for (std::size_t channel = 0; channel < m_lightDefaults.size(); ++channel) {
        m_lightDefaults[channel]->setEffects(document ? document->speakerLights()
                                                      : document::SpeakerLightSettings{});
        m_lightDefaults[channel]->setAction(document ? document->speakerLights().defaults[channel]
                                                     : document::LightAction{});
        m_lightDefaults[channel]->setEnabled(document != nullptr);
    }
    const document::ProjectLicense license =
        document ? document->license() : document::ProjectLicense{};
    m_filling = true;
    m_licenseComboBox->setCurrentIndex(
        std::max(0, m_licenseComboBox->findData(static_cast<int>(license.kind))));
    m_filling = false;
    m_licenseTextEdit->setPlainText(QString::fromStdString(license.customText));
    m_attributionEdit->setText(QString::fromStdString(license.attribution));
    m_licenseUrlEdit->setText(QString::fromStdString(license.url));
    updateLicenseLabel();
    for (QWidget* widget : std::initializer_list<QWidget*>{
             m_licenseComboBox, m_licenseTextEdit, m_attributionEdit, m_licenseUrlEdit}) {
        widget->setEnabled(document != nullptr);
    }
    m_targetComboBox->setEnabled(document != nullptr);
    m_scalingComboBox->setEnabled(document != nullptr);
    m_shadowComboBox->setEnabled(document != nullptr);
}

uint8_t ProjectSettingsPanel::scalingAlgorithm() const
{
    return static_cast<uint8_t>(m_scalingComboBox->currentData().toInt());
}

uint8_t ProjectSettingsPanel::shadowOffset() const
{
    return static_cast<uint8_t>(m_shadowComboBox->currentData().toInt());
}

document::LibserumTarget ProjectSettingsPanel::libserumTarget() const
{
    return static_cast<document::LibserumTarget>(m_targetComboBox->currentData().toInt());
}

document::LightActions ProjectSettingsPanel::lightDefaults() const
{
    document::LightActions defaults{};
    for (std::size_t channel = 0; channel < m_lightDefaults.size(); ++channel) {
        defaults[channel] = m_lightDefaults[channel]->action();
    }
    return defaults;
}

document::ProjectLicense ProjectSettingsPanel::license() const
{
    document::ProjectLicense license;
    license.kind = static_cast<document::LicenseKind>(m_licenseComboBox->currentData().toInt());
    license.customText = m_licenseTextEdit->toPlainText().toStdString();
    license.attribution = m_attributionEdit->text().trimmed().toStdString();
    license.url = m_licenseUrlEdit->text().trimmed().toStdString();
    return license;
}

void ProjectSettingsPanel::updateLicenseLabel()
{
    const auto kind = static_cast<document::LicenseKind>(m_licenseComboBox->currentData().toInt());
    const QString deed = QString::fromStdString(document::licenseDeedUrl(kind));
    QString text;
    switch (kind) {
    case document::LicenseKind::Unspecified:
        text = tr("Without a license, others cannot tell what they may do with the "
                  "colorization.");
        break;
    case document::LicenseKind::AllRightsReserved:
        text = tr("Others may play the colorization, but not share or change it without your "
                  "permission.");
        break;
    case document::LicenseKind::Custom:
        text = tr("The terms are your own text, below.");
        break;
    case document::LicenseKind::Cc0:
        text = tr("Anyone may use it for anything, without naming you.");
        break;
    case document::LicenseKind::CcBy:
        text = tr("Anyone may share and change it, also commercially, naming you.");
        break;
    case document::LicenseKind::CcBySa:
        text = tr("Anyone may share and change it, also commercially, naming you; changed "
                  "versions under the same license.");
        break;
    case document::LicenseKind::CcByNc:
        text = tr("Anyone may share and change it, not commercially, naming you.");
        break;
    case document::LicenseKind::CcByNcSa:
        text = tr("Anyone may share and change it, not commercially, naming you; changed "
                  "versions under the same license.");
        break;
    }
    text = text.toHtmlEscaped();
    if (!deed.isEmpty()) {
        text += QStringLiteral(" <a href=\"%1\">%2</a>").arg(deed, tr("The license"));
    }
    m_licenseLabel->setText(text);
    m_licenseTextEdit->setVisible(kind == document::LicenseKind::Custom);
}

bool ProjectSettingsPanel::targetFits() const
{
    return targetProblemText().isEmpty();
}

QString ProjectSettingsPanel::targetProblemText() const
{
    if (!m_document || libserumTarget() == m_document->libserumTarget()) {
        return QString();
    }
    const document::LibserumTargetProblems problems =
        m_document->libserumTargetProblems(libserumTarget());
    QStringList reasons;
    if (problems.tooManySprites()) {
        reasons << tr("The project has %1 sprites, it allows %2.")
                       .arg(problems.sprites)
                       .arg(problems.limits.maxSprites);
    }
    if (problems.tooManyBackgrounds()) {
        reasons << tr("The project has %1 backgrounds, it allows %2.")
                       .arg(problems.backgrounds)
                       .arg(problems.limits.maxBackgrounds);
    }
    if (problems.framesWithMoreSprites > 0) {
        reasons << tr("%n frame(s) use more than %1 sprites, the first is frame %2.", "",
                      static_cast<int>(problems.framesWithMoreSprites))
                       .arg(problems.limits.spritesPerFrame)
                       .arg(problems.firstFrameWithMoreSprites);
    }
    if (reasons.isEmpty()) {
        return QString();
    }
    return tr("The project does not fit %1: %2")
        .arg(m_targetComboBox->currentText(), reasons.join(QLatin1Char(' ')));
}

void ProjectSettingsPanel::updateTargetProblems()
{
    const QString problems = targetProblemText();
    if (!problems.isEmpty()) {
        m_targetLabel->setText(problems);
    } else if (libserumTarget() == document::LibserumTarget::Libserum29) {
        m_targetLabel->setText(
            tr("Plays with libserum 2.9 and later only. Up to %1 sprites per frame are possible.")
                    .arg(document::kMaxSpritesPerFrame) +
            QLatin1Char(' ') + SpritePanel::manySpritesAdvice());
    } else {
        m_targetLabel->setText(tr("Plays with libserum 2.8 and later."));
    }
}

}  // namespace colorizing::app
