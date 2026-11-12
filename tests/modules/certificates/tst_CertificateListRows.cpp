#include "CertificateTestSupport.h"

#include "core/AppSettingRepository.h"
#include "modules/certificates/data/CertificateExtensionRepository.h"
#include "modules/certificates/data/CertificateRepository.h"
#include "modules/certificates/data/EndorsementRepository.h"
#include "modules/certificates/service/CertificateListRows.h"

#include <QtTest>

using namespace CertificateTestSupport;

// certificate-category-tabs-spec §4 and §5: the service decides which rows
// the list shows and what each tab's number and colour are, in one pass. No
// widgets here — that is the point of this living in the service layer
// (CLAUDE.md §4 rule 3).
class TestCertificateListRows : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void allReturnsEveryCategory();
    void aCategoryReturnsOnlyThatCategory();
    void anEmptyCategoryReturnsNothingAndStillHasATab();

    void thereIsAlwaysOneTallyPerTabInDisplayOrder();
    void countsAreTheSameWhicheverTabIsSelected();
    void countsFollowTheAttentionCheckbox();

    void aCategoryWithAnExpiredCertificateNeedsAttention();
    void anAllValidCategoryDoesNotNeedAttention();
    void redFollowsTheVesselNotTheCheckbox();

    void aVesselThatCannotBeReadStillReturnsFiveTabs();

private:
    QSqlDatabase database() { return QSqlDatabase::database(m_connectionName); }

    // Three statutory, two class, one equipment, none "other". The expiring
    // one is statutory, so exactly one category needs attention.
    void seedFleet();

    // Everything build() needs, built once per test.
    //
    // A struct rather than six locals repeated in every test, the same shape
    // AttachmentFixture already uses in CertificateTestSupport.h:
    // CertificateListRows holds references to all four repositories, so they
    // have to live somewhere that outlives it — which a helper *function*
    // returning one could not arrange.
    struct Source {
        explicit Source(QSqlDatabase& db)
            : certificates(db, officeContext())
            , endorsements(db, officeContext())
            , extensions(db, officeContext())
            , appSettings(db)
            , rows(certificates, endorsements, extensions, appSettings)
        {
        }

        CertificateRepository          certificates;
        EndorsementRepository          endorsements;
        CertificateExtensionRepository extensions;
        AppSettingRepository           appSettings;

        // Declared last, so the four above are alive by the time it binds to
        // them.
        CertificateListRows rows;
    };


    static const CertificateListRows::CategoryTally& tallyOf(
        const QList<CertificateListRows::CategoryTally>& tallies, CertificateCategory category)
    {
        for (const CertificateListRows::CategoryTally& tally : tallies) {
            if (tally.category == category) {
                return tally;
            }
        }
        static const CertificateListRows::CategoryTally kMissing;
        return kMissing;
    }

    static QDate today() { return QDate(2026, 6, 1); }

    QString m_connectionName;
    QString m_vesselId;
};

void TestCertificateListRows::init()
{
    m_connectionName = QStringLiteral("listrows_%1").arg(QTest::currentTestFunction());
    QVERIFY(openDatabase(m_connectionName));

    QSqlDatabase db = database();
    m_vesselId      = seedVessel(db, QStringLiteral("v1"), QStringLiteral("MV Alpha"),
                                 QStringLiteral("9074729"));
    QVERIFY(!m_vesselId.isEmpty());
}

void TestCertificateListRows::cleanup()
{
    closeDatabase(m_connectionName);
}

void TestCertificateListRows::seedFleet()
{
    QSqlDatabase db = database();

    // Well in the future, so these are plainly Valid on today().
    const QDate issued = QDate(2026, 1, 1);
    const QDate safe   = QDate(2031, 1, 1);

    QVERIFY(!seedCategorisedCertificate(db, QStringLiteral("s1"), m_vesselId,
                                        QStringLiteral("Safety Construction"),
                                        QStringLiteral("STATUTORY"), issued, safe)
                 .isEmpty());
    QVERIFY(!seedCategorisedCertificate(db, QStringLiteral("s2"), m_vesselId,
                                        QStringLiteral("Load Line"),
                                        QStringLiteral("STATUTORY"), issued, safe)
                 .isEmpty());
    // Expired a month ago: the one certificate needing attention, and it is
    // statutory.
    QVERIFY(!seedCategorisedCertificate(db, QStringLiteral("s3"), m_vesselId,
                                        QStringLiteral("Ballast Water Management"),
                                        QStringLiteral("STATUTORY"), issued,
                                        QDate(2026, 5, 1))
                 .isEmpty());

    QVERIFY(!seedCategorisedCertificate(db, QStringLiteral("c1"), m_vesselId,
                                        QStringLiteral("Class Hull"), QStringLiteral("CLASS"),
                                        issued, safe)
                 .isEmpty());
    QVERIFY(!seedCategorisedCertificate(db, QStringLiteral("c2"), m_vesselId,
                                        QStringLiteral("Class Machinery"),
                                        QStringLiteral("CLASS"), issued, safe)
                 .isEmpty());

    QVERIFY(!seedCategorisedCertificate(db, QStringLiteral("e1"), m_vesselId,
                                        QStringLiteral("Liferaft Servicing"),
                                        QStringLiteral("EQUIPMENT"), issued, safe)
                 .isEmpty());
}

