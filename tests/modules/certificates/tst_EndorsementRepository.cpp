#include "CertificateTestSupport.h"

#include "modules/certificates/data/EndorsementRepository.h"

#include <QtTest>

using namespace CertificateTestSupport;

// certificate-endorsement-spec §8 items 15, 16 and 17.
class TestEndorsementRepository : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    // Item 15
    void createRefusesAnUnknownCertificate();
    void createRefusesASoftDeletedCertificate();
    void createRefusesAnotherVesselsCertificateInVesselMode();

    // Item 16
    void createRefusesAnUnsetSurveyType();
    void createRefusesAMissingDate();
    void createRefusesADateBeforeTheCertificateWasIssued();
    void rawCheckConstraintTextNeverReachesTheUser();

    // Item 17
    void listNeverReturnsAnotherVesselsEndorsementsInVesselMode();
    void listExcludesDeletedRows();

    void createStoresEveryFieldAndAuditColumns();
    void listReturnsEndorsementsInDateOrder();

private:
    QSqlDatabase database() { return QSqlDatabase::database(m_connectionName); }
    Endorsement  sampleEndorsement(const QString& certificateId) const;

    QString m_connectionName;
};

void TestEndorsementRepository::init()
{
    m_connectionName = QStringLiteral("endrepo_%1").arg(QTest::currentTestFunction());
    QVERIFY(openDatabase(m_connectionName));
}

void TestEndorsementRepository::cleanup()
{
    closeDatabase(m_connectionName);
}

Endorsement TestEndorsementRepository::sampleEndorsement(const QString& certificateId) const
{
    Endorsement endorsement;
    endorsement.certificateId   = certificateId;
    endorsement.surveyType      = SurveyType::Annual;
    endorsement.endorsementDate = QDate(2027, 2, 4);
    endorsement.place           = QStringLiteral("Rotterdam");
    endorsement.surveyor        = QStringLiteral("Lloyd's Register");
    endorsement.result          = QStringLiteral("Satisfactory");
    endorsement.remarks         = QStringLiteral("No outstanding items.");
    return endorsement;
}

void TestEndorsementRepository::createRefusesAnUnknownCertificate()
{
    QSqlDatabase              db      = database();
    const InstallationContext context = officeContext();
    EndorsementRepository     repository(db, context);

    QVERIFY(!repository.create(sampleEndorsement(QStringLiteral("no-such-certificate"))));
    QVERIFY(repository.errorString().contains(QStringLiteral("no longer exists")));
    QCOMPARE(rowCountOf(db, QStringLiteral("endorsement")), 0);
}

void TestEndorsementRepository::createRefusesASoftDeletedCertificate()
{
    QSqlDatabase db = database();
    seedCertificate(db, QStringLiteral("c1"), QStringLiteral("v1"), QStringLiteral("Load Line"),
                    /*deleted=*/true);

    const InstallationContext context = officeContext();
    EndorsementRepository     repository(db, context);

    QVERIFY(!repository.create(sampleEndorsement(QStringLiteral("c1"))));
    QCOMPARE(rowCountOf(db, QStringLiteral("endorsement")), 0);
}

void TestEndorsementRepository::createRefusesAnotherVesselsCertificateInVesselMode()
{
    QSqlDatabase db = database();
    seedCertificate(db, QStringLiteral("c2"), QStringLiteral("v2"), QStringLiteral("Secret"));

    const InstallationContext context = vesselContext(QStringLiteral("v1"));
    EndorsementRepository     repository(db, context);

    QVERIFY(!repository.create(sampleEndorsement(QStringLiteral("c2"))));
    QVERIFY(repository.errorString().contains(QStringLiteral("own vessel")));
    QCOMPARE(rowCountOf(db, QStringLiteral("endorsement")), 0);
}

void TestEndorsementRepository::createRefusesAnUnsetSurveyType()
{
    QSqlDatabase db = database();
    seedCertificate(db, QStringLiteral("c1"), QStringLiteral("v1"), QStringLiteral("Load Line"));

    const InstallationContext context = officeContext();
    EndorsementRepository     repository(db, context);

    Endorsement endorsement = sampleEndorsement(QStringLiteral("c1"));
    endorsement.surveyType  = SurveyType::Unset;

    QVERIFY(!repository.create(endorsement));
    QVERIFY(repository.errorString().contains(QStringLiteral("survey type is required")));
    QCOMPARE(rowCountOf(db, QStringLiteral("endorsement")), 0);
}

