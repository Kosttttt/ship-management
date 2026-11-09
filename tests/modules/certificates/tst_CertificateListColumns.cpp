#include "CertificateTestSupport.h"

#include "core/AppSettingRepository.h"
#include "modules/certificates/data/CertificateExtensionRepository.h"
#include "modules/certificates/data/CertificateRepository.h"
#include "modules/certificates/data/EndorsementRepository.h"
#include "modules/certificates/ui/CertificateListWidget.h"
#include "modules/certificates/ui/TableHeaderSettings.h"

#include <QHeaderView>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QtTest>

using namespace CertificateTestSupport;

// certificate-list-usability-fixes.md §1: the columns are draggable, Name
// starts wide, and whatever the user drags them to comes back next time.
class TestCertificateListColumns : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void everyColumnCanBeDraggedByTheUser();
    void theLastColumnTakesUpTheLeftoverWidth();
    void nameStartsWiderThanEveryOtherColumn();
    void theDefaultSortIsStillTheReferenceNumber();

    void withNothingSavedTheDefaultWidthsApply();
    void aResizedColumnComesBackTheSameWidthNextTime();
    void aSavedWidthWinsOverTheDefault();
    void aReorderedColumnComesBack();
    void aSavedSortColumnComesBack();
    void aLayoutSavedForOneTableDoesNotLeakIntoAnotherKey();

private:
    QSqlDatabase database() { return QSqlDatabase::database(m_connectionName); }

    // Columns, as CertificateListWidget lays them out. Repeated here rather
    // than shared, the same way the other list suites name the columns they
    // assert on.
    enum Column {
        ListNumber = 0,
        Name       = 1,
        IssueDate  = 2,
        Status     = 3,
        Expiry     = 4,
        SurveyFrom = 5,
        SurveyTo   = 6,
        DaysLeft   = 7,
        Document   = 8
    };
    static constexpr int kColumnCount = 9;

    static QHeaderView*  headerOf(const CertificateListWidget& widget);

    QString       m_connectionName;
    QTemporaryDir m_settingsFolder;
};

void TestCertificateListColumns::init()
{
    m_connectionName = QStringLiteral("listcolumns_%1").arg(QTest::currentTestFunction());
    QVERIFY(openDatabase(m_connectionName));

    // Never the developer's real settings - the same reason AttachmentFixture
    // redirects ManagedFileStore (CLAUDE.md §10.1). One file per test
    // function, so no test can see another's saved layout.
    QVERIFY(m_settingsFolder.isValid());
    TableHeaderSettings::setSettingsFileOverride(
        m_settingsFolder.filePath(QStringLiteral("ui.ini")));
}

void TestCertificateListColumns::cleanup()
{
    // Cleared, not left pointing at a folder about to vanish: the override is
    // process-wide.
    TableHeaderSettings::setSettingsFileOverride(QString());
    closeDatabase(m_connectionName);
}

QHeaderView* TestCertificateListColumns::headerOf(const CertificateListWidget& widget)
{
    auto* table = widget.findChild<QTableWidget*>(QStringLiteral("certificateTable"));
    return table == nullptr ? nullptr : table->horizontalHeader();
}

// Everything a CertificateListWidget needs, in one place, so each test can
// build and destroy one in a couple of lines.
#define MAKE_WIDGET(name)                                                                        \
    QSqlDatabase                   db      = database();                                         \
    const InstallationContext      context = officeContext();                                    \
    CertificateRepository          certificates(db, context);                                    \
    EndorsementRepository          endorsements(db, context);                                    \
    CertificateExtensionRepository extensions(db, context);                                      \
    AppSettingRepository           appSettings(db);                                              \
    AttachmentFixture              attachments(db, context);                                     \
    CertificateListWidget          name(certificates, endorsements, extensions,                  \
                                        attachments.service, appSettings,                        \
                                        attachments.csvExporter, attachments.csvImporter,        \
                                        attachments.reportBuilder, context)

void TestCertificateListColumns::everyColumnCanBeDraggedByTheUser()
{
    MAKE_WIDGET(widget);

    QHeaderView* header = headerOf(widget);
    QVERIFY(header != nullptr);

    // §1: Stretch and ResizeToContents both actively disable dragging, which
    // is why this is a changed setting rather than a missing feature.
    for (int column = 0; column < kColumnCount; ++column) {
        QCOMPARE(header->sectionResizeMode(column), QHeaderView::Interactive);
    }
}

void TestCertificateListColumns::theLastColumnTakesUpTheLeftoverWidth()
{
    MAKE_WIDGET(widget);
    QVERIFY(headerOf(widget)->stretchLastSection());
}

