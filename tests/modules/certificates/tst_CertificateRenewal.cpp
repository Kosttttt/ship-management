#include "CertificateTestSupport.h"

#include "core/AppSettingRepository.h"
#include "modules/certificates/data/CertificateExtensionRepository.h"
#include "modules/certificates/data/CertificateRepository.h"
#include "modules/certificates/data/EndorsementRepository.h"
#include "modules/certificates/ui/CertificateEditDialog.h"
#include "modules/certificates/ui/CertificateEditForm.h"
#include "modules/certificates/ui/CertificateListWidget.h"
#include "modules/certificates/ui/StatusItem.h"

#include <QApplication>
#include <QDialog>
#include <QGroupBox>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <QtTest>

#include <functional>
#include <memory>

using namespace CertificateTestSupport;

// certificate-renewal-spec §7, the nine edge cases.
class TestCertificateRenewal : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void successfulRenewalMovesDatesAndClearsSurveys();          // 1
    void cancellingTheConfirmationChangesNothing();              // 2
    void cancellingTheEditDialogChangesNothing();                // 3
    void renewIsDisabledWithNoSelection();                       // 4
    void renewIsDisabledForACertificateThatNeverExpires();       // 5
    void confirmationNamesTheSurveyCountOnlyWhenThereAreSome();  // 6
    void renewalModeHidesTheEndorsementsSection();               // 7
    void renewedCertificateComesBackAsValid();                   // 8
    void doubleClickStillOpensTheOrdinaryEditDialog();           // 9

    // Supporting the above.
    void deleteAllForCertificateSoftDeletesOnlyThatCertificates();
    void deleteAllForCertificateWithNothingToClearSucceeds();

private:
    QSqlDatabase database() { return QSqlDatabase::database(m_connectionName); }

    static QTableWidget* tableOf(CertificateListWidget& widget)
    {
        return widget.findChild<QTableWidget*>(QStringLiteral("certificateTable"));
    }
    static QPushButton* renewButtonOf(CertificateListWidget& widget)
    {
        return widget.findChild<QPushButton*>(QStringLiteral("renewCertificateButton"));
    }

    // Runs `action` once a modal window that is not a QMessageBox is up.
    // A repeating poll rather than a singleShot: the edit dialog opens only
    // after the confirmation box's event loop has finished unwinding, and a
    // zero-timer can fire in the gap between the two, finding nothing.
    static void whenEditDialogAppears(const std::function<void(QDialog*)>& action)
    {
        auto  ticks = std::make_shared<int>(0);
        auto* timer = new QTimer;
        timer->setInterval(5);

        QObject::connect(timer, &QTimer::timeout, timer, [timer, ticks, action]() {
            if (++(*ticks) > 400) { // never spin forever
                timer->stop();
                timer->deleteLater();
                return;
            }

            QDialog* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (qobject_cast<QMessageBox*>(dialog) != nullptr) {
                return; // the confirmation box, not the edit dialog
            }
            if (dialog == nullptr) {
                // Fall back to a scan: activeModalWidget() depends on window
                // activation, which a test's unshown parent widget may never get.
                for (QWidget* widget : QApplication::topLevelWidgets()) {
                    if (widget->isVisible() && qobject_cast<QMessageBox*>(widget) == nullptr) {
                        if (auto* candidate = qobject_cast<QDialog*>(widget)) {
                            dialog = candidate;
                            break;
                        }
                    }
                }
            }
            if (dialog != nullptr) {
                action(dialog);
                timer->stop();
                timer->deleteLater();
            }
        });

        timer->start();
    }
    // After confirming, fills the renewal/edit dialog and closes it.
    static void answerConfirmThenDialog(QMessageBox::StandardButton confirmWith,
                                        const QDate&                newExpiry,
                                        bool                        acceptEdit);

    QString m_connectionName;
    QString m_vesselId = QStringLiteral("v1");
};

void TestCertificateRenewal::init()
{
    m_connectionName = QStringLiteral("renewal_%1").arg(QTest::currentTestFunction());
    QVERIFY(openDatabase(m_connectionName));
}

