#include "CertificateTestSupport.h"

#include "core/CsvFile.h"
#include "modules/certificates/data/CertificateRepository.h"
#include "modules/certificates/data/EndorsementRepository.h"
#include "modules/certificates/service/CertificateCsvExporter.h"
#include "modules/certificates/service/CertificateCsvFormat.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using namespace CertificateTestSupport;

// certificate-import-export-spec §4, and the file-format half of §9.15 to
// §9.17 as the real exporter produces it.
class TestCertificateCsvExport : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void theFirstRowIsTheHeaderAndThenOneRowPerCertificate();
    void everyRowCarriesItsOwnIdSoAReImportCanMatchIt();
    void everyRowCarriesTheVesselNameAndImoNumber();

    // §4: list() sorts by name; the file follows the screen instead.
    void rowsAreSortedByListNumberRatherThanByName();
    void aBlankListNumberSortsLast();

    // §6 / §4
    void surveyHistoryIsWrittenAsASemicolonSeparatedList();
    void aTrackWithNoHistoryWritesABlankCell();

    // §9.17
    void exportingAgainIncludesASurveyRecordedSinceTheFirstExport();

    void aNeverExpiringCertificateWritesABlankExpiryCell();
    void aSoftDeletedCertificateIsNotExported();
    void anotherVesselsCertificatesAreNeverExported();
    void exportRefusesAVesselThatDoesNotExist();

    // §9.15 / §9.16, end to end through CsvFile.
    void freeTextWithACommaAQuoteAndANewlineRoundTrips();
    void nonAsciiTextIsWrittenWithTheByteOrderMarkAndReadBack();

private:
    QSqlDatabase database() { return QSqlDatabase::database(m_connectionName); }

    // The file as rows of text, read back the way an import would read it.
    QList<QStringList> exportAndRead(CertificateCsvExporter& exporter, const QString& vesselId);
    static QString     valueOf(const QList<QStringList>& rows, int dataRow, const QString& column);
    static QStringList columnValues(const QList<QStringList>& rows, const QString& column);

    QString       m_connectionName;
    QString       m_vesselId;
    QTemporaryDir m_folder;
};

void TestCertificateCsvExport::init()
{
    m_connectionName = QStringLiteral("csvexport_%1").arg(QTest::currentTestFunction());
    QVERIFY(openDatabase(m_connectionName));
    QVERIFY(m_folder.isValid());

    m_vesselId = QStringLiteral("v1");
    QSqlDatabase db = database();
    QVERIFY(!seedVessel(db, m_vesselId, QStringLiteral("MV Aurora"),
                        QStringLiteral("9074729"))
                 .isEmpty());
}

void TestCertificateCsvExport::cleanup()
{
    closeDatabase(m_connectionName);
}

QList<QStringList> TestCertificateCsvExport::exportAndRead(CertificateCsvExporter& exporter,
                                                           const QString&          vesselId)
{
    const QString path = m_folder.filePath(QStringLiteral("%1.csv").arg(
        QString::fromLatin1(QTest::currentTestFunction())));

    if (!exporter.exportToFile(vesselId, path)) {
        qWarning() << "export failed:" << exporter.errorString();
        return {};
    }

    QList<QStringList> rows;
    QString            error;
    if (!CsvFile::read(path, &rows, &error)) {
        qWarning() << "could not read the exported file:" << error;
        return {};
    }
    return rows;
}

QString TestCertificateCsvExport::valueOf(const QList<QStringList>& rows, int dataRow,
                                          const QString& column)
{
    if (rows.isEmpty()) {
        return QString();
    }
    const int position = rows.first().indexOf(column);
    if (position < 0 || dataRow + 1 >= rows.size()) {
        return QString();
    }
    const QStringList& cells = rows.at(dataRow + 1);
    return position < cells.size() ? cells.at(position) : QString();
}

