#pragma once

#include "modules/documents/domain/Document.h"

#include <QDialog>
#include <QList>
#include <QString>

class QTableWidget;

// Every version of one document, newest first, the current one marked
// (document-management-spec.md §4.4).
//
// This is the archive the module exists for: a superseded version is not
// deleted, hidden or overwritten, and any of them can still be opened years
// later. "Current" is the highest sequence, worked out when the list is
// built and never stored (CLAUDE.md §6.7).
class DocumentVersionsDialog : public QDialog
{
    Q_OBJECT

public:
    // `documentsRoot` is where the files live; a version whose file cannot be
    // resolved is still listed, because the record of it existing is worth as
    // much as the file.
    DocumentVersionsDialog(const QString&                documentTitle,
                           const QList<DocumentVersion>& versions,
                           const QString&                documentsRoot,
                           QWidget*                      parent = nullptr);

private:
    void openSelected();

    QList<DocumentVersion> m_versions;
    QString                m_documentsRoot;
    QTableWidget*          m_table = nullptr;
};
