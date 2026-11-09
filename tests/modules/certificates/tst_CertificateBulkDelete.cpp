#include "CertificateTestSupport.h"

#include "core/AppSettingRepository.h"
#include "modules/certificates/data/CertificateExtensionRepository.h"
#include "modules/certificates/data/CertificateRepository.h"
#include "modules/certificates/data/EndorsementRepository.h"
#include "modules/certificates/service/CertificateBulkDeletion.h"
#include "modules/certificates/service/CertificateDeletionService.h"
#include "modules/certificates/ui/CertificateListWidget.h"

#include <QPushButton>
#include <QTableWidget>
#include <QtTest>

using namespace CertificateTestSupport;

// certificate-list-usability-fixes.md §2: more than one row can be selected,
// only Delete acts on more than one, and a bulk delete is best effort per
// certificate.
class TestCertificateBulkDelete : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    // The selection rule
    void theTableAllowsMoreThanOneRowToBeSelected();
    void oneSelectedRowEnablesEveryAction();
    void twoSelectedRowsDisableEveryActionExceptDelete();
    void theAttachmentButtonIsDisabledTooWhenTwoRowsAreSelected();
    void selectingNothingDisablesDeleteAsWell();
    void goingBackToOneRowReEnablesTheSingleCertificateActions();

    // The loop
    void everySelectedCertificateIsDeleted();
    void aFailureInTheMiddleDoesNotStopTheRest();
    void aFailureIsReportedWithTheCertificateName();
    void theSuccessesAreCommittedDespiteTheFailure();
    void deletingNothingIsAnEmptyOutcomeRatherThanAnError();

private:
    QSqlDatabase database() { return QSqlDatabase::database(m_connectionName); }

    // Two certificates that both expire, so Renew and the extension button
    // have something they are willing to act on.
    void seedTwoExpiringCertificates();

    QList<Certificate> loadAll();

    QString m_connectionName;
    QString m_vesselId;
};

void TestCertificateBulkDelete::init()
{
    m_connectionName = QStringLiteral("bulkdelete_%1").arg(QTest::currentTestFunction());
    QVERIFY(openDatabase(m_connectionName));

    m_vesselId      = QStringLiteral("v1");
    QSqlDatabase db = database();
    QVERIFY(!seedVessel(db, m_vesselId, QStringLiteral("MV Aurora"),
                        QStringLiteral("9074729"))
                 .isEmpty());
}

void TestCertificateBulkDelete::cleanup()
{
    closeDatabase(m_connectionName);
}

void TestCertificateBulkDelete::seedTwoExpiringCertificates()
{
    QSqlDatabase db = database();
    seedCertificateWithSchedule(db, QStringLiteral("c1"), m_vesselId,
                                QStringLiteral("Safety Radio"), QStringLiteral("1"),
                                QDate(2026, 2, 4), QDate(2031, 2, 4), false, false);
    seedCertificateWithSchedule(db, QStringLiteral("c2"), m_vesselId,
                                QStringLiteral("Load Line"), QStringLiteral("2"),
                                QDate(2026, 2, 4), QDate(2031, 2, 4), false, false);
}

QList<Certificate> TestCertificateBulkDelete::loadAll()
{
    QSqlDatabase          db = database();
    CertificateRepository certificates(db, officeContext());
    QList<Certificate>    all;
    certificates.list(m_vesselId, &all);
    return all;
}

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
                                        attachments.reportBuilder, context);                     \
    name.setVesselId(m_vesselId)

namespace {

QPushButton* buttonNamed(const CertificateListWidget& widget, const char* name)
{
    return widget.findChild<QPushButton*>(QString::fromLatin1(name));
}

QTableWidget* tableOf(const CertificateListWidget& widget)
{
    return widget.findChild<QTableWidget*>(QStringLiteral("certificateTable"));
}

// Ctrl-clicking a second row, without going through the mouse.
void selectRows(QTableWidget* table, const QList<int>& rows)
{
    table->clearSelection();
    for (int row : rows) {
        table->selectionModel()->select(table->model()->index(row, 0),
                                        QItemSelectionModel::Select
                                            | QItemSelectionModel::Rows);
    }
}

} // namespace

