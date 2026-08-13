#include "app/MainWindow.h"
#include "app/ModuleRegistry.h"
#include "app/VesselEditForm.h"
#include "app/VesselListWidget.h"
#include "core/InstallationContext.h"
#include "core/MigrationRunner.h"
#include "core/Vessel.h"
#include "core/VesselRepository.h"

#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QPushButton>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTableWidget>
#include <QTimer>
#include <QtTest>

// certificate-crud-spec §8.5 items 11 and 12: the fleet changing while the
// application is running, and the toolbar selector noticing.
class TestVesselSelectorRefresh : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void vesselsChangedFiresAfterASuccessfulAdd();
    void vesselsChangedFiresAfterASuccessfulEdit();
    void vesselsChangedDoesNotFireWhenTheDialogIsCancelled();
    void selectorGainsANewVesselWithoutARestart();
    void selectorKeepsItsSelectionByIdAcrossARefresh();

private:
    QSqlDatabase database() { return QSqlDatabase::database(m_connectionName); }
    QString      seedVessel(const QString& id, const QString& name, const QString& imo);

    static InstallationContext officeContext()
    {
        InstallationRecord record;
        record.id     = QStringLiteral("install-office");
        record.mode   = InstallationMode::Office;
        record.nodeId = QStringLiteral("OFFICE");
        return InstallationContext(record);
    }

    // Fills whichever modal vessel dialog is open and closes it the given way.
    // The dialog runs its own event loop, so this has to happen from a timer
    // scheduled before the click that opens it.
    static void answerVesselDialog(const QString& name, const QString& imo, bool accept)
    {
        QTimer::singleShot(0, [name, imo, accept]() {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (dialog == nullptr) {
                return;
            }
            if (auto* form = dialog->findChild<VesselEditForm*>()) {
                Vessel vessel = form->vessel(); // keeps the id when editing
                vessel.name      = name;
                vessel.imoNumber = imo;
                form->setVessel(vessel);
            }
            if (accept) {
                dialog->accept();
            } else {
                dialog->reject();
            }
        });
    }

    QString m_connectionName;
};

void TestVesselSelectorRefresh::init()
{
    m_connectionName = QStringLiteral("selrefresh_%1").arg(QTest::currentTestFunction());

    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    db.setDatabaseName(QStringLiteral(":memory:"));
    QVERIFY(db.open());

    MigrationRunner runner(db, QStringLiteral(":/migrations"));
    QVERIFY2(runner.run(), qPrintable(runner.errorString()));
}

