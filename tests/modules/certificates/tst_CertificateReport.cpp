#include "CertificateTestSupport.h"

#include "core/AppSettingRepository.h"
#include "modules/certificates/data/CertificateExtensionRepository.h"
#include "modules/certificates/data/CertificateRepository.h"
#include "modules/certificates/data/EndorsementRepository.h"
#include "modules/certificates/domain/CertificateState.h"
#include "modules/certificates/service/CertificateReportBuilder.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using namespace CertificateTestSupport;

// certificate-import-export-spec §8 and §9.18: the printable statement for
// one vessel.
class TestCertificateReport : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void theHeadingNamesTheVesselAndItsImoNumber();
    void theGenerationTimestampIsTheOnePassedIn();
    void thereIsOneRowPerCertificateInListNumberOrder();
    void aSoftDeletedCertificateIsNotListed();
    void anotherVesselsCertificatesAreNeverListed();

    // §8: the status column is computed from dates at the moment of export,
    // never read from a stored value (CLAUDE.md §6.7).
    void anExpiredCertificateSaysSo();
    void anOutstandingSurveyShowsItsWindowAndStatus();
    void anActiveShortTermExtensionTakesOverTheExpiryCell();

    // §9.18
    void aNeverExpiringCertificateShowsNoSurveyWindow();
    void aVesselWithNoCertificatesStillRenders();

    void valuesAreEscapedSoAnAmpersandCannotBreakTheDocument();
    void aPdfFileIsActuallyWritten();
    void exportRefusesAVesselThatDoesNotExist();

private:
    QSqlDatabase database() { return QSqlDatabase::database(m_connectionName); }
    QString      buildHtml(const QDateTime& generatedAt);

    QString       m_connectionName;
    QString       m_vesselId;
    QTemporaryDir m_folder;
};

void TestCertificateReport::init()
{
    m_connectionName = QStringLiteral("report_%1").arg(QTest::currentTestFunction());
    QVERIFY(openDatabase(m_connectionName));
    QVERIFY(m_folder.isValid());

    m_vesselId      = QStringLiteral("v1");
    QSqlDatabase db = database();
    QVERIFY(!seedVessel(db, m_vesselId, QStringLiteral("MV Aurora"),
                        QStringLiteral("9074729"))
                 .isEmpty());
}

void TestCertificateReport::cleanup()
{
    closeDatabase(m_connectionName);
}

QString TestCertificateReport::buildHtml(const QDateTime& generatedAt)
{
    QSqlDatabase                   db = database();
    CertificateRepository          certificates(db, officeContext());
    EndorsementRepository          endorsements(db, officeContext());
    CertificateExtensionRepository extensions(db, officeContext());
    AppSettingRepository           appSettings(db);
    VesselRepository               vessels(db, officeContext());

    CertificateReportBuilder builder(certificates, endorsements, extensions, appSettings,
                                     vessels);

    QString html;
    if (!builder.buildHtml(m_vesselId, generatedAt, &html)) {
        qWarning() << "could not build the statement:" << builder.errorString();
        return QString();
    }
    return html;
}

void TestCertificateReport::theHeadingNamesTheVesselAndItsImoNumber()
{
    const QString html = buildHtml(QDateTime(QDate(2029, 6, 1), QTime(9, 30)));
    QVERIFY(html.contains(QStringLiteral("MV Aurora")));
    QVERIFY(html.contains(QStringLiteral("IMO 9074729")));
}

void TestCertificateReport::theGenerationTimestampIsTheOnePassedIn()
{
    // A parameter, not a clock read inside, which is the only way this can be
    // asserted at all (§8).
    const QString html = buildHtml(QDateTime(QDate(2029, 6, 1), QTime(9, 30)));
    QVERIFY(html.contains(QStringLiteral("01 Jun 2029 09:30")));
    QVERIFY(html.contains(QStringLiteral("Generated")));
}