void TestEndorsementRepository::createRefusesAMissingDate()
{
    QSqlDatabase db = database();
    seedCertificate(db, QStringLiteral("c1"), QStringLiteral("v1"), QStringLiteral("Load Line"));

    const InstallationContext context = officeContext();
    EndorsementRepository     repository(db, context);

    Endorsement endorsement      = sampleEndorsement(QStringLiteral("c1"));
    endorsement.endorsementDate  = QDate();

    QVERIFY(!repository.create(endorsement));
    QVERIFY(repository.errorString().contains(QStringLiteral("date is required")));
    QCOMPARE(rowCountOf(db, QStringLiteral("endorsement")), 0);
}

void TestEndorsementRepository::createRefusesADateBeforeTheCertificateWasIssued()
{
    // certificate-control-spec.md §4.8 item 5: a survey cannot have happened
    // before the certificate existed. The seeded certificate is issued
    // 2026-02-04.
    QSqlDatabase db = database();
    seedCertificate(db, QStringLiteral("c1"), QStringLiteral("v1"), QStringLiteral("Load Line"));

    const InstallationContext context = officeContext();
    EndorsementRepository     repository(db, context);

    Endorsement endorsement     = sampleEndorsement(QStringLiteral("c1"));
    endorsement.endorsementDate = QDate(2026, 2, 3);

    QVERIFY(!repository.create(endorsement));
    QVERIFY(repository.errorString().contains(QStringLiteral("before the certificate was issued")));
    QCOMPARE(rowCountOf(db, QStringLiteral("endorsement")), 0);

    // The issue date itself is acceptable.
    endorsement.endorsementDate = QDate(2026, 2, 4);
    QVERIFY2(repository.create(endorsement), qPrintable(repository.errorString()));
}

void TestEndorsementRepository::rawCheckConstraintTextNeverReachesTheUser()
{
    QSqlDatabase db = database();
    seedCertificate(db, QStringLiteral("c1"), QStringLiteral("v1"), QStringLiteral("Load Line"));

    const InstallationContext context = officeContext();
    EndorsementRepository     repository(db, context);

    Endorsement endorsement = sampleEndorsement(QStringLiteral("c1"));
    endorsement.surveyType  = SurveyType::Unset; // would violate the CHECK

    QVERIFY(!repository.create(endorsement));
    const QString message = repository.errorString();
    QVERIFY2(!message.contains(QStringLiteral("CHECK"), Qt::CaseInsensitive),
             "the raw SQLite constraint text must not reach the user");
    QVERIFY2(!message.contains(QStringLiteral("constraint"), Qt::CaseInsensitive),
             "the raw SQLite constraint text must not reach the user");
}

void TestEndorsementRepository::listNeverReturnsAnotherVesselsEndorsementsInVesselMode()
{
    QSqlDatabase db = database();
    seedCertificate(db, QStringLiteral("c1"), QStringLiteral("v1"), QStringLiteral("Ours"));
    seedCertificate(db, QStringLiteral("c2"), QStringLiteral("v2"), QStringLiteral("Theirs"));

    // Seeded directly, as a future sync might leave behind.
    QSqlQuery seed(db);
    seed.prepare(QStringLiteral(
        "INSERT INTO endorsement (id, certificate_id, survey_type, endorsement_date,"
        " created_at, created_by, updated_at, updated_by, is_deleted, origin_node, revision)"
        " VALUES (?, ?, 'ANNUAL', '2027-02-04', '2026-01-01T00:00:00Z', 'SEED',"
        " '2026-01-01T00:00:00Z', 'SEED', 0, 'OFFICE', 1)"));
    seed.addBindValue(QStringLiteral("e2"));
    seed.addBindValue(QStringLiteral("c2"));
    QVERIFY(seed.exec());

    const InstallationContext context = vesselContext(QStringLiteral("v1"));
    EndorsementRepository     repository(db, context);

    QList<Endorsement> endorsements;
    QVERIFY(repository.list(QStringLiteral("c2"), &endorsements));
    QVERIFY2(endorsements.isEmpty(),
             "a vessel installation must not see another ship's endorsements");
}

