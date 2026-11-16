#pragma once

#include "modules/documents/domain/DocumentPage.h"

#include <QDialog>
#include <QString>

class DocumentImageStore;
class DocumentRichEdit;
class QLabel;

// A page, read-only: one revision as it was published, or two of them
// compared (document-history-spec.md §2, §4.2).
//
// One dialog for both, like DocumentPublishDialog, because what differs is
// one line of text and whether the marks are painted on. Two dialogs that
// must agree about how a page is shown eventually do not.
//
// It shows a page and closes. It reads nothing and writes nothing: the
// content it is given has already been read by somebody allowed to read it.
class DocumentRevisionDialog : public QDialog
{
    Q_OBJECT

public:
    // One revision as it stands. `heading` is the line above it, e.g.
    // "Chapter 5 — Rev 3 of 12 Nov 2026".
    static DocumentRevisionDialog* view(const QString& heading, const QString& title,
                                        const QString& content, DocumentImageStore& images,
                                        QWidget* parent);

    // Two versions of one page, with the differences marked on the newer one
    // (§4.2). `legend` is the one-line "Comparing Rev 2 (03 Mar 2026) →
    // Rev 3 (12 Nov 2026)".
    static DocumentRevisionDialog* compare(const QString& legend, const QString& title,
                                           const QString& oldContent, const QString& newContent,
                                           DocumentImageStore& images, QWidget* parent);

    // Two published revisions of one page. The legend is built from their own
    // numbers and dates, so that the history list and the Record of
    // Revisions word it identically — this is the one place it is written.
    static DocumentRevisionDialog* compareRevisions(const DocumentPageRevision& older,
                                                    const DocumentPageRevision& newer,
                                                    DocumentImageStore&         images,
                                                    QWidget*                    parent);

    // A page's draft against what is published — "what have I changed?"
    // before publishing (§4.1).
    static DocumentRevisionDialog* compareWithDraft(const DocumentPageRevision& published,
                                                    const QString&              draftTitle,
                                                    const QString&              draftContent,
                                                    DocumentImageStore&         images,
                                                    QWidget*                    parent);

private:
    DocumentRevisionDialog(const QString& heading, DocumentImageStore& images, QWidget* parent);

    QLabel*           m_heading = nullptr;
    QLabel*           m_legend  = nullptr;
    DocumentRichEdit* m_page    = nullptr;
};
