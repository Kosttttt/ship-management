#include "CertificateTestSupport.h"

#include "core/AppSettingRepository.h"
#include "core/ManagedFileStore.h"
#include "modules/certificates/data/CertificateExtensionRepository.h"
#include "modules/certificates/data/CertificateRepository.h"
#include "modules/certificates/data/EndorsementRepository.h"
#include "modules/certificates/domain/AttachmentNaming.h"
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

// certificate-attachment-spec §9, the nine edge cases, plus the pure naming
// rules from §6 and the button states from §8.
class TestCertificateAttachment : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void attachingAFileForTheFirstTimeStoresTheRowAndTheFile(); // 1
    void attachingASecondFileArchivesTheFirst();               // 2
    void attachingAFolderWorksTheSameWay();                    // 3
    void replacingAFileWithAFolderAndBackArchivesEither();     // 4
    void theCurrentAttachmentResolvesForAFileAndAFolder();     // 5
    void anExtensionDocumentIsIndependentOfTheCertificates();  // 6
    void vesselScopeRefusesAnotherVesselsCertificate();        // 7
    void theReplacedRowAndItsFileBothSurvive();                // 8
    void theStoredPathIsRelativeAndFollowsTheRoot();           // 9

    // §6, the pure naming function.
    void theNameIsNumberThenNameThenDate();
    void aShortTermNameSaysSoBeforeTheDate();
    void illegalCharactersAreDroppedAndWhitespaceCollapses();
    void aMissingNumberOrDateSimplyLeavesThatPartOut();
    void anExtensionIsAppendedForAFileAndNotForAFolder();

    // §8, the list button.
    void theAttachmentButtonNeedsASelection();
    void openIsOfferedOnlyWhenSomethingIsAttached();

private:
    QSqlDatabase database() { return QSqlDatabase::database(m_connectionName); }

    // A source file somewhere else on disk, standing in for the user's own
    // computer. Returns its absolute path.
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

    // A source folder with two files in it, one of them nested.
    QString sourceFolder(const QString& name)
    {
        const QString path = QDir(m_source.path()).filePath(name);
        if (!QDir().mkpath(QDir(path).filePath(QStringLiteral("photos")))) {
            return QString();
        }
        QFile report(QDir(path).filePath(QStringLiteral("report.pdf")));
        if (report.open(QIODevice::WriteOnly)) {
            report.write("the report");
        }
        QFile photo(QDir(path).filePath(QStringLiteral("photos/bow.jpg")));
        if (photo.open(QIODevice::WriteOnly)) {
            photo.write("a photo");
        }
        return path;
    }

    static QString readFile(const QString& absolutePath)
    {
        QFile file(absolutePath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            return QString();
        }
        return QString::fromUtf8(file.readAll());
    }

    // The certificate every test attaches to: number 15D, expiring 2027-06-01,
    // which together make the generated name in §4's example.
    Certificate seedTheCertificate(const QString& id = QStringLiteral("c1"),
                                   const QString& vesselId = QStringLiteral("v1"))
    {
        QSqlDatabase db = database();
        seedCertificateWithSchedule(db, id, vesselId, QStringLiteral("Safety Construction"),
                                    QStringLiteral("15D"), QDate(2022, 6, 1), QDate(2027, 6, 1),
                                    /*requiresAnnual=*/false);

        const InstallationContext context = officeContext();
        CertificateRepository     certificates(db, context);

        std::optional<Certificate> loaded;
        certificates.findById(id, &loaded);
        return loaded.value_or(Certificate{});
    }

    // The generated name from §4, written out once so every expectation reads
    // the same.
    static QString expectedBaseName()
    {
        return QStringLiteral("15D_Safety_Construction_2027-06-01");
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

    QString m_connectionName;
    QTemporaryDir m_source;
};

void TestCertificateAttachment::init()
{
    m_connectionName = QStringLiteral("attachment_%1").arg(QTest::currentTestFunction());
    QVERIFY(openDatabase(m_connectionName));
    QVERIFY(m_source.isValid());
}

void TestCertificateAttachment::cleanup()
{
    closeDatabase(m_connectionName);
}