void TestCertificateReport::thereIsOneRowPerCertificateInListNumberOrder()
{
    QSqlDatabase db = database();
    // Alphabetically Anchor, Boiler; by reference number 3 then 15D.
    seedCertificate(db, QStringLiteral("c1"), m_vesselId, QStringLiteral("Anchor"), false,
                    QStringLiteral("15D"));
    seedCertificate(db, QStringLiteral("c2"), m_vesselId, QStringLiteral("Boiler"), false,
                    QStringLiteral("3"));

    const QString html = buildHtml(QDateTime(QDate(2029, 6, 1), QTime(9, 30)));
    QVERIFY(html.contains(QStringLiteral("Anchor")));
    QVERIFY(html.contains(QStringLiteral("Boiler")));
    QVERIFY2(html.indexOf(QStringLiteral("Boiler")) < html.indexOf(QStringLiteral("Anchor")),
             "the statement follows the screen's reference-number order, not list()'s names");
}

void TestCertificateReport::aSoftDeletedCertificateIsNotListed()
{
    QSqlDatabase db = database();
    seedCertificate(db, QStringLiteral("c1"), m_vesselId, QStringLiteral("Live"));
    seedCertificate(db, QStringLiteral("c2"), m_vesselId, QStringLiteral("Deleted"), true);

    const QString html = buildHtml(QDateTime(QDate(2029, 6, 1), QTime(9, 30)));
    QVERIFY(html.contains(QStringLiteral("Live")));
    QVERIFY(!html.contains(QStringLiteral("Deleted")));
}

void TestCertificateReport::anotherVesselsCertificatesAreNeverListed()
{
    QSqlDatabase db = database();
    seedVessel(db, QStringLiteral("v2"), QStringLiteral("MV Other"), QStringLiteral("9074730"));
    seedCertificate(db, QStringLiteral("c1"), m_vesselId, QStringLiteral("Mine"));
    seedCertificate(db, QStringLiteral("c2"), QStringLiteral("v2"), QStringLiteral("Theirs"));

    const QString html = buildHtml(QDateTime(QDate(2029, 6, 1), QTime(9, 30)));
    QVERIFY(html.contains(QStringLiteral("Mine")));
    QVERIFY(!html.contains(QStringLiteral("Theirs")));
}

void TestCertificateReport::anExpiredCertificateSaysSo()
{
    QSqlDatabase db = database();
    seedCertificateWithSchedule(db, QStringLiteral("c1"), m_vesselId, QStringLiteral("Load Line"),
                                QStringLiteral("1"), QDate(2020, 1, 1), QDate(2025, 1, 1),
                                false, false);

    const QString html = buildHtml(QDateTime(QDate(2029, 6, 1), QTime(9, 30)));
    QVERIFY(html.contains(displayStatusLabel(DisplayStatus::Expired)));
}

void TestCertificateReport::anOutstandingSurveyShowsItsWindowAndStatus()
{
    // Expiry 2031-02-04 gives anniversaries from 2027; with no annual ever
    // recorded, the 2027 window closed on 2027-05-04 and the survey is
    // overdue as at 2029.
    QSqlDatabase db = database();
    seedCertificateWithSchedule(db, QStringLiteral("c1"), m_vesselId,
                                QStringLiteral("Safety Construction"), QStringLiteral("1"),
                                QDate(2026, 2, 4), QDate(2031, 2, 4), true, false);

    const QString html = buildHtml(QDateTime(QDate(2029, 6, 1), QTime(9, 30)));
    QVERIFY(html.contains(displayStatusLabel(DisplayStatus::SurveyOverdue)));
    QVERIFY2(html.contains(QStringLiteral("04 Nov 2026")), "the window's opening date");
    QVERIFY2(html.contains(QStringLiteral("04 May 2027")), "the window's closing date");
}

void TestCertificateReport::anActiveShortTermExtensionTakesOverTheExpiryCell()
{
    QSqlDatabase db = database();
    seedCertificateWithSchedule(db, QStringLiteral("c1"), m_vesselId, QStringLiteral("Load Line"),
                                QStringLiteral("1"), QDate(2024, 1, 1), QDate(2029, 1, 1),
                                false, false);

    CertificateExtensionRepository extensions(db, officeContext());
    CertificateExtension           extension;
    extension.certificateId = QStringLiteral("c1");
    extension.expiryDate    = QDate(2029, 9, 1);
    QVERIFY2(extensions.start(extension), qPrintable(extensions.errorString()));

    const QString html = buildHtml(QDateTime(QDate(2029, 6, 1), QTime(9, 30)));
    QVERIFY2(html.contains(QStringLiteral("01 Sep 2029")),
             "the extension's own date, not the certificate's expired one");
    QVERIFY(html.contains(QStringLiteral("short term")));
}

