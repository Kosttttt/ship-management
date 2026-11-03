#include "CertificateTestSupport.h"

#include "core/AppSettingRepository.h"
#include "core/ManagedFileStore.h"
#include "core/VesselAttachmentRoots.h"
#include "core/VesselRepository.h"
#include "modules/certificates/data/CertificateAttachmentRepository.h"
#include "modules/certificates/data/CertificateAttachmentStorage.h"
#include "modules/certificates/data/CertificateExtensionRepository.h"
#include "modules/certificates/data/CertificateRepository.h"
#include "modules/certificates/data/EndorsementRepository.h"
#include "modules/certificates/service/CertificateAttachmentService.h"
#include "modules/certificates/ui/CertificateEditDialog.h"
#include "modules/certificates/ui/CertificateListWidget.h"
#include "modules/certificates/ui/DateItem.h"

#include <QDir>
#include <QFile>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QtTest>

#include <optional>

using namespace CertificateTestSupport;

// certificate-attachment-addendum-spec §7: the cases that involve the
// certificates module rather than core on its own.
//
// Unlike tst_CertificateAttachment, this suite runs with
// ManagedFileStore's override *cleared*, so every attachment has to go through
// the vessel's own configured folder — which is the whole thing under test
// (§2.2, §2.4). Every folder a vessel is pointed at here is a QTemporaryDir, so
// the real per-user data folder is never written to (CLAUDE.md §10.1).
class TestCertificateAttachmentFolders : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    // §7 case 1 — refused everywhere, because it is refused in the one place
    // all four screens go through.
    void attachingToAVesselWithNoFolderIsRefusedWithAFriendlyMessage();
    void anExtensionDocumentIsRefusedForTheSameReason();
    void nothingIsWrittenAnywhereWhenItIsRefused();

    // §7 cases 6 and 7 — the Add/Edit dialog's own control.
    void theAttachmentSectionIsOfferedOnAddEditAndRenewal();   // §3
    void theEmptyWordingDiffersBetweenAddingAndEditing();      // §3
    void attachingToAJustCreatedCertificateUsesItsGeneratedId(); // case 6
    void anOrdinaryEditNamesTheFileFromTheExistingExpiry();    // case 7

    // §7 cases 3, 8 and 9 — what the list shows.
    void theDocumentColumnMarksOnlyTheRowsThatHaveOne();       // case 8
    void clickingAnEmptyDocumentCellDoesNothing();             // case 8
    void theIssueDateColumnSortsChronologically();             // case 9
    void movingTheVesselsFolderLeavesTheStoredPathAlone();     // case 3

private:
    QSqlDatabase database() { return QSqlDatabase::database(m_connectionName); }

    // A vessel row with a real folder of its own, which is what everything
    // here needs before it can attach anything.
    QString configuredVessel(VesselAttachmentRoots& roots, const QString& imoNumber,
                             QTemporaryDir& folder)
    {
        QSqlDatabase db = database();

        QSqlQuery query(db);
        query.prepare(QStringLiteral(
            "INSERT INTO vessel (id, name, imo_number, created_at, created_by, updated_at,"
            " updated_by, is_deleted, origin_node, revision)"
            " VALUES (?, ?, ?, '2026-01-01T00:00:00Z', 'SEED', '2026-01-01T00:00:00Z', 'SEED',"
            " 0, 'OFFICE', 1)"));
        const QString id = QStringLiteral("vessel-%1").arg(imoNumber);
        query.addBindValue(id);
        query.addBindValue(QStringLiteral("MV %1").arg(imoNumber));
        query.addBindValue(imoNumber);
        if (!query.exec()) {
            qWarning() << "vessel seed failed:" << query.lastError().text();
            return QString();
        }

        if (!folder.isValid()) {
            return QString();
        }
        QString error;
        if (!roots.changeRoot(id, folder.path(), &error)) {
            qWarning() << "could not configure the vessel folder:" << error;
            return QString();
        }
        return id;
    }

    // A vessel row with no folder at all (§2.2).
    QString unconfiguredVessel(const QString& imoNumber)
    {
        QSqlDatabase db = database();

        QSqlQuery query(db);
        query.prepare(QStringLiteral(
            "INSERT INTO vessel (id, name, imo_number, created_at, created_by, updated_at,"
            " updated_by, is_deleted, origin_node, revision)"
            " VALUES (?, ?, ?, '2026-01-01T00:00:00Z', 'SEED', '2026-01-01T00:00:00Z', 'SEED',"
            " 0, 'OFFICE', 1)"));
        const QString id = QStringLiteral("vessel-%1").arg(imoNumber);
        query.addBindValue(id);
        query.addBindValue(QStringLiteral("MV %1").arg(imoNumber));
        query.addBindValue(imoNumber);
        if (!query.exec()) {
            qWarning() << "vessel seed failed:" << query.lastError().text();
            return QString();
        }
        return id;
    }

    QString sourceFile(const QString& name, const QString& contents)
    {
        const QString path = QDir(m_source.path()).filePath(name);
        QFile         file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            return QString();
        }
        file.write(contents.toUtf8());
        return path;
    }

    static QString activePathOf(QSqlDatabase& db, const QString& certificateId)
    {
        QSqlQuery query(db);
        query.prepare(QStringLiteral("SELECT relative_path FROM certificate_attachment"
                                     " WHERE certificate_id = ? AND is_deleted = 0"));
        query.addBindValue(certificateId);
        if (!query.exec() || !query.next()) {
            return QString();
        }
        return query.value(0).toString();
    }

    QString       m_connectionName;
    QTemporaryDir m_source;
};