void TestCertificateRenewal::cleanup()
{
    closeDatabase(m_connectionName);
}

// Renewal shows two modal windows in sequence: the confirmation box, then the
// edit dialog. The box is caught with a zero-timer (it is already up by the
// time the click returns into its event loop); the dialog needs the poll,
// since it only exists once the box's loop has unwound.
void TestCertificateRenewal::answerConfirmThenDialog(QMessageBox::StandardButton confirmWith,
                                                     const QDate&                newExpiry,
                                                     bool                        acceptEdit)
{
    QTimer::singleShot(0, [confirmWith, newExpiry, acceptEdit]() {
        auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (box == nullptr) {
            return;
        }

        // Click the actual button rather than calling done(): QMessageBox's
        // static helpers report which button was clicked, and done() leaves
        // clickedButton() null, so question() would return NoButton and the
        // caller would treat a Yes as a decline.
        if (QAbstractButton* button = box->button(confirmWith)) {
            button->click();
        } else {
            box->done(confirmWith);
        }

        if (confirmWith != QMessageBox::Yes) {
            return; // the edit dialog never opens
        }

        // Cast to QDialog rather than CertificateEditDialog: accept() and
        // reject() are public slots on the base, but the override is
        // protected, so calling them through the derived type will not compile.
        whenEditDialogAppears([newExpiry, acceptEdit](QDialog* dialog) {
            if (auto* form = dialog->findChild<CertificateEditForm*>()) {
                Certificate certificate = form->certificate(); // keeps id and vessel
                certificate.expiryDate  = newExpiry;
                certificate.issueDate   = newExpiry.addYears(-5);
                form->setCertificate(certificate);
            }
            if (acceptEdit) {
                dialog->accept();
            } else {
                dialog->reject();
            }
        });
    });
}

void TestCertificateRenewal::successfulRenewalMovesDatesAndClearsSurveys()
{
    // §7 item 1.
    QSqlDatabase db    = database();
    const QDate  today = QDate::currentDate();
    seedCertificateWithSchedule(db, QStringLiteral("c1"), m_vesselId,
                                QStringLiteral("Safety Construction"), QStringLiteral("1"),
                                today.addYears(-5), today.addDays(20), /*requiresAnnual=*/true);
    seedEndorsement(db, QStringLiteral("e1"), QStringLiteral("c1"), QStringLiteral("ANNUAL"),
                    today.addYears(-4));
    seedEndorsement(db, QStringLiteral("e2"), QStringLiteral("c1"), QStringLiteral("ANNUAL"),
                    today.addYears(-3));

    const InstallationContext context = officeContext();
    CertificateRepository          certificates(db, context);
    EndorsementRepository          endorsements(db, context);
    CertificateExtensionRepository extensions(db, context);
    AppSettingRepository           appSettings(db);

    CertificateListWidget widget(certificates, endorsements, extensions, appSettings, context);
    widget.setVesselId(m_vesselId);
    tableOf(widget)->selectRow(0);

    const QDate newExpiry = today.addYears(5);
    answerConfirmThenDialog(QMessageBox::Yes, newExpiry, /*acceptEdit=*/true);
    renewButtonOf(widget)->click();

    std::optional<Certificate> stored;
    QVERIFY(certificates.findById(QStringLiteral("c1"), &stored));
    QVERIFY(stored.has_value());
    QCOMPARE(stored->expiryDate, newExpiry);

    QList<Endorsement> remaining;
    QVERIFY(endorsements.list(QStringLiteral("c1"), &remaining));
    QVERIFY2(remaining.isEmpty(), "renewal clears the completed cycle's surveys");
}