void TestCertificateListColumns::nameStartsWiderThanEveryOtherColumn()
{
    MAKE_WIDGET(widget);

    QHeaderView* header = headerOf(widget);
    QVERIFY(header != nullptr);

    // The whole complaint behind §1: a long certificate name was clipped
    // because Stretch gave every column the same share.
    for (int column = 0; column < kColumnCount; ++column) {
        if (column == Name || column == Document) {
            continue; // Document stretches, so its width is not a default
        }
        QVERIFY2(header->sectionSize(Name) > header->sectionSize(column),
                 qPrintable(QStringLiteral("Name must start wider than column %1")
                                .arg(column)));
    }
}

void TestCertificateListColumns::theDefaultSortIsStillTheReferenceNumber()
{
    MAKE_WIDGET(widget);

    // certificate-crud-spec §8.2, unchanged by any of this.
    QCOMPARE(headerOf(widget)->sortIndicatorSection(), static_cast<int>(ListNumber));
    QCOMPARE(headerOf(widget)->sortIndicatorOrder(), Qt::AscendingOrder);
}

void TestCertificateListColumns::withNothingSavedTheDefaultWidthsApply()
{
    MAKE_WIDGET(widget);

    // A first run has nothing to restore, which is not an error.
    QVERIFY2(headerOf(widget)->sectionSize(Name) > 200,
             "the hardcoded default width should still be in force");
}

void TestCertificateListColumns::aResizedColumnComesBackTheSameWidthNextTime()
{
    {
        MAKE_WIDGET(widget);
        // What dragging a column border does.
        headerOf(widget)->resizeSection(Name, 480);
        QCOMPARE(headerOf(widget)->sectionSize(Name), 480);
    }

    // §1: saved on the resize itself, not on a clean shutdown, so a
    // force-quit does not lose it. The widget above was destroyed without
    // anything being closed down politely.
    {
        MAKE_WIDGET(reopened);
        QCOMPARE(headerOf(reopened)->sectionSize(Name), 480);
    }
}

void TestCertificateListColumns::aSavedWidthWinsOverTheDefault()
{
    {
        MAKE_WIDGET(widget);
        // Deliberately narrower than the default, so "restored" and "default"
        // cannot be confused for one another.
        headerOf(widget)->resizeSection(Name, 140);
    }

    {
        MAKE_WIDGET(reopened);
        QCOMPARE(headerOf(reopened)->sectionSize(Name), 140);
        QVERIFY2(headerOf(reopened)->sectionSize(Name) < 300,
                 "the restore has to happen after the defaults, not before");
    }
}

void TestCertificateListColumns::aReorderedColumnComesBack()
{
    {
        MAKE_WIDGET(widget);
        // saveState() captures order too, so dragging a whole column should
        // survive as well as resizing one.
        headerOf(widget)->moveSection(headerOf(widget)->visualIndex(Name), 0);
        QCOMPARE(headerOf(widget)->visualIndex(Name), 0);
    }

    {
        MAKE_WIDGET(reopened);
        QCOMPARE(headerOf(reopened)->visualIndex(Name), 0);
    }
}

void TestCertificateListColumns::aSavedSortColumnComesBack()
{
    {
        MAKE_WIDGET(widget);
        auto* table = widget.findChild<QTableWidget*>(QStringLiteral("certificateTable"));
        QVERIFY(table != nullptr);
        table->sortByColumn(Name, Qt::DescendingOrder);
    }

    // A consequence of using QHeaderView::saveState() rather than storing
    // widths alone, and a deliberate one: the state blob carries the sort
    // indicator, so a returning user gets the sort they left the table on
    // rather than the default from certificate-crud-spec §8.2. Asserted so
    // that it is a decision on the record rather than a surprise.
    {
        MAKE_WIDGET(reopened);
        QCOMPARE(headerOf(reopened)->sortIndicatorSection(), static_cast<int>(Name));
        QCOMPARE(headerOf(reopened)->sortIndicatorOrder(), Qt::DescendingOrder);
    }
}

void TestCertificateListColumns::aLayoutSavedForOneTableDoesNotLeakIntoAnotherKey()
{
    {
        MAKE_WIDGET(widget);
        headerOf(widget)->resizeSection(Name, 410);
    }

    // A second table, under its own key, must not inherit the first one's
    // layout - which is the reason the key is a constructor argument rather
    // than a constant inside TableHeaderSettings.
    QTableWidget other;
    other.setColumnCount(kColumnCount);
    TableHeaderSettings settings(other.horizontalHeader(),
                                 QStringLiteral("ui/someOtherTable/headerState"));
    QVERIFY2(!settings.restore(), "nothing has ever been saved under that key");
}

QTEST_MAIN(TestCertificateListColumns)
#include "tst_CertificateListColumns.moc"