void TestCertificateAttachmentFolders::init()
{
    m_connectionName = QStringLiteral("attfolders_%1").arg(QTest::currentTestFunction());
    QVERIFY(openDatabase(m_connectionName));
    QVERIFY(m_source.isValid());

    // The whole point of this suite: no override, so the vessel's own folder
    // is the only thing that can answer "where do these files go?"
    ManagedFileStore::setRootPathOverride(QString());
}

void TestCertificateAttachmentFolders::cleanup()
{
    ManagedFileStore::setRootPathOverride(QString());
    closeDatabase(m_connectionName);
}

void TestCertificateAttachmentFolders::attachingToAVesselWithNoFolderIsRefusedWithAFriendlyMessage()
{
    QSqlDatabase                 db      = database();
    const InstallationContext    context = officeContext();
    VesselRepository             vessels(db, context);
    VesselAttachmentRoots        roots(vessels);
    CertificateAttachmentRepository attachments(db, context);
    CertificateAttachmentStorage    storage;
    EndorsementRepository           endorsementRows(db, context);
    CertificateExtensionRepository  extensionRows(db, context);
    AppSettingRepository            settings(db);
    CertificateAttachmentService    service(attachments, storage, roots, endorsementRows,
                                            extensionRows, settings);

    const QString vesselId = unconfiguredVessel(QStringLiteral("9074729"));
    QVERIFY(!vesselId.isEmpty());
    seedCertificate(db, QStringLiteral("c1"), vesselId, QStringLiteral("Load Line"));

    Certificate certificate = sampleCertificate(vesselId, QStringLiteral("Load Line"));
    certificate.id = QStringLiteral("c1");

    const QString source = sourceFile(QStringLiteral("scan.pdf"), QStringLiteral("page one"));
    QVERIFY(!source.isEmpty());

    QVERIFY2(!service.replaceCurrent(certificate, source, false),
             "a vessel with no folder must not have anything attached to it");

    // The same message, from the same place, which is what makes §7 case 1
    // true for all four screens at once: every one of them shows
    // service.errorString() and nothing of its own.
    QVERIFY2(service.errorString().contains(QStringLiteral("Settings")),
             qPrintable(service.errorString()));
}

void TestCertificateAttachmentFolders::anExtensionDocumentIsRefusedForTheSameReason()
{
    QSqlDatabase                 db      = database();
    const InstallationContext    context = officeContext();
    VesselRepository             vessels(db, context);
    VesselAttachmentRoots        roots(vessels);
    CertificateAttachmentRepository attachments(db, context);
    CertificateAttachmentStorage    storage;
    EndorsementRepository           endorsementRows(db, context);
    CertificateExtensionRepository  extensionRows(db, context);
    AppSettingRepository            settings(db);
    CertificateAttachmentService    service(attachments, storage, roots, endorsementRows,
                                            extensionRows, settings);

    const QString vesselId = unconfiguredVessel(QStringLiteral("9074729"));
    seedCertificate(db, QStringLiteral("c1"), vesselId, QStringLiteral("Load Line"));

    Certificate certificate = sampleCertificate(vesselId, QStringLiteral("Load Line"));
    certificate.id = QStringLiteral("c1");

    QString relative;
    QVERIFY(!service.addExtensionDocument(
        certificate, QDate(2027, 3, 1),
        sourceFile(QStringLiteral("short.pdf"), QStringLiteral("short term")), false, &relative));
    QVERIFY(relative.isEmpty());
    QVERIFY(service.errorString().contains(QStringLiteral("Settings")));
}