void TestCertificateRenewal::cancellingTheConfirmationChangesNothing()
{
    // §7 item 2: the edit dialog never even opens.
    QSqlDatabase db    = database();
    const QDate  today = QDate::currentDate();
    const QDate  originalExpiry = today.addDays(20);
    seedCertificateWithSchedule(db, QStringLiteral("c1"), m_vesselId, QStringLiteral("Load Line"),
                                QStringLiteral("1"), today.addYears(-5), originalExpiry, true);
    seedEndorsement(db, QStringLiteral("e1"), QStringLiteral("c1"), QStringLiteral("ANNUAL"),
                    today.addYears(-4));

    const InstallationContext context = officeContext();
    CertificateRepository          certificates(db, context);
    EndorsementRepository          endorsements(db, context);
    CertificateExtensionRepository extensions(db, context);
    AppSettingRepository           appSettings(db);

    CertificateListWidget widget(certificates, endorsements, extensions, appSettings, context);
    widget.setVesselId(m_vesselId);
    tableOf(widget)->selectRow(0);

    answerConfirmThenDialog(QMessageBox::No, today.addYears(5), /*acceptEdit=*/true);
    renewButtonOf(widget)->click();

    std::optional<Certificate> stored;
    QVERIFY(certificates.findById(QStringLiteral("c1"), &stored));
    QCOMPARE(stored->expiryDate, originalExpiry);

    QList<Endorsement> remaining;
    QVERIFY(endorsements.list(QStringLiteral("c1"), &remaining));
    QCOMPARE(remaining.size(), 1);
}

void TestCertificateRenewal::cancellingTheEditDialogChangesNothing()
{
    // §7 item 3: confirmed, but then backed out of the dialog itself.
    QSqlDatabase db             = database();
    const QDate  today          = QDate::currentDate();
    const QDate  originalExpiry = today.addDays(20);
    seedCertificateWithSchedule(db, QStringLiteral("c1"), m_vesselId, QStringLiteral("Load Line"),
                                QStringLiteral("1"), today.addYears(-5), originalExpiry, true);
    seedEndorsement(db, QStringLiteral("e1"), QStringLiteral("c1"), QStringLiteral("ANNUAL"),
                    today.addYears(-4));

    const InstallationContext context = officeContext();
    CertificateRepository          certificates(db, context);
    EndorsementRepository          endorsements(db, context);
    CertificateExtensionRepository extensions(db, context);
    AppSettingRepository           appSettings(db);

    CertificateListWidget widget(certificates, endorsements, extensions, appSettings, context);
    widget.setVesselId(m_vesselId);
    tableOf(widget)->selectRow(0);

    answerConfirmThenDialog(QMessageBox::Yes, today.addYears(5), /*acceptEdit=*/false);
    renewButtonOf(widget)->click();

    std::optional<Certificate> stored;
    QVERIFY(certificates.findById(QStringLiteral("c1"), &stored));
    QCOMPARE(stored->expiryDate, originalExpiry);

    QList<Endorsement> remaining;
    QVERIFY(endorsements.list(QStringLiteral("c1"), &remaining));
    QVERIFY2(remaining.size() == 1, "cancelling must not clear surveys");
}

void TestCertificateRenewal::renewIsDisabledWithNoSelection()
{
    // §7 item 4.
    QSqlDatabase db    = database();
    const QDate  today = QDate::currentDate();
    seedCertificateWithSchedule(db, QStringLiteral("c1"), m_vesselId, QStringLiteral("Load Line"),
                                QStringLiteral("1"), today.addYears(-5), today.addDays(20), true);

    const InstallationContext context = officeContext();
    CertificateRepository          certificates(db, context);
    EndorsementRepository          endorsements(db, context);
    CertificateExtensionRepository extensions(db, context);
    AppSettingRepository           appSettings(db);

    CertificateListWidget widget(certificates, endorsements, extensions, appSettings, context);
    widget.setVesselId(m_vesselId);

    QVERIFY2(!renewButtonOf(widget)->isEnabled(), "nothing selected, so nothing to renew");

    tableOf(widget)->selectRow(0);
    QVERIFY(renewButtonOf(widget)->isEnabled());

    // Clearing the selection puts it back.
    tableOf(widget)->clearSelection();
    tableOf(widget)->setCurrentCell(-1, -1);
    QVERIFY(!renewButtonOf(widget)->isEnabled());
}

