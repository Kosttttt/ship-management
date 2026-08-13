#include "modules/certificates/domain/Certificate.h"

#include <QtTest>

// certificate-crud-spec §8.5 items 13 and 14, at the level the rule actually
// lives: a pure domain function, testable without a table or a database.
class TestCertificateListNumber : public QObject
{
    Q_OBJECT

private slots:
    void accepts_data();
    void accepts();
    void rejects_data();
    void rejects();

    void ordersByLeadingNumberThenLetters();
    void plainTextOrderWouldGetThisWrong();
    void blankSortsAfterEverythingElse();
    void sortingAWholeListPutsItInTheRightOrder();
    void lettersCompareWithoutRegardToCase();
};

void TestCertificateListNumber::accepts_data()
{
    QTest::addColumn<QString>("value");

    QTest::newRow("blank")            << "";
    QTest::newRow("single digit")     << "3";
    QTest::newRow("two digits")       << "15";
    QTest::newRow("digit letter")     << "3A";
    QTest::newRow("digits letter")    << "15D";
    QTest::newRow("digits letters")   << "15AB";
    QTest::newRow("lowercase letter") << "15d";
    QTest::newRow("long number")      << "1234567";
}

void TestCertificateListNumber::accepts()
{
    QFETCH(QString, value);
    QVERIFY(CertificateListNumber::isValid(value));
}

void TestCertificateListNumber::rejects_data()
{
    QTest::addColumn<QString>("value");

    // A letter before the first digit.
    QTest::newRow("letter first")        << "A15";
    QTest::newRow("letters only")        << "ABC";
    // A digit after a letter — the case that would make the split ambiguous.
    QTest::newRow("digit after letter")  << "3A1";
    QTest::newRow("alternating")         << "1A2B";
    // Anything that is neither a digit nor a letter.
    QTest::newRow("hyphen")              << "15-D";
    QTest::newRow("space inside")        << "15 D";
    QTest::newRow("leading space")       << " 15";
    QTest::newRow("trailing space")      << "15 ";
    QTest::newRow("slash")               << "15/D";
    QTest::newRow("punctuation")         << "15.";
    QTest::newRow("underscore")          << "15_D";
}

void TestCertificateListNumber::rejects()
{
    QFETCH(QString, value);
    QVERIFY(!CertificateListNumber::isValid(value));
}

void TestCertificateListNumber::ordersByLeadingNumberThenLetters()
{
    QVERIFY(CertificateListNumber::lessThan(QStringLiteral("3"), QStringLiteral("3A")));
    QVERIFY(CertificateListNumber::lessThan(QStringLiteral("3A"), QStringLiteral("3B")));
    QVERIFY(CertificateListNumber::lessThan(QStringLiteral("3B"), QStringLiteral("9")));
    QVERIFY(CertificateListNumber::lessThan(QStringLiteral("9"), QStringLiteral("15D")));

    // ...and not the other way round.
    QVERIFY(!CertificateListNumber::lessThan(QStringLiteral("15D"), QStringLiteral("9")));
    QVERIFY(!CertificateListNumber::lessThan(QStringLiteral("3A"), QStringLiteral("3")));
}

void TestCertificateListNumber::plainTextOrderWouldGetThisWrong()
{
    // The exact case the format exists to solve: as text, "15D" < "3A",
    // because '1' precedes '3' as a character.
    QVERIFY2(QStringLiteral("15D") < QStringLiteral("3A"),
             "if this ever fails, plain text comparison changed and the comment is stale");
    QVERIFY2(CertificateListNumber::lessThan(QStringLiteral("3A"), QStringLiteral("15D")),
             "certificate 3A comes before certificate 15D");
}

void TestCertificateListNumber::blankSortsAfterEverythingElse()
{
    QVERIFY(CertificateListNumber::lessThan(QStringLiteral("15D"), QString()));
    QVERIFY(!CertificateListNumber::lessThan(QString(), QStringLiteral("15D")));
    // Two blanks are equal, so neither comes first.
    QVERIFY(!CertificateListNumber::lessThan(QString(), QString()));
}

void TestCertificateListNumber::sortingAWholeListPutsItInTheRightOrder()
{
    QStringList numbers{QStringLiteral("9"),   QString(),          QStringLiteral("15D"),
                        QStringLiteral("3B"),  QStringLiteral("3"), QStringLiteral("3A")};

    std::sort(numbers.begin(), numbers.end(), CertificateListNumber::lessThan);

    const QStringList expected{QStringLiteral("3"),  QStringLiteral("3A"), QStringLiteral("3B"),
                               QStringLiteral("9"),  QStringLiteral("15D"), QString()};
    QCOMPARE(numbers, expected);
}

void TestCertificateListNumber::lettersCompareWithoutRegardToCase()
{
    QVERIFY(CertificateListNumber::lessThan(QStringLiteral("3a"), QStringLiteral("3B")));
    QVERIFY(CertificateListNumber::lessThan(QStringLiteral("3A"), QStringLiteral("3b")));
}

QTEST_APPLESS_MAIN(TestCertificateListNumber)
#include "tst_CertificateListNumber.moc"
