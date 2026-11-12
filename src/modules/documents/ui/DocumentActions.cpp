#include "modules/documents/ui/DocumentActions.h"

#include "core/DocumentsFolder.h"
#include "modules/documents/data/DocumentRepository.h"
#include "modules/documents/service/DocumentFileStore.h"
#include "modules/documents/ui/DocumentEditDialog.h"
#include "modules/documents/ui/DocumentVersionDialog.h"
#include "modules/documents/ui/DocumentVersionsDialog.h"

#include <QDesktopServices>
#include <QDir>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QUrl>

DocumentActions::DocumentActions(DocumentRepository& repository, DocumentFileStore& files,
                                 QTableWidget* table, QWidget* parent)
    : QObject(parent)
    , m_repository(repository)
    , m_files(files)
    , m_table(table)
    , m_dialogParent(parent)
{
    m_addVersionButton = new QPushButton(tr("Add New &Version…"), parent);
    m_addVersionButton->setObjectName(QStringLiteral("addVersionButton"));

    m_openButton = new QPushButton(tr("&Open"), parent);
    m_openButton->setObjectName(QStringLiteral("openDocumentButton"));

    m_versionsButton = new QPushButton(tr("Ver&sions…"), parent);
    m_versionsButton->setObjectName(QStringLiteral("versionsButton"));

    m_editButton = new QPushButton(tr("&Edit Details…"), parent);
    m_editButton->setObjectName(QStringLiteral("editDocumentButton"));

    // Last, and furthest from the others it could be mistaken for — the same
    // placement the certificate list settled on for its own Delete.
    m_deleteButton = new QPushButton(tr("&Delete…"), parent);
    m_deleteButton->setObjectName(QStringLiteral("deleteDocumentButton"));

    connect(m_addVersionButton, &QPushButton::clicked, this, &DocumentActions::addVersion);
    connect(m_openButton, &QPushButton::clicked, this, &DocumentActions::openCurrent);
    connect(m_versionsButton, &QPushButton::clicked, this, &DocumentActions::showVersions);
    connect(m_editButton, &QPushButton::clicked, this, &DocumentActions::editDetails);
    connect(m_deleteButton, &QPushButton::clicked, this, &DocumentActions::deleteDocument);

    connect(m_table, &QTableWidget::itemSelectionChanged, this,
            &DocumentActions::updateButtonStates);

    updateButtonStates();
}

QPushButton* DocumentActions::addVersionButton() const { return m_addVersionButton; }
QPushButton* DocumentActions::openButton() const { return m_openButton; }
QPushButton* DocumentActions::versionsButton() const { return m_versionsButton; }
QPushButton* DocumentActions::editButton() const { return m_editButton; }
QPushButton* DocumentActions::deleteButton() const { return m_deleteButton; }

QString DocumentActions::documentsRoot() const
{
    return DocumentsFolder::path();
}

// The selected row's document, read through the repository so the scope
// applies. False when nothing is selected or it is no longer there, with the
// message already shown — every action below starts here, and four of them
// did the same six lines before this existed.
bool DocumentActions::selectedDocument(Document* document)
{
    const QString id = selectedDocumentId();
    if (id.isEmpty()) {
        return false;
    }

    std::optional<Document> found;
    if (!m_repository.findById(id, &found) || !found.has_value()) {
        QMessageBox::warning(m_dialogParent, tr("Could not read the document"),
                             m_repository.errorString());
        return false;
    }
    *document = *found;
    return true;
}

QString DocumentActions::selectedDocumentId() const
{
    const QList<QTableWidgetItem*> selected = m_table->selectedItems();
    if (selected.isEmpty()) {
        return QString();
    }

    const int row = selected.first()->row();
    for (QTableWidgetItem* item : selected) {
        if (item->row() != row) {
            return QString(); // more than one row: not a single selection
        }
    }

    QTableWidgetItem* dataItem = m_table->item(row, DocumentRow::kDataColumn);
    return dataItem == nullptr ? QString()
                               : dataItem->data(DocumentRow::kDocumentIdRole).toString();
}

bool DocumentActions::selectionIsEditable() const
{
    const QList<QTableWidgetItem*> selected = m_table->selectedItems();
    if (selected.isEmpty()) {
        return false;
    }
    QTableWidgetItem* dataItem = m_table->item(selected.first()->row(), DocumentRow::kDataColumn);
    return dataItem != nullptr && dataItem->data(DocumentRow::kEditableRole).toBool();
}

void DocumentActions::updateButtonStates()
{
    const bool hasOne    = !selectedDocumentId().isEmpty();
    const bool mayChange = hasOne && selectionIsEditable();

    // Open and Versions are reading, so they follow the selection alone: a
    // vessel may read the company's SMS, it just may not change it (§4.3).
    m_openButton->setEnabled(hasOne);
    m_versionsButton->setEnabled(hasOne);

    m_addVersionButton->setEnabled(mayChange);
    m_editButton->setEnabled(mayChange);
    m_deleteButton->setEnabled(mayChange);

    const QString readOnlyHint =
        tr("Fleet-wide documents are managed at the office and are read-only here.");
    for (QPushButton* button : {m_addVersionButton, m_editButton, m_deleteButton}) {
        button->setToolTip(hasOne && !mayChange ? readOnlyHint : QString());
    }
}

