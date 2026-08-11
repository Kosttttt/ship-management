#pragma once

#include <QWizardPage>

class QLabel;
class QLineEdit;

// Page 2 of the first-run wizard, shown only for a VESSEL installation
// (first-run-wizard-spec §4). Asks for the vessel name and IMO number and
// nothing else — the remaining vessel fields belong to step 4.
class VesselIdentityPage : public QWizardPage
{
    Q_OBJECT

public:
    explicit VesselIdentityPage(QWidget* parent = nullptr);

    QString vesselName() const;
    QString imoNumber() const;   // digits only, ready to store

    // Finish stays disabled until both fields are acceptable.
    bool isComplete() const override;

private:
    void updateImoFeedback();

    QLineEdit* m_nameEdit  = nullptr;
    QLineEdit* m_imoEdit   = nullptr;
    QLabel*    m_imoError  = nullptr;
};