// §9 case 1.
void TestCertificateAttachment::attachingAFileForTheFirstTimeStoresTheRowAndTheFile()
{
    QSqlDatabase      db          = database();
    const Certificate certificate = seedTheCertificate();
    AttachmentFixture attachments(db, officeContext());

    const QString source = sourceFile(QStringLiteral("scan.pdf"), QStringLiteral("page one"));
    QVERIFY(!source.isEmpty());

    QVERIFY2(attachments.service.replaceCurrent(certificate, certificate.expiryDate, source,
                                                /*sourceIsFolder=*/false),
             qPrintable(attachments.service.errorString()));

    // §4: directly under attachments/<certificateId>/, named per §6, keeping
    // the original extension.
    const QString stored = activePathOf(db, certificate.id);
    QCOMPARE(stored, QStringLiteral("attachments/c1/%1.pdf").arg(expectedBaseName()));

    QCOMPARE(rowCountOf(db, QStringLiteral("certificate_attachment")), 1);

    const QString absolute = QDir(attachments.rootPath()).filePath(stored);
    QVERIFY2(QFileInfo::exists(absolute), qPrintable(absolute));
    QCOMPARE(readFile(absolute), QStringLiteral("page one"));
}

// §9 case 2.
void TestCertificateAttachment::attachingASecondFileArchivesTheFirst()
{
    QSqlDatabase      db          = database();
    const Certificate certificate = seedTheCertificate();
    AttachmentFixture attachments(db, officeContext());

    QVERIFY(attachments.service.replaceCurrent(
        certificate, certificate.expiryDate,
        sourceFile(QStringLiteral("old.pdf"), QStringLiteral("the old document")), false));

    const QString firstPath = activePathOf(db, certificate.id);

    QVERIFY2(attachments.service.replaceCurrent(
                 certificate, certificate.expiryDate,
                 sourceFile(QStringLiteral("new.pdf"), QStringLiteral("the new document")), false),
             qPrintable(attachments.service.errorString()));

    // The new one is current, under exactly the same clean name: archiving
    // first is what frees it (§6).
    const QString secondPath = activePathOf(db, certificate.id);
    QCOMPARE(secondPath, firstPath);
    QCOMPARE(readFile(QDir(attachments.rootPath()).filePath(secondPath)),
             QStringLiteral("the new document"));

    // Two rows, one active.
    QCOMPARE(rowCountOf(db, QStringLiteral("certificate_attachment")), 2);

    // And the old file is physically in the archive, not gone.
    const QDir archive(
        QDir(attachments.rootPath()).filePath(QStringLiteral("attachments/c1/archive")));
    const QStringList archived = archive.entryList(QDir::Files);
    QCOMPARE(archived.size(), 1);
    QVERIFY2(archived.first().startsWith(expectedBaseName()), qPrintable(archived.first()));
    QVERIFY2(archived.first().contains(QStringLiteral("__archived-")),
             qPrintable(archived.first()));
    QCOMPARE(readFile(archive.filePath(archived.first())), QStringLiteral("the old document"));
}

// §9 case 3.
void TestCertificateAttachment::attachingAFolderWorksTheSameWay()
{
    QSqlDatabase      db          = database();
    const Certificate certificate = seedTheCertificate();
    AttachmentFixture attachments(db, officeContext());

    const QString source = sourceFolder(QStringLiteral("survey pack"));
    QVERIFY(!source.isEmpty());

    QVERIFY2(attachments.service.replaceCurrent(certificate, certificate.expiryDate, source,
                                                /*sourceIsFolder=*/true),
             qPrintable(attachments.service.errorString()));

    // The folder is renamed; no extension, because it is a folder (§2).
    const QString stored = activePathOf(db, certificate.id);
    QCOMPARE(stored, QStringLiteral("attachments/c1/%1").arg(expectedBaseName()));

    const QDir copied(QDir(attachments.rootPath()).filePath(stored));
    QVERIFY(copied.exists());

    // The files inside keep their own names: renaming several arbitrary files
    // to one name would collide or destroy information (§2).
    QCOMPARE(readFile(copied.filePath(QStringLiteral("report.pdf"))),
             QStringLiteral("the report"));
    QCOMPARE(readFile(copied.filePath(QStringLiteral("photos/bow.jpg"))),
             QStringLiteral("a photo"));
}

