#pragma once

#include "modules/certificates/domain/Endorsement.h"

#include <QDialog>
#include <QList>

class EndorsementEditForm;
class EndorsementRepository;
class QPushButton;

// Records one endorsement against an existing certificate. Add-only: there is
// no edit mode, because editing a compliance record is a design question this
// step deliberately leaves open (certificate-endorsement-spec §6).
class EndorsementEditDialog : public QDialog
{
    Q_OBJECT

public:
    EndorsementEditDialog(EndorsementRepository&   repository,
                          const QString&           certificateId,
                          const QList<SurveyType>& allowedTypes,
                          QWidget*                 parent = nullptr);

protected:
    void accept() override;

private:
    EndorsementRepository& m_repository;
    EndorsementEditForm*   m_form     = nullptr;
    QPushButton*           m_okButton = nullptr;
};
