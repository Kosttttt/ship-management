#include "modules/certificates/CertificatesModule.h"

#include "modules/certificates/ui/CertificateListWidget.h"

#include <QCoreApplication>

CertificatesModule::CertificatesModule(QSqlDatabase&              database,
                                       const InstallationContext& installation)
    : m_installation(installation)
    , m_certificates(database, installation)
    , m_endorsements(database, installation)
    , m_appSettings(database)
    , m_vessels(database, installation)
    , m_alertProvider(m_vessels, m_certificates, m_endorsements, m_appSettings, installation)
{
}

QString CertificatesModule::id() const
{
    return QStringLiteral("certificates");
}

QString CertificatesModule::displayName() const
{
    return QCoreApplication::translate("CertificatesModule", "Certificates");
}

QIcon CertificatesModule::icon() const
{
    // Blank for now: there are no icon assets in resources/ yet. A cosmetic
    // gap, not a blocking one (certificate-crud-spec §3).
    return QIcon();
}

QWidget* CertificatesModule::createMainWidget(QWidget* parent)
{
    return new CertificateListWidget(m_certificates, m_endorsements, m_appSettings,
                                     m_installation, parent);
}

QList<Migration> CertificatesModule::migrations() const
{
    // Deliberately empty. migrations/ stays one flat, globally numbered folder
    // (certificate-crud-spec §3) — 003_create_certificate.sql is listed in
    // CMakeLists.txt exactly the way 001 and 002 are.
    return {};
}

QList<AlertProvider*> CertificatesModule::alertProviders()
{
    // Non-owning: this module owns the provider for its own lifetime, the
    // same way it owns its repositories (alerts-spec.md §3).
    return {&m_alertProvider};
}
