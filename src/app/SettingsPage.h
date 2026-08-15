#pragma once

#include <QWidget>

class AppSettingRepository;
class QLabel;
class QPushButton;
class QSpinBox;

// The Settings screen (settings-app-setting-spec §6).
//
// A form with no dialog wrapper: it edits the one settings row directly, the
// same way VesselDetailPage edits the one vessel in VESSEL mode. Core-owned
// UI, so it sits alongside VesselListWidget rather than under any module.
class SettingsPage : public QWidget
{
    Q_OBJECT

public:
    explicit SettingsPage(AppSettingRepository& settings, QWidget* parent = nullptr);

    // Re-reads the stored row into the form.
    void reload();

private:
    void save();

    // The three values must stay strictly increasing. The Save button follows
    // this, so the invalid combination is never something a user can submit —
    // the repository's own check stays as the backstop.
    void updateValidity();
    bool isValid() const;

    AppSettingRepository& m_settings;

    QSpinBox* m_criticalSpin     = nullptr;
    QSpinBox* m_expiringSoonSpin = nullptr;
    QSpinBox* m_dueSoonSpin      = nullptr;

    QLabel*      m_hintLabel   = nullptr;
    QLabel*      m_statusLabel = nullptr;
    QPushButton* m_saveButton  = nullptr;

    QString m_settingId;
};
