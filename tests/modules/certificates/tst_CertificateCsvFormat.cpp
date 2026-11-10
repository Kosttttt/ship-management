#include "modules/certificates/service/CertificateCsvFormat.h"

#include <QtTest>

// certificate-import-export-spec §3, §5.3, §5.4 and §6's list syntax: the
// column layout and every per-row parsing rule, with no database and no
// widget in sight (§11).
class TestCertificateCsvFormat : public QObject
{
    Q_OBJECT

private slots:
    void theHeaderNamesEveryColumnInTheSpecOrder();

    // §9.7
    void aMissingRequiredHeaderIsAFileLevelRefusal();
    void anUnrecognisedHeaderIsAFileLevelRefusalThatNamesIt();
    void aMisspelledExpiryHeaderIsRefusedRatherThanIgnored();
    void aRepeatedHeaderIsRefused();
    void headersMatchWhateverTheirCaseAndSpacing();
    void aBlankHeaderCellIsIgnored();
    void anAbsentOptionalColumnIsFine();

    // §5.3 / §9.14
    void aValidRowParsesEveryField();
    void anIsoDateIsRequired();
    void aSlashedDateIsRefusedRatherThanGuessed();
    void aSingleDigitMonthIsRefused();
    void anImpossibleDateIsRefused();

    // §9.14
    void anUnknownCategoryIsRefusedWithItsOwnReason();
    void aMissingNameIsRefusedWithItsOwnReason();
    void aMissingIssueDateIsRefusedWithItsOwnReason();
    void eachFailureGivesADifferentReason();

    // §5.4
    void everyAcceptedBooleanSpellingWorks();
    void aBlankBooleanIsFalse();
    void anUnknownBooleanIsRefused();

    // §9.12 / §9.13
    void aBlankExpiryWithASurveyRequirementIsRefused();
    void anInvalidListNumberIsRefused();

    // §6 / §9.9
    void aSurveyListBecomesEveryDateInIt();
    void aSurveyListForATrackThatIsNotRequiredIsIgnored();
    void aBadDateInASurveyListIsRefused();
    void anEmptyEntryInASurveyListIsSkipped();

    void intermediateModeDefaultsToAdditional();
    void anUnknownIntermediateModeIsRefused();
    void anUnknownIntermediateModeIsIgnoredWhenNoIntermediateIsRequired();

    void freeTextKeepsItsOwnWhitespace();
    void aRowShorterThanTheHeaderReadsTheMissingCellsAsBlank();

private:
    // A header naming every column, and a matching row of good values.
    static QHash<QString, int> fullHeader();
    static QStringList         validRow();
    static int                 columnOf(const QString& name);
};

QHash<QString, int> TestCertificateCsvFormat::fullHeader()
{
    QHash<QString, int> columnIndex;
    QString             error;
    const bool ok = CertificateCsvFormat::mapHeader(CertificateCsvFormat::headerRow(),
                                                    &columnIndex, &error);
    if (!ok) {
        qWarning() << "the spec's own header should map:" << error;
    }
    return columnIndex;
}

int TestCertificateCsvFormat::columnOf(const QString& name)
{
    return CertificateCsvFormat::headerRow().indexOf(name);
}

QStringList TestCertificateCsvFormat::validRow()
{
    QStringList cells(CertificateCsvFormat::headerRow().size());
    cells[columnOf(QStringLiteral("Name"))]        = QStringLiteral("Safety Construction");
    cells[columnOf(QStringLiteral("Category"))]    = QStringLiteral("STATUTORY");
    cells[columnOf(QStringLiteral("Issue Date"))]  = QStringLiteral("2026-02-04");
    cells[columnOf(QStringLiteral("Expiry Date"))] = QStringLiteral("2031-02-04");
    return cells;
}