QStringList TestCertificateCsvExport::columnValues(const QList<QStringList>& rows,
                                                   const QString&            column)
{
    QStringList values;
    for (int row = 0; row + 1 < rows.size(); ++row) {
        values.append(valueOf(rows, row, column));
    }
    return values;
}

void TestCertificateCsvExport::theFirstRowIsTheHeaderAndThenOneRowPerCertificate()
{
    QSqlDatabase db = database();
    seedCertificate(db, QStringLiteral("c1"), m_vesselId, QStringLiteral("Safety Radio"));
    seedCertificate(db, QStringLiteral("c2"), m_vesselId, QStringLiteral("Load Line"));

    CertificateRepository  certificates(db, officeContext());
    EndorsementRepository  endorsements(db, officeContext());
    VesselRepository       vessels(db, officeContext());
    CertificateCsvExporter exporter(certificates, endorsements, vessels);

    const QList<QStringList> rows = exportAndRead(exporter, m_vesselId);
    QCOMPARE(rows.size(), 3);
    QCOMPARE(rows.first(), CertificateCsvFormat::headerRow());
}

void TestCertificateCsvExport::everyRowCarriesItsOwnIdSoAReImportCanMatchIt()
{
    QSqlDatabase db = database();
    seedCertificate(db, QStringLiteral("c1"), m_vesselId, QStringLiteral("Safety Radio"));

    CertificateRepository  certificates(db, officeContext());
    EndorsementRepository  endorsements(db, officeContext());
    VesselRepository       vessels(db, officeContext());
    CertificateCsvExporter exporter(certificates, endorsements, vessels);

    const QList<QStringList> rows = exportAndRead(exporter, m_vesselId);
    QCOMPARE(valueOf(rows, 0, QStringLiteral("Id")), QStringLiteral("c1"));
}

void TestCertificateCsvExport::everyRowCarriesTheVesselNameAndImoNumber()
{
    QSqlDatabase db = database();
    seedCertificate(db, QStringLiteral("c1"), m_vesselId, QStringLiteral("Safety Radio"));

    CertificateRepository  certificates(db, officeContext());
    EndorsementRepository  endorsements(db, officeContext());
    VesselRepository       vessels(db, officeContext());
    CertificateCsvExporter exporter(certificates, endorsements, vessels);

    const QList<QStringList> rows = exportAndRead(exporter, m_vesselId);
    QCOMPARE(valueOf(rows, 0, QStringLiteral("Vessel IMO")), QStringLiteral("9074729"));
    QCOMPARE(valueOf(rows, 0, QStringLiteral("Vessel Name")), QStringLiteral("MV Aurora"));
}

void TestCertificateCsvExport::rowsAreSortedByListNumberRatherThanByName()
{
    QSqlDatabase db = database();
    // Alphabetically this is Anchor, Boiler, Crane; by reference number it is
    // 3, 3A, 15D. list() returns the first order, and the file must not.
    seedCertificate(db, QStringLiteral("c1"), m_vesselId, QStringLiteral("Anchor"), false,
                    QStringLiteral("15D"));
    seedCertificate(db, QStringLiteral("c2"), m_vesselId, QStringLiteral("Boiler"), false,
                    QStringLiteral("3"));
    seedCertificate(db, QStringLiteral("c3"), m_vesselId, QStringLiteral("Crane"), false,
                    QStringLiteral("3A"));

    CertificateRepository  certificates(db, officeContext());
    EndorsementRepository  endorsements(db, officeContext());
    VesselRepository       vessels(db, officeContext());
    CertificateCsvExporter exporter(certificates, endorsements, vessels);

    const QList<QStringList> rows = exportAndRead(exporter, m_vesselId);
    QCOMPARE(columnValues(rows, QStringLiteral("No.")),
             QStringList({QStringLiteral("3"), QStringLiteral("3A"), QStringLiteral("15D")}));
}