void TestCertificateAttachmentFolders::nothingIsWrittenAnywhereWhenItIsRefused()
{
    QSqlDatabase                 db      = database();
    const InstallationContext    context = officeContext();
    VesselRepository             vessels(db, context);
    VesselAttachmentRoots        roots(vessels);
    CertificateAttachmentRepository attachments(db, context);
    CertificateAttachmentStorage    storage;
    EndorsementRepository           endorsementRows(db, context);
    CertificateExtensionRepository  extensionRows(db, context);
    AppSettingRepository            settings(db);
    CertificateAttachmentService    service(attachments, storage, roots, endorsementRows,
                                            extensionRows, settings);

    const QString vesselId = unconfiguredVessel(QStringLiteral("9074729"));
    seedCertificate(db, QStringLiteral("c1"), vesselId, QStringLiteral("Load Line"));

    Certificate certificate = sampleCertificate(vesselId, QStringLiteral("Load Line"));
    certificate.id = QStringLiteral("c1");

    QVERIFY(!service.replaceCurrent(
        certificate,
        sourceFile(QStringLiteral("scan.pdf"), QStringLiteral("page one")), false));

    // The refusal happens before anything is read or copied, so there is no
    // row and no orphaned file to clean up afterwards.
    QCOMPARE(rowCountOf(db, QStringLiteral("certificate_attachment")), 0);
}

void TestCertificateAttachmentFolders::theAttachmentSectionIsOfferedOnAddEditAndRenewal()
{
    QSqlDatabase              db      = database();
    const InstallationContext context = officeContext();
    CertificateRepository     certificates(db, context);
    EndorsementRepository     endorsements(db, context);

    AttachmentFixture attachments(db, context);

    seedCertificate(db, QStringLiteral("c1"), QStringLiteral("v1"),
                    QStringLiteral("Load Line"));

    Certificate existing = sampleCertificate(QStringLiteral("v1"), QStringLiteral("Load Line"));
    existing.id          = QStringLiteral("c1");

    // §3: all three, where step 12 offered it on renewal alone.
    CertificateEditDialog adding(certificates, endorsements, attachments.service,
                                 QStringLiteral("v1"), std::nullopt, /*isRenewal=*/false);
    QVERIFY(adding.findChild<QGroupBox*>(QStringLiteral("attachmentSection")) != nullptr);

    CertificateEditDialog editing(certificates, endorsements, attachments.service,
                                 QStringLiteral("v1"), existing, /*isRenewal=*/false);
    QVERIFY(editing.findChild<QGroupBox*>(QStringLiteral("attachmentSection")) != nullptr);

    CertificateEditDialog renewing(certificates, endorsements, attachments.service,
                                   QStringLiteral("v1"), existing, /*isRenewal=*/true);
    QVERIFY(renewing.findChild<QGroupBox*>(QStringLiteral("attachmentSection")) != nullptr);
}

void TestCertificateAttachmentFolders::theEmptyWordingDiffersBetweenAddingAndEditing()
{
    QSqlDatabase              db      = database();
    const InstallationContext context = officeContext();
    CertificateRepository     certificates(db, context);
    EndorsementRepository     endorsements(db, context);

    AttachmentFixture attachments(db, context);

    seedCertificate(db, QStringLiteral("c1"), QStringLiteral("v1"),
                    QStringLiteral("Load Line"));
    Certificate existing = sampleCertificate(QStringLiteral("v1"), QStringLiteral("Load Line"));
    existing.id          = QStringLiteral("c1");

    const QString labelName = QStringLiteral("attachmentSourceLabel");

    // Adding: there is no document, and saying one "stays as it is" would be a
    // small lie (§3).
    CertificateEditDialog adding(certificates, endorsements, attachments.service,
                                 QStringLiteral("v1"), std::nullopt, /*isRenewal=*/false);
    QLabel* addingLabel = adding.findChild<QLabel*>(labelName);
    QVERIFY(addingLabel != nullptr);
    QVERIFY2(addingLabel->text().contains(QStringLiteral("No document is attached")),
             qPrintable(addingLabel->text()));

    // Editing: there may well be one, and doing nothing must obviously keep it.
    CertificateEditDialog editing(certificates, endorsements, attachments.service,
                                 QStringLiteral("v1"), existing, /*isRenewal=*/false);
    QLabel* editingLabel = editing.findChild<QLabel*>(labelName);
    QVERIFY(editingLabel != nullptr);
    QVERIFY2(editingLabel->text().contains(QStringLiteral("stays as it is")),
             qPrintable(editingLabel->text()));
}

