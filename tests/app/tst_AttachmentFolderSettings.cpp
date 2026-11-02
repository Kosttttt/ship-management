// Reaches across to the vessel fixture rather than copying its helpers: this
// screen is about vessels, and seeding one the same way the VesselRepository
// suites do is the point. The only cross-folder include in tests/, and
// deliberately so — a second copy of seedVessel() would be free to drift.
#include "../core/VesselTestSupport.h"

#include "app/AttachmentFolderSettings.h"
#include "core/ManagedFileStore.h"
#include "core/VesselAttachmentRoots.h"
#include "core/VesselRepository.h"

#include <QDir>
#include <QLabel>
#include <QPushButton>
#include <QTemporaryDir>
#include <QtTest>

using namespace VesselTestSupport;

// certificate-attachment-addendum-spec §2.1 and §7 case 5: one control per
// vessel at the office, and exactly one — with no list to speak of — on a
// vessel installation.
//
// Choosing a folder itself is not driven from here: it opens a native
// QFileDialog, which a test cannot answer. What that button goes on to do is
// VesselAttachmentRoots::changeRoot(), which has its own suite.
class TestAttachmentFolderSettings : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void officeModeShowsOneRowPerVessel();              // case 5
    void vesselModeShowsExactlyItsOwnVessel();          // case 5
    void anUnconfiguredVesselSaysSoRatherThanLookingSet();
    void aConfiguredVesselShowsTheFolderItIsUsing();
    void reloadFollowsAVesselBeingAdded();

private:
    QSqlDatabase database() { return QSqlDatabase::database(m_connectionName); }

    static QLabel* pathLabelFor(AttachmentFolderSettings& page, const QString& vesselId)
    {
        return page.findChild<QLabel*>(
            QStringLiteral("attachmentFolderPath_%1").arg(vesselId));
    }

    static QPushButton* buttonFor(AttachmentFolderSettings& page, const QString& vesselId)
    {
        return page.findChild<QPushButton*>(
            QStringLiteral("attachmentFolderButton_%1").arg(vesselId));
    }

    QString m_connectionName;
};

void TestAttachmentFolderSettings::init()
{
    m_connectionName = QStringLiteral("folders_%1").arg(QTest::currentTestFunction());
    QVERIFY(openDatabase(m_connectionName));

    // This screen reads and writes the real stored column, so the override has
    // no part to play — and would mask the "not set" state under test.
    ManagedFileStore::setRootPathOverride(QString());
}

void TestAttachmentFolderSettings::cleanup()
{
    closeDatabase(m_connectionName);
}

void TestAttachmentFolderSettings::officeModeShowsOneRowPerVessel()
{
    QSqlDatabase db = database();

    const QString first  = seedVessel(db, QStringLiteral("MV One"),
                                      QStringLiteral("9074729"));
    const QString second = seedVessel(db, QStringLiteral("MV Two"),
                                      QStringLiteral("9245807"));

    VesselRepository         vessels(db, officeContext());
    AttachmentFolderSettings page(vessels);

    QCOMPARE(page.vesselRowCount(), 2);
    QVERIFY(buttonFor(page, first) != nullptr);
    QVERIFY(buttonFor(page, second) != nullptr);
}

void TestAttachmentFolderSettings::vesselModeShowsExactlyItsOwnVessel()
{
    QSqlDatabase db = database();

    const QString mine     = seedVessel(db, QStringLiteral("MV Mine"),
                                        QStringLiteral("9074729"));
    const QString somebody = seedVessel(db, QStringLiteral("MV Somebody Else"),
                                        QStringLiteral("9245807"));

    // The other vessel's row exists — the sort of thing a future sync would
    // bring in — and must not appear. The filter doing that work lives in the
    // repository, not here (CLAUDE.md §3), which is the point of asserting it.
    VesselRepository         vessels(db, vesselContext(mine, QStringLiteral("9074729")));
    AttachmentFolderSettings page(vessels);

    QCOMPARE(page.vesselRowCount(), 1);
    QVERIFY(buttonFor(page, mine) != nullptr);
    QVERIFY2(buttonFor(page, somebody) == nullptr,
             "a vessel installation must not offer to point another ship at a folder");
}

void TestAttachmentFolderSettings::anUnconfiguredVesselSaysSoRatherThanLookingSet()
{
    QSqlDatabase db = database();

    const QString vesselId = seedVessel(db, QStringLiteral("MV Example"),
                                        QStringLiteral("9074729"));

    VesselRepository         vessels(db, officeContext());
    AttachmentFolderSettings page(vessels);

    QLabel* label = pathLabelFor(page, vesselId);
    QVERIFY(label != nullptr);

    // §2.2: the consequence is spelled out rather than left as a blank field,
    // because a blank field reads as "fine" and this is not fine.
    QVERIFY2(label->text().contains(QStringLiteral("Not set")), qPrintable(label->text()));
    QVERIFY(label->text().contains(QStringLiteral("attached")));
}

void TestAttachmentFolderSettings::aConfiguredVesselShowsTheFolderItIsUsing()
{
    QSqlDatabase db = database();

    const QString vesselId = seedVessel(db, QStringLiteral("MV Example"),
                                        QStringLiteral("9074729"));

    QTemporaryDir folder;
    QVERIFY(folder.isValid());

    VesselRepository      vessels(db, officeContext());
    VesselAttachmentRoots roots(vessels);

    QString error;
    QVERIFY2(roots.changeRoot(vesselId, folder.path(), &error), qPrintable(error));

    AttachmentFolderSettings page(vessels);

    QLabel* label = pathLabelFor(page, vesselId);
    QVERIFY(label != nullptr);

    // Shown in native separators, because this is a path the user chose and
    // will recognise — unlike the relative paths stored against a certificate,
    // which are never displayed.
    QCOMPARE(label->text(),
             QDir::toNativeSeparators(QFileInfo(folder.path()).absoluteFilePath()));
}

void TestAttachmentFolderSettings::reloadFollowsAVesselBeingAdded()
{
    QSqlDatabase db = database();

    VesselRepository         vessels(db, officeContext());
    AttachmentFolderSettings page(vessels);

    QCOMPARE(page.vesselRowCount(), 0);
    QVERIFY(page.findChild<QLabel*>(QStringLiteral("noVesselsForAttachments")) != nullptr);

    const QString vesselId = seedVessel(db, QStringLiteral("MV Later"),
                                        QStringLiteral("9074729"));
    page.reload();

    QCOMPARE(page.vesselRowCount(), 1);
    QVERIFY(buttonFor(page, vesselId) != nullptr);
}

QTEST_MAIN(TestAttachmentFolderSettings)
#include "tst_AttachmentFolderSettings.moc"
