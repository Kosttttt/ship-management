#include "core/ImoNumberValidator.h"

#include <QtTest>

// Lets QFETCH carry the enum through QTest's data table.
Q_DECLARE_METATYPE(ImoNumberValidator::Result)

// first-run-wizard-spec §10.1.
class TestImoNumberValidator : public QObject
{
    Q_OBJECT

private slots:
    void validates_data();
    void validates();

    void stripsNonDigitsBeforeValidating();
    void workedExampleFromTheSpec();
};

// A data-driven test: Qt runs the test function once per row in the _data
// table, so adding a case is one line rather than one function.
void TestImoNumberValidator::validates_data()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<ImoNumberValidator::Result>("expected");

    using R = ImoNumberValidator::Result;

    QTest::newRow("spec example")     << "9074729" << R::Valid;
    QTest::newRow("another valid")    << "9319466" << R::Valid;
    QTest::newRow("with IMO prefix")  << "IMO 9074729" << R::Valid;
    QTest::newRow("with spaces")      << " 9074729 " << R::Valid;

    QTest::newRow("empty")            << "" << R::Empty;
    QTest::newRow("only letters")     << "ABCDEFG" << R::Empty;

    QTest::newRow("six digits")       << "907472" << R::WrongLength;
    QTest::newRow("eight digits")     << "90747290" << R::WrongLength;
    // Letters are stripped, leaving six digits — so this fails on length.
    QTest::newRow("letter inside")    << "90747X9" << R::WrongLength;

    QTest::newRow("wrong check digit")     << "9074728" << R::CheckDigitMismatch;
    QTest::newRow("wrong check digit two") << "9319460" << R::CheckDigitMismatch;
    QTest::newRow("all zeros is valid")    << "0000000" << R::Valid;
}

void TestImoNumberValidator::validates()
{
    QFETCH(QString, input);
    QFETCH(ImoNumberValidator::Result, expected);

    QCOMPARE(ImoNumberValidator::validate(input), expected);
}

void TestImoNumberValidator::stripsNonDigitsBeforeValidating()
{
    QCOMPARE(ImoNumberValidator::digitsOnly(QStringLiteral("IMO 9074729")),
             QStringLiteral("9074729"));
    QCOMPARE(ImoNumberValidator::digitsOnly(QStringLiteral("no digits here")), QString());
}

void TestImoNumberValidator::workedExampleFromTheSpec()
{
    // 7*9 + 6*0 + 5*7 + 4*4 + 3*7 + 2*2 = 139, last digit 9, matches d7.
    QVERIFY(ImoNumberValidator::isValid(QStringLiteral("9074729")));
    QVERIFY(!ImoNumberValidator::isValid(QStringLiteral("9074720")));
}

QTEST_MAIN(TestImoNumberValidator)
#include "tst_ImoNumberValidator.moc"
