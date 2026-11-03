#pragma once

#include "core/AppSettingRepository.h"
#include "core/InstallationContext.h"
#include "core/ManagedFileStore.h"
#include "core/MigrationRunner.h"
#include "core/VesselAttachmentRoots.h"
#include "core/VesselRepository.h"
#include "modules/certificates/data/CertificateAttachmentRepository.h"
#include "modules/certificates/data/CertificateAttachmentStorage.h"
#include "modules/certificates/data/CertificateExtensionRepository.h"
#include "modules/certificates/data/CertificateRepository.h"
#include "modules/certificates/data/EndorsementRepository.h"
#include "modules/certificates/domain/Certificate.h"
#include "modules/certificates/service/CertificateAttachmentService.h"
#include "modules/certificates/service/CertificateCsvExporter.h"
#include "modules/certificates/service/CertificateCsvImporter.h"
#include "modules/certificates/service/CertificateReportBuilder.h"

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

// Shared fixture for the certificate test suites, mirroring
// tests/core/VesselTestSupport.h.
namespace CertificateTestSupport {

inline bool openDatabase(const QString& connectionName)
{
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
    db.setDatabaseName(QStringLiteral(":memory:"));
    if (!db.open()) {
        qWarning() << "could not open in-memory database";
        return false;
    }

    QSqlQuery pragma(db);
    if (!pragma.exec(QStringLiteral("PRAGMA foreign_keys = ON"))) {
        return false;
    }

    MigrationRunner runner(db, QStringLiteral(":/migrations"));
    if (!runner.run()) {
        qWarning() << "migrations failed:" << runner.errorString();
        return false;
    }
    return true;
}

inline void closeDatabase(const QString& connectionName)
{
    {
        QSqlDatabase db = QSqlDatabase::database(connectionName);
        if (db.isOpen()) {
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(connectionName);
}

inline InstallationContext officeContext()
{
    InstallationRecord record;
    record.id     = QStringLiteral("install-office");
    record.mode   = InstallationMode::Office;
    record.nodeId = QStringLiteral("OFFICE");
    return InstallationContext(record);
}

inline InstallationContext vesselContext(const QString& vesselId)
{
    InstallationRecord record;
    record.id              = QStringLiteral("install-vessel");
    record.mode            = InstallationMode::Vessel;
    record.nodeId          = QStringLiteral("VESSEL-9074729");
    record.vesselId        = vesselId;
    record.vesselImoNumber = QStringLiteral("9074729");
    return InstallationContext(record);
}


// A vessel row, bypassing the repository for the same reason
// seedCertificate() below does. The import and export suites need a real ship
// with a real IMO number, because the IMO is what the file is sanity-checked
// against (certificate-import-export-spec §7).
inline QString seedVessel(QSqlDatabase&  db,
                          const QString& id,
                          const QString& name,
                          const QString& imoNumber)
{
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "INSERT INTO vessel (id, name, imo_number, created_at, created_by, updated_at,"
        " updated_by, is_deleted, origin_node, revision)"
        " VALUES (?, ?, ?, '2026-01-01T00:00:00Z', 'SEED', '2026-01-01T00:00:00Z', 'SEED',"
        " 0, 'OFFICE', 1)"));
    query.addBindValue(id);
    query.addBindValue(name);
    query.addBindValue(imoNumber);

    if (!query.exec()) {
        qWarning() << "vessel seed failed:" << query.lastError().text();
        return QString();
    }
    return id;
}

// A valid certificate, ready to have one field spoiled by a test.
inline Certificate sampleCertificate(const QString& vesselId, const QString& name)
{
    Certificate certificate;
    certificate.vesselId          = vesselId;
    certificate.name              = name;
    certificate.category          = CertificateCategory::Statutory;
    certificate.certificateNumber = QStringLiteral("SC-12345");
    certificate.issueDate         = QDate(2026, 2, 4);
    certificate.expiryDate        = QDate(2031, 2, 4);
    certificate.issuedBy          = QStringLiteral("Lloyd's Register");
    certificate.placeOfIssue      = QStringLiteral("Limassol");
    certificate.requiresAnnualSurvey = true;
    return certificate;
}

// Writes a certificate row directly, bypassing the repository — used to
// simulate rows a future sync might bring in, which the scope filter must
// still hide.
inline QString seedCertificate(QSqlDatabase&  db,
                               const QString& id,
                               const QString& vesselId,
                               const QString& name,
                               bool           deleted    = false,
                               const QString& listNumber = QString())
{
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "INSERT INTO certificate (id, vessel_id, name, category, issue_date, expiry_date,"
        " list_number,"
        " created_at, created_by, updated_at, updated_by, is_deleted, origin_node, revision)"
        " VALUES (?, ?, ?, 'STATUTORY', '2026-02-04', '2031-02-04', ?,"
        " '2026-01-01T00:00:00Z', 'SEED', '2026-01-01T00:00:00Z', 'SEED', ?, 'OFFICE', 1)"));
    query.addBindValue(id);
    query.addBindValue(vesselId);
    query.addBindValue(name);
    query.addBindValue(listNumber.isEmpty() ? QVariant(QMetaType(QMetaType::QString))
                                            : QVariant(listNumber));
    query.addBindValue(deleted ? 1 : 0);

    if (!query.exec()) {
        qWarning() << "seed failed:" << query.lastError().text();
        return QString();
    }
    return id;
}