void TestCertificateCsvFormat::theHeaderNamesEveryColumnInTheSpecOrder()
{
    const QStringList header = CertificateCsvFormat::headerRow();
    QCOMPARE(header.size(), 19);
    QCOMPARE(header.first(), QStringLiteral("Id"));
    QCOMPARE(header.at(1), QStringLiteral("Vessel IMO"));
    QCOMPARE(header.at(3), QStringLiteral("No."));
    QCOMPARE(header.at(4), QStringLiteral("Name"));
    QCOMPARE(header.at(17), QStringLiteral("Annual Surveys"));
    QCOMPARE(header.at(18), QStringLiteral("Intermediate Surveys"));
    // §3: there is no such column and cannot be — nothing in the schema can
    // be populated from it.
    QVERIFY(!header.contains(QStringLiteral("Previous Certificate")));
}

void TestCertificateCsvFormat::aMissingRequiredHeaderIsAFileLevelRefusal()
{
    QHash<QString, int> columnIndex;
    QString             error;
    QVERIFY2(!CertificateCsvFormat::mapHeader({QStringLiteral("Name"),
                                               QStringLiteral("Issue Date")},
                                              &columnIndex, &error),
             "a file with no Category column cannot be imported at all");
    QVERIFY(error.contains(QStringLiteral("Category")));
}

void TestCertificateCsvFormat::anUnrecognisedHeaderIsAFileLevelRefusalThatNamesIt()
{
    QHash<QString, int> columnIndex;
    QString             error;
    QStringList         header = CertificateCsvFormat::headerRow();
    header.append(QStringLiteral("Hull Number"));

    QVERIFY(!CertificateCsvFormat::mapHeader(header, &columnIndex, &error));
    QVERIFY2(error.contains(QStringLiteral("Hull Number")),
             "the message has to name the column so it can be found and fixed");
}

void TestCertificateCsvFormat::aMisspelledExpiryHeaderIsRefusedRatherThanIgnored()
{
    // The case §3 is written around: silently ignoring this heading would
    // blank every row's expiry and turn a whole vessel's certificates into
    // "does not expire".
    QStringList header = CertificateCsvFormat::headerRow();
    header[columnOf(QStringLiteral("Expiry Date"))] = QStringLiteral("Expiry Dte");

    QHash<QString, int> columnIndex;
    QString             error;
    QVERIFY(!CertificateCsvFormat::mapHeader(header, &columnIndex, &error));
    QVERIFY(error.contains(QStringLiteral("Expiry Dte")));
}

void TestCertificateCsvFormat::aRepeatedHeaderIsRefused()
{
    QStringList header = CertificateCsvFormat::headerRow();
    header.append(QStringLiteral("Name"));

    QHash<QString, int> columnIndex;
    QString             error;
    QVERIFY(!CertificateCsvFormat::mapHeader(header, &columnIndex, &error));
    QVERIFY(error.contains(QStringLiteral("Name")));
}

void TestCertificateCsvFormat::headersMatchWhateverTheirCaseAndSpacing()
{
    QHash<QString, int> columnIndex;
    QString             error;
    QVERIFY2(CertificateCsvFormat::mapHeader({QStringLiteral("  name "),
                                              QStringLiteral("CATEGORY"),
                                              QStringLiteral("issue date")},
                                             &columnIndex, &error),
             qPrintable(error));
    QCOMPARE(columnIndex.value(QStringLiteral("Name")), 0);
    QCOMPARE(columnIndex.value(QStringLiteral("Category")), 1);
    QCOMPARE(columnIndex.value(QStringLiteral("Issue Date")), 2);
}

void TestCertificateCsvFormat::aBlankHeaderCellIsIgnored()
{
    // A trailing comma on the header line is a spreadsheet artefact, not a
    // column with no name.
    QStringList header = CertificateCsvFormat::headerRow();
    header.append(QString());

    QHash<QString, int> columnIndex;
    QString             error;
    QVERIFY2(CertificateCsvFormat::mapHeader(header, &columnIndex, &error), qPrintable(error));
}