// §9 case 4.
void TestCertificateAttachment::replacingAFileWithAFolderAndBackArchivesEither()
{
    QSqlDatabase      db          = database();
    const Certificate certificate = seedTheCertificate();
    AttachmentFixture attachments(db, officeContext());

    QVERIFY(attachments.service.replaceCurrent(
        certificate, certificate.expiryDate,
        sourceFile(QStringLiteral("first.pdf"), QStringLiteral("a file")), false));

    // File replaced by a folder.
    QVERIFY2(attachments.service.replaceCurrent(certificate, certificate.expiryDate,
                                                sourceFolder(QStringLiteral("pack")), true),
             qPrintable(attachments.service.errorString()));
    QCOMPARE(activePathOf(db, certificate.id),
             QStringLiteral("attachments/c1/%1").arg(expectedBaseName()));

    // Folder replaced by a file.
    QVERIFY2(attachments.service.replaceCurrent(
                 certificate, certificate.expiryDate,
                 sourceFile(QStringLiteral("third.pdf"), QStringLiteral("a file again")), false),
             qPrintable(attachments.service.errorString()));
    QCOMPARE(activePathOf(db, certificate.id),
             QStringLiteral("attachments/c1/%1.pdf").arg(expectedBaseName()));

    // Both of the retired ones are in the archive, one a file and one a
    // folder, and the mechanism never had to know which it was dealing with.
    const QDir archive(
        QDir(attachments.rootPath()).filePath(QStringLiteral("attachments/c1/archive")));
    QCOMPARE(archive.entryList(QDir::Files).size(), 1);
    QCOMPARE(archive.entryList(QDir::Dirs | QDir::NoDotAndDotDot).size(), 1);

    QCOMPARE(rowCountOf(db, QStringLiteral("certificate_attachment")), 3);
}

// §9 case 5: resolving for opening needs no branching on file or folder.
void TestCertificateAttachment::theCurrentAttachmentResolvesForAFileAndAFolder()
{
    QSqlDatabase      db          = database();
    const Certificate certificate = seedTheCertificate();
    AttachmentFixture attachments(db, officeContext());

    // Nothing attached yet: an empty answer, which is also how the list
    // decides not to offer "Open Current Attachment" at all (§8).
    QCOMPARE(attachments.service.currentAbsolutePath(certificate.id), QString());

    QVERIFY(attachments.service.replaceCurrent(
        certificate, certificate.expiryDate,
        sourceFile(QStringLiteral("scan.pdf"), QStringLiteral("page one")), false));

    const QString asAFile = attachments.service.currentAbsolutePath(certificate.id);
    QVERIFY(QDir::isAbsolutePath(asAFile));
    QVERIFY(QFileInfo(asAFile).isFile());

    QVERIFY(attachments.service.replaceCurrent(certificate, certificate.expiryDate,
                                               sourceFolder(QStringLiteral("pack")), true));

    const QString asAFolder = attachments.service.currentAbsolutePath(certificate.id);
    QVERIFY(QDir::isAbsolutePath(asAFolder));
    QVERIFY(QFileInfo(asAFolder).isDir());
}

// §9 case 6: the two are completely separate mechanisms.
void TestCertificateAttachment::anExtensionDocumentIsIndependentOfTheCertificates()
{
    QSqlDatabase      db          = database();
    const Certificate certificate = seedTheCertificate();
    const InstallationContext      context = officeContext();
    CertificateExtensionRepository extensions(db, context);
    AttachmentFixture              attachments(db, context);

    // The certificate's own document first.
    QVERIFY(attachments.service.replaceCurrent(
        certificate, certificate.expiryDate,
        sourceFile(QStringLiteral("real.pdf"), QStringLiteral("the real certificate")), false));
    const QString certificatePath = activePathOf(db, certificate.id);

    // Then a short-term extension with its own paperwork, named from the
    // extension's expiry date and not the certificate's (§2).
    const QDate extensionExpiry = QDate(2027, 3, 1);
    QString     extensionDocument;
    QVERIFY2(attachments.service.addExtensionDocument(
                 certificate, extensionExpiry,
                 sourceFile(QStringLiteral("short.pdf"), QStringLiteral("the short-term one")),
                 /*sourceIsFolder=*/false, &extensionDocument),
             qPrintable(attachments.service.errorString()));

    QCOMPARE(extensionDocument,
             QStringLiteral("attachments/c1/extensions/15D_Safety_Construction_ShortTerm_"
                            "2027-03-01.pdf"));

    CertificateExtension extension;
    extension.certificateId  = certificate.id;
    extension.expiryDate     = extensionExpiry;
    extension.attachmentPath = extensionDocument;
    QVERIFY2(extensions.start(extension), qPrintable(extensions.errorString()));

    // Starting an extension touched nothing about certificate_attachment.
    QCOMPARE(rowCountOf(db, QStringLiteral("certificate_attachment")), 1);
    QCOMPARE(activePathOf(db, certificate.id), certificatePath);

    // The path really is on the row, and comes back out of it.
    std::optional<CertificateExtension> stored;
    QVERIFY(extensions.findActiveFor(certificate.id, &stored));
    QVERIFY(stored.has_value());
    QCOMPARE(stored->attachmentPath, extensionDocument);

    // Resolving does nothing to the file at all (§2).
    QVERIFY(extensions.resolve(certificate.id));
    QVERIFY(QFileInfo::exists(QDir(attachments.rootPath()).filePath(extensionDocument)));

    // And replacing the certificate's own document leaves the extension's
    // where it was — the archive step never looks inside extensions/.
    QVERIFY(attachments.service.replaceCurrent(
        certificate, certificate.expiryDate,
        sourceFile(QStringLiteral("renewed.pdf"), QStringLiteral("the renewed one")), false));
    QVERIFY(QFileInfo::exists(QDir(attachments.rootPath()).filePath(extensionDocument)));
}