void TestCertificateCsvExport::aBlankListNumberSortsLast()
{
    QSqlDatabase db = database();
    seedCertificate(db, QStringLiteral("c1"), m_vesselId, QStringLiteral("Anchor"));
    seedCertificate(db, QStringLiteral("c2"), m_vesselId, QStringLiteral("Zebra"), false,
                    QStringLiteral("9"));

    CertificateRepository  certificates(db, officeContext());
    EndorsementRepository  endorsements(db, officeContext());
    VesselRepository       vessels(db, officeContext());
    CertificateCsvExporter exporter(certificates, endorsements, vessels);

    const QList<QStringList> rows = exportAndRead(exporter, m_vesselId);
    QCOMPARE(columnValues(rows, QStringLiteral("Name")),
             QStringList({QStringLiteral("Zebra"), QStringLiteral("Anchor")}));
}

void TestCertificateCsvExport::surveyHistoryIsWrittenAsASemicolonSeparatedList()
{
    QSqlDatabase db = database();
    seedCertificateWithSchedule(db, QStringLiteral("c1"), m_vesselId,
                                QStringLiteral("Safety Construction"), QStringLiteral("1"),
                                QDate(2026, 2, 4), QDate(2031, 2, 4), true, true);

    // Deliberately seeded out of order, so the file's order proves the
    // exporter sorts rather than the database happening to.
    seedEndorsement(db, QStringLiteral("e2"), QStringLiteral("c1"), QStringLiteral("ANNUAL"),
                    QDate(2028, 2, 4));
    seedEndorsement(db, QStringLiteral("e1"), QStringLiteral("c1"), QStringLiteral("ANNUAL"),
                    QDate(2027, 2, 10));
    seedEndorsement(db, QStringLiteral("e3"), QStringLiteral("c1"),
                    QStringLiteral("INTERMEDIATE"), QDate(2029, 3, 1));

    CertificateRepository  certificates(db, officeContext());
    EndorsementRepository  endorsements(db, officeContext());
    VesselRepository       vessels(db, officeContext());
    CertificateCsvExporter exporter(certificates, endorsements, vessels);

    const QList<QStringList> rows = exportAndRead(exporter, m_vesselId);
    QCOMPARE(valueOf(rows, 0, QStringLiteral("Annual Surveys")),
             QStringLiteral("2027-02-10;2028-02-04"));
    QCOMPARE(valueOf(rows, 0, QStringLiteral("Intermediate Surveys")),
             QStringLiteral("2029-03-01"));
}

void TestCertificateCsvExport::aTrackWithNoHistoryWritesABlankCell()
{
    QSqlDatabase db = database();
    seedCertificateWithSchedule(db, QStringLiteral("c1"), m_vesselId,
                                QStringLiteral("Safety Construction"), QStringLiteral("1"),
                                QDate(2026, 2, 4), QDate(2031, 2, 4), true, false);

    CertificateRepository  certificates(db, officeContext());
    EndorsementRepository  endorsements(db, officeContext());
    VesselRepository       vessels(db, officeContext());
    CertificateCsvExporter exporter(certificates, endorsements, vessels);

    const QList<QStringList> rows = exportAndRead(exporter, m_vesselId);
    QVERIFY(valueOf(rows, 0, QStringLiteral("Annual Surveys")).isEmpty());
    QVERIFY(valueOf(rows, 0, QStringLiteral("Intermediate Surveys")).isEmpty());
}

