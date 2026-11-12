#pragma once

#include "core/InstallationContext.h"
#include "modules/documents/domain/Document.h"

#include <QDialog>
#include <QString>

#include <optional>

class DocumentVersionFields;
class QComboBox;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;

// Add Document and Edit Details, in one dialog
// (document-management-spec.md §4.4).
//
// One dialog for both because the fields are the same apart from the first
// file, which only an addition has. The same call CertificateEditDialog
// makes, and for the same reason: two dialogs that must stay identical
// eventually do not.
class DocumentEditDialog : public QDialog
{
    Q_OBJECT

public:
    // `existing` empty means Add. `selectedVesselId` is the vessel the screen
    // is scoped to, offered as the non-fleet choice; empty means the only
    // scope on offer is fleet-wide.
    DocumentEditDialog(const InstallationContext&     installation,
                       const QString&                 selectedVesselId,
                       const QString&                 selectedVesselName,
                       const std::optional<Document>& existing,
                       QWidget*                       parent = nullptr);

    // Valid only after exec() returned Accepted. The id is empty when adding.
    Document document() const;

    // The first version's file and details. Only meaningful when adding —
    // editing the details of a document never touches its versions.
    QString        firstVersionSourcePath() const;
    DocumentVersion firstVersionValues() const;

private:
    void updateOkState();

    bool m_isEditing = false;

    QComboBox*      m_scopeCombo    = nullptr;
    QComboBox*      m_categoryCombo = nullptr;
    QLineEdit*      m_referenceEdit = nullptr;
    QLineEdit*      m_titleEdit     = nullptr;
    QPlainTextEdit* m_notesEdit     = nullptr;

    // Null when editing: there is no first file to choose then.
    DocumentVersionFields* m_versionFields = nullptr;

    QPushButton* m_okButton = nullptr;

    QString m_documentId;
    QString m_existingVesselId;
};
