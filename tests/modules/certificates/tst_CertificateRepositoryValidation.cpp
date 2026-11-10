#include "CertificateTestSupport.h"

#include "modules/certificates/data/CertificateRepository.h"

#include <QtTest>

using namespace CertificateTestSupport;

// certificate-crud-spec §7 items 3, 4 and 5: everything the repository refuses
// to write, and the friendly messages it refuses with.
class TestCertificateRepositoryValidation : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void createRejectedForAnotherVesselInVesselMode();
    void updateRejectedForAnotherVesselInVesselMode();
    void createRejectsMissingVessel();
    void createRejectsMissingName();
    void createRejectsMissingCategory();
    void createRejectsMissingIssueDate();
    void createRejectsNoExpiryWithAnnualSurvey();
    void createRejectsNoExpiryWithIntermediateSurvey();
    void updateRejectsNoExpiryWithSurvey();
    void rawCheckConstraintTextNeverReachesTheUser();

    void acceptsValidListNumbers_data();
    void acceptsValidListNumbers();
    void rejectsInvalidListNumbers_data();
    void rejectsInvalidListNumbers();
    void listNumberSurvivesARoundTrip();

    // prefix-spec §5
    void theWidenedCheckAcceptsPrefixedNumbersWrittenDirectly();
    void theWidenedCheckStillRefusesWhatGlobCanCatch();

private:
    QSqlDatabase database() { return QSqlDatabase::database(m_connectionName); }
    QString      m_connectionName;
};

void TestCertificateRepositoryValidation::init()
{
    m_connectionName = QStringLiteral("certvalid_%1").arg(QTest::currentTestFunction());
    QVERIFY(openDatabase(m_connectionName));
}

void TestCertificateRepositoryValidation::cleanup()
{
    closeDatabase(m_connectionName);
}

void TestCertificateRepositoryValidation::createRejectedForAnotherVesselInVesselMode()
{
    QSqlDatabase              db      = database();
    const InstallationContext context = vesselContext(QStringLiteral("v1"));
    CertificateRepository     repository(db, context);

    QVERIFY(!repository.create(sampleCertificate(QStringLiteral("v2"),
                                                 QStringLiteral("Load Line"))));
    QVERIFY(repository.errorString().contains(QStringLiteral("own vessel")));
    QCOMPARE(rowCountOf(db, QStringLiteral("certificate")), 0);
}