// §9 case 7: the same transitive check endorsements and extensions already use.
void TestCertificateAttachment::vesselScopeRefusesAnotherVesselsCertificate()
{
    QSqlDatabase db = database();

    // A certificate belonging to a different vessel from the one this
    // installation is.
    const Certificate certificate =
        seedTheCertificate(QStringLiteral("c1"), QStringLiteral("v2"));

    const InstallationContext context = vesselContext(QStringLiteral("v1"));
    AttachmentFixture         attachments(db, context);

    // Asked of the repository directly: the write is refused with a message,
    // not silently allowed.
    QVERIFY2(!attachments.repository.attach(QStringLiteral("c1"),
                                            QStringLiteral("attachments/c1/anything.pdf")),
             "another vessel's certificate must be refused");
    QVERIFY2(attachments.repository.errorString().contains(QStringLiteral("its own vessel")),
             qPrintable(attachments.repository.errorString()));

    // And through the service, which is what the screens call. Note that the
    // service refuses at the database step, so a copy may already have been
    // made on disk by then — the documented failure mode of doing the
    // filesystem work first (§7). The screens never reach this state, because
    // they load the certificate through CertificateRepository, which applies
    // the same scope filter before any of this is offered.
    QVERIFY(!attachments.service.replaceCurrent(
        certificate, QDate(2027, 6, 1),
        sourceFile(QStringLiteral("scan.pdf"), QStringLiteral("page one")), false));

    // Nothing was recorded either way.
    QCOMPARE(rowCountOf(db, QStringLiteral("certificate_attachment")), 0);
    QCOMPARE(activePathOf(db, QStringLiteral("c1")), QString());
}

// §9 case 8: nothing is ever actually deleted.
void TestCertificateAttachment::theReplacedRowAndItsFileBothSurvive()
{
    QSqlDatabase      db          = database();
    const Certificate certificate = seedTheCertificate();
    AttachmentFixture attachments(db, officeContext());

    QVERIFY(attachments.service.replaceCurrent(
        certificate, certificate.expiryDate,
        sourceFile(QStringLiteral("old.pdf"), QStringLiteral("the old document")), false));
    QVERIFY(attachments.service.replaceCurrent(
        certificate, certificate.expiryDate,
        sourceFile(QStringLiteral("new.pdf"), QStringLiteral("the new document")), false));

    // The retired row is soft-deleted, not removed, and its revision moved on
    // (CLAUDE.md §6.4 and §6.5).
    QSqlQuery query(db);
    QVERIFY(query.exec(QStringLiteral(
        "SELECT relative_path, revision FROM certificate_attachment"
        " WHERE is_deleted = 1 ORDER BY created_at")));
    QVERIFY2(query.next(), "the replaced row must survive, soft-deleted");
    QCOMPARE(query.value(1).toInt(), 2);
    QVERIFY2(!query.next(), "exactly one row should have been retired");

    // Its file survives under archive/ with the disambiguating suffix.
    const QDir archive(
        QDir(attachments.rootPath()).filePath(QStringLiteral("attachments/c1/archive")));
    QCOMPARE(archive.entryList(QDir::Files).size(), 1);
    QCOMPARE(readFile(archive.filePath(archive.entryList(QDir::Files).first())),
             QStringLiteral("the old document"));
}

