#pragma once

#include "core/Vessel.h"

#include <QWidget>

class QLabel;
class QLineEdit;
class QSpinBox;

// The six vessel fields, as a plain widget rather than a dialog
// (vessel-crud-spec §4).
//
// It is a QWidget precisely so it can be used twice: wrapped in a dialog for
// the OFFICE list, and embedded directly in the VESSEL detail page. Defining
// the same six fields in two places would guarantee they drift apart.
class VesselEditForm : public QWidget
{
    Q_OBJECT

public:
    explicit VesselEditForm(QWidget* parent = nullptr);

    // Fills the fields. The id is remembered and handed back by vessel().
    void setVessel(const Vessel& vessel);

    // The current contents of the fields.
    Vessel vessel() const;

    bool isValid() const;

    void setFocusOnFirstField();

signals:
    // Emitted whenever isValid() may have changed, so a Save or OK button can
    // follow it without polling.
    void validityChanged(bool valid);

private:
    void updateImoFeedback();
    void announceValidity();

    QLineEdit* m_nameEdit           = nullptr;
    QLineEdit* m_imoEdit            = nullptr;
    QLabel*    m_imoError           = nullptr;
    QLineEdit* m_callSignEdit       = nullptr;
    QSpinBox*  m_grossTonnageSpin   = nullptr;
    QLineEdit* m_portOfRegistryEdit = nullptr;
    QLineEdit* m_flagStateEdit      = nullptr;

    QString m_id;   // carried through; never shown
};
