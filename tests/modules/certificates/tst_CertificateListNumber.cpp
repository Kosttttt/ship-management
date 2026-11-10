#include "modules/certificates/domain/Certificate.h"

#include <QtTest>

#include <algorithm>

// certificate-crud-spec §8.5 items 13 and 14, widened by
// certificate-list-number-prefix-spec.md §5, at the level the rule actually
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

    // prefix-spec §3
    void groupsByPrefixInTheSheetsOwnSectionOrder();
    void everyPlainNumberComesBeforeEveryPrefixedOne();
    void aPrefixOutranksTheNumberBesideIt();
    void aPrefixIsComparedWithoutRegardToCaseOrItsDot();
    void anUnknownPrefixSortsAfterEveryKnownGroup();
    void twoUnknownPrefixesSortAlphabeticallyBetweenThemselves();
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

    // prefix-spec §1: one real code from each of the eight annexes on the
    // company's S-14 sheet. Every one of these was rejected by the old rule,
    // and all sixty rows the first bulk import refused were one of them.
    QTest::newRow("S, no dot")        << "S1";
    QTest::newRow("S, two digits")    << "S19";
    QTest::newRow("P.")               << "P.1";
    QTest::newRow("P., two digits")   << "P.48";
    QTest::newRow("EU.")              << "EU.9";
    QTest::newRow("US.")              << "US.12";
    QTest::newRow("USCA.")            << "USCA.4";
    QTest::newRow("USAL.")            << "USAL.2";
    QTest::newRow("PC.")              << "PC.2";
    QTest::newRow("IS.")              << "IS.2";

    // The prefix is matched as the sheet writes it, not normalised, so a
    // lowercase one is still a valid thing to type.
    QTest::newRow("lowercase prefix") << "p.6";

    // A letter before the first digit used to be the headline invalid case.
    // It is now simply a one-letter prefix, structurally identical to "S1" —
    // there is no way to accept S1 and still reject this.
    QTest::newRow("one-letter prefix")   << "A15";

    // A prefix may carry trailing letters after its number like any other.
    QTest::newRow("prefix with letters") << "P.9A";

    // Six letters is the limit, so this is the last accepted width.
    QTest::newRow("six-letter prefix")   << "ABCDEF.1";
    QTest::newRow("prefix without dot")  << "PC2";
}

void TestCertificateListNumber::accepts()
{
    QFETCH(QString, value);
    QVERIFY(CertificateListNumber::isValid(value));
}

