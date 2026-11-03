#pragma once

#include "modules/certificates/service/CertificateCsvImporter.h"

#include <QDialog>

class QLabel;
class QPushButton;
class QTableWidget;

// The dry-run preview a CSV import always goes through
// (certificate-import-export-spec §5.5, and certificate-control-spec.md §8's
// original "a dry-run preview is shown before anything is written").
//
// It writes nothing itself. It shows what the importer worked out about every
// row, collects the one decision only the user can make — skip or overwrite,
// for each row that matches a certificate already on file — and hands the
// whole preview back with those decisions filled in.
//
// Import is refused outright while any row is Invalid: §5.2 promises the
// import either fully succeeds or changes nothing, and a file with a bad row
// in it has to be corrected rather than partly swallowed.
class CertificateImportPreviewDialog : public QDialog
{
    Q_OBJECT

public:
    CertificateImportPreviewDialog(const CertificateImportPreview& preview,
                                   QWidget*                       parent = nullptr);

    // The preview as shown, with each matching row's overwrite flag set from
    // the checkbox beside it. Unchecked — skip — is the default, so an
    // accidental Enter overwrites nothing.
    CertificateImportPreview decisions() const;

private:
    void fillTable();

    CertificateImportPreview m_preview;

    QTableWidget* m_table         = nullptr;
    QLabel*       m_summary       = nullptr;
    QPushButton*  m_importButton  = nullptr;
};
