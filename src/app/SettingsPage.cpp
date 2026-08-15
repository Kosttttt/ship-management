#include "app/SettingsPage.h"

#include "core/AppSettingRepository.h"

#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {

// A day count, bounded so a mistyped value cannot be absurd. Ten years is
// generous for the longest of these and still finite.
constexpr int kMinimumDays = 1;
constexpr int kMaximumDays = 3650;

QSpinBox* makeDaySpin(QWidget* parent, const QString& objectName)
{
    auto* spin = new QSpinBox(parent);
    spin->setObjectName(objectName);
    spin->setRange(kMinimumDays, kMaximumDays);
    spin->setSuffix(SettingsPage::tr(" days"));
    return spin;
}

} // namespace

SettingsPage::SettingsPage(AppSettingRepository& settings, QWidget* parent)
    : QWidget(parent)
    , m_settings(settings)
{
    auto* heading = new QLabel(tr("Alert thresholds"), this);
    QFont headingFont = heading->font();
    headingFont.setBold(true);
    heading->setFont(headingFont);

    auto* explanation =
        new QLabel(tr("How far ahead the certificate list warns you. A certificate is shown as "
                      "Critical, then Expiring Soon, as its expiry approaches; a survey window "
                      "is flagged as Due Soon before it opens."),
                   this);
    explanation->setWordWrap(true);
    explanation->setEnabled(false); // renders as secondary text

    m_criticalSpin     = makeDaySpin(this, QStringLiteral("criticalSpin"));
    m_expiringSoonSpin = makeDaySpin(this, QStringLiteral("expiringSoonSpin"));
    m_dueSoonSpin      = makeDaySpin(this, QStringLiteral("dueSoonSpin"));

    auto* group       = new QGroupBox(tr("Certificate alerts"), this);
    auto* groupLayout = new QFormLayout(group);
    groupLayout->addRow(tr("&Critical within:"), m_criticalSpin);
    groupLayout->addRow(tr("&Expiring soon within:"), m_expiringSoonSpin);
    groupLayout->addRow(tr("&Due soon within:"), m_dueSoonSpin);

    m_hintLabel = new QLabel(this);
    m_hintLabel->setObjectName(QStringLiteral("thresholdHint"));
    m_hintLabel->setWordWrap(true);
    QPalette hintPalette = m_hintLabel->palette();
    hintPalette.setColor(QPalette::WindowText, QColor(0xC0, 0x39, 0x2B));
    m_hintLabel->setPalette(hintPalette);

    m_saveButton = new QPushButton(tr("&Save"), this);
    m_saveButton->setObjectName(QStringLiteral("saveSettingsButton"));

    auto* discardButton = new QPushButton(tr("Discard &changes"), this);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setEnabled(false);

    auto* buttonRow = new QHBoxLayout;
    buttonRow->addWidget(m_saveButton);
    buttonRow->addWidget(discardButton);
    buttonRow->addStretch();
    buttonRow->addWidget(m_statusLabel);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(heading);
    layout->addWidget(explanation);
    layout->addSpacing(8);
    layout->addWidget(group);
    layout->addWidget(m_hintLabel);
    layout->addSpacing(8);
    layout->addLayout(buttonRow);
    layout->addStretch();

    for (QSpinBox* spin : {m_criticalSpin, m_expiringSoonSpin, m_dueSoonSpin}) {
        connect(spin, &QSpinBox::valueChanged, this, [this]() { updateValidity(); });
    }
    connect(m_saveButton, &QPushButton::clicked, this, &SettingsPage::save);
    connect(discardButton, &QPushButton::clicked, this, &SettingsPage::reload);

    reload();
}

void SettingsPage::reload()
{
    AppSetting setting;
    if (!m_settings.read(&setting)) {
        qWarning() << "Could not read the application settings:" << m_settings.errorString();
        m_statusLabel->setText(tr("Settings could not be read."));
        m_saveButton->setEnabled(false);
        return;
    }

    m_settingId = setting.id;
    m_criticalSpin->setValue(setting.criticalDays);
    m_expiringSoonSpin->setValue(setting.expiringSoonDays);
    m_dueSoonSpin->setValue(setting.dueSoonDays);

    m_statusLabel->clear();
    updateValidity();
}

bool SettingsPage::isValid() const
{
    return m_criticalSpin->value() < m_expiringSoonSpin->value()
           && m_expiringSoonSpin->value() < m_dueSoonSpin->value();
}

void SettingsPage::updateValidity()
{
    const bool valid = isValid();
    m_saveButton->setEnabled(valid);

    if (valid) {
        m_hintLabel->clear();
        return;
    }

    // Name the pair that is out of order, the same way the repository's own
    // message does, rather than a bare "invalid".
    if (m_criticalSpin->value() >= m_expiringSoonSpin->value()) {
        m_hintLabel->setText(tr("Critical must be fewer days than Expiring soon."));
    } else {
        m_hintLabel->setText(tr("Expiring soon must be fewer days than Due soon."));
    }
}

void SettingsPage::save()
{
    if (!isValid()) {
        return;
    }

    AppSetting setting;
    setting.id               = m_settingId;
    setting.criticalDays     = m_criticalSpin->value();
    setting.expiringSoonDays = m_expiringSoonSpin->value();
    setting.dueSoonDays      = m_dueSoonSpin->value();

    if (!m_settings.update(setting)) {
        qWarning() << "Settings save failed:" << m_settings.errorString();
        QMessageBox::warning(this,
                             tr("Could not save"),
                             tr("Nothing has been changed, so you can correct this and try "
                                "again.\n\n%1")
                                 .arg(m_settings.errorString()));
        return;
    }

    qInfo() << "Alert thresholds saved.";

    // Read back rather than trusting the form: the stored row is the truth.
    reload();
    m_statusLabel->setText(tr("Saved. The certificate list uses these the next time it loads."));
}
