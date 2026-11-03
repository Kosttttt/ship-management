#include "CertificateTestSupport.h"

#include "core/AppSettingRepository.h"
#include "modules/certificates/data/CertificateRepository.h"
#include "modules/certificates/service/CertificateDeletionService.h"
#include "modules/certificates/ui/CertificateListWidget.h"

#include <QAction>
#include <QDir>
#include <QFile>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QtTest>

#include <optional>

using namespace CertificateTestSupport;

// certificate-attachment-addendum-2-spec §4 and §5, and §9 cases 2, 3 and 4:
// deleting a certificate, and deleting just its attachment.
//
// The two actions differ in what they destroy and in nothing else about their
// mechanics, which is why they share a suite: both soft-delete rows, both
// delete real files rather than archiving them, and both leave the rows behind
// for a future sync to carry (CLAUDE.md §6.4).
class TestCertificateDeletion : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    // §5 and §9 case 2.
    void deletingAnAttachmentRemovesTheFileWithNoArchiveCopy();
    void deletingAnAttachmentRetiresTheRowButKeepsIt();
    void deletingAnAttachmentOnACertificateWithNoneIsHarmless();
    void theDocumentColumnAndTheOpenEntryBothGoBackToEmpty();

    // §4 and §9 cases 3 and 4.
    void deletingACertificateCascadesToItsChildren();
    void deletingACertificateRemovesItsFilesForGood();
    void deletingACertificateWithNoAttachmentIsClean();
    void theRowsSurviveSoftDeletedRatherThanBeingRemoved();
    void archivedCopiesAreLeftAloneWhichIsTheKnownLimitation();
    void vesselScopeRefusesDeletingAnotherVesselsCertificate();

    // §4's ordering rule.
    void aCertificateThatIsAlreadyGoneIsRefusedAndNothingIsDeleted();

    // §4: reachable from the list, alongside Renew and the extension button.
    void theDeleteButtonNeedsASelectionAndIgnoresTheOtherRules();

private:
    QSqlDatabase database() { return QSqlDatabase::database(m_connectionName); }

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

    // Rows that are *not* soft-deleted, which is what every read in the
    // application sees.
    static int liveRowsIn(QSqlDatabase& db, const QString& table, const QString& certificateId)
    {
        QSqlQuery query(db);
        query.prepare(QStringLiteral("SELECT COUNT(*) FROM %1"
                                     " WHERE certificate_id = ? AND is_deleted = 0")
                          .arg(table));
        query.addBindValue(certificateId);
        if (!query.exec() || !query.next()) {
            return -1;
        }
        return query.value(0).toInt();
    }

    // A certificate with no survey requirement, so its attachment's name is
    // simply its expiry date (addendum 2 §7 has nothing nearer to use) and
    // these tests stay about deletion.
    Certificate seedOne(const QString& id         = QStringLiteral("c1"),
                        const QString& vesselId   = QStringLiteral("v1"),
                        const QString& listNumber = QStringLiteral("15D"))
    {
        QSqlDatabase db = database();
        seedCertificateWithSchedule(db, id, vesselId, QStringLiteral("Load Line"), listNumber,
                                    QDate(2026, 2, 4), QDate(2031, 2, 4),
                                    /*requiresAnnual=*/false);

        CertificateRepository      certificates(db, officeContext());
        std::optional<Certificate> loaded;
        certificates.findById(id, &loaded);
        return loaded.value_or(Certificate{});
    }

    QString       m_connectionName;
    QTemporaryDir m_source;
};

void TestCertificateDeletion::init()
{
    m_connectionName = QStringLiteral("deletion_%1").arg(QTest::currentTestFunction());
    QVERIFY(openDatabase(m_connectionName));
    QVERIFY(m_source.isValid());
}

void TestCertificateDeletion::cleanup()
{
    closeDatabase(m_connectionName);
}