void TestVesselSelectorRefresh::cleanup()
{
    {
        QSqlDatabase db = QSqlDatabase::database(m_connectionName);
        if (db.isOpen()) {
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(m_connectionName);
}

QString TestVesselSelectorRefresh::seedVessel(const QString& id,
                                              const QString& name,
                                              const QString& imo)
{
    QSqlQuery query(database());
    query.prepare(QStringLiteral(
        "INSERT INTO vessel (id, name, imo_number, created_at, created_by, updated_at,"
        " updated_by, is_deleted, origin_node, revision)"
        " VALUES (?, ?, ?, '2026-01-01T00:00:00Z', 'SEED', '2026-01-01T00:00:00Z', 'SEED',"
        " 0, 'OFFICE', 1)"));
    query.addBindValue(id);
    query.addBindValue(name);
    query.addBindValue(imo);
    query.exec();
    return id;
}

void TestVesselSelectorRefresh::vesselsChangedFiresAfterASuccessfulAdd()
{
    QSqlDatabase              db      = database();
    const InstallationContext context = officeContext();
    VesselRepository          vessels(db, context);

    VesselListWidget widget(vessels);
    QSignalSpy       spy(&widget, &VesselListWidget::vesselsChanged);

    auto* addButton = widget.findChildren<QPushButton*>().value(0);
    QVERIFY(addButton != nullptr);

    answerVesselDialog(QStringLiteral("MV Added"), QStringLiteral("9074729"), /*accept=*/true);
    addButton->click();

    QCOMPARE(spy.count(), 1);

    QList<Vessel> fleet;
    QVERIFY(vessels.list(&fleet));
    QCOMPARE(fleet.size(), 1);
}

void TestVesselSelectorRefresh::vesselsChangedFiresAfterASuccessfulEdit()
{
    QSqlDatabase              db      = database();
    const InstallationContext context = officeContext();
    VesselRepository          vessels(db, context);
    seedVessel(QStringLiteral("v1"), QStringLiteral("MV Alpha"), QStringLiteral("9074729"));

    VesselListWidget widget(vessels);
    QSignalSpy       spy(&widget, &VesselListWidget::vesselsChanged);

    auto* table = widget.findChild<QTableWidget*>();
    QVERIFY(table != nullptr);
    QCOMPARE(table->rowCount(), 1);

    answerVesselDialog(QStringLiteral("MV Renamed"), QStringLiteral("9074729"), /*accept=*/true);
    // Editing is normally reached by double-clicking a row; invoking the
    // table's own signal is the same entry point without a mouse.
    QMetaObject::invokeMethod(table, "itemDoubleClicked",
                              Q_ARG(QTableWidgetItem*, table->item(0, 0)));

    QCOMPARE(spy.count(), 1);

    QList<Vessel> fleet;
    QVERIFY(vessels.list(&fleet));
    QCOMPARE(fleet.first().name, QStringLiteral("MV Renamed"));
}

void TestVesselSelectorRefresh::vesselsChangedDoesNotFireWhenTheDialogIsCancelled()
{
    QSqlDatabase              db      = database();
    const InstallationContext context = officeContext();
    VesselRepository          vessels(db, context);

    VesselListWidget widget(vessels);
    QSignalSpy       spy(&widget, &VesselListWidget::vesselsChanged);

    auto* addButton = widget.findChildren<QPushButton*>().value(0);
    answerVesselDialog(QStringLiteral("MV Discarded"), QStringLiteral("9074729"),
                       /*accept=*/false);
    addButton->click();

    QCOMPARE(spy.count(), 0);

    QList<Vessel> fleet;
    QVERIFY(vessels.list(&fleet));
    QVERIFY(fleet.isEmpty());
}

void TestVesselSelectorRefresh::selectorGainsANewVesselWithoutARestart()
{
    seedVessel(QStringLiteral("v1"), QStringLiteral("MV Alpha"), QStringLiteral("9074729"));

    QSqlDatabase              db      = database();
    const InstallationContext context = officeContext();
    VesselRepository          vessels(db, context);
    ModuleRegistry            registry(db, context);

    MainWindow window(context, vessels, registry);

    auto* selector = window.findChild<QComboBox*>(QStringLiteral("vesselSelector"));
    QVERIFY(selector != nullptr);
    QCOMPARE(selector->count(), 1);

    auto* list = window.findChild<VesselListWidget*>();
    QVERIFY(list != nullptr);
    auto* addButton = list->findChildren<QPushButton*>().value(0);
    QVERIFY(addButton != nullptr);

    answerVesselDialog(QStringLiteral("MV Bravo"), QStringLiteral("9319466"), /*accept=*/true);
    addButton->click();

    // The whole point: no restart in between.
    QCOMPARE(selector->count(), 2);
}

void TestVesselSelectorRefresh::selectorKeepsItsSelectionByIdAcrossARefresh()
{
    // "MV Zulu" sorts last now and second after "MV Bravo" is inserted, so a
    // refresh that restored the selection by position would land on the wrong
    // vessel.
    seedVessel(QStringLiteral("v1"), QStringLiteral("MV Alpha"), QStringLiteral("9074729"));
    seedVessel(QStringLiteral("v2"), QStringLiteral("MV Zulu"), QStringLiteral("9319466"));

    QSqlDatabase              db      = database();
    const InstallationContext context = officeContext();
    VesselRepository          vessels(db, context);
    ModuleRegistry            registry(db, context);

    MainWindow window(context, vessels, registry);

    auto* selector = window.findChild<QComboBox*>(QStringLiteral("vesselSelector"));
    QVERIFY(selector != nullptr);
    QCOMPARE(selector->count(), 2);

    const int zuluIndex = selector->findData(QStringLiteral("v2"));
    QCOMPARE(zuluIndex, 1);
    selector->setCurrentIndex(zuluIndex);

    auto* list      = window.findChild<VesselListWidget*>();
    auto* addButton = list->findChildren<QPushButton*>().value(0);
    answerVesselDialog(QStringLiteral("MV Bravo"), QStringLiteral("9247455"), /*accept=*/true);
    addButton->click();

    QCOMPARE(selector->count(), 3);
    // Still MV Zulu, even though it has moved from position 1 to position 2.
    QCOMPARE(selector->currentData().toString(), QStringLiteral("v2"));
    QCOMPARE(selector->currentIndex(), 2);
    QVERIFY(selector->currentText().contains(QStringLiteral("Zulu")));
}

QTEST_MAIN(TestVesselSelectorRefresh)
#include "tst_VesselSelectorRefresh.moc"
