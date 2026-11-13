#pragma once

#include "modules/documents/service/DocumentPublisher.h"

#include <QDialog>
#include <QList>
#include <QString>

class QPlainTextEdit;
class QPushButton;

// Publish… for a Collection page, and Publish revision… for a whole manual
// (document-management-spec.md §6.2).
//
// One dialog for both, because what differs is small and explicit: a manual
// shows the list of what the revision will cover and insists on a note; a
// collection page asks for one and does not mind. Two dialogs that must stay
// consistent about publishing eventually are not.
class DocumentPublishDialog : public QDialog
{
    Q_OBJECT

public:
    // A Collection page: its title, and the number it is about to get.
    static DocumentPublishDialog* forPage(const QString& pageTitle, int nextNumber,
                                          QWidget* parent);

    // A manual: its name, the new Rev number, and everything the revision
    // will cover.
    static DocumentPublishDialog* forManual(const QString& spaceName, int nextNumber,
                                            const QList<DocumentPublisher::PendingChange>& changes,
                                            QWidget* parent);

    QString note() const;

private:
    DocumentPublishDialog(const QString& heading, const QString& explanation, bool noteRequired,
                          QWidget* parent);

    void buildChangeList(const QList<DocumentPublisher::PendingChange>& changes);
    void updateOkState();

    bool            m_noteRequired = false;
    QPlainTextEdit* m_noteEdit     = nullptr;
    QPushButton*    m_okButton     = nullptr;
};