// §9 case 9: CLAUDE.md §6.9 in practice. What is stored is relative, so the
// same row finds the same document after the folder has been copied to the
// other side.
void TestCertificateAttachment::theStoredPathIsRelativeAndFollowsTheRoot()
{
    QSqlDatabase      db          = database();
    const Certificate certificate = seedTheCertificate();
    AttachmentFixture attachments(db, officeContext());

    QVERIFY(attachments.service.replaceCurrent(
        certificate, certificate.expiryDate,
        sourceFile(QStringLiteral("scan.pdf"), QStringLiteral("page one")), false));

    const QString stored = activePathOf(db, certificate.id);
    QVERIFY2(!QDir::isAbsolutePath(stored), qPrintable(stored));
    QVERIFY2(!stored.contains(QLatin1Char('\\')), qPrintable(stored));
    QVERIFY2(!stored.contains(attachments.rootPath()), qPrintable(stored));

    const QString onTheVessel = attachments.service.currentAbsolutePath(certificate.id);
    QCOMPARE(onTheVessel, QDir(attachments.rootPath()).filePath(stored));

    // The office machine: the same managed folder, in a different place. Only
    // the root changes, and the row needs no migration.
    QTemporaryDir office;
    QVERIFY(office.isValid());
    const QString officeCopy = QDir(office.path()).filePath(stored);
    QVERIFY(QDir().mkpath(QFileInfo(officeCopy).absolutePath()));
    QVERIFY(QFile::copy(onTheVessel, officeCopy));

    ManagedFileStore::setRootPathOverride(office.path());
    const QString inTheOffice = attachments.service.currentAbsolutePath(certificate.id);

    QVERIFY(inTheOffice != onTheVessel);
    QCOMPARE(inTheOffice, QDir(office.path()).filePath(stored));
    QCOMPARE(readFile(inTheOffice), QStringLiteral("page one"));

    // Put it back, because the fixture is about to be asked to clean up after
    // the folder it actually created.
    ManagedFileStore::setRootPathOverride(attachments.rootPath());
}

// §6.
void TestCertificateAttachment::theNameIsNumberThenNameThenDate()
{
    Certificate certificate;
    certificate.listNumber = QStringLiteral("15D");
    certificate.name       = QStringLiteral("Safety Construction");

    QCOMPARE(AttachmentNaming::buildName(certificate, QDate(2027, 6, 1), /*isShortTerm=*/false),
             QStringLiteral("15D_Safety_Construction_2027-06-01"));
}

void TestCertificateAttachment::aShortTermNameSaysSoBeforeTheDate()
{
    Certificate certificate;
    certificate.listNumber = QStringLiteral("15D");
    certificate.name       = QStringLiteral("Safety Construction");

    QCOMPARE(AttachmentNaming::buildName(certificate, QDate(2027, 3, 1), /*isShortTerm=*/true),
             QStringLiteral("15D_Safety_Construction_ShortTerm_2027-03-01"));
}

void TestCertificateAttachment::illegalCharactersAreDroppedAndWhitespaceCollapses()
{
    Certificate certificate;
    certificate.listNumber = QStringLiteral("3A");
    // A plausible thing to type that is completely illegal to write to disk:
    // quotes, a slash, a double space, and a stray control character.
    certificate.name = QStringLiteral("Safety \"Construction\" / Hull  Plan")
                       + QChar(QChar::Tabulation) + QChar(0x07);

    const QString name =
        AttachmentNaming::buildName(certificate, QDate(2027, 6, 1), /*isShortTerm=*/false);

    for (const QChar illegal : QStringLiteral("<>:\"/\\|?*")) {
        QVERIFY2(!name.contains(illegal), qPrintable(name));
    }
    QVERIFY2(!name.contains(QLatin1Char(' ')), qPrintable(name));
    QVERIFY2(!name.contains(QStringLiteral("__")), qPrintable(name));

    // Illegal characters vanish; whitespace around them becomes one
    // underscore, so two words never run together.
    QCOMPARE(name, QStringLiteral("3A_Safety_Construction_Hull_Plan_2027-06-01"));

    // A whole string of nothing but forbidden characters contributes nothing,
    // rather than a run of underscores.
    Certificate onlyIllegal;
    onlyIllegal.listNumber = QStringLiteral("9");
    onlyIllegal.name       = QStringLiteral("Load Line");
    QCOMPARE(AttachmentNaming::buildName(onlyIllegal, QDate(2027, 6, 1), false),
             QStringLiteral("9_Load_Line_2027-06-01"));
}

