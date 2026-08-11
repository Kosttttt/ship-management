#include "core/SqlStatementSplitter.h"

#include <QtTest>

class TestSqlStatementSplitter : public QObject
{
    Q_OBJECT

private slots:
    void emptyScriptYieldsNothing();
    void singleStatementWithoutSemicolon();
    void severalStatements();
    void semicolonInsideQuotedTextIsNotABoundary();
    void semicolonInsideLineCommentIsNotABoundary();
    void semicolonInsideBlockCommentIsNotABoundary();
    void commentsAreStripped();
};

void TestSqlStatementSplitter::emptyScriptYieldsNothing()
{
    QVERIFY(SqlStatementSplitter::split(QString()).isEmpty());
    QVERIFY(SqlStatementSplitter::split(QStringLiteral("   \n  ")).isEmpty());
    QVERIFY(SqlStatementSplitter::split(QStringLiteral(";;;")).isEmpty());
}

void TestSqlStatementSplitter::singleStatementWithoutSemicolon()
{
    const QStringList result = SqlStatementSplitter::split(QStringLiteral("SELECT 1"));
    QCOMPARE(result.size(), 1);
    QCOMPARE(result.first(), QStringLiteral("SELECT 1"));
}

void TestSqlStatementSplitter::severalStatements()
{
    const QStringList result =
        SqlStatementSplitter::split(QStringLiteral("SELECT 1; SELECT 2;\nSELECT 3;"));
    QCOMPARE(result.size(), 3);
    QCOMPARE(result.at(0), QStringLiteral("SELECT 1"));
    QCOMPARE(result.at(1), QStringLiteral("SELECT 2"));
    QCOMPARE(result.at(2), QStringLiteral("SELECT 3"));
}

void TestSqlStatementSplitter::semicolonInsideQuotedTextIsNotABoundary()
{
    const QStringList result =
        SqlStatementSplitter::split(QStringLiteral("INSERT INTO t VALUES ('a;b');"));
    QCOMPARE(result.size(), 1);
    QVERIFY(result.first().contains(QStringLiteral("'a;b'")));
}

void TestSqlStatementSplitter::semicolonInsideLineCommentIsNotABoundary()
{
    const QStringList result =
        SqlStatementSplitter::split(QStringLiteral("-- one; two; three\nSELECT 1;"));
    QCOMPARE(result.size(), 1);
    QCOMPARE(result.first(), QStringLiteral("SELECT 1"));
}

void TestSqlStatementSplitter::semicolonInsideBlockCommentIsNotABoundary()
{
    const QStringList result =
        SqlStatementSplitter::split(QStringLiteral("/* one; two */ SELECT 1;"));
    QCOMPARE(result.size(), 1);
    QCOMPARE(result.first(), QStringLiteral("SELECT 1"));
}

void TestSqlStatementSplitter::commentsAreStripped()
{
    const QStringList result =
        SqlStatementSplitter::split(QStringLiteral("SELECT 1 -- trailing note\n;"));
    QCOMPARE(result.size(), 1);
    QVERIFY(!result.first().contains(QStringLiteral("trailing note")));
}

QTEST_MAIN(TestSqlStatementSplitter)
#include "tst_SqlStatementSplitter.moc"
