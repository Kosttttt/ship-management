#pragma once

#include "app/IModule.h"
#include "core/AppSettingRepository.h"
#include "core/InstallationContext.h"
#include "core/VesselAttachmentRoots.h"
#include "core/VesselRepository.h"
#include "modules/certificates/CertificateAlertProvider.h"
#include "modules/certificates/data/CertificateAttachmentRepository.h"
#include "modules/certificates/data/CertificateAttachmentStorage.h"
#include "modules/certificates/data/CertificateExtensionRepository.h"
#include "modules/certificates/data/CertificateRepository.h"
#include "modules/certificates/data/EndorsementRepository.h"
#include "modules/certificates/service/CertificateAttachmentService.h"
#include "modules/certificates/service/CertificateCsvExporter.h"
#include "modules/certificates/service/CertificateCsvImporter.h"
#include "modules/certificates/service/CertificateReportBuilder.h"

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
    // By value, never by reference — see InstallationContext.h. Still declared
    // first, because the repositories below are constructed from it and C++
    // initialises members in declaration order.
    InstallationContext            m_installation;
    CertificateRepository          m_certificates;
    EndorsementRepository          m_endorsements;
    CertificateExtensionRepository m_extensions;

    // Core-owned, but the module reads them: a module depending on core is
    // the direction dependencies are allowed to point (CLAUDE.md §4).
    //
    // Declared before the attachment members below, not after, because the
    // folder resolution is built from the vessel repository and C++ initialises
    // members in declaration order.
    AppSettingRepository m_appSettings;
    VesselRepository     m_vessels;

    // Where each vessel keeps its own documents
    // (certificate-attachment-addendum-spec §2). Built here from this module's
    // own VesselRepository, the same way every other repository this module
    // needs is: ModuleRegistry still constructs every module from nothing but a
    // database connection and an installation context, which is what "adding a
    // module requires zero changes to existing modules" has to mean in
    // practice (CLAUDE.md §7).
    VesselAttachmentRoots m_attachmentRoots;

    // The attachment trio, in the order they depend on each other: the
    // repository knows the rows, the storage knows the files, and the service
    // is the only thing that knows what order to use them in and which folder
    // they belong in (certificate-attachment-spec §7). Only the service is
    // handed to the UI.
    CertificateAttachmentRepository m_attachments;
    CertificateAttachmentStorage    m_attachmentStorage;
    CertificateAttachmentService    m_attachmentService;

    // Step 13's bulk I/O (certificate-import-export-spec §11). Owned here,
    // alongside every other collaborator this module's screens need, so the
    // list widget is handed finished services and never holds a database
    // connection itself. Declared after the vessel repository and the
    // settings, which all three are built from.
    CertificateCsvExporter   m_csvExporter;
    CertificateCsvImporter   m_csvImporter;
    CertificateReportBuilder m_reportBuilder;

    // Declared last: it holds references to several of the above, so they must
    // already be constructed when it is built.
    CertificateAlertProvider m_alertProvider;
};