void TestCertificateReport::aNeverExpiringCertificateShowsNoSurveyWindow()
{
    // §9.18, and the same blank-cell behaviour the list screen has.
    QSqlDatabase db = database();
    seedCertificateWithSchedule(db, QStringLiteral("c1"), m_vesselId, QStringLiteral("Tonnage"),
                                QStringLiteral("1"), QDate(2026, 2, 4), QDate(), false, false);

    const QString html = buildHtml(QDateTime(QDate(2029, 6, 1), QTime(9, 30)));
    QVERIFY(html.contains(QStringLiteral("Tonnage")));
    QVERIFY(html.contains(QStringLiteral("Does not expire")));
    QVERIFY(html.contains(displayStatusLabel(DisplayStatus::Valid)));
    // No survey window anywhere: nothing to schedule against.
    QVERIFY(!html.contains(QStringLiteral("Nov 20")));
}

void TestCertificateReport::aVesselWithNoCertificatesStillRenders()
{
    // §9.18. An empty table could be read as a page that failed to print, so
    // the document says it out loud.
    const QString html = buildHtml(QDateTime(QDate(2029, 6, 1), QTime(9, 30)));
    QVERIFY(!html.isEmpty());
    QVERIFY(html.contains(QStringLiteral("MV Aurora")));
    QVERIFY(html.contains(QStringLiteral("No certificates are recorded")));
}

void TestCertificateReport::valuesAreEscapedSoAnAmpersandCannotBreakTheDocument()
{
    QSqlDatabase db = database();
    seedCertificate(db, QStringLiteral("c1"), m_vesselId,
                    QStringLiteral("Hull & Machinery <Class>"));

    const QString html = buildHtml(QDateTime(QDate(2029, 6, 1), QTime(9, 30)));
    QVERIFY(html.contains(QStringLiteral("Hull &amp; Machinery &lt;Class&gt;")));
}

void TestCertificateReport::aPdfFileIsActuallyWritten()
{
    QSqlDatabase db = database();
    seedCertificate(db, QStringLiteral("c1"), m_vesselId, QStringLiteral("Safety Radio"));

    CertificateRepository          certificates(db, officeContext());
    EndorsementRepository          endorsements(db, officeContext());
    CertificateExtensionRepository extensions(db, officeContext());
    AppSettingRepository           appSettings(db);
    VesselRepository               vessels(db, officeContext());
    CertificateReportBuilder       builder(certificates, endorsements, extensions, appSettings,
                                           vessels);

    const QString path = m_folder.filePath(QStringLiteral("statement.pdf"));
    QVERIFY2(builder.exportToPdf(m_vesselId, path), qPrintable(builder.errorString()));

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray bytes = file.readAll();
    QVERIFY2(bytes.startsWith(QByteArray("%PDF")), "that has to be a real PDF, not an empty file");
    QVERIFY(bytes.size() > 500);
}

void TestCertificateReport::exportRefusesAVesselThatDoesNotExist()
{
    QSqlDatabase                   db = database();
    CertificateRepository          certificates(db, officeContext());
    EndorsementRepository          endorsements(db, officeContext());
    CertificateExtensionRepository extensions(db, officeContext());
    AppSettingRepository           appSettings(db);
    VesselRepository               vessels(db, officeContext());
    CertificateReportBuilder       builder(certificates, endorsements, extensions, appSettings,
                                           vessels);

    QString html;
    QVERIFY(!builder.buildHtml(QStringLiteral("no-such-vessel"),
                               QDateTime(QDate(2029, 6, 1), QTime(9, 30)), &html));
    QVERIFY(!builder.errorString().isEmpty());
}

QTEST_MAIN(TestCertificateReport)
#include "tst_CertificateReport.moc"