void TestCertificateRepositoryValidation::updateRejectedForAnotherVesselInVesselMode()
{
    QSqlDatabase db = database();
    seedCertificate(db, QStringLiteral("c2"), QStringLiteral("v2"), QStringLiteral("Secret"));

    const InstallationContext context = vesselContext(QStringLiteral("v1"));
    CertificateRepository     repository(db, context);

    Certificate hijack = sampleCertificate(QStringLiteral("v2"), QStringLiteral("Hijacked"));
    hijack.id          = QStringLiteral("c2");

    QVERIFY(!repository.update(hijack));
    QVERIFY(repository.errorString().contains(QStringLiteral("own vessel")));

    QSqlQuery query(db);
    QVERIFY(query.exec(QStringLiteral("SELECT name FROM certificate WHERE id = 'c2'")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toString(), QStringLiteral("Secret")); // untouched
}

void TestCertificateRepositoryValidation::createRejectsMissingVessel()
{
    QSqlDatabase              db      = database();
    const InstallationContext context = officeContext();
    CertificateRepository     repository(db, context);

    QVERIFY(!repository.create(sampleCertificate(QString(), QStringLiteral("Load Line"))));
    QVERIFY(repository.errorString().contains(QStringLiteral("belong to a vessel")));
    QCOMPARE(rowCountOf(db, QStringLiteral("certificate")), 0);
}

void TestCertificateRepositoryValidation::createRejectsMissingName()
{
    QSqlDatabase              db      = database();
    const InstallationContext context = officeContext();
    CertificateRepository     repository(db, context);

    QVERIFY(!repository.create(sampleCertificate(QStringLiteral("v1"), QStringLiteral("   "))));
    QVERIFY(repository.errorString().contains(QStringLiteral("name is required")));
    QCOMPARE(rowCountOf(db, QStringLiteral("certificate")), 0);
}

void TestCertificateRepositoryValidation::createRejectsMissingCategory()
{
    QSqlDatabase              db      = database();
    const InstallationContext context = officeContext();
    CertificateRepository     repository(db, context);

    Certificate certificate = sampleCertificate(QStringLiteral("v1"), QStringLiteral("Load Line"));
    certificate.category    = CertificateCategory::Unset;

    QVERIFY(!repository.create(certificate));
    QVERIFY(repository.errorString().contains(QStringLiteral("category is required")));
    QCOMPARE(rowCountOf(db, QStringLiteral("certificate")), 0);
}

void TestCertificateRepositoryValidation::createRejectsMissingIssueDate()
{
    QSqlDatabase              db      = database();
    const InstallationContext context = officeContext();
    CertificateRepository     repository(db, context);

    Certificate certificate = sampleCertificate(QStringLiteral("v1"), QStringLiteral("Load Line"));
    certificate.issueDate   = QDate(); // null

    QVERIFY(!repository.create(certificate));
    QVERIFY(repository.errorString().contains(QStringLiteral("issue date is required")));
    QCOMPARE(rowCountOf(db, QStringLiteral("certificate")), 0);
}

void TestCertificateRepositoryValidation::createRejectsNoExpiryWithAnnualSurvey()
{
    QSqlDatabase              db      = database();
    const InstallationContext context = officeContext();
    CertificateRepository     repository(db, context);

    Certificate certificate = sampleCertificate(QStringLiteral("v1"), QStringLiteral("Tonnage"));
    certificate.expiryDate           = QDate(); // never expires
    certificate.requiresAnnualSurvey = true;

    QVERIFY(!repository.create(certificate));
    QVERIFY(repository.errorString().contains(QStringLiteral("cannot require a survey")));
    QCOMPARE(rowCountOf(db, QStringLiteral("certificate")), 0);
}

void TestCertificateRepositoryValidation::createRejectsNoExpiryWithIntermediateSurvey()
{
    QSqlDatabase              db      = database();
    const InstallationContext context = officeContext();
    CertificateRepository     repository(db, context);

    Certificate certificate = sampleCertificate(QStringLiteral("v1"), QStringLiteral("Tonnage"));
    certificate.expiryDate                 = QDate();
    certificate.requiresAnnualSurvey       = false;
    certificate.requiresIntermediateSurvey = true;

    QVERIFY(!repository.create(certificate));
    QVERIFY(repository.errorString().contains(QStringLiteral("cannot require a survey")));
    QCOMPARE(rowCountOf(db, QStringLiteral("certificate")), 0);
}

void TestCertificateRepositoryValidation::updateRejectsNoExpiryWithSurvey()
{
    QSqlDatabase              db      = database();
    const InstallationContext context = officeContext();
    CertificateRepository     repository(db, context);

    QString newId;
    QVERIFY2(repository.create(sampleCertificate(QStringLiteral("v1"),
                                                 QStringLiteral("Load Line")),
                               &newId),
             qPrintable(repository.errorString()));

    Certificate edited          = sampleCertificate(QStringLiteral("v1"),
                                                    QStringLiteral("Load Line"));
    edited.id                   = newId;
    edited.expiryDate           = QDate();  // now claims never to expire...
    edited.requiresAnnualSurvey = true;     // ...while still requiring a survey

    QVERIFY(!repository.update(edited));
    QVERIFY(repository.errorString().contains(QStringLiteral("cannot require a survey")));
}

void TestCertificateRepositoryValidation::rawCheckConstraintTextNeverReachesTheUser()
{
    // The migration's CHECK constraint is the backstop, but the friendly
    // message is the path a user actually sees (certificate-crud-spec §6).
    QSqlDatabase              db      = database();
    const InstallationContext context = officeContext();
    CertificateRepository     repository(db, context);

    Certificate certificate = sampleCertificate(QStringLiteral("v1"), QStringLiteral("Tonnage"));
    certificate.expiryDate           = QDate();
    certificate.requiresAnnualSurvey = true;

    QVERIFY(!repository.create(certificate));
    const QString message = repository.errorString();
    QVERIFY2(!message.contains(QStringLiteral("CHECK"), Qt::CaseInsensitive),
             "the raw SQLite constraint text must not reach the user");
    QVERIFY2(!message.contains(QStringLiteral("constraint"), Qt::CaseInsensitive),
             "the raw SQLite constraint text must not reach the user");
}

// certificate-crud-spec §8.5 item 13, at the repository layer. The form's
// validator is the primary defence; this is the backstop that catches a value
// arriving another way, such as a CSV import in a later step.
void TestCertificateRepositoryValidation::acceptsValidListNumbers_data()
{
    QTest::addColumn<QString>("listNumber");
    QTest::newRow("blank")          << "";
    QTest::newRow("digit")          << "3";
    QTest::newRow("digit letter")   << "3A";
    QTest::newRow("digits letter")  << "15D";

    // prefix-spec §1: one real code from each annex on the company's S-14
    // sheet. All of these were refused by the old rule, which is what the
    // first bulk import ran into.
    QTest::newRow("S prefix")       << "S1";
    QTest::newRow("P prefix")       << "P.48";
    QTest::newRow("EU prefix")      << "EU.9";
    QTest::newRow("US prefix")      << "US.12";
    QTest::newRow("USCA prefix")    << "USCA.4";
    QTest::newRow("USAL prefix")    << "USAL.2";
    QTest::newRow("PC prefix")      << "PC.2";
    QTest::newRow("IS prefix")      << "IS.2";
    QTest::newRow("lowercase prefix") << "p.6";
    // A letter before the first digit used to be the headline rejection. It
    // is a one-letter prefix now, and there is no way to accept S1 and still
    // refuse this.
    QTest::newRow("one-letter prefix") << "A15";
}

void TestCertificateRepositoryValidation::acceptsValidListNumbers()
{
    QFETCH(QString, listNumber);

    QSqlDatabase              db      = database();
    const InstallationContext context = officeContext();
    CertificateRepository     repository(db, context);

    Certificate certificate = sampleCertificate(QStringLiteral("v1"), QStringLiteral("Load Line"));
    certificate.listNumber  = listNumber;

    QVERIFY2(repository.create(certificate), qPrintable(repository.errorString()));
    QCOMPARE(rowCountOf(db, QStringLiteral("certificate")), 1);
}

void TestCertificateRepositoryValidation::rejectsInvalidListNumbers_data()
{
    QTest::addColumn<QString>("listNumber");
    // No digits at all, prefix or not: nothing to sort by.
    QTest::newRow("letters only")          << "ABC";
    QTest::newRow("prefix with no digits") << "P.";
    // A digit after a letter.
    QTest::newRow("digit after letter")    << "3A1";
    // The dot belongs to the prefix, and there is only ever one of it.
    QTest::newRow("two dots")              << "P..1";
    QTest::newRow("dot after digits")      << "15.D";
    // A prefix longer than prefix-spec §2 allows.
    QTest::newRow("prefix too long")       << "ABCDEFG.1";
    // Characters that are neither digits, letters, nor the prefix's dot.
    QTest::newRow("hyphen")                << "15-D";
    QTest::newRow("slash")                 << "15/D";
    QTest::newRow("space inside")          << "15 D";
    QTest::newRow("punctuation")           << "15.";
}

void TestCertificateRepositoryValidation::rejectsInvalidListNumbers()
{
    QFETCH(QString, listNumber);

    QSqlDatabase              db      = database();
    const InstallationContext context = officeContext();
    CertificateRepository     repository(db, context);

    Certificate certificate = sampleCertificate(QStringLiteral("v1"), QStringLiteral("Load Line"));
    certificate.listNumber  = listNumber;

    QVERIFY(!repository.create(certificate));
    QVERIFY(repository.errorString().contains(QStringLiteral("not a valid number")));
    QCOMPARE(rowCountOf(db, QStringLiteral("certificate")), 0);
}

void TestCertificateRepositoryValidation::listNumberSurvivesARoundTrip()
{
    QSqlDatabase              db      = database();
    const InstallationContext context = officeContext();
    CertificateRepository     repository(db, context);

    Certificate certificate = sampleCertificate(QStringLiteral("v1"), QStringLiteral("Load Line"));
    certificate.listNumber  = QStringLiteral("15D");

    QString newId;
    QVERIFY2(repository.create(certificate, &newId), qPrintable(repository.errorString()));

    std::optional<Certificate> stored;
    QVERIFY(repository.findById(newId, &stored));
    QVERIFY(stored.has_value());
    QCOMPARE(stored->listNumber, QStringLiteral("15D"));

    // ...and an edit can change it.
    Certificate edited = *stored;
    edited.listNumber  = QStringLiteral("3A");
    QVERIFY2(repository.update(edited), qPrintable(repository.errorString()));

    QVERIFY(repository.findById(newId, &stored));
    QCOMPARE(stored->listNumber, QStringLiteral("3A"));

    // A blank number stores as NULL, like every other optional field.
    edited.listNumber = QString();
    QVERIFY(repository.update(edited));

    QSqlQuery query(db);
    QVERIFY(query.exec(QStringLiteral("SELECT list_number IS NULL FROM certificate")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 1);
}

QTEST_MAIN(TestCertificateRepositoryValidation)
#include "tst_CertificateRepositoryValidation.moc"

// prefix-spec §5. The migration's CHECK is the last net, and the one thing it
// must not do is refuse a real code that arrived by a route which never
// consults validateCertificate() — a raw INSERT here, a future ship-to-shore
// sync in practice. Exactly the role the no-expiry CHECK's own test plays.
void TestCertificateRepositoryValidation::theWidenedCheckAcceptsPrefixedNumbersWrittenDirectly()
{
    QSqlDatabase db = database();

    // One real code from each of the eight annexes on the S-14 sheet.
    const QStringList codes{QStringLiteral("S1"),     QStringLiteral("P.48"),
                            QStringLiteral("EU.9"),   QStringLiteral("US.12"),
                            QStringLiteral("USCA.4"), QStringLiteral("USAL.2"),
                            QStringLiteral("PC.2"),   QStringLiteral("IS.2")};

    for (const QString& code : codes) {
        const QString id = QStringLiteral("raw-%1").arg(code);
        QVERIFY2(!seedCertificate(db, id, QStringLiteral("v1"), QStringLiteral("Written past"),
                                  false, code)
                      .isEmpty(),
                 qPrintable(code));
    }

    QCOMPARE(rowCountOf(db, QStringLiteral("certificate")), codes.size());
}

// Looser than isValid() on purpose (§4), but not absent. These are the three
// things GLOB can still express dependably, and all three still bite.
void TestCertificateRepositoryValidation::theWidenedCheckStillRefusesWhatGlobCanCatch()
{
    QSqlDatabase db = database();

    const QStringList rubbish{
        QStringLiteral("15-D"), // a character that is not a letter, digit or dot
        QStringLiteral("P..1"), // more than one dot
        QStringLiteral("ABC")   // no digit anywhere
    };

    for (const QString& value : rubbish) {
        QVERIFY2(seedCertificate(db, QStringLiteral("raw"), QStringLiteral("v1"),
                                 QStringLiteral("Written past"), false, value)
                     .isEmpty(),
                 qPrintable(value));
    }

    QCOMPARE(rowCountOf(db, QStringLiteral("certificate")), 0);
}
