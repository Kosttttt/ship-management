#pragma once

#include "core/InstallationContext.h"

#include <QWizardPage>

class QRadioButton;

// Page 1 of the first-run wizard: OFFICE or VESSEL
// (first-run-wizard-spec §4).
class InstallationModePage : public QWizardPage
{
    Q_OBJECT

public:
    explicit InstallationModePage(QWidget* parent = nullptr);

    InstallationMode selectedMode() const;

    // OFFICE has nothing more to ask, so it skips straight to Finish.
    int nextId() const override;

private:
    QRadioButton* m_officeButton = nullptr;
    QRadioButton* m_vesselButton = nullptr;
};