void TestCertificateListRows::allReturnsEveryCategory()
{
    seedFleet();

    QSqlDatabase db = database();
    Source       source(db);

    QList<CertificateListRows::Row>           rows;
    QList<CertificateListRows::CategoryTally> tallies;
    QVERIFY2(source.rows.build(m_vesselId, CertificateCategory::Unset, /*onlyNeedingAttention=*/false,
                          today(), &rows, &tallies),
             qPrintable(source.rows.errorString()));

    QCOMPARE(rows.size(), 6);
}

void TestCertificateListRows::aCategoryReturnsOnlyThatCategory()
{
    seedFleet();

    QSqlDatabase db = database();
    Source       source(db);

    QList<CertificateListRows::Row>           rows;
    QList<CertificateListRows::CategoryTally> tallies;

    QVERIFY(source.rows.build(m_vesselId, CertificateCategory::Class, false, today(), &rows, &tallies));
    QCOMPARE(rows.size(), 2);
    for (const CertificateListRows::Row& row : rows) {
        QCOMPARE(row.certificate.category, CertificateCategory::Class);
    }

    QVERIFY(source.rows.build(m_vesselId, CertificateCategory::Statutory, false, today(), &rows,
                         &tallies));
    QCOMPARE(rows.size(), 3);
    for (const CertificateListRows::Row& row : rows) {
        QCOMPARE(row.certificate.category, CertificateCategory::Statutory);
    }
}

void TestCertificateListRows::anEmptyCategoryReturnsNothingAndStillHasATab()
{
    seedFleet(); // nothing is seeded as OTHER

    QSqlDatabase db = database();
    Source       source(db);

    QList<CertificateListRows::Row>           rows;
    QList<CertificateListRows::CategoryTally> tallies;
    QVERIFY(source.rows.build(m_vesselId, CertificateCategory::Other, false, today(), &rows, &tallies));

    QVERIFY(rows.isEmpty());

    // The tab still exists and reads zero: one that vanished when its
    // category emptied would move the others under the cursor.
    QCOMPARE(tallies.size(), 5);
    QCOMPARE(tallyOf(tallies, CertificateCategory::Other).visibleCount, 0);
    QVERIFY(!tallyOf(tallies, CertificateCategory::Other).needsAttention);
}

void TestCertificateListRows::thereIsAlwaysOneTallyPerTabInDisplayOrder()
{
    seedFleet();

    QSqlDatabase db = database();
    Source       source(db);

    QList<CertificateListRows::Row>           rows;
    QList<CertificateListRows::CategoryTally> tallies;
    QVERIFY(source.rows.build(m_vesselId, CertificateCategory::Unset, false, today(), &rows, &tallies));

    // §3's order, which the widget relies on to put the numbers against the
    // tabs without matching on the category.
    const QList<CertificateCategory> expected{
        CertificateCategory::Unset, CertificateCategory::Statutory, CertificateCategory::Class,
        CertificateCategory::Equipment, CertificateCategory::Other};
    QCOMPARE(CertificateListRows::categoryOrder(), expected);
    QCOMPARE(tallies.size(), expected.size());
    for (int index = 0; index < expected.size(); ++index) {
        QCOMPARE(tallies.at(index).category, expected.at(index));
    }

    QCOMPARE(tallyOf(tallies, CertificateCategory::Unset).visibleCount, 6);
    QCOMPARE(tallyOf(tallies, CertificateCategory::Statutory).visibleCount, 3);
    QCOMPARE(tallyOf(tallies, CertificateCategory::Class).visibleCount, 2);
    QCOMPARE(tallyOf(tallies, CertificateCategory::Equipment).visibleCount, 1);
}

void TestCertificateListRows::countsAreTheSameWhicheverTabIsSelected()
{
    seedFleet();

    QSqlDatabase db = database();
    Source       source(db);

    QList<CertificateListRows::Row>           rows;
    QList<CertificateListRows::CategoryTally> onAll;
    QList<CertificateListRows::CategoryTally> onEquipment;

    QVERIFY(source.rows.build(m_vesselId, CertificateCategory::Unset, false, today(), &rows, &onAll));
    QVERIFY(source.rows.build(m_vesselId, CertificateCategory::Equipment, false, today(), &rows,
                         &onEquipment));

    // §3: every tab shows its own number whichever tab is open. Standing on
    // Equipment must not make the other tabs read zero.
    for (CertificateCategory category : CertificateListRows::categoryOrder()) {
        QCOMPARE(tallyOf(onEquipment, category).visibleCount,
                 tallyOf(onAll, category).visibleCount);
    }
    QCOMPARE(rows.size(), 1); // the Equipment build's own rows
}

