#include "modules/documents/DocumentsModule.h"

#include "modules/documents/ui/DocumentListWidget.h"

#include <QCoreApplication>

DocumentsModule::DocumentsModule(QSqlDatabase& database, const InstallationContext& installation)
    : m_installation(installation)
    , m_documents(database, installation)
    , m_vessels(database, installation)
    , m_files(m_vessels)
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
    m_screen = new DocumentListWidget(m_documents, m_files, m_vessels, m_installation, parent);

    // The screen belongs to its parent, so it can outlive nothing and this
    // module must not be left holding a dangling pointer when the window
    // closes. Qt's destroyed() signal is what makes that automatic.
    QObject::connect(m_screen, &QObject::destroyed, m_screen,
                     [this](QObject*) { m_screen = nullptr; });

    // A selection may already have been made — MainWindow fills its toolbar
    // after it builds the screens — so the screen opens on it rather than
    // waiting for the next change.
    m_screen->setVesselId(m_selectedVesselId);

    return m_screen;
}

QList<Migration> DocumentsModule::migrations() const
{
    // Deliberately empty, the same call the certificates module makes:
    // migrations/ stays one flat, globally numbered folder, and
    // 012_create_document.sql is listed in CMakeLists.txt exactly as 001 is.
    return {};
}

QList<AlertProvider*> DocumentsModule::alertProviders()
{
    // Nothing to alert about: this module has no dates that fall due (§1 of
    // its spec). A later step may add something; step 1 does not.
    return {};
}

void DocumentsModule::setSelectedVessel(const QString& vesselId)
{
    m_selectedVesselId = vesselId;

    if (m_screen != nullptr) {
        m_screen->setVesselId(vesselId);
    }
}