void TestEndorsementRepository::listExcludesDeletedRows()
{
    QSqlDatabase db = database();
    seedCertificate(db, QStringLiteral("c1"), QStringLiteral("v1"), QStringLiteral("Load Line"));

    const InstallationContext context = officeContext();
    EndorsementRepository     repository(db, context);

    QString keptId;
    QVERIFY(repository.create(sampleEndorsement(QStringLiteral("c1")), &keptId));

    QString goneId;
    Endorsement second      = sampleEndorsement(QStringLiteral("c1"));
    second.endorsementDate  = QDate(2028, 2, 4);
    QVERIFY(repository.create(second, &goneId));

    QSqlQuery remove(db);
    remove.prepare(QStringLiteral("UPDATE endorsement SET is_deleted = 1 WHERE id = ?"));
    remove.addBindValue(goneId);
    QVERIFY(remove.exec());

    QList<Endorsement> endorsements;
    QVERIFY(repository.list(QStringLiteral("c1"), &endorsements));
    QCOMPARE(endorsements.size(), 1);
    QCOMPARE(endorsements.first().id, keptId);
}

void TestEndorsementRepository::createStoresEveryFieldAndAuditColumns()
{
    QSqlDatabase db = database();
    seedCertificate(db, QStringLiteral("c1"), QStringLiteral("v1"), QStringLiteral("Load Line"));

    const InstallationContext context = officeContext();
    EndorsementRepository     repository(db, context);

    QString newId;
    QVERIFY2(repository.create(sampleEndorsement(QStringLiteral("c1")), &newId),
             qPrintable(repository.errorString()));

    QList<Endorsement> endorsements;
    QVERIFY(repository.list(QStringLiteral("c1"), &endorsements));
    QCOMPARE(endorsements.size(), 1);

    const Endorsement& stored = endorsements.first();
    QCOMPARE(stored.id, newId);
    QCOMPARE(stored.certificateId, QStringLiteral("c1"));
    QCOMPARE(stored.surveyType, SurveyType::Annual);
    QCOMPARE(stored.endorsementDate, QDate(2027, 2, 4));
    QCOMPARE(stored.place, QStringLiteral("Rotterdam"));
    QCOMPARE(stored.surveyor, QStringLiteral("Lloyd's Register"));
    QCOMPARE(stored.result, QStringLiteral("Satisfactory"));
    QCOMPARE(stored.remarks, QStringLiteral("No outstanding items."));

    QSqlQuery query(db);
    QVERIFY(query.exec(QStringLiteral("SELECT created_by, updated_by, origin_node, revision,"
                                      " is_deleted FROM endorsement")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toString(), QStringLiteral("SYSTEM"));
    QCOMPARE(query.value(1).toString(), QStringLiteral("SYSTEM"));
    QCOMPARE(query.value(2).toString(), QStringLiteral("OFFICE"));
    QCOMPARE(query.value(3).toInt(), 1);
    QCOMPARE(query.value(4).toInt(), 0);
}

void TestEndorsementRepository::listReturnsEndorsementsInDateOrder()
{
    QSqlDatabase db = database();
    seedCertificate(db, QStringLiteral("c1"), QStringLiteral("v1"), QStringLiteral("Load Line"));

    const InstallationContext context = officeContext();
    EndorsementRepository     repository(db, context);

    Endorsement later      = sampleEndorsement(QStringLiteral("c1"));
    later.endorsementDate  = QDate(2029, 2, 4);
    Endorsement earlier    = sampleEndorsement(QStringLiteral("c1"));
    earlier.endorsementDate = QDate(2027, 2, 4);

    QVERIFY(repository.create(later));
    QVERIFY(repository.create(earlier));

    QList<Endorsement> endorsements;
    QVERIFY(repository.list(QStringLiteral("c1"), &endorsements));
    QCOMPARE(endorsements.size(), 2);
    QCOMPARE(endorsements.at(0).endorsementDate, QDate(2027, 2, 4));
    QCOMPARE(endorsements.at(1).endorsementDate, QDate(2029, 2, 4));
}

QTEST_MAIN(TestEndorsementRepository)
#include "tst_EndorsementRepository.moc"