void TestCertificateBulkDelete::theTableAllowsMoreThanOneRowToBeSelected()
{
    seedTwoExpiringCertificates();
    MAKE_WIDGET(widget);

    QCOMPARE(tableOf(widget)->selectionMode(), QAbstractItemView::ExtendedSelection);
}

void TestCertificateBulkDelete::oneSelectedRowEnablesEveryAction()
{
    seedTwoExpiringCertificates();
    MAKE_WIDGET(widget);

    tableOf(widget)->selectRow(0);

    QVERIFY(buttonNamed(widget, "renewCertificateButton")->isEnabled());
    QVERIFY(buttonNamed(widget, "certificateExtensionButton")->isEnabled());
    QVERIFY(buttonNamed(widget, "certificateAttachmentButton")->isEnabled());
    QVERIFY(buttonNamed(widget, "deleteCertificateButton")->isEnabled());
}

void TestCertificateBulkDelete::twoSelectedRowsDisableEveryActionExceptDelete()
{
    seedTwoExpiringCertificates();
    MAKE_WIDGET(widget);

    selectRows(tableOf(widget), {0, 1});

    // §2: renewing, extending and attaching are all inherently about one
    // certificate, so two selected rows means they have no subject. The rule
    // lives once, in CertificateSelection, rather than three times here.
    QVERIFY2(!buttonNamed(widget, "renewCertificateButton")->isEnabled(),
             "Renew has no single certificate to renew");
    QVERIFY2(!buttonNamed(widget, "certificateExtensionButton")->isEnabled(),
             "an extension applies to one certificate's cycle");
    QVERIFY2(buttonNamed(widget, "deleteCertificateButton")->isEnabled(),
             "Delete is the one action that works on a whole selection");
}

void TestCertificateBulkDelete::theAttachmentButtonIsDisabledTooWhenTwoRowsAreSelected()
{
    // Its own test because this button is the one that nearly got missed: it
    // kept a private copy of "the first selected row" rather than asking
    // CertificateSelection, so it would have stayed enabled and acted on a
    // row the user had not singled out.
    seedTwoExpiringCertificates();
    MAKE_WIDGET(widget);

    selectRows(tableOf(widget), {0, 1});
    QVERIFY(!buttonNamed(widget, "certificateAttachmentButton")->isEnabled());
}

void TestCertificateBulkDelete::selectingNothingDisablesDeleteAsWell()
{
    seedTwoExpiringCertificates();
    MAKE_WIDGET(widget);

    tableOf(widget)->clearSelection();
    QVERIFY(!buttonNamed(widget, "deleteCertificateButton")->isEnabled());
}

void TestCertificateBulkDelete::goingBackToOneRowReEnablesTheSingleCertificateActions()
{
    seedTwoExpiringCertificates();
    MAKE_WIDGET(widget);

    selectRows(tableOf(widget), {0, 1});
    QVERIFY(!buttonNamed(widget, "renewCertificateButton")->isEnabled());

    selectRows(tableOf(widget), {1});
    QVERIFY2(buttonNamed(widget, "renewCertificateButton")->isEnabled(),
             "the rule is about how many are selected, not a latch");
    QVERIFY(buttonNamed(widget, "certificateAttachmentButton")->isEnabled());
}

void TestCertificateBulkDelete::everySelectedCertificateIsDeleted()
{
    seedTwoExpiringCertificates();

    QSqlDatabase                   db = database();
    const InstallationContext      context = officeContext();
    CertificateRepository          certificates(db, context);
    CertificateExtensionRepository extensions(db, context);
    AttachmentFixture              attachments(db, context);
    CertificateDeletionService     deletion(certificates, extensions, attachments.service);

    const BulkDeletionOutcome outcome = deleteCertificates(deletion, loadAll());

    QCOMPARE(outcome.deletedNames.size(), 2);
    QVERIFY(outcome.failures.isEmpty());
    QVERIFY(outcome.fileLeftovers.isEmpty());
    QVERIFY(outcome.anythingDeleted());

    QList<Certificate> left;
    QVERIFY(certificates.list(m_vesselId, &left));
    QVERIFY2(left.isEmpty(), "both should be gone from the list");
}