void TestCertificateAttachment::aMissingNumberOrDateSimplyLeavesThatPartOut()
{
    Certificate noNumber;
    noNumber.name = QStringLiteral("Tonnage");
    QCOMPARE(AttachmentNaming::buildName(noNumber, QDate(2027, 6, 1), false),
             QStringLiteral("Tonnage_2027-06-01"));

    // A certificate that never expires has no date to use, and is still
    // perfectly able to have a document attached (§8).
    Certificate neverExpires;
    neverExpires.listNumber = QStringLiteral("7");
    neverExpires.name       = QStringLiteral("Tonnage");
    QCOMPARE(AttachmentNaming::buildName(neverExpires, QDate(), false),
             QStringLiteral("7_Tonnage"));

    // Nothing usable at all still produces a legal filename.
    Certificate empty;
    empty.name = QStringLiteral("///");
    QCOMPARE(AttachmentNaming::buildName(empty, QDate(), false), QStringLiteral("attachment"));
}

void TestCertificateAttachment::anExtensionIsAppendedForAFileAndNotForAFolder()
{
    const QString base = QStringLiteral("15D_Safety_Construction_2027-06-01");

    QCOMPARE(AttachmentNaming::withExtensionFrom(base, QStringLiteral("C:/scans/doc.pdf"), false),
             base + QStringLiteral(".pdf"));
    QCOMPARE(AttachmentNaming::withExtensionFrom(base, QStringLiteral("C:/scans/doc.pdf"), true),
             base);

    // A dot in a folder along the way is not an extension, and neither is a
    // file with no dot at all.
    QCOMPARE(AttachmentNaming::withExtensionFrom(base, QStringLiteral("C:/my.docs/scan"), false),
             base);
    QCOMPARE(
        AttachmentNaming::withExtensionFrom(base, QStringLiteral("/home/me/pack.tar.gz"), false),
        base + QStringLiteral(".gz"));
}

// §8.
void TestCertificateAttachment::theAttachmentButtonNeedsASelection()
{
    QSqlDatabase db = database();
    seedTheCertificate();

    const InstallationContext      context = officeContext();
    CertificateRepository          certificates(db, context);
    EndorsementRepository          endorsements(db, context);
    CertificateExtensionRepository extensions(db, context);
    AppSettingRepository           appSettings(db);

    AttachmentFixture attachments(db, context);
    CertificateListWidget widget(certificates, endorsements, extensions, attachments.service,
                                 appSettings, context);
    widget.setVesselId(QStringLiteral("v1"));

    auto* button =
        widget.findChild<QPushButton*>(QStringLiteral("certificateAttachmentButton"));
    QVERIFY(button != nullptr);
    QVERIFY2(!button->isEnabled(), "disabled with nothing selected");

    widget.findChild<QTableWidget*>(QStringLiteral("certificateTable"))->selectRow(0);
    QVERIFY2(button->isEnabled(), "a selected certificate can always have a document attached");
}

void TestCertificateAttachment::openIsOfferedOnlyWhenSomethingIsAttached()
{
    QSqlDatabase      db          = database();
    const Certificate certificate = seedTheCertificate();

    const InstallationContext      context = officeContext();
    CertificateRepository          certificates(db, context);
    EndorsementRepository          endorsements(db, context);
    CertificateExtensionRepository extensions(db, context);
    AppSettingRepository           appSettings(db);

    AttachmentFixture attachments(db, context);
    CertificateListWidget widget(certificates, endorsements, extensions, attachments.service,
                                 appSettings, context);
    widget.setVesselId(QStringLiteral("v1"));

    auto* openAction = widget.findChild<QAction*>(QStringLiteral("openAttachmentAction"));
    QVERIFY(openAction != nullptr);

    widget.findChild<QTableWidget*>(QStringLiteral("certificateTable"))->selectRow(0);
    QVERIFY2(!openAction->isVisible(),
             "nothing attached: Open is a question with no useful answer");

    QVERIFY(attachments.service.replaceCurrent(
        certificate, certificate.expiryDate,
        sourceFile(QStringLiteral("scan.pdf"), QStringLiteral("page one")), false));

    // The list reloads after an attach, which is what re-reads this.
    widget.reload();
    widget.findChild<QTableWidget*>(QStringLiteral("certificateTable"))->selectRow(0);
    QVERIFY2(openAction->isVisible(), "with a document attached, Open appears");
}

QTEST_MAIN(TestCertificateAttachment)
#include "tst_CertificateAttachment.moc"
