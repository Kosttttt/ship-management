#include "CertificateTestSupport.h"

#include "modules/certificates/data/CertificateRepository.h"

#include <QtTest>

using namespace CertificateTestSupport;

// certificate-crud-spec §7 items 1 and 2: the VESSEL-mode scope filter and the
// is_deleted exclusion, both applied inside the repository.
class TestCertificateRepositoryScope : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void listInOfficeModeReturnsEveryCertificateOfThatVessel();
    void listInVesselModeIgnoresAnotherVesselsCertificates();
    void listInVesselModeRefusesAnotherVesselIdOutright();
    void findByIdInVesselModeRefusesAnotherVesselsCertificate();
    void listExcludesDeletedRows();
    void findByIdExcludesDeletedRows();

private:
    QSqlDatabase database() { return QSqlDatabase::database(m_connectionName); }
    QString      m_connectionName;
};

void TestCertificateRepositoryScope::init()
{
    m_connectionName = QStringLiteral("certscope_%1").arg(QTest::currentTestFunction());
    QVERIFY(openDatabase(m_connectionName));
}

void TestCertificateRepositoryScope::cleanup()
{
    closeDatabase(m_connectionName);
}

void TestCertificateRepositoryScope::listInOfficeModeReturnsEveryCertificateOfThatVessel()
{
    QSqlDatabase db = database();
    seedCertificate(db, QStringLiteral("c1"), QStringLiteral("v1"), QStringLiteral("Load Line"));
    seedCertificate(db, QStringLiteral("c2"), QStringLiteral("v1"), QStringLiteral("IOPP"));
    seedCertificate(db, QStringLiteral("c3"), QStringLiteral("v2"), QStringLiteral("Tonnage"));

    const InstallationContext context = officeContext();
    CertificateRepository     repository(db, context);

    QList<Certificate> certificates;
    QVERIFY2(repository.list(QStringLiteral("v1"), &certificates),
             qPrintable(repository.errorString()));

    // Only vessel v1's, and sorted by name.
    QCOMPARE(certificates.size(), 2);
    QCOMPARE(certificates.at(0).name, QStringLiteral("IOPP"));
    QCOMPARE(certificates.at(1).name, QStringLiteral("Load Line"));
}

void TestCertificateRepositoryScope::listInVesselModeIgnoresAnotherVesselsCertificates()
{
    // Rows for another vessel exist, as a future sync might leave behind.
    QSqlDatabase db = database();
    seedCertificate(db, QStringLiteral("c1"), QStringLiteral("v1"), QStringLiteral("Load Line"));
    seedCertificate(db, QStringLiteral("c2"), QStringLiteral("v2"), QStringLiteral("Secret"));
    QCOMPARE(rowCountOf(db, QStringLiteral("certificate")), 2);

    const InstallationContext context = vesselContext(QStringLiteral("v1"));
    CertificateRepository     repository(db, context);

    QList<Certificate> certificates;
    QVERIFY(repository.list(QStringLiteral("v1"), &certificates));
    QCOMPARE(certificates.size(), 1);
    QCOMPARE(certificates.at(0).name, QStringLiteral("Load Line"));
}

void TestCertificateRepositoryScope::listInVesselModeRefusesAnotherVesselIdOutright()
{
    QSqlDatabase db = database();
    seedCertificate(db, QStringLiteral("c2"), QStringLiteral("v2"), QStringLiteral("Secret"));

    const InstallationContext context = vesselContext(QStringLiteral("v1"));
    CertificateRepository     repository(db, context);

    // Asking directly for the other vessel's list is not an error — it simply
    // returns nothing, the "not found is a successful answer" precedent.
    QList<Certificate> certificates;
    QVERIFY(repository.list(QStringLiteral("v2"), &certificates));
    QVERIFY(certificates.isEmpty());
}

void TestCertificateRepositoryScope::findByIdInVesselModeRefusesAnotherVesselsCertificate()
{
    QSqlDatabase db = database();
    seedCertificate(db, QStringLiteral("c1"), QStringLiteral("v1"), QStringLiteral("Load Line"));
    seedCertificate(db, QStringLiteral("c2"), QStringLiteral("v2"), QStringLiteral("Secret"));

    const InstallationContext context = vesselContext(QStringLiteral("v1"));
    CertificateRepository     repository(db, context);

    std::optional<Certificate> found;
    QVERIFY(repository.findById(QStringLiteral("c2"), &found));
    QVERIFY2(!found.has_value(), "a vessel installation must not see another ship's certificate");

    QVERIFY(repository.findById(QStringLiteral("c1"), &found));
    QVERIFY(found.has_value());
    QCOMPARE(found->name, QStringLiteral("Load Line"));
}

void TestCertificateRepositoryScope::listExcludesDeletedRows()
{
    QSqlDatabase db = database();
    seedCertificate(db, QStringLiteral("c1"), QStringLiteral("v1"), QStringLiteral("Load Line"));
    seedCertificate(db, QStringLiteral("c2"), QStringLiteral("v1"), QStringLiteral("Gone"),
                    /*deleted=*/true);

    const InstallationContext context = officeContext();
    CertificateRepository     repository(db, context);

    QList<Certificate> certificates;
    QVERIFY(repository.list(QStringLiteral("v1"), &certificates));
    QCOMPARE(certificates.size(), 1);
    QCOMPARE(certificates.at(0).name, QStringLiteral("Load Line"));
}

void TestCertificateRepositoryScope::findByIdExcludesDeletedRows()
{
    QSqlDatabase db = database();
    seedCertificate(db, QStringLiteral("c2"), QStringLiteral("v1"), QStringLiteral("Gone"),
                    /*deleted=*/true);

    const InstallationContext context = officeContext();
    CertificateRepository     repository(db, context);

    std::optional<Certificate> found;
    QVERIFY(repository.findById(QStringLiteral("c2"), &found));
    QVERIFY(!found.has_value());
}

QTEST_MAIN(TestCertificateRepositoryScope)
#include "tst_CertificateRepositoryScope.moc"
