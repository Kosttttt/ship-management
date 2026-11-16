#pragma once

#include "modules/documents/data/DocumentHistoryReader.h"
#include "modules/documents/domain/DocumentPage.h"

#include <QDialog>
#include <QList>
#include <QString>

class DocumentImageStore;
class DocumentRestorer;
class QPushButton;
class QTableWidget;

// One page's history (document-history-spec.md §2): every published revision,
// newest first, with View, Compare with previous, Compare… and Restore…
//
// Reading is allowed everywhere, including on a space this installation may
// only read — a vessel may look through a company manual's history and
// compare its revisions. Only Restore… is greyed, and the service refuses it
// as well (§5), because a greyed button is a courtesy and not a rule.
class DocumentHistoryDialog : public QDialog
{
    Q_OBJECT

public:
    DocumentHistoryDialog(DocumentHistoryReader& history, DocumentRestorer& restorer,
                          DocumentImageStore& images, QWidget* parent = nullptr);

    // Fills the list for one page. `manualSpace` adds the "Manual rev"
    // column, which a Collection space has nothing to put in. False when the
    // history could not be read, with the reason in errorString().
    bool showFor(const DocumentPage& page, const QString& pageTitle, bool manualSpace,
                 bool editable);

    // True once a revision has been restored into the draft, so the screen
    // that opened this knows to re-read the page.
    bool didRestore() const { return m_restored; }

    QString errorString() const { return m_errorString; }

private:
    void fillTable();
    void updateButtons();

    // The selected rows, as indexes into m_entries, newest first.
    QList<int> selectedRows() const;

    void viewSelected();
    void compareWithPrevious();
    void compareWithAnother();
    void restoreSelected();

    // Opens the comparison of two revisions of this page, older on the left
    // of the legend whichever way round they were picked.
    void compareRevisions(const DocumentPageRevision& older,
                          const DocumentPageRevision& newer);

    QString describe(const DocumentHistoryReader::PageHistoryEntry& entry) const;

    DocumentHistoryReader& m_history;
    DocumentRestorer&      m_restorer;
    DocumentImageStore&    m_images;

    QTableWidget* m_table           = nullptr;
    QPushButton*  m_viewButton      = nullptr;
    QPushButton*  m_previousButton  = nullptr;
    QPushButton*  m_compareButton   = nullptr;
    QPushButton*  m_restoreButton   = nullptr;

    QList<DocumentHistoryReader::PageHistoryEntry> m_entries;

    DocumentPage m_page;
    QString      m_pageTitle;
    bool         m_manualSpace = false;
    bool         m_editable    = false;
    bool         m_restored    = false;
    QString      m_errorString;
};
