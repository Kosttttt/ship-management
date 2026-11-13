#include "modules/documents/DocumentsModule.h"

#include "modules/documents/ui/DocumentsWidget.h"

#include <QCoreApplication>
#include <QDebug>

DocumentsModule::DocumentsModule(QSqlDatabase& database, const InstallationContext& installation)
    : m_installation(installation)
    , m_vessels(database, installation)
    , m_spaces(database, installation, m_vessels)
    , m_vesselSpaces(database, installation, m_vessels)
    , m_pages(database, m_spaces)
    , m_moves(database, m_pages)
    , m_removals(database, m_pages)
    , m_revisions(database, installation.nodeId())
    , m_images(database, installation.nodeId())
    , m_publisher(database, m_spaces, m_pages, m_removals, m_revisions)
{
}

QString DocumentsModule::id() const
{
    return QStringLiteral("documents");
}

QString DocumentsModule::displayName() const
{
    return QCoreApplication::translate("DocumentsModule", "Documents");
}

QIcon DocumentsModule::icon() const
{
    // Blank, like the certificates module's: there are no icon assets in
    // resources/ yet. A cosmetic gap, not a blocking one.
    return QIcon();
}

QWidget* DocumentsModule::createMainWidget(QWidget* parent)
{
    // §4.3: one space per active vessel, made the first time this screen is
    // built rather than at start-up, so a program that never opens Documents
    // never writes a row — and idempotent, so opening it again changes
    // nothing.
    int created = 0;
    if (!m_vesselSpaces.ensureAll(&created)) {
        // Not fatal: the company spaces are still readable, and the vessel
        // spaces will be made the next time this screen opens.
        qWarning() << "Could not create the vessel spaces:" << m_vesselSpaces.errorString();
    }

    return new DocumentsWidget(m_spaces, m_pages, m_moves, m_removals, m_revisions, m_publisher,
                               m_images, parent);
}

QList<Migration> DocumentsModule::migrations() const
{
    // Deliberately empty, the same call the certificates module makes:
    // migrations/ stays one flat, globally numbered folder, and 013 and 014
    // are listed in CMakeLists.txt exactly as 001 is.
    return {};
}

QList<AlertProvider*> DocumentsModule::alertProviders()
{
    // Nothing to alert about: this module has no dates that fall due (§1 of
    // its spec — no expiry or review tracking). A later step may add
    // something; step 1 does not.
    return {};
}