void TestCertificateCsvExport::exportingAgainIncludesASurveyRecordedSinceTheFirstExport()
{
    // §9.17: export is a live read, not a replay of whatever a file once
    // said or of what an import put there.
    QSqlDatabase db = database();
    seedCertificateWithSchedule(db, QStringLiteral("c1"), m_vesselId,
                                QStringLiteral("Safety Construction"), QStringLiteral("1"),
                                QDate(2026, 2, 4), QDate(2031, 2, 4), true, false);
    seedEndorsement(db, QStringLiteral("e1"), QStringLiteral("c1"), QStringLiteral("ANNUAL"),
                    QDate(2027, 2, 10));

    CertificateRepository  certificates(db, officeContext());
    EndorsementRepository  endorsements(db, officeContext());
    VesselRepository       vessels(db, officeContext());
    CertificateCsvExporter exporter(certificates, endorsements, vessels);

    const QString first = m_folder.filePath(QStringLiteral("first.csv"));
    QVERIFY2(exporter.exportToFile(m_vesselId, first), qPrintable(exporter.errorString()));

    // A second survey recorded the ordinary way, through the repository the
    // endorsement dialog uses.
    Endorsement second;
    second.certificateId   = QStringLiteral("c1");
    second.surveyType      = SurveyType::Annual;
    second.endorsementDate = QDate(2028, 2, 4);
    QVERIFY2(endorsements.create(second), qPrintable(endorsements.errorString()));

    const QString secondPath = m_folder.filePath(QStringLiteral("second.csv"));
    QVERIFY2(exporter.exportToFile(m_vesselId, secondPath), qPrintable(exporter.errorString()));

    QList<QStringList> firstRows;
    QList<QStringList> secondRows;
    QString            error;
    QVERIFY(CsvFile::read(first, &firstRows, &error));
    QVERIFY(CsvFile::read(secondPath, &secondRows, &error));

    QCOMPARE(valueOf(firstRows, 0, QStringLiteral("Annual Surveys")),
             QStringLiteral("2027-02-10"));
    QCOMPARE(valueOf(secondRows, 0, QStringLiteral("Annual Surveys")),
             QStringLiteral("2027-02-10;2028-02-04"));
}

void TestCertificateCsvExport::aNeverExpiringCertificateWritesABlankExpiryCell()
{
    QSqlDatabase db = database();
    seedCertificateWithSchedule(db, QStringLiteral("c1"), m_vesselId,
                                QStringLiteral("Tonnage"), QStringLiteral("1"),
                                QDate(2026, 2, 4), QDate(), false, false);

    CertificateRepository  certificates(db, officeContext());
    EndorsementRepository  endorsements(db, officeContext());
    VesselRepository       vessels(db, officeContext());
    CertificateCsvExporter exporter(certificates, endorsements, vessels);

    const QList<QStringList> rows = exportAndRead(exporter, m_vesselId);
    QVERIFY2(valueOf(rows, 0, QStringLiteral("Expiry Date")).isEmpty(),
             "blank is how the file says \"does not expire\"");
    QCOMPARE(valueOf(rows, 0, QStringLiteral("Issue Date")), QStringLiteral("2026-02-04"));
}

void TestCertificateCsvExport::aSoftDeletedCertificateIsNotExported()
{
    QSqlDatabase db = database();
    seedCertificate(db, QStringLiteral("c1"), m_vesselId, QStringLiteral("Live"));
    seedCertificate(db, QStringLiteral("c2"), m_vesselId, QStringLiteral("Deleted"), true);

    CertificateRepository  certificates(db, officeContext());
    EndorsementRepository  endorsements(db, officeContext());
    VesselRepository       vessels(db, officeContext());
    CertificateCsvExporter exporter(certificates, endorsements, vessels);

    const QList<QStringList> rows = exportAndRead(exporter, m_vesselId);
    QCOMPARE(columnValues(rows, QStringLiteral("Name")), QStringList({QStringLiteral("Live")}));
}

void TestCertificateCsvExport::anotherVesselsCertificatesAreNeverExported()
{
    QSqlDatabase db = database();
    seedVessel(db, QStringLiteral("v2"), QStringLiteral("MV Other"), QStringLiteral("9074730"));
    seedCertificate(db, QStringLiteral("c1"), m_vesselId, QStringLiteral("Mine"));
    seedCertificate(db, QStringLiteral("c2"), QStringLiteral("v2"), QStringLiteral("Theirs"));

    CertificateRepository  certificates(db, officeContext());
    EndorsementRepository  endorsements(db, officeContext());
    VesselRepository       vessels(db, officeContext());
    CertificateCsvExporter exporter(certificates, endorsements, vessels);

    const QList<QStringList> rows = exportAndRead(exporter, m_vesselId);
    QCOMPARE(columnValues(rows, QStringLiteral("Name")), QStringList({QStringLiteral("Mine")}));
}