void TestCertificateRenewal::renewIsDisabledForACertificateThatNeverExpires()
{
    // §7 item 5. Renewing a Tonnage Certificate corresponds to nothing real.
    QSqlDatabase db    = database();
    const QDate  today = QDate::currentDate();
    seedCertificateWithSchedule(db, QStringLiteral("c1"), m_vesselId, QStringLiteral("Tonnage"),
                                QStringLiteral("1"), today.addYears(-1), QDate(),
                                /*requiresAnnual=*/false);
    seedCertificateWithSchedule(db, QStringLiteral("c2"), m_vesselId, QStringLiteral("Load Line"),
                                QStringLiteral("2"), today.addYears(-5), today.addDays(20), true);

    const InstallationContext context = officeContext();
    CertificateRepository          certificates(db, context);
    EndorsementRepository          endorsements(db, context);
    CertificateExtensionRepository extensions(db, context);
    AppSettingRepository           appSettings(db);

    CertificateListWidget widget(certificates, endorsements, extensions, appSettings, context);
    widget.setVesselId(m_vesselId);

    // Row 0 is "1" (Tonnage, never expires); row 1 is "2" (Load Line).
    tableOf(widget)->selectRow(0);
    QVERIFY2(!renewButtonOf(widget)->isEnabled(), "a certificate with no expiry cannot renew");

    tableOf(widget)->selectRow(1);
    QVERIFY(renewButtonOf(widget)->isEnabled());
}

void TestCertificateRenewal::confirmationNamesTheSurveyCountOnlyWhenThereAreSome()
{
    // §7 item 6.
    QSqlDatabase db    = database();
    const QDate  today = QDate::currentDate();
    seedCertificateWithSchedule(db, QStringLiteral("c1"), m_vesselId, QStringLiteral("Load Line"),
                                QStringLiteral("1"), today.addYears(-5), today.addDays(20), true);

    const InstallationContext context = officeContext();
    CertificateRepository          certificates(db, context);
    EndorsementRepository          endorsements(db, context);
    CertificateExtensionRepository extensions(db, context);
    AppSettingRepository           appSettings(db);

    CertificateListWidget widget(certificates, endorsements, extensions, appSettings, context);
    widget.setVesselId(m_vesselId);
    tableOf(widget)->selectRow(0);

    // With no endorsements recorded, the question is the short form.
    QString captured;
    QTimer::singleShot(0, [&captured]() {
        if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
            captured = box->text();
            box->done(QMessageBox::No);
        }
    });
    renewButtonOf(widget)->click();

    QVERIFY(captured.contains(QStringLiteral("Load Line")));
    QVERIFY2(!captured.contains(QStringLiteral("recorded survey")),
             "no surveys recorded, so none are mentioned");

    // Record two, and the question names the count.
    seedEndorsement(db, QStringLiteral("e1"), QStringLiteral("c1"), QStringLiteral("ANNUAL"),
                    today.addYears(-4));
    seedEndorsement(db, QStringLiteral("e2"), QStringLiteral("c1"), QStringLiteral("ANNUAL"),
                    today.addYears(-3));

    captured.clear();
    QTimer::singleShot(0, [&captured]() {
        if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
            captured = box->text();
            box->done(QMessageBox::No);
        }
    });
    renewButtonOf(widget)->click();

    QVERIFY(captured.contains(QStringLiteral("2")));
    QVERIFY(captured.contains(QStringLiteral("recorded survey")));
}