void TestCertificateCsvFormat::anAbsentOptionalColumnIsFine()
{
    QHash<QString, int> columnIndex;
    QString             error;
    QVERIFY2(CertificateCsvFormat::mapHeader({QStringLiteral("Name"),
                                              QStringLiteral("Category"),
                                              QStringLiteral("Issue Date")},
                                             &columnIndex, &error),
             qPrintable(error));

    // A row read through that header has no Notes cell at all, which is not
    // the same thing as a blank one being wrong.
    CertificateCsvRow row;
    QString           reason;
    QVERIFY2(CertificateCsvFormat::parseRow({QStringLiteral("Load Line"),
                                             QStringLiteral("CLASS"),
                                             QStringLiteral("2026-01-01")},
                                            columnIndex, &row, &reason),
             qPrintable(reason));
    QVERIFY(row.certificate.notes.isEmpty());
    QVERIFY2(row.certificate.neverExpires(),
             "no Expiry Date column means no expiry, which is a legitimate certificate");
}

void TestCertificateCsvFormat::aValidRowParsesEveryField()
{
    const QHash<QString, int> columnIndex = fullHeader();
    QStringList               cells       = validRow();

    cells[columnOf(QStringLiteral("Id"))]                 = QStringLiteral("cert-1");
    cells[columnOf(QStringLiteral("Vessel IMO"))]          = QStringLiteral("9074729");
    cells[columnOf(QStringLiteral("No."))]                = QStringLiteral("15D");
    cells[columnOf(QStringLiteral("Applies To"))]          = QStringLiteral("Main engine");
    cells[columnOf(QStringLiteral("Certificate Number"))]  = QStringLiteral("SC-12345");
    cells[columnOf(QStringLiteral("Issued By"))]           = QStringLiteral("Lloyd's Register");
    cells[columnOf(QStringLiteral("Place of Issue"))]      = QStringLiteral("Limassol");
    cells[columnOf(QStringLiteral("Interim"))]             = QStringLiteral("Yes");
    cells[columnOf(QStringLiteral("Notes"))]               = QStringLiteral("Carried on board");
    cells[columnOf(QStringLiteral("Requires Annual Survey"))] = QStringLiteral("Yes");

    CertificateCsvRow row;
    QString           reason;
    QVERIFY2(CertificateCsvFormat::parseRow(cells, columnIndex, &row, &reason),
             qPrintable(reason));

    QCOMPARE(row.fileId, QStringLiteral("cert-1"));
    QCOMPARE(row.vesselImoNumber, QStringLiteral("9074729"));
    QCOMPARE(row.certificate.listNumber, QStringLiteral("15D"));
    QCOMPARE(row.certificate.name, QStringLiteral("Safety Construction"));
    QCOMPARE(row.certificate.category, CertificateCategory::Statutory);
    QCOMPARE(row.certificate.appliesTo, QStringLiteral("Main engine"));
    QCOMPARE(row.certificate.certificateNumber, QStringLiteral("SC-12345"));
    QCOMPARE(row.certificate.issueDate, QDate(2026, 2, 4));
    QCOMPARE(row.certificate.expiryDate, QDate(2031, 2, 4));
    QCOMPARE(row.certificate.issuedBy, QStringLiteral("Lloyd's Register"));
    QCOMPARE(row.certificate.placeOfIssue, QStringLiteral("Limassol"));
    QVERIFY(row.certificate.isInterim);
    QCOMPARE(row.certificate.notes, QStringLiteral("Carried on board"));
    QVERIFY(row.certificate.requiresAnnualSurvey);

    // §3/§7: the file never says which vessel a row belongs to.
    QVERIFY2(row.certificate.vesselId.isEmpty(),
             "the vessel comes from the selection, never from the file");
}

void TestCertificateCsvFormat::anIsoDateIsRequired()
{
    const QHash<QString, int> columnIndex = fullHeader();
    QStringList               cells       = validRow();
    cells[columnOf(QStringLiteral("Issue Date"))] = QStringLiteral("04 Feb 2026");

    CertificateCsvRow row;
    QString           reason;
    QVERIFY(!CertificateCsvFormat::parseRow(cells, columnIndex, &row, &reason));
    QVERIFY(reason.contains(QStringLiteral("YYYY-MM-DD")));
}