// §7 case 6. The dialog's own field cannot be filled from a test without
// answering a native file dialog, so what is asserted here is the sequence the
// dialog performs: create the row, take the id the repository generated, and
// only then name a file after it.
void TestCertificateAttachmentFolders::attachingToAJustCreatedCertificateUsesItsGeneratedId()
{
    QSqlDatabase                 db      = database();
    const InstallationContext    context = officeContext();
    VesselRepository             vessels(db, context);
    VesselAttachmentRoots        roots(vessels);
    CertificateRepository        certificates(db, context);
    CertificateAttachmentRepository attachments(db, context);
    CertificateAttachmentStorage    storage;
    EndorsementRepository           endorsementRows(db, context);
    CertificateExtensionRepository  extensionRows(db, context);
    AppSettingRepository            settings(db);
    CertificateAttachmentService    service(attachments, storage, roots, endorsementRows,
                                            extensionRows, settings);

    QTemporaryDir folder;
    const QString vesselId = configuredVessel(roots, QStringLiteral("9074729"), folder);
    QVERIFY(!vesselId.isEmpty());

    Certificate certificate  = sampleCertificate(vesselId, QStringLiteral("Load Line"));
    certificate.listNumber   = QStringLiteral("15D");
    // No survey requirement, so addendum 2 §7 has nothing nearer than the
    // expiry date to name the file after. The naming rule itself is exercised
    // in tst_CertificateAttachmentNaming; this test is about the generated id.
    certificate.requiresAnnualSurvey = false;
    certificate.expiryDate   = QDate(2031, 2, 4);

    QString createdId;
    QVERIFY2(certificates.create(certificate, &createdId),
             qPrintable(certificates.errorString()));
    QVERIFY(!createdId.isEmpty());

    certificate.id = createdId;

    QVERIFY2(service.replaceCurrent(
                 certificate,
                 sourceFile(QStringLiteral("scan.pdf"), QStringLiteral("page one")), false),
             qPrintable(service.errorString()));

    // Named from the certificate's own number, name and expiry, and filed
    // under the id that did not exist a moment ago.
    const QString stored = activePathOf(db, createdId);
    QCOMPARE(stored, QStringLiteral("15D_Load_Line_2031-02-04.pdf"));
    QVERIFY(QFileInfo::exists(QDir(folder.path()).filePath(stored)));
}

// §7 case 7: an ordinary edit is not a renewal, so the name carries the dates
// the certificate already has rather than a new expiry. With no survey
// requirement, addendum 2 §7 leaves that as the expiry date itself; which date
// wins when a survey window is nearer is tst_CertificateAttachmentNaming's
// subject.
void TestCertificateAttachmentFolders::anOrdinaryEditNamesTheFileFromTheExistingExpiry()
{
    QSqlDatabase                 db      = database();
    const InstallationContext    context = officeContext();
    VesselRepository             vessels(db, context);
    VesselAttachmentRoots        roots(vessels);
    CertificateAttachmentRepository attachments(db, context);
    CertificateAttachmentStorage    storage;
    EndorsementRepository           endorsementRows(db, context);
    CertificateExtensionRepository  extensionRows(db, context);
    AppSettingRepository            settings(db);
    CertificateAttachmentService    service(attachments, storage, roots, endorsementRows,
                                            extensionRows, settings);

    QTemporaryDir folder;
    const QString vesselId = configuredVessel(roots, QStringLiteral("9074729"), folder);
    seedCertificate(db, QStringLiteral("c1"), vesselId, QStringLiteral("Load Line"));

    Certificate certificate = sampleCertificate(vesselId, QStringLiteral("Load Line"));
    certificate.id          = QStringLiteral("c1");
    certificate.listNumber  = QStringLiteral("15D");
    certificate.requiresAnnualSurvey = false;
    certificate.expiryDate  = QDate(2031, 2, 4);

    // Exactly what CertificateEditDialog passes outside renewal mode: the
    // saved row as it now stands.
    QVERIFY2(service.replaceCurrent(
                 certificate,
                 sourceFile(QStringLiteral("scan.pdf"), QStringLiteral("page one")), false),
             qPrintable(service.errorString()));

    QCOMPARE(activePathOf(db, QStringLiteral("c1")),
             QStringLiteral("15D_Load_Line_2031-02-04.pdf"));
}