void TestCertificateRenewal::renewalModeHidesTheEndorsementsSection()
{
    // §7 item 7: hidden even for a certificate that has recorded surveys.
    QSqlDatabase db    = database();
    const QDate  today = QDate::currentDate();
    seedCertificateWithSchedule(db, QStringLiteral("c1"), m_vesselId, QStringLiteral("Load Line"),
                                QStringLiteral("1"), today.addYears(-5), today.addDays(20), true);
    seedEndorsement(db, QStringLiteral("e1"), QStringLiteral("c1"), QStringLiteral("ANNUAL"),
                    today.addYears(-4));

    const InstallationContext context = officeContext();
    CertificateRepository     certificates(db, context);
    EndorsementRepository     endorsements(db, context);

    std::optional<Certificate> certificate;
    QVERIFY(certificates.findById(QStringLiteral("c1"), &certificate));
    QVERIFY(certificate.has_value());

    CertificateEditDialog renewal(certificates, endorsements, m_vesselId, certificate,
                                  /*isRenewal=*/true);
    QVERIFY2(renewal.findChild<QGroupBox*>(QStringLiteral("endorsementsSection")) == nullptr,
             "renewal mode never shows the endorsements section");
    QVERIFY(renewal.windowTitle().contains(QStringLiteral("Renew")));

    // The ordinary edit of the same certificate still shows it.
    CertificateEditDialog edit(certificates, endorsements, m_vesselId, certificate,
                               /*isRenewal=*/false);
    QVERIFY(edit.findChild<QGroupBox*>(QStringLiteral("endorsementsSection")) != nullptr);
}

void TestCertificateRenewal::renewedCertificateComesBackAsValid()
{
    // §7 item 8, end to end: new dates in, endorsements cleared, state
    // recomputed. Before renewal this certificate is Critical (20 days out);
    // after, it is Valid with a fresh five-year cycle.
    QSqlDatabase db    = database();
    const QDate  today = QDate::currentDate();
    seedCertificateWithSchedule(db, QStringLiteral("c1"), m_vesselId,
                                QStringLiteral("Safety Construction"), QStringLiteral("1"),
                                today.addYears(-5), today.addDays(20), /*requiresAnnual=*/true);
    seedEndorsement(db, QStringLiteral("e1"), QStringLiteral("c1"), QStringLiteral("ANNUAL"),
                    today.addYears(-4));

    const InstallationContext context = officeContext();
    CertificateRepository          certificates(db, context);
    EndorsementRepository          endorsements(db, context);
    CertificateExtensionRepository extensions(db, context);
    AppSettingRepository           appSettings(db);

    CertificateListWidget widget(certificates, endorsements, extensions, appSettings, context);
    widget.setVesselId(m_vesselId);

    QTableWidget* table = tableOf(widget);
    // Something is wrong to begin with. Precisely what is not the point here:
    // with one endorsement against a five-year term, the later anniversaries
    // are unsatisfied, so this reads Survey Overdue rather than Critical —
    // either way it is not Valid, which is what renewal has to change.
    QVERIFY(table->item(0, 2)->text() != StatusItem::labelFor(DisplayStatus::Valid));

    table->selectRow(0);
    answerConfirmThenDialog(QMessageBox::Yes, today.addYears(5), /*acceptEdit=*/true);
    renewButtonOf(widget)->click();

    // The list has reloaded and recomputed from the new dates.
    QCOMPARE(table->rowCount(), 1);
    QCOMPARE(table->item(0, 2)->text(), StatusItem::labelFor(DisplayStatus::Valid));

    QList<Endorsement> remaining;
    QVERIFY(endorsements.list(QStringLiteral("c1"), &remaining));
    QVERIFY(remaining.isEmpty());
}

