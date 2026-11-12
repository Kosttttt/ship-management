#include "app/FirstRunWizard.h"
#include "app/InstallationModePage.h"

#include "core/BackupSettings.h"
#include "core/InstallationRepository.h"
#include "core/MigrationRunner.h"

#include <QPushButton>
#include <QRadioButton>
#include <QSqlDatabase>
#include <QTemporaryDir>
#include <QtTest>

// database-backup-spec §10.2: the first-run setup offers a way to restore a
// backup, because Settings cannot be reached until setup is finished.
//
// Only what can be tested without driving a modal file dialog: that the
// button is there, and that adding it left the page's own job alone. The
// sequence behind it is RestoreFromBackup, which is the same one
// tst_RestoreSwap and tst_BackupFileInspector already cover end to end — the
// point of §10.2 was that the checks must exist in exactly one place, so
// there is nothing second to test. The full flow is checked by hand on a
// second computer or a fresh user account.
class TestFirstRunRestore : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void theModePageOffersToRestoreABackup();
    void theWizardShowsTheRestoreButtonOnItsFirstPage();
    void theRestoreButtonDoesNotDisturbTheModeChoice();

private:
    QSqlDatabase database() { return QSqlDatabase::database(m_connectionName); }

    QString       m_connectionName;
    QTemporaryDir m_dataFolder;
    QTemporaryDir m_settingsFolder;
};

void TestFirstRunRestore::init()
{
    m_connectionName = QStringLiteral("firstrunrestore_%1").arg(QTest::currentTestFunction());

    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    db.setDatabaseName(QStringLiteral(":memory:"));
    QVERIFY(db.open());

    MigrationRunner runner(db, QStringLiteral(":/migrations"));
    QVERIFY2(runner.run(), qPrintable(runner.errorString()));

    // Throwaway, both of them: nothing here may reach the real data folder or
    // the real settings (CLAUDE.md §10.1).
    QVERIFY(m_dataFolder.isValid());
    QVERIFY(m_settingsFolder.isValid());
    BackupSettings::setSettingsFileOverride(
        m_settingsFolder.filePath(QStringLiteral("%1.ini").arg(QTest::currentTestFunction())));
}

void TestFirstRunRestore::cleanup()
{
    {
        QSqlDatabase db = QSqlDatabase::database(m_connectionName);
        if (db.isOpen()) {
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(m_connectionName);
    BackupSettings::setSettingsFileOverride(QString());
}

void TestFirstRunRestore::theModePageOffersToRestoreABackup()
{
    QSqlDatabase         db = database();
    InstallationModePage page(db, m_dataFolder.path());

    auto* button = page.findChild<QPushButton*>(QStringLiteral("restoreFromBackupButton"));
    QVERIFY2(button != nullptr, "the mode page must offer a way to restore a backup");
    QVERIFY(button->isEnabled());
    QVERIFY2(button->text().contains(QStringLiteral("Restore")), qPrintable(button->text()));
}

void TestFirstRunRestore::theWizardShowsTheRestoreButtonOnItsFirstPage()
{
    // Through the wizard rather than the page alone, because the button is
    // only useful if it is on the page somebody actually lands on: the mode
    // page is the start page, and the one page reachable before any
    // installation exists.
    QSqlDatabase           db = database();
    InstallationRepository repository(db);
    FirstRunWizard         wizard(repository, db, m_dataFolder.path());

    // startId() rather than currentPage(), which QWizard only fills in once
    // the wizard is shown — and showing a dialog is not something a test
    // should need to do to answer "which page comes first".
    QCOMPARE(wizard.startId(), static_cast<int>(WizardPageId::Mode));

    const QWizardPage* first = wizard.page(wizard.startId());
    QVERIFY(first != nullptr);
    QVERIFY(first->findChild<QPushButton*>(QStringLiteral("restoreFromBackupButton")) != nullptr);
}

void TestFirstRunRestore::theRestoreButtonDoesNotDisturbTheModeChoice()
{
    // The page's own job, asserted after the addition: OFFICE by default and
    // the last page, VESSEL leading on to the identity page
    // (first-run-wizard-spec §4).
    QSqlDatabase         db = database();
    InstallationModePage page(db, m_dataFolder.path());

    QCOMPARE(page.selectedMode(), InstallationMode::Office);
    QCOMPARE(page.nextId(), -1); // -1 means "last page", so the button reads Finish

    const QList<QRadioButton*> radios = page.findChildren<QRadioButton*>();
    QCOMPARE(radios.size(), 2); // still two choices, not three

    QRadioButton* vessel = nullptr;
    for (QRadioButton* radio : radios) {
        if (radio->text().contains(QStringLiteral("Vessel"))) {
            vessel = radio;
        }
    }
    QVERIFY(vessel != nullptr);
    vessel->setChecked(true);

    QCOMPARE(page.selectedMode(), InstallationMode::Vessel);
    QCOMPARE(page.nextId(), static_cast<int>(WizardPageId::VesselIdentity));
}

QTEST_MAIN(TestFirstRunRestore)
#include "tst_FirstRunRestore.moc"