void TestCertificateListNumber::rejects_data()
{
    QTest::addColumn<QString>("value");

    // Still no digits at all, prefix or not: there is nothing to sort by.
    QTest::newRow("letters only")        << "ABC";
    QTest::newRow("prefix with no digits") << "P.";
    QTest::newRow("bare prefix letter")  << "S";
    // A digit after a letter — the case that would make the split ambiguous.
    QTest::newRow("digit after letter")  << "3A1";
    QTest::newRow("alternating")         << "1A2B";
    // Seven letters is past the prefix limit in §2.
    QTest::newRow("prefix too long")     << "ABCDEFG.1";
    // The dot belongs to the prefix and there is only ever one.
    QTest::newRow("two dots")            << "P..1";
    QTest::newRow("dot after digits")    << "15.D";
    QTest::newRow("dot alone")           << ".";
    QTest::newRow("prefix dot letters")  << "P.A";
    // Anything that is neither a digit, a letter, nor the prefix's own dot.
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

    // Including after a prefixed one, which sorts last of the set values.
    QVERIFY(CertificateListNumber::lessThan(QStringLiteral("IS.2"), QString()));
    QVERIFY(!CertificateListNumber::lessThan(QString(), QStringLiteral("IS.2")));
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

void TestCertificateListNumber::groupsByPrefixInTheSheetsOwnSectionOrder()
{
    // prefix-spec §3's worked example, verbatim, as a whole sort - shuffled
    // going in so the order coming out is the comparator's work and not the
    // input's.
    QStringList numbers{QStringLiteral("US.12"), QStringLiteral("P.1"),
                        QStringLiteral("IS.2"),  QString(),
                        QStringLiteral("S19"),   QStringLiteral("15D"),
                        QStringLiteral("PC.1"),  QStringLiteral("USAL.2"),
                        QStringLiteral("EU.1"),  QStringLiteral("5"),
                        QStringLiteral("USCA.4"),QStringLiteral("S1"),
                        QStringLiteral("IS.1"),  QStringLiteral("169"),
                        QStringLiteral("P.48"),  QStringLiteral("USCA.1"),
                        QStringLiteral("EU.9"),  QStringLiteral("US.1"),
                        QStringLiteral("USAL.1"),QStringLiteral("PC.2")};

    std::sort(numbers.begin(), numbers.end(), CertificateListNumber::lessThan);

    const QStringList expected{QStringLiteral("5"),      QStringLiteral("15D"),
                               QStringLiteral("169"),    QStringLiteral("S1"),
                               QStringLiteral("S19"),    QStringLiteral("P.1"),
                               QStringLiteral("P.48"),   QStringLiteral("EU.1"),
                               QStringLiteral("EU.9"),   QStringLiteral("US.1"),
                               QStringLiteral("US.12"),  QStringLiteral("USCA.1"),
                               QStringLiteral("USCA.4"), QStringLiteral("USAL.1"),
                               QStringLiteral("USAL.2"), QStringLiteral("PC.1"),
                               QStringLiteral("PC.2"),   QStringLiteral("IS.1"),
                               QStringLiteral("IS.2"),   QString()};
    QCOMPARE(numbers, expected);
}

void TestCertificateListNumber::everyPlainNumberComesBeforeEveryPrefixedOne()
{
    // The plain numbers are the sheet's first section, so even its highest
    // number precedes the first entry of every annex.
    QVERIFY(CertificateListNumber::lessThan(QStringLiteral("169"), QStringLiteral("S1")));
    QVERIFY(CertificateListNumber::lessThan(QStringLiteral("169"), QStringLiteral("P.1")));
    QVERIFY(CertificateListNumber::lessThan(QStringLiteral("169"), QStringLiteral("IS.1")));
    QVERIFY(!CertificateListNumber::lessThan(QStringLiteral("S1"), QStringLiteral("169")));
}

void TestCertificateListNumber::aPrefixOutranksTheNumberBesideIt()
{
    // The whole point of grouping: S19 is a bigger number than P.1, and still
    // comes first, because Statements are the section above Plans.
    QVERIFY(CertificateListNumber::lessThan(QStringLiteral("S19"), QStringLiteral("P.1")));
    QVERIFY(!CertificateListNumber::lessThan(QStringLiteral("P.1"), QStringLiteral("S19")));

    // US. before USCA. before USAL., which is the sheet's order and not
    // alphabetical - alphabetically USAL would precede USCA.
    QVERIFY(CertificateListNumber::lessThan(QStringLiteral("US.12"), QStringLiteral("USCA.1")));
    QVERIFY2(CertificateListNumber::lessThan(QStringLiteral("USCA.4"),
                                             QStringLiteral("USAL.1")),
             "California before Alaska, because that is the order the S-14 sheet uses");
    QVERIFY2(QStringLiteral("USAL") < QStringLiteral("USCA"),
             "if this fails, alphabetical and sheet order have stopped disagreeing and the "
             "test above has stopped proving anything");
}

void TestCertificateListNumber::aPrefixIsComparedWithoutRegardToCaseOrItsDot()
{
    // The same reasoning already applied to "3a" and "3A": one certificate,
    // however it was typed. Neither is less than the other.
    QVERIFY(!CertificateListNumber::lessThan(QStringLiteral("p.6"), QStringLiteral("P.6")));
    QVERIFY(!CertificateListNumber::lessThan(QStringLiteral("P.6"), QStringLiteral("p.6")));

    // And the dot is punctuation on the prefix, not part of its identity, so
    // "P6" and "P.6" are the same entry in the same group.
    QVERIFY(!CertificateListNumber::lessThan(QStringLiteral("P6"), QStringLiteral("P.6")));
    QVERIFY(!CertificateListNumber::lessThan(QStringLiteral("P.6"), QStringLiteral("P6")));

    // Which also means a lowercase prefix still groups correctly against
    // other sections rather than falling out of the order.
    QVERIFY(CertificateListNumber::lessThan(QStringLiteral("s1"), QStringLiteral("p.6")));
}

void TestCertificateListNumber::anUnknownPrefixSortsAfterEveryKnownGroup()
{
    // §3: a ninth annex the sheet does not have yet is never lost or
    // rejected. It sorts at the bottom until someone adds it to the priority
    // list, which is the one place that spec needs revisiting.
    QVERIFY(CertificateListNumber::lessThan(QStringLiteral("IS.2"), QStringLiteral("ZZ.1")));
    QVERIFY(CertificateListNumber::lessThan(QStringLiteral("S1"), QStringLiteral("ZZ.1")));
    QVERIFY(CertificateListNumber::lessThan(QStringLiteral("169"), QStringLiteral("ZZ.1")));
    QVERIFY(!CertificateListNumber::lessThan(QStringLiteral("ZZ.1"), QStringLiteral("IS.2")));

    // Still before blank, though: it is a number somebody typed.
    QVERIFY(CertificateListNumber::lessThan(QStringLiteral("ZZ.1"), QString()));
}

void TestCertificateListNumber::twoUnknownPrefixesSortAlphabeticallyBetweenThemselves()
{
    // They share one rank, so without this they would have no defined order
    // at all.
    QVERIFY(CertificateListNumber::lessThan(QStringLiteral("AA.1"), QStringLiteral("ZZ.1")));
    QVERIFY(!CertificateListNumber::lessThan(QStringLiteral("ZZ.1"), QStringLiteral("AA.1")));

    // And within one unknown prefix the number still decides.
    QVERIFY(CertificateListNumber::lessThan(QStringLiteral("ZZ.1"), QStringLiteral("ZZ.2")));

    QStringList numbers{QStringLiteral("ZZ.1"), QStringLiteral("IS.1"),
                        QStringLiteral("AA.2"), QStringLiteral("AA.1")};
    std::sort(numbers.begin(), numbers.end(), CertificateListNumber::lessThan);
    const QStringList expected{QStringLiteral("IS.1"), QStringLiteral("AA.1"),
                               QStringLiteral("AA.2"), QStringLiteral("ZZ.1")};
    QCOMPARE(numbers, expected);
}

QTEST_APPLESS_MAIN(TestCertificateListNumber)
#include "tst_CertificateListNumber.moc"
