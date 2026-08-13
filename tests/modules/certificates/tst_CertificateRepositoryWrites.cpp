#include "CertificateTestSupport.h"

#include "modules/certificates/data/CertificateRepository.h"

#include <QtTest>

using namespace CertificateTestSupport;

// certificate-crud-spec §7 item 6, plus the round trips that prove every field
// survives a write. What the repository *refuses* to write lives next door in
// tst_CertificateRepositoryValidation.
class TestCertificateRepositoryWrites : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void createStoresEveryField();
    void createStoresBlankOptionalFieldsAsNull();
    void createAcceptsNoExpiryWithoutSurveys();
    void updateIncrementsRevisionAndLeavesCreationAlone();
    void updateChangesEveryEditableField();

private:
    QSqlDatabase database() { return QSqlDatabase::database(m_connectionName); }
    QString      m_connectionName;
};

void TestCertificateRepositoryWrites::init()
{
    m_connectionName = QStringLiteral("certwrite_%1").arg(QTest::currentTestFunction());
    QVERIFY(openDatabase(m_connectionName));
}

void TestCertificateRepositoryWrites::cleanup()
{
    closeDatabase(m_connectionName);
}

void TestCertificateRepositoryWrites::createStoresEveryField()
{
    QSqlDatabase              db      = database();
    const InstallationContext context = officeContext();
    CertificateRepository     repository(db, context);

    Certificate certificate = sampleCertificate(QStringLiteral("v1"), QStringLiteral("Load Line"));
    certificate.category                   = CertificateCategory::Class;
    certificate.appliesTo                  = QStringLiteral("Liferaft No. 3, S/N 44821");
    certificate.isInterim                  = true;
    certificate.requiresIntermediateSurvey = true;
    certificate.intermediateMode           = IntermediateMode::ReplacesAnnual;
    certificate.notes                      = QStringLiteral("Renewed early in Rotterdam.");

    QString newId;
    QVERIFY2(repository.create(certificate, &newId), qPrintable(repository.errorString()));

    std::optional<Certificate> stored;
    QVERIFY(repository.findById(newId, &stored));
    QVERIFY(stored.has_value());

    QCOMPARE(stored->vesselId, QStringLiteral("v1"));
    QCOMPARE(stored->name, QStringLiteral("Load Line"));
    QCOMPARE(stored->category, CertificateCategory::Class);
    QCOMPARE(stored->certificateNumber, QStringLiteral("SC-12345"));
    QCOMPARE(stored->appliesTo, QStringLiteral("Liferaft No. 3, S/N 44821"));
    QCOMPARE(stored->issueDate, QDate(2026, 2, 4));
    QCOMPARE(stored->expiryDate, QDate(2031, 2, 4));
    QCOMPARE(stored->issuedBy, QStringLiteral("Lloyd's Register"));
    QCOMPARE(stored->placeOfIssue, QStringLiteral("Limassol"));
    QVERIFY(stored->isInterim);
    QVERIFY(stored->requiresAnnualSurvey);
    QVERIFY(stored->requiresIntermediateSurvey);
    QCOMPARE(stored->intermediateMode, IntermediateMode::ReplacesAnnual);
    QCOMPARE(stored->notes, QStringLiteral("Renewed early in Rotterdam."));

    QSqlQuery query(db);
    QVERIFY(query.exec(QStringLiteral("SELECT created_by, updated_by, origin_node, revision,"
                                      " is_deleted FROM certificate")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toString(), QStringLiteral("SYSTEM"));
    QCOMPARE(query.value(1).toString(), QStringLiteral("SYSTEM"));
    QCOMPARE(query.value(2).toString(), QStringLiteral("OFFICE"));
    QCOMPARE(query.value(3).toInt(), 1);
    QCOMPARE(query.value(4).toInt(), 0);
}

void TestCertificateRepositoryWrites::createStoresBlankOptionalFieldsAsNull()
{
    QSqlDatabase              db      = database();
    const InstallationContext context = officeContext();
    CertificateRepository     repository(db, context);

    Certificate bare;
    bare.vesselId  = QStringLiteral("v1");
    bare.name      = QStringLiteral("Bare Certificate");
    bare.category  = CertificateCategory::Other;
    bare.issueDate = QDate(2026, 2, 4);

    QString newId;
    QVERIFY2(repository.create(bare, &newId), qPrintable(repository.errorString()));

    // The same "not entered stays distinguishable" convention every table uses.
    QSqlQuery query(db);
    QVERIFY(query.exec(QStringLiteral(
        "SELECT certificate_number IS NULL, applies_to IS NULL, issued_by IS NULL,"
        " place_of_issue IS NULL, notes IS NULL FROM certificate")));
    QVERIFY(query.next());
    for (int column = 0; column < 5; ++column) {
        QVERIFY2(query.value(column).toInt() == 1, "a blank optional field should store as NULL");
    }
}

void TestCertificateRepositoryWrites::createAcceptsNoExpiryWithoutSurveys()
{
    // A Tonnage Certificate, typically: never expires, no survey schedule.
    QSqlDatabase              db      = database();
    const InstallationContext context = officeContext();
    CertificateRepository     repository(db, context);

    Certificate certificate = sampleCertificate(QStringLiteral("v1"), QStringLiteral("Tonnage"));
    certificate.expiryDate                 = QDate();
    certificate.requiresAnnualSurvey       = false;
    certificate.requiresIntermediateSurvey = false;

    QString newId;
    QVERIFY2(repository.create(certificate, &newId), qPrintable(repository.errorString()));

    std::optional<Certificate> stored;
    QVERIFY(repository.findById(newId, &stored));
    QVERIFY(stored.has_value());
    QVERIFY2(stored->neverExpires(), "a null expiry must read back as null, not as some date");

    QSqlQuery query(db);
    QVERIFY(query.exec(QStringLiteral("SELECT expiry_date IS NULL FROM certificate")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 1);
}

void TestCertificateRepositoryWrites::updateIncrementsRevisionAndLeavesCreationAlone()
{
    QSqlDatabase              db      = database();
    const InstallationContext context = officeContext();
    CertificateRepository     repository(db, context);

    QString newId;
    QVERIFY(repository.create(sampleCertificate(QStringLiteral("v1"),
                                                QStringLiteral("Load Line")),
                              &newId));

    QSqlQuery before(db);
    QVERIFY(before.exec(
        QStringLiteral("SELECT revision, created_at, created_by FROM certificate")));
    QVERIFY(before.next());
    const int     revisionBefore  = before.value(0).toInt();
    const QString createdAtBefore = before.value(1).toString();
    const QString createdByBefore = before.value(2).toString();
    QCOMPARE(revisionBefore, 1);

    Certificate edited = sampleCertificate(QStringLiteral("v1"), QStringLiteral("Load Line"));
    edited.id          = newId;
    QVERIFY2(repository.update(edited), qPrintable(repository.errorString()));

    QSqlQuery after(db);
    QVERIFY(after.exec(QStringLiteral(
        "SELECT revision, created_at, created_by, updated_by FROM certificate")));
    QVERIFY(after.next());
    QCOMPARE(after.value(0).toInt(), revisionBefore + 1);
    QCOMPARE(after.value(1).toString(), createdAtBefore); // creation untouched
    QCOMPARE(after.value(2).toString(), createdByBefore);
    QCOMPARE(after.value(3).toString(), QStringLiteral("SYSTEM"));

    // A second save bumps it again, rather than sticking at 2.
    QVERIFY(repository.update(edited));
    QSqlQuery third(db);
    QVERIFY(third.exec(QStringLiteral("SELECT revision FROM certificate")));
    QVERIFY(third.next());
    QCOMPARE(third.value(0).toInt(), revisionBefore + 2);
}

void TestCertificateRepositoryWrites::updateChangesEveryEditableField()
{
    QSqlDatabase              db      = database();
    const InstallationContext context = officeContext();
    CertificateRepository     repository(db, context);

    QString newId;
    QVERIFY(repository.create(sampleCertificate(QStringLiteral("v1"),
                                                QStringLiteral("Load Line")),
                              &newId));

    Certificate edited;
    edited.id                         = newId;
    edited.vesselId                   = QStringLiteral("v1");
    edited.name                       = QStringLiteral("Safety Radio Certificate");
    edited.category                   = CertificateCategory::Equipment;
    edited.certificateNumber          = QStringLiteral("SR-999");
    edited.appliesTo                  = QStringLiteral("EPIRB");
    edited.issueDate                  = QDate(2027, 3, 5);
    edited.expiryDate                 = QDate(2032, 3, 5);
    edited.issuedBy                   = QStringLiteral("DNV");
    edited.placeOfIssue               = QStringLiteral("Valletta");
    edited.isInterim                  = true;
    edited.requiresAnnualSurvey       = false;
    edited.requiresIntermediateSurvey = true;
    edited.intermediateMode           = IntermediateMode::ReplacesAnnual;
    edited.notes                      = QStringLiteral("Replaced after a radio refit.");

    QVERIFY2(repository.update(edited), qPrintable(repository.errorString()));

    std::optional<Certificate> stored;
    QVERIFY(repository.findById(newId, &stored));
    QVERIFY(stored.has_value());
    QCOMPARE(stored->name, QStringLiteral("Safety Radio Certificate"));
    QCOMPARE(stored->category, CertificateCategory::Equipment);
    QCOMPARE(stored->certificateNumber, QStringLiteral("SR-999"));
    QCOMPARE(stored->appliesTo, QStringLiteral("EPIRB"));
    QCOMPARE(stored->issueDate, QDate(2027, 3, 5));
    QCOMPARE(stored->expiryDate, QDate(2032, 3, 5));
    QCOMPARE(stored->issuedBy, QStringLiteral("DNV"));
    QCOMPARE(stored->placeOfIssue, QStringLiteral("Valletta"));
    QVERIFY(stored->isInterim);
    QVERIFY(!stored->requiresAnnualSurvey);
    QVERIFY(stored->requiresIntermediateSurvey);
    QCOMPARE(stored->intermediateMode, IntermediateMode::ReplacesAnnual);
    QCOMPARE(stored->notes, QStringLiteral("Replaced after a radio refit."));
}

QTEST_MAIN(TestCertificateRepositoryWrites)
#include "tst_CertificateRepositoryWrites.moc"
