#pragma once

#include "app/IModule.h"
#include "core/InstallationContext.h"
#include "core/VesselRepository.h"
#include "modules/documents/data/DocumentImageStore.h"
#include "modules/documents/data/DocumentPageMoves.h"
#include "modules/documents/data/DocumentPageRemovals.h"
#include "modules/documents/data/DocumentPageRepository.h"
#include "modules/documents/data/DocumentRevisionStore.h"
#include "modules/documents/data/DocumentSpaceRepository.h"
#include "modules/documents/data/DocumentVesselSpaces.h"
#include "modules/documents/service/DocumentPublisher.h"

#include <QString>

class QSqlDatabase;

// The Document Management module — a simple Confluence
// (document-management-spec.md).
//
// Like every module it is constructed from nothing but a database connection
// and an installation context, and owns its own repositories from those,
// which is what lets ModuleRegistry register it with one line (CLAUDE.md §7).
//
// It includes **nothing** from modules/certificates/, and the build says so
// rather than trusting anyone to remember: `ShipDocuments` links `ShipCore`
// and nothing else. What it genuinely shares with that module —
// `VesselRepository` and `Vessel`, for the vessel spaces — comes from core/,
// which is the direction dependencies are allowed to point (CLAUDE.md §5).
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

    // IModule::setSelectedVessel() is deliberately **not** overridden. It is
    // kept on the interface because it is generic and harmless (§4.1), but
    // this screen picks spaces in its own panel and has nothing to do with
    // the toolbar's vessel — and a module that does not care overrides
    // nothing and notices no difference.

private:
    // By value, never by reference — see InstallationContext.h. Declared
    // first, because everything below is built from it and C++ initialises
    // members in declaration order.
    InstallationContext m_installation;

    // Core-owned, and read here for one thing: which vessels are active, so
    // each gets its own space (§4.3).
    VesselRepository m_vessels;

    // In the order they depend on each other: the pages ask the spaces about
    // permission, the moves ask the pages, and the publisher needs all three
    // plus the revisions.
    DocumentSpaceRepository m_spaces;
    DocumentVesselSpaces    m_vesselSpaces;
    DocumentPageRepository  m_pages;
    DocumentPageMoves       m_moves;
    DocumentPageRemovals    m_removals;
    DocumentRevisionStore   m_revisions;
    DocumentImageStore      m_images;
    DocumentPublisher       m_publisher;
};