// §5, the whole point of it: a replacement archives what it supersedes, a
// deletion does not. The difference is deliberate — replacing is a real
// supersession worth a trail, deleting exists to correct a mistake.
void TestCertificateDeletion::deletingAnAttachmentRemovesTheFileWithNoArchiveCopy()
{
    QSqlDatabase      db          = database();
    const Certificate certificate = seedOne();
    AttachmentFixture attachments(db, officeContext());

    QVERIFY(attachments.service.replaceCurrent(
        certificate, sourceFile(QStringLiteral("scan.pdf"), QStringLiteral("a mistake")), false));

    const QString stored   = activePathOf(db, certificate.id);
    const QString absolute = QDir(attachments.rootPath()).filePath(stored);
    QVERIFY(QFileInfo::exists(absolute));

    QVERIFY2(attachments.service.deleteCurrent(certificate),
             qPrintable(attachments.service.errorString()));
    QVERIFY(attachments.service.errorString().isEmpty());

    QVERIFY2(!QFileInfo::exists(absolute), "the file itself must be gone");
    QVERIFY2(!QFileInfo::exists(QDir(attachments.rootPath()).filePath(QStringLiteral("archive"))),
             "no archive copy: that is exactly what makes this different from a replace");

    // And the certificate now reports nothing attached, which is what the list
    // and the menu both read.
    QCOMPARE(attachments.service.currentAbsolutePath(certificate.vesselId, certificate.id),
             QString());
}