void TestCertificateCsvFormat::aSlashedDateIsRefusedRatherThanGuessed()
{
    // §5.3: 02/03/2026 is the second of March to one reader and the third of
    // February to another, so it is refused rather than interpreted.
    const QHash<QString, int> columnIndex = fullHeader();
    QStringList               cells       = validRow();
    cells[columnOf(QStringLiteral("Issue Date"))] = QStringLiteral("02/03/2026");

    CertificateCsvRow row;
    QString           reason;
    QVERIFY(!CertificateCsvFormat::parseRow(cells, columnIndex, &row, &reason));
}

void TestCertificateCsvFormat::aSingleDigitMonthIsRefused()
{
    const QHash<QString, int> columnIndex = fullHeader();
    QStringList               cells       = validRow();
    cells[columnOf(QStringLiteral("Issue Date"))] = QStringLiteral("2026-2-4");

    CertificateCsvRow row;
    QString           reason;
    QVERIFY(!CertificateCsvFormat::parseRow(cells, columnIndex, &row, &reason));
}

void TestCertificateCsvFormat::anImpossibleDateIsRefused()
{
    const QHash<QString, int> columnIndex = fullHeader();
    QStringList               cells       = validRow();
    cells[columnOf(QStringLiteral("Issue Date"))] = QStringLiteral("2026-02-30");

    CertificateCsvRow row;
    QString           reason;
    QVERIFY(!CertificateCsvFormat::parseRow(cells, columnIndex, &row, &reason));
}

void TestCertificateCsvFormat::anUnknownCategoryIsRefusedWithItsOwnReason()
{
    const QHash<QString, int> columnIndex = fullHeader();
    QStringList               cells       = validRow();
    cells[columnOf(QStringLiteral("Category"))] = QStringLiteral("TRADING");

    CertificateCsvRow row;
    QString           reason;
    QVERIFY(!CertificateCsvFormat::parseRow(cells, columnIndex, &row, &reason));
    QVERIFY(reason.contains(QStringLiteral("TRADING")));
    QVERIFY(reason.contains(QStringLiteral("STATUTORY")));
}

void TestCertificateCsvFormat::aMissingNameIsRefusedWithItsOwnReason()
{
    const QHash<QString, int> columnIndex = fullHeader();
    QStringList               cells       = validRow();
    cells[columnOf(QStringLiteral("Name"))] = QString();

    CertificateCsvRow row;
    QString           reason;
    QVERIFY(!CertificateCsvFormat::parseRow(cells, columnIndex, &row, &reason));
    QVERIFY2(reason.contains(QStringLiteral("name")),
             "the same sentence the edit dialog shows for an empty name");
}

void TestCertificateCsvFormat::aMissingIssueDateIsRefusedWithItsOwnReason()
{
    const QHash<QString, int> columnIndex = fullHeader();
    QStringList               cells       = validRow();
    cells[columnOf(QStringLiteral("Issue Date"))] = QString();

    CertificateCsvRow row;
    QString           reason;
    QVERIFY(!CertificateCsvFormat::parseRow(cells, columnIndex, &row, &reason));
    QVERIFY(reason.contains(QStringLiteral("issue date")));
}

void TestCertificateCsvFormat::eachFailureGivesADifferentReason()
{
    // §9.14: three separate rows, three distinct and specific messages — not
    // one generic "this row is invalid" for all of them.
    const QHash<QString, int> columnIndex = fullHeader();

    QStringList badDate                              = validRow();
    badDate[columnOf(QStringLiteral("Issue Date"))]   = QStringLiteral("04-02-2026");
    QStringList badCategory                          = validRow();
    badCategory[columnOf(QStringLiteral("Category"))] = QStringLiteral("NONSENSE");
    QStringList noName                               = validRow();
    noName[columnOf(QStringLiteral("Name"))]          = QString();

    QStringList reasons;
    for (const QStringList& cells : {badDate, badCategory, noName}) {
        CertificateCsvRow row;
        QString           reason;
        QVERIFY(!CertificateCsvFormat::parseRow(cells, columnIndex, &row, &reason));
        QVERIFY(!reason.isEmpty());
        reasons.append(reason);
    }

    QCOMPARE(reasons.size(), 3);
    QVERIFY(reasons.at(0) != reasons.at(1));
    QVERIFY(reasons.at(1) != reasons.at(2));
    QVERIFY(reasons.at(0) != reasons.at(2));
}

