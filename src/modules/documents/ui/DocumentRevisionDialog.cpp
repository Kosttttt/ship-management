#include "modules/documents/ui/DocumentRevisionDialog.h"

#include "modules/documents/domain/DocumentDiff.h"
#include "modules/documents/ui/DocumentDiffRenderer.h"
#include "modules/documents/ui/DocumentRichEdit.h"

#include <QDialogButtonBox>
#include <QFont>
#include <QLabel>
#include <QVBoxLayout>

DocumentRevisionDialog::DocumentRevisionDialog(const QString&      heading,
                                               DocumentImageStore& images, QWidget* parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("documentRevisionDialog"));
    setModal(true);
    resize(760, 620);

    m_heading = new QLabel(heading, this);
    m_heading->setObjectName(QStringLiteral("revisionHeading"));
    m_heading->setWordWrap(true);
    QFont headingFont = m_heading->font();
    headingFont.setBold(true);
    m_heading->setFont(headingFont);

    m_legend = new QLabel(QString(), this);
    m_legend->setObjectName(QStringLiteral("revisionLegend"));
    m_legend->setWordWrap(true);
    m_legend->setVisible(false);

    // The same widget the page itself is read in, so a revision's pictures,
    // tables and lists look exactly as they did when it was in force.
    m_page = new DocumentRichEdit(images, this);
    m_page->setObjectName(QStringLiteral("revisionPage"));
    m_page->setReadOnly(true);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_heading);
    layout->addWidget(m_legend);
    layout->addWidget(m_page, 1);
    layout->addWidget(buttons);
}

DocumentRevisionDialog* DocumentRevisionDialog::view(const QString& heading, const QString& title,
                                                     const QString&      content,
                                                     DocumentImageStore& images, QWidget* parent)
{
    auto* dialog = new DocumentRevisionDialog(heading, images, parent);
    dialog->setWindowTitle(title);
    dialog->m_page->setHtml(content);
    return dialog;
}

DocumentRevisionDialog* DocumentRevisionDialog::compare(const QString& legend,
                                                        const QString& title,
                                                        const QString& oldContent,
                                                        const QString& newContent,
                                                        DocumentImageStore& images,
                                                        QWidget*            parent)
{
    auto* dialog = new DocumentRevisionDialog(title, images, parent);
    dialog->setWindowTitle(tr("Compare"));

    dialog->m_legend->setText(legend);
    dialog->m_legend->setVisible(true);

    // The newer version is the page; the marks go on top of it (§4.2).
    dialog->m_page->setHtml(newContent);

    const DocumentDiff::Result result = DocumentDiff::compare(oldContent, newContent);
    DocumentDiffRenderer::apply(dialog->m_page->document(), result);

    if (result.isUnchanged()) {
        // Said plainly rather than shown as a page with no marks on it,
        // which looks the same as a comparison that failed.
        dialog->m_legend->setText(legend + QStringLiteral("\n")
                                  + tr("Nothing has changed between these two."));
    }

    // Painting the marks is an edit as far as the widget is concerned, so
    // the undo history is cleared: there is nothing here a reader may undo.
    dialog->m_page->document()->clearUndoRedoStacks();
    return dialog;
}

namespace {

QString shownDate(const QDateTime& utc)
{
    // Stored UTC, shown local (CLAUDE.md §6.2).
    return utc.toLocalTime().toString(QStringLiteral("dd MMM yyyy"));
}

} // namespace

DocumentRevisionDialog* DocumentRevisionDialog::compareRevisions(
    const DocumentPageRevision& older, const DocumentPageRevision& newer,
    DocumentImageStore& images, QWidget* parent)
{
    return compare(tr("Comparing Rev %1 (%2) → Rev %3 (%4)")
                       .arg(older.number)
                       .arg(shownDate(older.publishedAtUtc))
                       .arg(newer.number)
                       .arg(shownDate(newer.publishedAtUtc)),
                   newer.title, older.content, newer.content, images, parent);
}

DocumentRevisionDialog* DocumentRevisionDialog::compareWithDraft(
    const DocumentPageRevision& published, const QString& draftTitle,
    const QString& draftContent, DocumentImageStore& images, QWidget* parent)
{
    return compare(tr("Comparing Rev %1 (%2) → this draft")
                       .arg(published.number)
                       .arg(shownDate(published.publishedAtUtc)),
                   draftTitle.isEmpty() ? published.title : draftTitle, published.content,
                   draftContent, images, parent);
}