void TestCertificateBulkDelete::aFailureInTheMiddleDoesNotStopTheRest()
{
    seedTwoExpiringCertificates();

    QSqlDatabase                   db = database();
    const InstallationContext      context = officeContext();
    CertificateRepository          certificates(db, context);
    CertificateExtensionRepository extensions(db, context);
    AttachmentFixture              attachments(db, context);
    CertificateDeletionService     deletion(certificates, extensions, attachments.service);

    // A certificate that is not in the database at all, sandwiched between
    // two that are: softDeleteWithChildren() refuses it outright, which is
    // the cleanest deterministic failure there is.
    QList<Certificate> all = loadAll();
    QCOMPARE(all.size(), 2);

    Certificate missing = all.first();
    missing.id          = QStringLiteral("no-such-certificate");
    missing.name        = QStringLiteral("Ghost");

    const QList<Certificate> batch = {all.at(0), missing, all.at(1)};

    const BulkDeletionOutcome outcome = deleteCertificates(deletion, batch);

    // §2: the other two go through rather than being rolled back over one
    // failure. That is the whole reason there is no outer transaction.
    QCOMPARE(outcome.deletedNames.size(), 2);
    QCOMPARE(outcome.failures.size(), 1);
}

void TestCertificateBulkDelete::aFailureIsReportedWithTheCertificateName()
{
    seedTwoExpiringCertificates();

    QSqlDatabase                   db = database();
    const InstallationContext      context = officeContext();
    CertificateRepository          certificates(db, context);
    CertificateExtensionRepository extensions(db, context);
    AttachmentFixture              attachments(db, context);
    CertificateDeletionService     deletion(certificates, extensions, attachments.service);

    Certificate missing = loadAll().first();
    missing.id          = QStringLiteral("no-such-certificate");
    missing.name        = QStringLiteral("Ghost");

    const BulkDeletionOutcome outcome = deleteCertificates(deletion, {missing});

    QCOMPARE(outcome.failures.size(), 1);
    QVERIFY2(outcome.failures.first().contains(QStringLiteral("Ghost")),
             "a summary over twenty rows is useless without the name");
    QVERIFY(!outcome.anythingDeleted());
}

void TestCertificateBulkDelete::theSuccessesAreCommittedDespiteTheFailure()
{
    seedTwoExpiringCertificates();

    QSqlDatabase                   db = database();
    const InstallationContext      context = officeContext();
    CertificateRepository          certificates(db, context);
    CertificateExtensionRepository extensions(db, context);
    AttachmentFixture              attachments(db, context);
    CertificateDeletionService     deletion(certificates, extensions, attachments.service);

    QList<Certificate> all = loadAll();
    Certificate        missing = all.first();
    missing.id                 = QStringLiteral("no-such-certificate");

    deleteCertificates(deletion, {all.at(0), missing, all.at(1)});

    // The database, not just the outcome struct: each call commits its own
    // transaction inside softDeleteWithChildren(), and nothing wraps the
    // loop, so a later failure cannot undo an earlier success.
    QList<Certificate> left;
    QVERIFY(certificates.list(m_vesselId, &left));
    QVERIFY2(left.isEmpty(), "both real certificates stay deleted");
}

void TestCertificateBulkDelete::deletingNothingIsAnEmptyOutcomeRatherThanAnError()
{
    QSqlDatabase                   db = database();
    const InstallationContext      context = officeContext();
    CertificateRepository          certificates(db, context);
    CertificateExtensionRepository extensions(db, context);
    AttachmentFixture              attachments(db, context);
    CertificateDeletionService     deletion(certificates, extensions, attachments.service);

    const BulkDeletionOutcome outcome = deleteCertificates(deletion, {});

    QVERIFY(outcome.deletedNames.isEmpty());
    QVERIFY(outcome.failures.isEmpty());
    QVERIFY(!outcome.anythingDeleted());
}

QTEST_MAIN(TestCertificateBulkDelete)
#include "tst_CertificateBulkDelete.moc"