void TestCertificateCsvFormat::everyAcceptedBooleanSpellingWorks()
{
    const QHash<QString, int> columnIndex = fullHeader();

    const QStringList truths = {QStringLiteral("true"), QStringLiteral("TRUE"),
                                QStringLiteral("Yes"),  QStringLiteral("y"),
                                QStringLiteral("Y"),    QStringLiteral("1")};
    for (const QString& value : truths) {
        QStringList cells                         = validRow();
        cells[columnOf(QStringLiteral("Interim"))] = value;
        CertificateCsvRow row;
        QString           reason;
        QVERIFY2(CertificateCsvFormat::parseRow(cells, columnIndex, &row, &reason),
                 qPrintable(reason));
        QVERIFY2(row.certificate.isInterim, qPrintable(value));
    }

    const QStringList falsehoods = {QStringLiteral("false"), QStringLiteral("No"),
                                    QStringLiteral("n"), QStringLiteral("0")};
    for (const QString& value : falsehoods) {
        QStringList cells                         = validRow();
        cells[columnOf(QStringLiteral("Interim"))] = value;
        CertificateCsvRow row;
        QString           reason;
        QVERIFY2(CertificateCsvFormat::parseRow(cells, columnIndex, &row, &reason),
                 qPrintable(reason));
        QVERIFY2(!row.certificate.isInterim, qPrintable(value));
    }
}

void TestCertificateCsvFormat::aBlankBooleanIsFalse()
{
    const QHash<QString, int> columnIndex = fullHeader();
    CertificateCsvRow         row;
    QString                   reason;
    QVERIFY2(CertificateCsvFormat::parseRow(validRow(), columnIndex, &row, &reason),
             qPrintable(reason));
    QVERIFY(!row.certificate.isInterim);
    QVERIFY(!row.certificate.requiresAnnualSurvey);
    QVERIFY(!row.certificate.requiresIntermediateSurvey);
}

void TestCertificateCsvFormat::anUnknownBooleanIsRefused()
{
    const QHash<QString, int> columnIndex = fullHeader();
    QStringList               cells       = validRow();
    cells[columnOf(QStringLiteral("Requires Annual Survey"))] = QStringLiteral("maybe");

    CertificateCsvRow row;
    QString           reason;
    QVERIFY(!CertificateCsvFormat::parseRow(cells, columnIndex, &row, &reason));
    QVERIFY(reason.contains(QStringLiteral("Requires Annual Survey")));
}

void TestCertificateCsvFormat::aBlankExpiryWithASurveyRequirementIsRefused()
{
    // §9.12 and the cross-field rule in §3: there is no anniversary to
    // schedule a survey against without an expiry date.
    const QHash<QString, int> columnIndex = fullHeader();
    QStringList               cells       = validRow();
    cells[columnOf(QStringLiteral("Expiry Date"))]            = QString();
    cells[columnOf(QStringLiteral("Requires Annual Survey"))] = QStringLiteral("Yes");

    CertificateCsvRow row;
    QString           reason;
    QVERIFY(!CertificateCsvFormat::parseRow(cells, columnIndex, &row, &reason));
    QVERIFY2(reason.contains(QStringLiteral("no expiry")),
             "the same sentence the repository gives for this combination");
}

