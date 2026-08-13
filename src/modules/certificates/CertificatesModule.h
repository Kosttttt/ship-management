#pragma once

#include "app/IModule.h"
#include "modules/certificates/data/CertificateRepository.h"

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
};