// Seeds a certificate with dates and survey rules chosen by the caller, for
// the tests that need a specific computed status rather than a generic row.
// An invalid expiryDate seeds SQL NULL — "never expires".
inline QString seedCertificateWithSchedule(QSqlDatabase&  db,
                                           const QString& id,
                                           const QString& vesselId,
                                           const QString& name,
                                           const QString& listNumber,
                                           const QDate&   issueDate,
                                           const QDate&   expiryDate,
                                           bool           requiresAnnual,
                                           bool           requiresIntermediate = false)
{
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "INSERT INTO certificate (id, vessel_id, name, category, issue_date, expiry_date,"
        " list_number, requires_annual_survey, requires_intermediate_survey,"
        " created_at, created_by, updated_at, updated_by, is_deleted, origin_node, revision)"
        " VALUES (?, ?, ?, 'STATUTORY', ?, ?, ?, ?, ?,"
        " '2026-01-01T00:00:00Z', 'SEED', '2026-01-01T00:00:00Z', 'SEED', 0, 'OFFICE', 1)"));
    query.addBindValue(id);
    query.addBindValue(vesselId);
    query.addBindValue(name);
    query.addBindValue(issueDate.toString(Qt::ISODate));
    query.addBindValue(expiryDate.isValid() ? QVariant(expiryDate.toString(Qt::ISODate))
                                            : QVariant(QMetaType(QMetaType::QString)));
    query.addBindValue(listNumber.isEmpty() ? QVariant(QMetaType(QMetaType::QString))
                                            : QVariant(listNumber));
    query.addBindValue(requiresAnnual ? 1 : 0);
    query.addBindValue(requiresIntermediate ? 1 : 0);

    if (!query.exec()) {
        qWarning() << "seed failed:" << query.lastError().text();
        return QString();
    }
    return id;
}

inline QString seedEndorsement(QSqlDatabase&  db,
                               const QString& id,
                               const QString& certificateId,
                               const QString& surveyTypeCode,
                               const QDate&   date)
{
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "INSERT INTO endorsement (id, certificate_id, survey_type, endorsement_date,"
        " created_at, created_by, updated_at, updated_by, is_deleted, origin_node, revision)"
        " VALUES (?, ?, ?, ?,"
        " '2026-01-01T00:00:00Z', 'SEED', '2026-01-01T00:00:00Z', 'SEED', 0, 'OFFICE', 1)"));
    query.addBindValue(id);
    query.addBindValue(certificateId);
    query.addBindValue(surveyTypeCode);
    query.addBindValue(date.toString(Qt::ISODate));

    if (!query.exec()) {
        qWarning() << "endorsement seed failed:" << query.lastError().text();
        return QString();
    }
    return id;
}