void TestCertificateDeletion::deletingAnAttachmentRetiresTheRowButKeepsIt()
{
    QSqlDatabase      db          = database();
    const Certificate certificate = seedOne();
    AttachmentFixture attachments(db, officeContext());

    QVERIFY(attachments.service.replaceCurrent(
        certificate, sourceFile(QStringLiteral("scan.pdf"), QStringLiteral("a mistake")), false));
    QVERIFY(attachments.service.deleteCurrent(certificate));

    // CLAUDE.md §6.4: soft delete, never a hard DELETE. What §5 refuses to
    // keep is the document, not the record that there was one — a deletion has
    // to replicate to the other side.
    QCOMPARE(rowCountOf(db, QStringLiteral("certificate_attachment")), 1);
    QCOMPARE(liveRowsIn(db, QStringLiteral("certificate_attachment"), certificate.id), 0);

    QSqlQuery query(db);
    QVERIFY(query.exec(QStringLiteral(
        "SELECT revision FROM certificate_attachment WHERE is_deleted = 1")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 2); // CLAUDE.md §6.5
}

void TestCertificateDeletion::deletingAnAttachmentOnACertificateWithNoneIsHarmless()
{
    QSqlDatabase      db          = database();
    const Certificate certificate = seedOne();
    AttachmentFixture attachments(db, officeContext());

    // The menu entry is only offered when something is attached, so reaching
    // this is a race rather than an error — the same judgment
    // CertificateExtensionRepository::resolve() already makes.
    QVERIFY(attachments.service.deleteCurrent(certificate));
    QVERIFY(attachments.service.errorString().isEmpty());
    QCOMPARE(rowCountOf(db, QStringLiteral("certificate_attachment")), 0);
}

// §9 case 2's second half, through the real screen: the Document column and the
// Open entry both go back to how they look when nothing was ever attached.
void TestCertificateDeletion::theDocumentColumnAndTheOpenEntryBothGoBackToEmpty()
{
    QSqlDatabase              db      = database();
    const InstallationContext context = officeContext();
    const Certificate         certificate = seedOne();

    CertificateRepository          certificates(db, context);
    EndorsementRepository          endorsements(db, context);
    CertificateExtensionRepository extensions(db, context);
    AppSettingRepository           appSettings(db);
    AttachmentFixture              attachments(db, context);

    QVERIFY(attachments.service.replaceCurrent(
        certificate, sourceFile(QStringLiteral("scan.pdf"), QStringLiteral("page one")), false));

    CertificateListWidget widget(certificates, endorsements, extensions, attachments.service,
                                 appSettings, attachments.csvExporter,
                                 attachments.csvImporter, attachments.reportBuilder, context);
    widget.setVesselId(QStringLiteral("v1"));

    auto* table = widget.findChild<QTableWidget*>(QStringLiteral("certificateTable"));
    QVERIFY(table != nullptr);
    const int document = table->columnCount() - 1;

    table->selectRow(0);

    auto* openAction = widget.findChild<QAction*>(QStringLiteral("openAttachmentAction"));
    auto* deleteAction =
        widget.findChild<QAction*>(QStringLiteral("deleteAttachmentAction"));
    QVERIFY(openAction != nullptr);
    QVERIFY(deleteAction != nullptr);

    QVERIFY(!table->item(0, document)->text().isEmpty());
    QVERIFY(openAction->isVisible());
    // §5: offered under the same rule as Open — never an offer to delete
    // nothing.
    QVERIFY(deleteAction->isVisible());

    QVERIFY(attachments.service.deleteCurrent(certificate));
    widget.reload();
    table->selectRow(0);

    QVERIFY2(table->item(0, document)->text().isEmpty(),
             "the mark must go, exactly as if nothing had ever been attached");
    QVERIFY(table->item(0, document)->toolTip().isEmpty());
    QVERIFY2(!openAction->isVisible(), "Open stops being offered");
    QVERIFY2(!deleteAction->isVisible(), "and so does Delete Attachment");
}

// §9 case 3: endorsements, the active extension and the attachment row all go
// with the certificate.
void TestCertificateDeletion::deletingACertificateCascadesToItsChildren()
{
    QSqlDatabase              db      = database();
    const InstallationContext context = officeContext();
    const Certificate         certificate = seedOne();

    CertificateRepository certificates(db, context);
    AttachmentFixture     attachments(db, context);

    seedEndorsement(db, QStringLiteral("e1"), certificate.id, QStringLiteral("ANNUAL"),
                    QDate(2027, 2, 4));
    seedEndorsement(db, QStringLiteral("e2"), certificate.id, QStringLiteral("ANNUAL"),
                    QDate(2028, 2, 4));

    CertificateExtension extension;
    extension.certificateId = certificate.id;
    extension.expiryDate    = QDate(2031, 5, 4);
    QVERIFY2(attachments.extensions.start(extension),
             qPrintable(attachments.extensions.errorString()));

    QVERIFY(attachments.service.replaceCurrent(
        certificate, sourceFile(QStringLiteral("scan.pdf"), QStringLiteral("page one")), false));

    CertificateDeletionService deletion(certificates, attachments.extensions,
                                        attachments.service);
    QVERIFY2(deletion.deleteCertificate(certificate), qPrintable(deletion.errorString()));
    QVERIFY(deletion.errorString().isEmpty());

    // Nothing live is left, on any of the four tables.
    std::optional<Certificate> reloaded;
    QVERIFY(certificates.findById(certificate.id, &reloaded));
    QVERIFY2(!reloaded.has_value(), "a deleted certificate must not come back out of findById");

    QCOMPARE(liveRowsIn(db, QStringLiteral("endorsement"), certificate.id), 0);
    QCOMPARE(liveRowsIn(db, QStringLiteral("certificate_extension"), certificate.id), 0);
    QCOMPARE(liveRowsIn(db, QStringLiteral("certificate_attachment"), certificate.id), 0);
}

void TestCertificateDeletion::deletingACertificateRemovesItsFilesForGood()
{
    QSqlDatabase              db      = database();
    const InstallationContext context = officeContext();
    const Certificate         certificate = seedOne();

    CertificateRepository certificates(db, context);
    AttachmentFixture     attachments(db, context);

    // The certificate's own document...
    QVERIFY(attachments.service.replaceCurrent(
        certificate, sourceFile(QStringLiteral("real.pdf"), QStringLiteral("the real one")),
        false));
    const QString certificateFile =
        QDir(attachments.rootPath()).filePath(activePathOf(db, certificate.id));

    // ...and an extension's own, which §4 step 5 deletes as well.
    QString extensionRelative;
    QVERIFY2(attachments.service.addExtensionDocument(
                 certificate, QDate(2031, 5, 4),
                 sourceFile(QStringLiteral("short.pdf"), QStringLiteral("the short-term one")),
                 false, &extensionRelative),
             qPrintable(attachments.service.errorString()));

    CertificateExtension extension;
    extension.certificateId  = certificate.id;
    extension.expiryDate     = QDate(2031, 5, 4);
    extension.attachmentPath = extensionRelative;
    QVERIFY(attachments.extensions.start(extension));

    const QString extensionFile = QDir(attachments.rootPath()).filePath(extensionRelative);
    QVERIFY(QFileInfo::exists(certificateFile));
    QVERIFY(QFileInfo::exists(extensionFile));

    CertificateDeletionService deletion(certificates, attachments.extensions,
                                        attachments.service);
    QVERIFY2(deletion.deleteCertificate(certificate), qPrintable(deletion.errorString()));

    QVERIFY2(!QFileInfo::exists(certificateFile), "the certificate's document must be gone");
    QVERIFY2(!QFileInfo::exists(extensionFile), "and so must the extension's");

    // Genuinely deleted, not archived (§4).
    QVERIFY(!QFileInfo::exists(QDir(attachments.rootPath()).filePath(QStringLiteral("archive"))));
}

// §9 case 4: nothing attached means no filesystem step is attempted at all,
// and certainly no error.
void TestCertificateDeletion::deletingACertificateWithNoAttachmentIsClean()
{
    QSqlDatabase              db      = database();
    const InstallationContext context = officeContext();
    const Certificate         certificate = seedOne();

    CertificateRepository certificates(db, context);
    AttachmentFixture     attachments(db, context);

    CertificateDeletionService deletion(certificates, attachments.extensions,
                                        attachments.service);
    QVERIFY2(deletion.deleteCertificate(certificate), qPrintable(deletion.errorString()));
    QVERIFY2(deletion.errorString().isEmpty(), qPrintable(deletion.errorString()));

    // The vessel's folder is untouched — not even created.
    QVERIFY(ManagedFileStore::entriesIn(attachments.rootPath()).isEmpty());
}

// §9 case 3's second half, asked of the database directly rather than of the
// UI: the rows are all still there, soft-deleted (CLAUDE.md §6.4).
void TestCertificateDeletion::theRowsSurviveSoftDeletedRatherThanBeingRemoved()
{
    QSqlDatabase              db      = database();
    const InstallationContext context = officeContext();
    const Certificate         certificate = seedOne();

    CertificateRepository certificates(db, context);
    AttachmentFixture     attachments(db, context);

    seedEndorsement(db, QStringLiteral("e1"), certificate.id, QStringLiteral("ANNUAL"),
                    QDate(2027, 2, 4));
    QVERIFY(attachments.service.replaceCurrent(
        certificate, sourceFile(QStringLiteral("scan.pdf"), QStringLiteral("page one")), false));

    CertificateDeletionService deletion(certificates, attachments.extensions,
                                        attachments.service);
    QVERIFY(deletion.deleteCertificate(certificate));

    QCOMPARE(rowCountOf(db, QStringLiteral("certificate")), 1);
    QCOMPARE(rowCountOf(db, QStringLiteral("endorsement")), 1);
    QCOMPARE(rowCountOf(db, QStringLiteral("certificate_attachment")), 1);

    // And each one's revision moved on, so a sync can tell it changed
    // (CLAUDE.md §6.5).
    QSqlQuery query(db);
    QVERIFY(query.exec(QStringLiteral("SELECT is_deleted, revision FROM certificate")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 1);
    QCOMPARE(query.value(1).toInt(), 2);
}

// §4 and §10: the accepted limitation, written down as a test so that nobody
// later reads the gap as an oversight. Once a replaced file has been renamed
// into the shared archive there is no reliable way back from it to the
// certificate it came from.
void TestCertificateDeletion::archivedCopiesAreLeftAloneWhichIsTheKnownLimitation()
{
    QSqlDatabase              db      = database();
    const InstallationContext context = officeContext();
    const Certificate         certificate = seedOne();

    CertificateRepository certificates(db, context);
    AttachmentFixture     attachments(db, context);

    // Attached, then replaced, so one copy is in the archive and one is live.
    QVERIFY(attachments.service.replaceCurrent(
        certificate, sourceFile(QStringLiteral("old.pdf"), QStringLiteral("the old one")),
        false));
    QVERIFY(attachments.service.replaceCurrent(
        certificate, sourceFile(QStringLiteral("new.pdf"), QStringLiteral("the new one")),
        false));

    const QDir archive(QDir(attachments.rootPath()).filePath(QStringLiteral("archive")));
    QCOMPARE(archive.entryList(QDir::Files).size(), 1);

    CertificateDeletionService deletion(certificates, attachments.extensions,
                                        attachments.service);
    QVERIFY(deletion.deleteCertificate(certificate));

    // The live one went; the archived one did not. Deliberate, not a bug.
    QCOMPARE(archive.entryList(QDir::Files).size(), 1);
}

void TestCertificateDeletion::vesselScopeRefusesDeletingAnotherVesselsCertificate()
{
    QSqlDatabase db = database();

    // A certificate belonging to a different vessel from the one this
    // installation is.
    const Certificate certificate = seedOne(QStringLiteral("c1"), QStringLiteral("v2"));

    const InstallationContext context = vesselContext(QStringLiteral("v1"));
    CertificateRepository     certificates(db, context);
    AttachmentFixture         attachments(db, context);

    CertificateDeletionService deletion(certificates, attachments.extensions,
                                        attachments.service);

    // The scope filter lives in the repository (CLAUDE.md §3), so the cascade
    // cannot reach another vessel's rows however it is called.
    QVERIFY2(!deletion.deleteCertificate(certificate),
             "another vessel's certificate must not be deletable");
    QVERIFY(!deletion.errorString().isEmpty());

    QCOMPARE(rowCountOf(db, QStringLiteral("certificate")), 1);
    QSqlQuery query(db);
    QVERIFY(query.exec(QStringLiteral("SELECT is_deleted FROM certificate")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 0);
}

// §4's ordering rule, in the one case it can be observed: the database step
// runs first, so a failure there leaves the file alone and the action can
// simply be repeated.
void TestCertificateDeletion::aCertificateThatIsAlreadyGoneIsRefusedAndNothingIsDeleted()
{
    QSqlDatabase              db      = database();
    const InstallationContext context = officeContext();
    const Certificate         certificate = seedOne();

    CertificateRepository certificates(db, context);
    AttachmentFixture     attachments(db, context);

    QVERIFY(attachments.service.replaceCurrent(
        certificate, sourceFile(QStringLiteral("scan.pdf"), QStringLiteral("page one")), false));
    const QString file =
        QDir(attachments.rootPath()).filePath(activePathOf(db, certificate.id));

    CertificateDeletionService deletion(certificates, attachments.extensions,
                                        attachments.service);
    QVERIFY(deletion.deleteCertificate(certificate));
    QVERIFY(!QFileInfo::exists(file));

    // Asked again, as a second click on a stale list would: refused with a
    // message, and nothing else happens.
    QVERIFY(!deletion.deleteCertificate(certificate));
    QVERIFY2(deletion.errorString().contains(QStringLiteral("no longer")),
             qPrintable(deletion.errorString()));
}

// §4: the one destructive button in the row. Unlike Renew and the extension
// button it applies no never-expires rule and no active-extension rule — any
// certificate can have been entered by mistake.
void TestCertificateDeletion::theDeleteButtonNeedsASelectionAndIgnoresTheOtherRules()
{
    QSqlDatabase              db      = database();
    const InstallationContext context = officeContext();

    CertificateRepository          certificates(db, context);
    EndorsementRepository          endorsements(db, context);
    CertificateExtensionRepository extensions(db, context);
    AppSettingRepository           appSettings(db);
    AttachmentFixture              attachments(db, context);

    // One ordinary certificate, and one that never expires — which Renew and
    // the extension button both refuse.
    seedOne(QStringLiteral("c1"), QStringLiteral("v1"), QStringLiteral("1"));
    seedCertificateWithSchedule(db, QStringLiteral("c2"), QStringLiteral("v1"),
                                QStringLiteral("Tonnage"), QStringLiteral("2"),
                                QDate(2026, 2, 4), QDate(), /*requiresAnnual=*/false);

    CertificateListWidget widget(certificates, endorsements, extensions, attachments.service,
                                 appSettings, attachments.csvExporter,
                                 attachments.csvImporter, attachments.reportBuilder, context);
    widget.setVesselId(QStringLiteral("v1"));

    auto* deleteButton =
        widget.findChild<QPushButton*>(QStringLiteral("deleteCertificateButton"));
    auto* renewButton = widget.findChild<QPushButton*>(QStringLiteral("renewCertificateButton"));
    QVERIFY(deleteButton != nullptr);
    QVERIFY(renewButton != nullptr);

    QVERIFY2(!deleteButton->isEnabled(), "disabled with nothing selected");

    auto* table = widget.findChild<QTableWidget*>(QStringLiteral("certificateTable"));
    QVERIFY(table != nullptr);
    QCOMPARE(table->rowCount(), 2);

    table->selectRow(0);
    QVERIFY(deleteButton->isEnabled());

    // The never-expires row: Renew rules itself out, Delete does not.
    table->selectRow(1);
    QVERIFY2(!renewButton->isEnabled(), "the fixture's second row must be the one that never expires");
    QVERIFY2(deleteButton->isEnabled(),
             "a certificate that never expires can still have been entered by mistake");
}

QTEST_MAIN(TestCertificateDeletion)
#include "tst_CertificateDeletion.moc"
