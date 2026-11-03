#include "core/CsvFile.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

// certificate-import-export-spec §3.1 and §9.15/§9.16: the file format, on
// its own, with nothing about certificates in it.
class TestCsvFile : public QObject
{
    Q_OBJECT

private slots:
    void plainValuesNeedNoQuoting();
    void linesEndWithCrLf();

    // §9.15, one character per test.
    void aValueWithACommaIsQuoted();
    void aValueWithAQuoteDoublesIt();
    void aValueWithANewlineIsQuoted();
    void aCommaRoundTrips();
    void aQuoteRoundTrips();
    void aNewlineRoundTrips();

    void crLfAndLfAreBothAccepted();
    void aTrailingNewlineDoesNotAddAnEmptyRow();
    void aBlankLineInTheMiddleIsSkipped();
    void aByteOrderMarkIsNotPartOfTheFirstValue();
    void rowsMayBeShorterThanTheHeader();
    void anUnterminatedQuoteIsRefused();

    // §9.16
    void aWrittenFileStartsWithTheUtf8ByteOrderMark();
    void nonAsciiTextSurvivesTheRoundTrip();

private:
    static QList<QStringList> parse(const QString& text);
};

QList<QStringList> TestCsvFile::parse(const QString& text)
{
    QList<QStringList> rows;
    QString            error;
    if (!CsvFile::parseTable(text, &rows, &error)) {
        qWarning() << "unexpected parse failure:" << error;
        return {};
    }
    return rows;
}

void TestCsvFile::plainValuesNeedNoQuoting()
{
    QCOMPARE(CsvFile::formatTable({{QStringLiteral("a"), QStringLiteral("b")}}),
             QStringLiteral("a,b\r\n"));
}

void TestCsvFile::linesEndWithCrLf()
{
    const QString text = CsvFile::formatTable({{QStringLiteral("a")}, {QStringLiteral("b")}});
    QCOMPARE(text, QStringLiteral("a\r\nb\r\n"));
}

void TestCsvFile::aValueWithACommaIsQuoted()
{
    QCOMPARE(CsvFile::formatTable({{QStringLiteral("Lloyd's, Register")}}),
             QStringLiteral("\"Lloyd's, Register\"\r\n"));
}

void TestCsvFile::aValueWithAQuoteDoublesIt()
{
    QCOMPARE(CsvFile::formatTable({{QStringLiteral("say \"when\"")}}),
             QStringLiteral("\"say \"\"when\"\"\"\r\n"));
}

void TestCsvFile::aValueWithANewlineIsQuoted()
{
    QCOMPARE(CsvFile::formatTable({{QStringLiteral("first\nsecond")}}),
             QStringLiteral("\"first\nsecond\"\r\n"));
}

void TestCsvFile::aCommaRoundTrips()
{
    const QStringList row  = {QStringLiteral("one, two"), QStringLiteral("three")};
    const auto        back = parse(CsvFile::formatTable({row}));
    QCOMPARE(back.size(), 1);
    QCOMPARE(back.first(), row);
}

void TestCsvFile::aQuoteRoundTrips()
{
    const QStringList row  = {QStringLiteral("a \"quoted\" word"), QStringLiteral("plain")};
    const auto        back = parse(CsvFile::formatTable({row}));
    QCOMPARE(back.size(), 1);
    QCOMPARE(back.first(), row);
}

void TestCsvFile::aNewlineRoundTrips()
{
    const QStringList row  = {QStringLiteral("line one\nline two"), QStringLiteral("plain")};
    const auto        back = parse(CsvFile::formatTable({row}));
    QCOMPARE(back.size(), 1);
    QCOMPARE(back.first(), row);
    QVERIFY2(back.first().first().contains(QLatin1Char('\n')),
             "the embedded newline must survive, not become a second row");
}

void TestCsvFile::crLfAndLfAreBothAccepted()
{
    const auto windows = parse(QStringLiteral("a,b\r\nc,d\r\n"));
    const auto posix   = parse(QStringLiteral("a,b\nc,d\n"));
    QCOMPARE(windows.size(), 2);
    QCOMPARE(windows, posix);
}

void TestCsvFile::aTrailingNewlineDoesNotAddAnEmptyRow()
{
    QCOMPARE(parse(QStringLiteral("a,b\r\n")).size(), 1);
}

void TestCsvFile::aBlankLineInTheMiddleIsSkipped()
{
    const auto rows = parse(QStringLiteral("a\r\n\r\nb\r\n"));
    QCOMPARE(rows.size(), 2);
    QCOMPARE(rows.at(0).first(), QStringLiteral("a"));
    QCOMPARE(rows.at(1).first(), QStringLiteral("b"));
}

void TestCsvFile::aByteOrderMarkIsNotPartOfTheFirstValue()
{
    const auto rows = parse(QString(QChar(0xFEFF)) + QStringLiteral("Id,Name\r\n"));
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows.first().first(), QStringLiteral("Id"));
}

void TestCsvFile::rowsMayBeShorterThanTheHeader()
{
    // A spreadsheet drops trailing empty cells, so lining a short row up with
    // the header is the caller's business, not a parse error.
    const auto rows = parse(QStringLiteral("a,b,c\r\nx\r\n"));
    QCOMPARE(rows.size(), 2);
    QCOMPARE(rows.at(1).size(), 1);
}

void TestCsvFile::anUnterminatedQuoteIsRefused()
{
    QList<QStringList> rows;
    QString            error;
    QVERIFY2(!CsvFile::parseTable(QStringLiteral("a,\"unclosed\r\n"), &rows, &error),
             "a quoted value that is never closed cannot be read");
    QVERIFY(!error.isEmpty());
}

void TestCsvFile::aWrittenFileStartsWithTheUtf8ByteOrderMark()
{
    QTemporaryDir folder;
    QVERIFY(folder.isValid());
    const QString path = folder.filePath(QStringLiteral("out.csv"));

    QString error;
    QVERIFY2(CsvFile::write(path, {{QStringLiteral("Id")}}, &error), qPrintable(error));

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray bytes = file.readAll();

    // §3.1: these three bytes are not optional. Excel on Windows reads a
    // UTF-8 file without them as the local code page.
    QVERIFY2(bytes.startsWith(QByteArray("\xEF\xBB\xBF")),
             "the file must begin with the UTF-8 byte-order mark");
}

void TestCsvFile::nonAsciiTextSurvivesTheRoundTrip()
{
    // Written as escapes so this source file stays pure ASCII and cannot be
    // damaged by a tool that re-saves it in another encoding. The text is
    // "Peiraias" in Greek.
    const QString greek = QString::fromUtf16(u"Πειραιάς");

    QTemporaryDir folder;
    QVERIFY(folder.isValid());
    const QString path = folder.filePath(QStringLiteral("greek.csv"));

    QString error;
    QVERIFY2(CsvFile::write(path, {{QStringLiteral("Place of Issue")}, {greek}}, &error),
             qPrintable(error));

    QList<QStringList> rows;
    QVERIFY2(CsvFile::read(path, &rows, &error), qPrintable(error));
    QCOMPARE(rows.size(), 2);
    QCOMPARE(rows.at(1).first(), greek);
}

QTEST_MAIN(TestCsvFile)
#include "tst_CsvFile.moc"