bool DocumentActions::requireDocumentsFolder()
{
    if (!documentsRoot().isEmpty()) {
        return true;
    }
    // §4.4: say so and point at Settings, rather than failing later with
    // something about a path.
    QMessageBox::information(m_dialogParent, tr("No documents folder yet"),
                             tr("Choose a documents folder in Settings first. That is where "
                                "the files are kept."));
    return false;
}

void DocumentActions::openDocument(int row)
{
    QTableWidgetItem* dataItem = m_table->item(row, DocumentRow::kDataColumn);
    if (dataItem == nullptr) {
        return;
    }
    const QString id = dataItem->data(DocumentRow::kDocumentIdRole).toString();
    if (id.isEmpty()) {
        return;
    }

    std::optional<DocumentVersion> current;
    if (!m_repository.currentVersion(id, &current)) {
        QMessageBox::warning(m_dialogParent, tr("Could not open the document"),
                             m_repository.errorString());
        return;
    }
    if (!current.has_value()) {
        QMessageBox::information(m_dialogParent, tr("Nothing to open"),
                                 tr("This document has no versions."));
        return;
    }

    const QString absolutePath =
        DocumentFileStore::absolutePathFor(documentsRoot(), current->filePath);
    if (absolutePath.isEmpty()) {
        QMessageBox::information(m_dialogParent, tr("No documents folder yet"),
                                 tr("Choose a documents folder in Settings first."));
        return;
    }

    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(absolutePath))) {
        QMessageBox::warning(m_dialogParent, tr("Could not open the document"),
                             tr("Windows, macOS or your desktop refused to open:\n\n%1")
                                 .arg(QDir::toNativeSeparators(absolutePath)));
    }
}

void DocumentActions::openCurrent()
{
    const QList<QTableWidgetItem*> selected = m_table->selectedItems();
    if (!selected.isEmpty()) {
        openDocument(selected.first()->row());
    }
}

void DocumentActions::addVersion()
{
    Document document;
    if (!requireDocumentsFolder() || !selectedDocument(&document)) {
        return;
    }
    const QString id = document.id;

    DocumentVersionDialog dialog(document.title, m_dialogParent);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    // The file is copied in before the row is written: a row pointing at a
    // file that was never copied would be a version nobody can open, while a
    // copied file with no row is a stray the next version simply sits beside.
    // The same order certificate attachments settled on.
    QList<DocumentVersion> existing;
    m_repository.listVersions(id, &existing);
    const int nextSequence = existing.isEmpty() ? 1 : existing.first().sequence + 1;

    DocumentVersion version = dialog.values();
    QString         error;
    if (!m_files.store(documentsRoot(), document, nextSequence, dialog.sourcePath(),
                       &version.filePath, &error)) {
        QMessageBox::warning(m_dialogParent, tr("Could not file the document"), error);
        return;
    }

    if (!m_repository.addVersion(id, version, nullptr)) {
        QMessageBox::warning(m_dialogParent, tr("Could not save the version"),
                             m_repository.errorString());
        return;
    }

    emit listChanged();
}

void DocumentActions::showVersions()
{
    Document document;
    if (!selectedDocument(&document)) {
        return;
    }

    QList<DocumentVersion> versions;
    if (!m_repository.listVersions(document.id, &versions)) {
        QMessageBox::warning(m_dialogParent, tr("Could not read the versions"),
                             m_repository.errorString());
        return;
    }

    DocumentVersionsDialog dialog(document.title, versions, documentsRoot(), m_dialogParent);
    dialog.exec();
}

void DocumentActions::editDetails()
{
    Document document;
    if (!selectedDocument(&document)) {
        return;
    }

    // The installation and the selected vessel are not needed here: editing
    // never changes the scope, and the dialog shows the stored one disabled.
    DocumentEditDialog dialog(InstallationContext(), document.vesselId, QString(),
                              std::optional<Document>(document), m_dialogParent);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    if (!m_repository.updateDetails(dialog.document())) {
        QMessageBox::warning(m_dialogParent, tr("Could not save the document"),
                             m_repository.errorString());
        return;
    }

    emit listChanged();
}

void DocumentActions::deleteDocument()
{
    Document document;
    if (!selectedDocument(&document)) {
        return;
    }

    // §4.4: the confirmation says the files stay. Somebody deleting an SMS
    // record should know what is and is not being destroyed before they
    // answer, not find out afterwards.
    QMessageBox confirmation(m_dialogParent);
    confirmation.setIcon(QMessageBox::Question);
    confirmation.setWindowTitle(tr("Delete document?"));
    confirmation.setText(tr("Delete '%1'?").arg(document.title));
    confirmation.setInformativeText(
        tr("It will be removed from the list along with its version history.\n\n"
           "The files themselves stay in the documents folder and are not deleted."));
    confirmation.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    confirmation.setDefaultButton(QMessageBox::No);

    if (confirmation.exec() != QMessageBox::Yes) {
        return;
    }

    if (!m_repository.softDelete(document.id)) {
        QMessageBox::warning(m_dialogParent, tr("Could not delete the document"),
                             m_repository.errorString());
        return;
    }

    emit listChanged();
}