// The attachment collaborators a CertificateListWidget needs, bundled
// together — and, more importantly, pointed at a throwaway folder.
//
// The second part is not convenience. ManagedFileStore::rootPath() resolves to
// the real per-user data folder by default, and CLAUDE.md §10.1 exists because
// that folder was once destroyed by a command that looked correct. A test that
// wrote a file there would be doing the same class of thing by accident, so
// every test holding one of these writes into a QTemporaryDir that disappears
// with it.
//
// The override is also what keeps these suites free of the addendum's
// per-vessel folders: it is consulted ahead of any vessel lookup
// (certificate-attachment-addendum-spec §2.4), so a test may seed a
// certificate against a vessel id that has no vessel row at all and still
// attach to it. The per-vessel resolution and its refusal are exercised
// deliberately, with the override cleared, in tst_VesselAttachmentRoots and
// tst_CertificateAttachmentFolders.
class AttachmentFixture
{
public:
    AttachmentFixture(QSqlDatabase& db, const InstallationContext& installation)
        : context(installation)
        , vessels(db, context)
        , roots(vessels)
        , endorsements(db, context)
        , extensions(db, context)
        , appSettings(db)
        , certificates(db, context)
        , csvExporter(certificates, endorsements, vessels)
        , csvImporter(db, certificates, endorsements, vessels)
        , reportBuilder(certificates, endorsements, extensions, appSettings, vessels)
        , repository(db, context)
        , service(repository, storage, roots, endorsements, extensions, appSettings)
    {
        QVERIFY2(temporaryRoot.isValid(), "could not create a temporary managed data folder");
        ManagedFileStore::setRootPathOverride(temporaryRoot.path());
    }

    ~AttachmentFixture()
    {
        // Cleared, not left pointing at a folder that is about to vanish: the
        // override is process-wide, so the next test would inherit it.
        ManagedFileStore::setRootPathOverride(QString());
    }

    Q_DISABLE_COPY(AttachmentFixture)

    QString rootPath() const { return temporaryRoot.path(); }

    QTemporaryDir temporaryRoot;

    // One named copy of the installation context, shared by every repository
    // below.
    //
    // This is a convenience, not a correctness requirement. It was once the
    // latter: repositories used to store the context as a
    // `const InstallationContext&`, so passing a temporary — which
    // `AttachmentFixture f(db, officeContext());` does — left them holding a
    // dangling reference. Repositories now hold their context by value
    // (installation-context-storage-fix.md), so that whole hazard is gone and
    // no caller has to think about the lifetime of what it passes in.
    //
    // Kept because one named value is clearer than handing `installation` to
    // five constructors in turn, and because it makes it obvious that all five
    // are scoped identically.
    InstallationContext context;

    // Declared in dependency order: the folder resolution is built from the
    // vessel repository, and the service from the resolution, and C++
    // initialises members in the order they are declared.
    VesselRepository      vessels;
    VesselAttachmentRoots roots;

    // The service reads these three only to work out which date an attached
    // file's name should carry (certificate-attachment-addendum-2-spec §7):
    // the certificate's expiry, or a nearer survey window, or an active
    // extension's own expiry. Bundled here so that every suite holding a
    // fixture gets the same answer the list screen would show.
    EndorsementRepository          endorsements;
    CertificateExtensionRepository extensions;
    AppSettingRepository           appSettings;

    // Step 13's bulk I/O, so every suite that builds a CertificateListWidget
    // has the three services its constructor now asks for
    // (certificate-import-export-spec §11). They need a CertificateRepository
    // of their own, which is why one appears here even though most suites
    // also make one locally: a repository holds no state beyond its last
    // error message, so a second one on the same connection costs nothing.
    CertificateRepository    certificates;
    CertificateCsvExporter   csvExporter;
    CertificateCsvImporter   csvImporter;
    CertificateReportBuilder reportBuilder;

    CertificateAttachmentRepository repository;
    CertificateAttachmentStorage    storage;
    CertificateAttachmentService    service;
};

inline int rowCountOf(QSqlDatabase& db, const QString& tableName)
{
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("SELECT COUNT(*) FROM %1").arg(tableName)) || !query.next()) {
        return -1;
    }
    return query.value(0).toInt();
}

} // namespace CertificateTestSupport