void TestCertificateCsvExport::exportRefusesAVesselThatDoesNotExist()
{
    QSqlDatabase           db = database();
    CertificateRepository  certificates(db, officeContext());
    EndorsementRepository  endorsements(db, officeContext());
    VesselRepository       vessels(db, officeContext());
    CertificateCsvExporter exporter(certificates, endorsements, vessels);

    QList<QStringList> rows;
    QVERIFY(!exporter.buildRows(QStringLiteral("no-such-vessel"), &rows));
    QVERIFY(!exporter.errorString().isEmpty());
}

void TestCertificateCsvExport::freeTextWithACommaAQuoteAndANewlineRoundTrips()
{
    // §9.15, through the real exporter: the values a user would actually type
    // into Notes come back out of the file unchanged.
    QSqlDatabase db = database();
    seedCertificate(db, QStringLiteral("c1"), m_vesselId, QStringLiteral("Safety Radio"));

    const QString notes = QStringLiteral("Renewed in Rotterdam, 2026\n"
                                         "Surveyor said \"fit for purpose\"");

    CertificateRepository certificates(db, officeContext());
    EndorsementRepository endorsements(db, officeContext());
    VesselRepository      vessels(db, officeContext());

    std::optional<Certificate> loaded;
    QVERIFY(certificates.findById(QStringLiteral("c1"), &loaded));
    QVERIFY(loaded.has_value());
    loaded->notes        = notes;
    loaded->appliesTo    = QStringLiteral("Bridge, chart room");
    QVERIFY2(certificates.update(*loaded), qPrintable(certificates.errorString()));

    CertificateCsvExporter   exporter(certificates, endorsements, vessels);
    const QList<QStringList> rows = exportAndRead(exporter, m_vesselId);

    QCOMPARE(valueOf(rows, 0, QStringLiteral("Notes")), notes);
    QCOMPARE(valueOf(rows, 0, QStringLiteral("Applies To")),
             QStringLiteral("Bridge, chart room"));
    QVERIFY2(rows.size() == 2, "the embedded newline must not become a second row");
}

void TestCertificateCsvExport::nonAsciiTextIsWrittenWithTheByteOrderMarkAndReadBack()
{
    // §9.16. Written as escapes so this source file stays pure ASCII; the
    // text is "Peiraias" in Greek.
    const QString greek = QString::fromUtf16(u"Πειραιάς");

    QSqlDatabase db = database();
    seedCertificate(db, QStringLiteral("c1"), m_vesselId, QStringLiteral("Safety Radio"));

    CertificateRepository certificates(db, officeContext());
    EndorsementRepository endorsements(db, officeContext());
    VesselRepository      vessels(db, officeContext());

    std::optional<Certificate> loaded;
    QVERIFY(certificates.findById(QStringLiteral("c1"), &loaded));
    QVERIFY(loaded.has_value());
    loaded->placeOfIssue = greek;
    QVERIFY2(certificates.update(*loaded), qPrintable(certificates.errorString()));

    CertificateCsvExporter exporter(certificates, endorsements, vessels);
    const QString          path = m_folder.filePath(QStringLiteral("greek.csv"));
    QVERIFY2(exporter.exportToFile(m_vesselId, path), qPrintable(exporter.errorString()));

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QVERIFY2(file.readAll().startsWith(QByteArray("\xEF\xBB\xBF")),
             "Excel on Windows needs the byte-order mark to read this as UTF-8");
    file.close();

    QList<QStringList> rows;
    QString            error;
    QVERIFY2(CsvFile::read(path, &rows, &error), qPrintable(error));
    QCOMPARE(valueOf(rows, 0, QStringLiteral("Place of Issue")), greek);
}

QTEST_MAIN(TestCertificateCsvExport)
#include "tst_CertificateCsvExport.moc"
