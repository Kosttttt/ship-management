#pragma once

#include "app/IModule.h"
#include "core/AppSettingRepository.h"
#include "core/VesselRepository.h"
#include "modules/certificates/CertificateAlertProvider.h"
#include "modules/certificates/data/CertificateRepository.h"
#include "modules/certificates/data/EndorsementRepository.h"

class InstallationContext;
class QSqlDatabase;

// The Certificate Control module — the first and, so far, only IModule.
//
// It owns its own repository, built from the database connection and the
// installation context it is handed. That is why ModuleRegistry can construct
// every module the same way without knowing what any of them needs.
class CertificatesModule : public IModule
{
public:
    CertificatesModule(QSqlDatabase& database, const InstallationContext& installation);

    QString id() const override;
    QString displayName() const override;
    QIcon   icon() const override;

    QWidget* createMainWidget(QWidget* parent) override;

    QList<Migration>      migrations() const override;
    QList<AlertProvider*> alertProviders() override;

private:
    const InstallationContext& m_installation;
    CertificateRepository      m_certificates;
    EndorsementRepository      m_endorsements;
    // Core-owned, but the module reads them: a module depending on core is
    // the direction dependencies are allowed to point (CLAUDE.md §4).
    AppSettingRepository m_appSettings;
    VesselRepository     m_vessels;

    // Declared last: it holds references to the four above, so they must
    // already be constructed when it is built.
    CertificateAlertProvider m_alertProvider;
};
