#pragma once

#include "modules/certificates/domain/Endorsement.h"

#include <QDialog>
#include <QList>

#include <optional>

class EndorsementEditForm;
class EndorsementRepository;
class QPushButton;

// Records one endorsement against an existing certificate, or corrects one
// already recorded (certificate-attachment-addendum-2-spec §6).
//
// One dialog for both, the same shape CertificateEditDialog has always had for
// Add and Edit. Editing saves back onto the same row rather than adding a
// second one: a correction is a fix to the record of something that happened
// once, not a new survey.
class EndorsementEditDialog : public QDialog
{
    Q_OBJECT

public:
    // existing is std::nullopt when adding, which is what every call site did
    // before this parameter existed. When it holds a value, the form is
    // pre-filled from it and a successful save is an UPDATE of that row.
    EndorsementEditDialog(EndorsementRepository&            repository,
                          const QString&                    certificateId,
                          const QList<SurveyType>&          allowedTypes,
                          const std::optional<Endorsement>& existing = std::nullopt,
                          QWidget*                          parent    = nullptr);

protected:
    void accept() override;

private:
    EndorsementRepository& m_repository;
    EndorsementEditForm*   m_form      = nullptr;
    QPushButton*           m_okButton  = nullptr;
    bool                   m_isEditing = false;
};
