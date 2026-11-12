#pragma once

#include "app/IModule.h"
#include "core/InstallationContext.h"
#include "core/VesselRepository.h"
#include "modules/documents/data/DocumentRepository.h"
#include "modules/documents/service/DocumentFileStore.h"

#include <QString>

class DocumentListWidget;
class QSqlDatabase;

// The Document Management module (document-management-spec.md §3).
//
// Like every module it is constructed from nothing but a database connection
// and an installation context, and owns its own repositories from those —
// which is what lets ModuleRegistry register it with one line.
//
// It includes **nothing** from modules/certificates/ (CLAUDE.md §5). What it
// genuinely shares with that module — ManagedFileStore, Vessel,
// VesselRepository — it takes from core/, which is the direction
// dependencies are allowed to point.
class DocumentsModule : public IModule
{
public:
    DocumentsModule(QSqlDatabase& database, const InstallationContext& installation);

    QString id() const override;
    QString displayName() const override;
    QIcon   icon() const override;

    QWidget* createMainWidget(QWidget* parent) override;

    QList<Migration>      migrations() const override;
    QList<AlertProvider*> alertProviders() override;

    // §3.1: this is the module the hook was added for. The shell calls it on
    // every module when the toolbar's vessel changes, so it never has to know
    // what any module's screen is.
    void setSelectedVessel(const QString& vesselId) override;

private:
    // By value, never by reference — see InstallationContext.h. Declared
    // first, because the repositories below are built from it and C++
    // initialises members in declaration order.
    InstallationContext m_installation;

    DocumentRepository m_documents;

    // Core-owned, and read here for one thing: the IMO number a vessel's
    // document folder is named after.
    VesselRepository  m_vessels;
    DocumentFileStore m_files;

    // The screen, kept so setSelectedVessel() can reach it. Owned by its
    // parent widget through Qt's parent-child ownership, not by this module;
    // the pointer is cleared when it is destroyed.
    DocumentListWidget* m_screen = nullptr;

    // Remembered so a screen created after the first selection still opens on
    // the right vessel. MainWindow builds the screens before it fills the
    // toolbar selector, and the order of those two is not this module's to
    // rely on.
    QString m_selectedVesselId;
};