void TestCertificateAttachmentFolders::theDocumentColumnMarksOnlyTheRowsThatHaveOne()
{
    QSqlDatabase              db      = database();
    const InstallationContext context = officeContext();
    CertificateRepository     certificates(db, context);
    EndorsementRepository     endorsements(db, context);
    CertificateExtensionRepository extensions(db, context);
    AppSettingRepository           appSettings(db);

    // The override is in force through the fixture, which is what lets this
    // test seed certificates against a bare vessel id.
    AttachmentFixture attachments(db, context);

    seedCertificateWithSchedule(db, QStringLiteral("c1"), QStringLiteral("v1"),
                                QStringLiteral("With Document"), QStringLiteral("1"),
                                QDate(2026, 2, 4), QDate(2031, 2, 4), false);
    seedCertificateWithSchedule(db, QStringLiteral("c2"), QStringLiteral("v1"),
                                QStringLiteral("Without"), QStringLiteral("2"),
                                QDate(2026, 2, 4), QDate(2031, 2, 4), false);

    Certificate withDocument = sampleCertificate(QStringLiteral("v1"),
                                                 QStringLiteral("With Document"));
    withDocument.id          = QStringLiteral("c1");
    QVERIFY(attachments.service.replaceCurrent(
        withDocument,
        sourceFile(QStringLiteral("scan.pdf"), QStringLiteral("page one")), false));

    CertificateListWidget widget(certificates, endorsements, extensions, attachments.service,
                                 appSettings, attachments.csvExporter,
                                 attachments.csvImporter, attachments.reportBuilder, context);
    widget.setVesselId(QStringLiteral("v1"));

    auto* table = widget.findChild<QTableWidget*>(QStringLiteral("certificateTable"));
    QVERIFY(table != nullptr);
    QCOMPARE(table->rowCount(), 2);

    const int document = table->columnCount() - 1;

    // §4: a mark on the row that has one, and an empty cell — not a dash, not
    // a filename — on the row that has not.
    QVERIFY2(!table->item(0, document)->text().isEmpty(),
             "the row with an attachment must be marked");
    QVERIFY2(table->item(1, document)->text().isEmpty(),
             "the row without one must show nothing at all");

    // The mark is a quiet indicator, so it carries its own explanation rather
    // than relying on anyone guessing that a cell is clickable.
    QVERIFY(!table->item(0, document)->toolTip().isEmpty());
    QVERIFY(table->item(1, document)->toolTip().isEmpty());
}

// §4: an empty cell does nothing when clicked — not even a message, because
// there is nothing to report. Only the empty case is driven here: clicking a
// marked cell would hand a real file to the desktop and open it.
void TestCertificateAttachmentFolders::clickingAnEmptyDocumentCellDoesNothing()
{
    QSqlDatabase              db      = database();
    const InstallationContext context = officeContext();
    CertificateRepository     certificates(db, context);
    EndorsementRepository     endorsements(db, context);
    CertificateExtensionRepository extensions(db, context);
    AppSettingRepository           appSettings(db);

    AttachmentFixture attachments(db, context);

    seedCertificate(db, QStringLiteral("c1"), QStringLiteral("v1"),
                    QStringLiteral("Without"));

    CertificateListWidget widget(certificates, endorsements, extensions, attachments.service,
                                 appSettings, attachments.csvExporter,
                                 attachments.csvImporter, attachments.reportBuilder, context);
    widget.setVesselId(QStringLiteral("v1"));

    auto* table = widget.findChild<QTableWidget*>(QStringLiteral("certificateTable"));
    QVERIFY(table != nullptr);
    QCOMPARE(table->rowCount(), 1);

    const int document = table->columnCount() - 1;

    // Nothing selected beforehand, and nothing selected afterwards: the column
    // reacts to which cell was clicked, not to a selection (§4).
    QVERIFY(table->selectedItems().isEmpty());

    emit table->cellClicked(0, document);

    // No dialog, no crash, and the row is still there. A QMessageBox here would
    // block the test, so reaching this line is the assertion.
    QCOMPARE(table->rowCount(), 1);
}