void TestCertificateListRows::countsFollowTheAttentionCheckbox()
{
    seedFleet();

    QSqlDatabase db = database();
    Source       source(db);

    QList<CertificateListRows::Row>           rows;
    QList<CertificateListRows::CategoryTally> tallies;
    QVERIFY(source.rows.build(m_vesselId, CertificateCategory::Unset, /*onlyNeedingAttention=*/true,
                         today(), &rows, &tallies));

    // §3: "with the checkbox ticked, Class (2) means two class certificates
    // needing attention". Only the expired statutory one qualifies here.
    QCOMPARE(rows.size(), 1);
    QCOMPARE(tallyOf(tallies, CertificateCategory::Unset).visibleCount, 1);
    QCOMPARE(tallyOf(tallies, CertificateCategory::Statutory).visibleCount, 1);
    QCOMPARE(tallyOf(tallies, CertificateCategory::Class).visibleCount, 0);
    QCOMPARE(tallyOf(tallies, CertificateCategory::Equipment).visibleCount, 0);
}

void TestCertificateListRows::aCategoryWithAnExpiredCertificateNeedsAttention()
{
    seedFleet();

    QSqlDatabase db = database();
    Source       source(db);

    QList<CertificateListRows::Row>           rows;
    QList<CertificateListRows::CategoryTally> tallies;
    QVERIFY(source.rows.build(m_vesselId, CertificateCategory::Unset, false, today(), &rows, &tallies));

    QVERIFY(tallyOf(tallies, CertificateCategory::Statutory).needsAttention);

    // And All is red too, because something on this vessel needs attention.
    QVERIFY(tallyOf(tallies, CertificateCategory::Unset).needsAttention);
}

void TestCertificateListRows::anAllValidCategoryDoesNotNeedAttention()
{
    seedFleet();

    QSqlDatabase db = database();
    Source       source(db);

    QList<CertificateListRows::Row>           rows;
    QList<CertificateListRows::CategoryTally> tallies;
    QVERIFY(source.rows.build(m_vesselId, CertificateCategory::Unset, false, today(), &rows, &tallies));

    // The whole value of the colour is that it is not on everything: Class
    // and Equipment hold only certificates valid for years yet.
    QVERIFY(!tallyOf(tallies, CertificateCategory::Class).needsAttention);
    QVERIFY(!tallyOf(tallies, CertificateCategory::Equipment).needsAttention);
    QVERIFY(!tallyOf(tallies, CertificateCategory::Other).needsAttention);
}

void TestCertificateListRows::redFollowsTheVesselNotTheCheckbox()
{
    seedFleet();

    QSqlDatabase db = database();
    Source       source(db);

    QList<CertificateListRows::Row>           rows;
    QList<CertificateListRows::CategoryTally> unfiltered;
    QList<CertificateListRows::CategoryTally> filtered;
    QList<CertificateListRows::CategoryTally> onClass;

    QVERIFY(source.rows.build(m_vesselId, CertificateCategory::Unset, false, today(), &rows,
                         &unfiltered));
    QVERIFY(source.rows.build(m_vesselId, CertificateCategory::Unset, true, today(), &rows, &filtered));
    // Standing on a tab that holds nothing needing attention, with the
    // checkbox on: the statutory tab must still be red, which is the entire
    // point — a problem in a category nobody is looking at (§3).
    QVERIFY(source.rows.build(m_vesselId, CertificateCategory::Class, true, today(), &rows, &onClass));

    for (CertificateCategory category : CertificateListRows::categoryOrder()) {
        QCOMPARE(tallyOf(filtered, category).needsAttention,
                 tallyOf(unfiltered, category).needsAttention);
        QCOMPARE(tallyOf(onClass, category).needsAttention,
                 tallyOf(unfiltered, category).needsAttention);
    }
    QVERIFY(tallyOf(onClass, CertificateCategory::Statutory).needsAttention);
}

void TestCertificateListRows::aVesselThatCannotBeReadStillReturnsFiveTabs()
{
    // An empty vessel id, which the repository refuses. The tabs are filled
    // in before anything can fail, so the screen shows five zeros rather
    // than no tabs at all.
    QSqlDatabase db = database();
    Source       source(db);

    QList<CertificateListRows::Row>           rows;
    QList<CertificateListRows::CategoryTally> tallies;
    tallies.append(CertificateListRows::CategoryTally()); // junk, to be cleared

    source.rows.build(QString(), CertificateCategory::Unset, false, today(), &rows, &tallies);

    QCOMPARE(tallies.size(), 5);
    QVERIFY(rows.isEmpty());
}

// QTEST_MAIN, not QTEST_APPLESS_MAIN: there are no widgets here, but opening
// a SQLite connection needs an application object — without one this crashes
// before the first test runs, which is how it was found. The pure-domain
// suites next door can be APPLESS because they touch no database.
QTEST_MAIN(TestCertificateListRows)
#include "tst_CertificateListRows.moc"