void TestCertificateCsvFormat::anInvalidListNumberIsRefused()
{
    // §9.13. The form's validator refuses these keystrokes, so a CSV file is
    // the only way they can arrive — which is exactly what the check in
    // validateCertificate() was written for.
    const QHash<QString, int> columnIndex = fullHeader();

    for (const QString& bad : {QStringLiteral("15/D"), QStringLiteral("P..1"),
                               QStringLiteral("ABC")}) {
        QStringList cells                     = validRow();
        cells[columnOf(QStringLiteral("No."))] = bad;

        CertificateCsvRow row;
        QString           reason;
        QVERIFY2(!CertificateCsvFormat::parseRow(cells, columnIndex, &row, &reason),
                 qPrintable(bad));
        QVERIFY(reason.contains(bad));
    }

    // And the valid shapes still pass - including the prefixed codes
    // certificate-list-number-prefix-spec.md widened the format to accept,
    // which reach the importer as plain text in this column like any other
    // list number and need no code of their own here (that spec's §6).
    for (const QString& good : {QStringLiteral("15"), QStringLiteral("3A"),
                                QStringLiteral("15D"), QStringLiteral("S1"),
                                QStringLiteral("P.48"), QStringLiteral("USCA.4"),
                                QString()}) {
        QStringList cells                     = validRow();
        cells[columnOf(QStringLiteral("No."))] = good;

        CertificateCsvRow row;
        QString           reason;
        QVERIFY2(CertificateCsvFormat::parseRow(cells, columnIndex, &row, &reason),
                 qPrintable(reason));
        QCOMPARE(row.certificate.listNumber, good);
    }
}

void TestCertificateCsvFormat::aSurveyListBecomesEveryDateInIt()
{
    const QHash<QString, int> columnIndex = fullHeader();
    QStringList               cells       = validRow();
    cells[columnOf(QStringLiteral("Requires Annual Survey"))] = QStringLiteral("Yes");
    cells[columnOf(QStringLiteral("Annual Surveys"))] =
        QStringLiteral("2027-02-10;2028-02-04;2029-03-01");

    CertificateCsvRow row;
    QString           reason;
    QVERIFY2(CertificateCsvFormat::parseRow(cells, columnIndex, &row, &reason),
             qPrintable(reason));
    QCOMPARE(row.annualSurveys.size(), 3);
    QCOMPARE(row.annualSurveys.at(0), QDate(2027, 2, 10));
    QCOMPARE(row.annualSurveys.at(2), QDate(2029, 3, 1));
}

void TestCertificateCsvFormat::aSurveyListForATrackThatIsNotRequiredIsIgnored()
{
    // §9.9: ignored, not an error. The certificate does not contradict
    // itself; the column is simply irrelevant to it.
    const QHash<QString, int> columnIndex = fullHeader();
    QStringList               cells       = validRow();
    cells[columnOf(QStringLiteral("Intermediate Surveys"))] = QStringLiteral("2028-02-04");

    CertificateCsvRow row;
    QString           reason;
    QVERIFY2(CertificateCsvFormat::parseRow(cells, columnIndex, &row, &reason),
             qPrintable(reason));
    QVERIFY(row.intermediateSurveys.isEmpty());
}

void TestCertificateCsvFormat::aBadDateInASurveyListIsRefused()
{
    const QHash<QString, int> columnIndex = fullHeader();
    QStringList               cells       = validRow();
    cells[columnOf(QStringLiteral("Requires Annual Survey"))] = QStringLiteral("Yes");
    cells[columnOf(QStringLiteral("Annual Surveys"))] =
        QStringLiteral("2027-02-10;10 Feb 2028");

    CertificateCsvRow row;
    QString           reason;
    QVERIFY(!CertificateCsvFormat::parseRow(cells, columnIndex, &row, &reason));
    QVERIFY(reason.contains(QStringLiteral("10 Feb 2028")));
    QVERIFY(reason.contains(QStringLiteral("Annual Surveys")));
}