// §7 case 9: as a date, not as text. "02 Dec" before "11 Jan" is what text
// sorting gives, and it is wrong.
void TestCertificateAttachmentFolders::theIssueDateColumnSortsChronologically()
{
    QSqlDatabase              db      = database();
    const InstallationContext context = officeContext();
    CertificateRepository     certificates(db, context);
    EndorsementRepository     endorsements(db, context);
    CertificateExtensionRepository extensions(db, context);
    AppSettingRepository           appSettings(db);

    AttachmentFixture attachments(db, context);

    // Three dates whose text order and date order disagree on every pair.
    seedCertificateWithSchedule(db, QStringLiteral("c1"), QStringLiteral("v1"),
                                QStringLiteral("Third"), QStringLiteral("1"),
                                QDate(2026, 12, 2), QDate(2031, 1, 1), false);
    seedCertificateWithSchedule(db, QStringLiteral("c2"), QStringLiteral("v1"),
                                QStringLiteral("First"), QStringLiteral("2"),
                                QDate(2026, 1, 11), QDate(2031, 1, 1), false);
    seedCertificateWithSchedule(db, QStringLiteral("c3"), QStringLiteral("v1"),
                                QStringLiteral("Second"), QStringLiteral("3"),
                                QDate(2026, 3, 5), QDate(2031, 1, 1), false);

    CertificateListWidget widget(certificates, endorsements, extensions, attachments.service,
                                 appSettings, attachments.csvExporter,
                                 attachments.csvImporter, attachments.reportBuilder, context);
    widget.setVesselId(QStringLiteral("v1"));

    auto* table = widget.findChild<QTableWidget*>(QStringLiteral("certificateTable"));
    QVERIFY(table != nullptr);

    const int issue = 2; // §5: between Name and Status

    // The column really does hold what §5 says it holds.
    QCOMPARE(table->horizontalHeaderItem(issue)->text(), QStringLiteral("Issue Date"));

    table->sortByColumn(issue, Qt::AscendingOrder);

    QStringList names;
    for (int row = 0; row < table->rowCount(); ++row) {
        names.append(table->item(row, 1)->text());
    }
    QCOMPARE(names, QStringList({QStringLiteral("First"), QStringLiteral("Second"),
                                 QStringLiteral("Third")}));

    table->sortByColumn(issue, Qt::DescendingOrder);
    QCOMPARE(table->item(0, 1)->text(), QStringLiteral("Third"));

    // And the cell still reads the way every other date on this screen does.
    QCOMPARE(table->item(0, issue)->text(),
             QDate(2026, 12, 2).toString(DateItem::kDateFormat));
}

// §7 case 3, end to end through the module: the file moves, the row does not.
void TestCertificateAttachmentFolders::movingTheVesselsFolderLeavesTheStoredPathAlone()
{
    QSqlDatabase                 db      = database();
    const InstallationContext    context = officeContext();
    VesselRepository             vessels(db, context);
    VesselAttachmentRoots        roots(vessels);
    CertificateAttachmentRepository attachments(db, context);
    CertificateAttachmentStorage    storage;
    EndorsementRepository           endorsementRows(db, context);
    CertificateExtensionRepository  extensionRows(db, context);
    AppSettingRepository            settings(db);
    CertificateAttachmentService    service(attachments, storage, roots, endorsementRows,
                                            extensionRows, settings);

    QTemporaryDir first;
    const QString vesselId = configuredVessel(roots, QStringLiteral("9074729"), first);
    QVERIFY(!vesselId.isEmpty());
    seedCertificate(db, QStringLiteral("c1"), vesselId, QStringLiteral("Load Line"));

    Certificate certificate = sampleCertificate(vesselId, QStringLiteral("Load Line"));
    certificate.id          = QStringLiteral("c1");

    QVERIFY2(service.replaceCurrent(
                 certificate,
                 sourceFile(QStringLiteral("scan.pdf"), QStringLiteral("page one")), false),
             qPrintable(service.errorString()));

    const QString storedBefore = activePathOf(db, QStringLiteral("c1"));
    QVERIFY(!storedBefore.isEmpty());

    QTemporaryDir second;
    QVERIFY(second.isValid());
    QString error;
    QVERIFY2(roots.changeRoot(vesselId, second.path(), &error), qPrintable(error));

    // The row is untouched...
    QCOMPARE(activePathOf(db, QStringLiteral("c1")), storedBefore);

    // ...and it now points, unchanged, at the file in its new home.
    const QString opened = service.currentAbsolutePath(vesselId, QStringLiteral("c1"));
    QCOMPARE(opened, QDir(QFileInfo(second.path()).absoluteFilePath()).filePath(storedBefore));
    QVERIFY(QFileInfo::exists(opened));
}

QTEST_MAIN(TestCertificateAttachmentFolders)
#include "tst_CertificateAttachmentFolders.moc"