void TestCertificateRenewal::doubleClickStillOpensTheOrdinaryEditDialog()
{
    // §7 item 9: the ordinary edit path is untouched by this step, and in
    // particular carries no survey-clearing side effect.
    QSqlDatabase db    = database();
    const QDate  today = QDate::currentDate();
    seedCertificateWithSchedule(db, QStringLiteral("c1"), m_vesselId, QStringLiteral("Load Line"),
                                QStringLiteral("1"), today.addYears(-5), today.addDays(20), true);
    seedEndorsement(db, QStringLiteral("e1"), QStringLiteral("c1"), QStringLiteral("ANNUAL"),
                    today.addYears(-4));

    const InstallationContext context = officeContext();
    CertificateRepository          certificates(db, context);
    EndorsementRepository          endorsements(db, context);
    CertificateExtensionRepository extensions(db, context);
    AppSettingRepository           appSettings(db);

    CertificateListWidget widget(certificates, endorsements, extensions, appSettings, context);
    widget.setVesselId(m_vesselId);

    QString capturedTitle;
    bool    hadEndorsementsSection = false;
    bool    wasCertificateDialog   = false;
    whenEditDialogAppears([&capturedTitle, &hadEndorsementsSection,
                           &wasCertificateDialog](QDialog* dialog) {
        wasCertificateDialog   = qobject_cast<CertificateEditDialog*>(dialog) != nullptr;
        capturedTitle          = dialog->windowTitle();
        hadEndorsementsSection =
            dialog->findChild<QGroupBox*>(QStringLiteral("endorsementsSection")) != nullptr;
        dialog->accept(); // an ordinary save
    });

    QTableWidget* table = tableOf(widget);
    QMetaObject::invokeMethod(table, "itemDoubleClicked",
                              Q_ARG(QTableWidgetItem*, table->item(0, 0)));

    QVERIFY2(wasCertificateDialog, "double-click opens the certificate edit dialog");
    QCOMPARE(capturedTitle, QStringLiteral("Edit Certificate"));
    QVERIFY2(hadEndorsementsSection, "the ordinary edit still shows recorded surveys");

    // ...and saving through it left the surveys alone.
    QList<Endorsement> remaining;
    QVERIFY(endorsements.list(QStringLiteral("c1"), &remaining));
    QCOMPARE(remaining.size(), 1);
}

void TestCertificateRenewal::deleteAllForCertificateSoftDeletesOnlyThatCertificates()
{
    QSqlDatabase db    = database();
    const QDate  today = QDate::currentDate();
    seedCertificateWithSchedule(db, QStringLiteral("c1"), m_vesselId, QStringLiteral("One"),
                                QStringLiteral("1"), today.addYears(-5), today.addDays(20), true);
    seedCertificateWithSchedule(db, QStringLiteral("c2"), m_vesselId, QStringLiteral("Two"),
                                QStringLiteral("2"), today.addYears(-5), today.addDays(20), true);
    seedEndorsement(db, QStringLiteral("e1"), QStringLiteral("c1"), QStringLiteral("ANNUAL"),
                    today.addYears(-4));
    seedEndorsement(db, QStringLiteral("e2"), QStringLiteral("c2"), QStringLiteral("ANNUAL"),
                    today.addYears(-4));

    const InstallationContext context = officeContext();
    EndorsementRepository     endorsements(db, context);

    QVERIFY(endorsements.deleteAllForCertificate(QStringLiteral("c1")));

    QList<Endorsement> forFirst;
    QVERIFY(endorsements.list(QStringLiteral("c1"), &forFirst));
    QVERIFY(forFirst.isEmpty());

    // The other certificate's surveys are untouched.
    QList<Endorsement> forSecond;
    QVERIFY(endorsements.list(QStringLiteral("c2"), &forSecond));
    QCOMPARE(forSecond.size(), 1);

    // Soft delete, never a hard DELETE: the row survives with is_deleted = 1.
    QSqlQuery query(db);
    QVERIFY(query.exec(QStringLiteral("SELECT is_deleted FROM endorsement WHERE id = 'e1'")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 1);
}

void TestCertificateRenewal::deleteAllForCertificateWithNothingToClearSucceeds()
{
    QSqlDatabase db    = database();
    const QDate  today = QDate::currentDate();
    seedCertificateWithSchedule(db, QStringLiteral("c1"), m_vesselId, QStringLiteral("One"),
                                QStringLiteral("1"), today.addYears(-5), today.addDays(20), true);

    const InstallationContext context = officeContext();
    EndorsementRepository     endorsements(db, context);

    QVERIFY2(endorsements.deleteAllForCertificate(QStringLiteral("c1")),
             "affecting zero rows is not a failure");
    QVERIFY(endorsements.errorString().isEmpty());
}

QTEST_MAIN(TestCertificateRenewal)
#include "tst_CertificateRenewal.moc"