void TestCertificateCsvFormat::anEmptyEntryInASurveyListIsSkipped()
{
    const QHash<QString, int> columnIndex = fullHeader();
    QStringList               cells       = validRow();
    cells[columnOf(QStringLiteral("Requires Annual Survey"))] = QStringLiteral("Yes");
    cells[columnOf(QStringLiteral("Annual Surveys"))] = QStringLiteral("2027-02-10;;");

    CertificateCsvRow row;
    QString           reason;
    QVERIFY2(CertificateCsvFormat::parseRow(cells, columnIndex, &row, &reason),
             qPrintable(reason));
    QCOMPARE(row.annualSurveys.size(), 1);
}

void TestCertificateCsvFormat::intermediateModeDefaultsToAdditional()
{
    const QHash<QString, int> columnIndex = fullHeader();
    QStringList               cells       = validRow();
    cells[columnOf(QStringLiteral("Requires Intermediate Survey"))] = QStringLiteral("Yes");

    CertificateCsvRow row;
    QString           reason;
    QVERIFY2(CertificateCsvFormat::parseRow(cells, columnIndex, &row, &reason),
             qPrintable(reason));
    QCOMPARE(row.certificate.intermediateMode, IntermediateMode::Additional);

    cells[columnOf(QStringLiteral("Intermediate Mode"))] = QStringLiteral("replaces_annual");
    QVERIFY2(CertificateCsvFormat::parseRow(cells, columnIndex, &row, &reason),
             qPrintable(reason));
    QCOMPARE(row.certificate.intermediateMode, IntermediateMode::ReplacesAnnual);
}

void TestCertificateCsvFormat::anUnknownIntermediateModeIsRefused()
{
    const QHash<QString, int> columnIndex = fullHeader();
    QStringList               cells       = validRow();
    cells[columnOf(QStringLiteral("Requires Intermediate Survey"))] = QStringLiteral("Yes");
    cells[columnOf(QStringLiteral("Intermediate Mode"))] = QStringLiteral("SOMETIMES");

    CertificateCsvRow row;
    QString           reason;
    QVERIFY(!CertificateCsvFormat::parseRow(cells, columnIndex, &row, &reason));
    QVERIFY(reason.contains(QStringLiteral("SOMETIMES")));
}

void TestCertificateCsvFormat::anUnknownIntermediateModeIsIgnoredWhenNoIntermediateIsRequired()
{
    // §3: the column is ignored entirely when the certificate requires no
    // intermediate survey, so a stale value cannot invalidate a good row.
    const QHash<QString, int> columnIndex = fullHeader();
    QStringList               cells       = validRow();
    cells[columnOf(QStringLiteral("Intermediate Mode"))] = QStringLiteral("SOMETIMES");

    CertificateCsvRow row;
    QString           reason;
    QVERIFY2(CertificateCsvFormat::parseRow(cells, columnIndex, &row, &reason),
             qPrintable(reason));
}

void TestCertificateCsvFormat::freeTextKeepsItsOwnWhitespace()
{
    const QHash<QString, int> columnIndex = fullHeader();
    QStringList               cells       = validRow();
    cells[columnOf(QStringLiteral("Notes"))] = QStringLiteral("first line\nsecond line");

    CertificateCsvRow row;
    QString           reason;
    QVERIFY2(CertificateCsvFormat::parseRow(cells, columnIndex, &row, &reason),
             qPrintable(reason));
    QCOMPARE(row.certificate.notes, QStringLiteral("first line\nsecond line"));
}

void TestCertificateCsvFormat::aRowShorterThanTheHeaderReadsTheMissingCellsAsBlank()
{
    const QHash<QString, int> columnIndex = fullHeader();

    // Only the cells up to Issue Date, which is what a spreadsheet writes when
    // everything after them is empty.
    QStringList cells = validRow();
    cells             = cells.mid(0, 9);

    CertificateCsvRow row;
    QString           reason;
    QVERIFY2(CertificateCsvFormat::parseRow(cells, columnIndex, &row, &reason),
             qPrintable(reason));
    QCOMPARE(row.certificate.name, QStringLiteral("Safety Construction"));
    QVERIFY(row.certificate.notes.isEmpty());
}

QTEST_MAIN(TestCertificateCsvFormat)
#include "tst_CertificateCsvFormat.moc"
