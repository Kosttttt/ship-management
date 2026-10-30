#include "CertificateTestSupport.h"

#include "app/AlertProvider.h"
#include "core/AppSettingRepository.h"
#include "core/VesselRepository.h"
#include "modules/certificates/CertificateAlertProvider.h"
#include "modules/certificates/data/CertificateExtensionRepository.h"
#include "modules/certificates/data/CertificateRepository.h"
#include "modules/certificates/data/EndorsementRepository.h"
#include "modules/certificates/domain/CertificateState.h"
#include "modules/certificates/ui/CertificateListWidget.h"
#include "modules/certificates/ui/StatusItem.h"

#include <QApplication>
#include <QDateEdit>
#include <QDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <QtTest>

#include <functional>
#include <memory>
#include <optional>

using namespace CertificateTestSupport;

// certificate-extension-spec §8, the nine edge cases.
class TestCertificateExtension : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void startingAnExtensionShowsItsDateWithTheShortTermSuffix();   // 1
    void startingASecondExtensionIsRefused();                       // 2
    void extensionButtonIsDisabledForACertificateThatNeverExpires(); // 3
    void resolvingRevertsTheListToTheCertificatesOwnExpiry();       // 4
    void anActiveExtensionSuppressesSurveyEvaluation();             // 5
    void renewIsDisabledWhileAnExtensionIsActive();                 // 6
    void aResolvedExtensionSurvivesAsSoftDeleted();                 // 7
    void vesselScopeRefusesAnotherVesselsCertificate();             // 8
    void alertProviderIgnoresAPausedSurveyWhileExtended();          // 9

    // Supporting the above.
    void findActiveForReturnsNothingWhenThereIsNone();
    void startRefusesAnExtensionWithNoExpiryDate();
    void resolveWithNothingActiveSucceeds();
    void omittingTheParameterLeavesTheCalculationUnchanged();

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
    static QPushButton* extensionButtonOf(CertificateListWidget& widget)
    {
        return widget.findChild<QPushButton*>(QStringLiteral("certificateExtensionButton"));
    }
    static QString cell(CertificateListWidget& widget, int row, int column)
    {
        QTableWidgetItem* item = tableOf(widget)->item(row, column);
        return item == nullptr ? QString() : item->text();
    }

    // Column indexes, matching the enum inside CertificateListWidget.cpp.
    enum Column { Status = 2, Expiry = 3, SurveyFrom = 4, SurveyTo = 5 };

    // Drives the extension dialog once it is up. A repeating poll rather than
    // a singleShot, and a scan as well as activeModalWidget(): a widget the
    // test never show()s may never be activated, so the modal child is not
    // always reported as the active one — the trap found in step 10.
    static void whenExtensionDialogAppears(const std::function<void(QDialog*)>& action)
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

            const QString wanted = QStringLiteral("certificateExtensionDialog");

            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (dialog == nullptr || dialog->objectName() != wanted) {
                dialog = nullptr;
                for (QWidget* widget : QApplication::topLevelWidgets()) {
                    if (widget->isVisible() && widget->objectName() == wanted) {
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

    // Fills the dialog's two fields and closes it. accept() and reject() are
    // public slots on QDialog, so the call goes through the base pointer and
    // still dispatches to the dialog's own override.
    static void fillExtensionDialog(const QDate& expiry, const QString& note, bool acceptIt)
    {
        whenExtensionDialogAppears([expiry, note, acceptIt](QDialog* dialog) {
            if (auto* dateEdit =
                    dialog->findChild<QDateEdit*>(QStringLiteral("extensionExpiryEdit"))) {
                dateEdit->setDate(expiry);
            }
            if (auto* noteEdit =
                    dialog->findChild<QLineEdit*>(QStringLiteral("extensionNoteEdit"))) {
                noteEdit->setText(note);
            }
            if (acceptIt) {
                dialog->accept();
            } else {
                dialog->reject();
            }
        });
    }

    // A certificate whose own schedule is genuinely in trouble: issued four
    // years ago with an annual survey and nothing recorded, so anniversaries
    // have passed unsatisfied.
    QString seedOverdueCertificate(const QString& id, const QString& vesselId, const QDate& today)
    {
        // A named local: database() hands back a QSqlDatabase by value, and
        // the seed helpers take a non-const reference.
        QSqlDatabase db = database();
        return seedCertificateWithSchedule(
            db, id, vesselId, QStringLiteral("Safety Construction"), QStringLiteral("1"),
            today.addYears(-4), today.addYears(1), /*requiresAnnual=*/true);
    }

    QString seedVessel(const QString& id, const QString& name)
    {
        QSqlQuery query(database());
        query.prepare(QStringLiteral(
            "INSERT INTO vessel (id, name, imo_number, created_at, created_by, updated_at,"
            " updated_by, is_deleted, origin_node, revision)"
            " VALUES (?, ?, '9074729', '2026-01-01T00:00:00Z', 'SEED', '2026-01-01T00:00:00Z',"
            " 'SEED', 0, 'OFFICE', 1)"));
        query.addBindValue(id);
        query.addBindValue(name);
        if (!query.exec()) {
            qWarning() << "vessel seed failed:" << query.lastError().text();
            return QString();
        }
        return id;
    }

    // Starts one through the repository, which is also how the UI does it.
    bool startExtension(CertificateExtensionRepository& repository,
                        const QString&                  certificateId,
                        const QDate&                    expiry,
                        const QString&                  note = QString())
    {
        CertificateExtension extension;
        extension.certificateId = certificateId;
        extension.expiryDate    = expiry;
        extension.note          = note;
        return repository.start(extension);
    }

    static QString shortTermLabel(const QDate& date)
    {
        return QStringLiteral("%1 — Short Term").arg(date.toString(QStringLiteral("dd MMM yyyy")));
    }

    QString m_connectionName;
    QString m_vesselId = QStringLiteral("v1");
};

void TestCertificateExtension::init()
{
    m_connectionName = QStringLiteral("extension_%1").arg(QTest::currentTestFunction());
    QVERIFY(openDatabase(m_connectionName));
}

void TestCertificateExtension::cleanup()
{
    closeDatabase(m_connectionName);
}

// §8 case 1.
void TestCertificateExtension::startingAnExtensionShowsItsDateWithTheShortTermSuffix()
{
    QSqlDatabase db    = database();
    const QDate  today = QDate::currentDate();
    seedCertificateWithSchedule(db, QStringLiteral("c1"), m_vesselId, QStringLiteral("Load Line"),
                                QStringLiteral("1"), today.addYears(-1), today.addYears(4),
                                /*requiresAnnual=*/false);

    const InstallationContext      context = officeContext();
    CertificateRepository          certificates(db, context);
    EndorsementRepository          endorsements(db, context);
    CertificateExtensionRepository extensions(db, context);
    AppSettingRepository           appSettings(db);

    AttachmentFixture attachments(db, context);
    CertificateListWidget widget(certificates, endorsements, extensions, attachments.service,
                                 appSettings, context);
    widget.setVesselId(m_vesselId);
    tableOf(widget)->selectRow(0);

    const QDate extensionExpiry = today.addMonths(2);
    fillExtensionDialog(extensionExpiry, QStringLiteral("Awaiting class attendance"),
                        /*acceptIt=*/true);
    extensionButtonOf(widget)->click();

    QCOMPARE(cell(widget, 0, Expiry), shortTermLabel(extensionExpiry));

    // The extension really was recorded, not just rendered.
    std::optional<CertificateExtension> stored;
    QVERIFY(extensions.findActiveFor(QStringLiteral("c1"), &stored));
    QVERIFY(stored.has_value());
    QCOMPARE(stored->expiryDate, extensionExpiry);
    QCOMPARE(stored->note, QStringLiteral("Awaiting class attendance"));
}

// §8 case 2.
void TestCertificateExtension::startingASecondExtensionIsRefused()
{
    QSqlDatabase db    = database();
    const QDate  today = QDate::currentDate();
    seedCertificateWithSchedule(db, QStringLiteral("c1"), m_vesselId, QStringLiteral("Load Line"),
                                QStringLiteral("1"), today.addYears(-1), today.addYears(4), false);

    const InstallationContext      context = officeContext();
    CertificateExtensionRepository extensions(db, context);

    QVERIFY(startExtension(extensions, QStringLiteral("c1"), today.addMonths(2)));

    QVERIFY2(!startExtension(extensions, QStringLiteral("c1"), today.addMonths(3)),
             "a second active extension must be refused, not stacked");
    QVERIFY2(extensions.errorString().contains(QStringLiteral("already has an active short-term")),
             qPrintable(extensions.errorString()));

    // Nothing changed: still exactly one row, still the original date.
    QCOMPARE(rowCountOf(db, QStringLiteral("certificate_extension")), 1);
    std::optional<CertificateExtension> stored;
    QVERIFY(extensions.findActiveFor(QStringLiteral("c1"), &stored));
    QVERIFY(stored.has_value());
    QCOMPARE(stored->expiryDate, today.addMonths(2));
}

// §8 case 3.
void TestCertificateExtension::extensionButtonIsDisabledForACertificateThatNeverExpires()
{
    QSqlDatabase db    = database();
    const QDate  today = QDate::currentDate();
    // An invalid expiry seeds SQL NULL — "does not expire".
    seedCertificateWithSchedule(db, QStringLiteral("c1"), m_vesselId, QStringLiteral("Tonnage"),
                                QStringLiteral("1"), today.addYears(-1), QDate(), false);

    const InstallationContext      context = officeContext();
    CertificateRepository          certificates(db, context);
    EndorsementRepository          endorsements(db, context);
    CertificateExtensionRepository extensions(db, context);
    AppSettingRepository           appSettings(db);

    AttachmentFixture attachments(db, context);
    CertificateListWidget widget(certificates, endorsements, extensions, attachments.service,
                                 appSettings, context);
    widget.setVesselId(m_vesselId);

    QVERIFY2(!extensionButtonOf(widget)->isEnabled(), "disabled with nothing selected");

    tableOf(widget)->selectRow(0);
    QVERIFY2(!extensionButtonOf(widget)->isEnabled(),
             "a certificate that never expires cannot be extended either");
}

// §8 case 4.
void TestCertificateExtension::resolvingRevertsTheListToTheCertificatesOwnExpiry()
{
    QSqlDatabase db    = database();
    const QDate  today = QDate::currentDate();
    const QDate  ownExpiry = today.addYears(4);
    seedCertificateWithSchedule(db, QStringLiteral("c1"), m_vesselId, QStringLiteral("Load Line"),
                                QStringLiteral("1"), today.addYears(-1), ownExpiry, false);

    const InstallationContext      context = officeContext();
    CertificateRepository          certificates(db, context);
    EndorsementRepository          endorsements(db, context);
    CertificateExtensionRepository extensions(db, context);
    AppSettingRepository           appSettings(db);

    QVERIFY(startExtension(extensions, QStringLiteral("c1"), today.addMonths(2)));

    AttachmentFixture attachments(db, context);
    CertificateListWidget widget(certificates, endorsements, extensions, attachments.service,
                                 appSettings, context);
    widget.setVesselId(m_vesselId);
    tableOf(widget)->selectRow(0);

    QCOMPARE(cell(widget, 0, Expiry), shortTermLabel(today.addMonths(2)));
    QCOMPARE(extensionButtonOf(widget)->text(), QStringLiteral("Short-Term Active — &Resolve"));

    // Resolving takes no dialog at all, so this is a plain click.
    extensionButtonOf(widget)->click();

    QCOMPARE(cell(widget, 0, Expiry), ownExpiry.toString(QStringLiteral("dd MMM yyyy")));
    QCOMPARE(cell(widget, 0, Status), StatusItem::labelFor(DisplayStatus::Valid));
    QCOMPARE(extensionButtonOf(widget)->text(), QStringLiteral("Start Short-Term &Extension…"));
}

// §8 case 5: suppression is real, not cosmetic.
void TestCertificateExtension::anActiveExtensionSuppressesSurveyEvaluation()
{
    // Built by hand rather than seeded: this is the pure function, and fixed
    // dates make the expectation exact.
    Certificate certificate;
    certificate.id                   = QStringLiteral("c1");
    certificate.vesselId             = m_vesselId;
    certificate.name                 = QStringLiteral("Safety Construction");
    certificate.category             = CertificateCategory::Statutory;
    certificate.issueDate            = QDate(2022, 6, 1);
    certificate.expiryDate           = QDate(2027, 6, 1);
    certificate.requiresAnnualSurvey = true;

    const QDate           today = QDate(2026, 6, 1);
    const AlertThresholds thresholds;

    // Nothing recorded against four passed anniversaries.
    const CertificateState bare =
        computeCertificateState(certificate, {}, thresholds, today);
    QCOMPARE(bare.survey, SurveySeverity::Overdue);
    QCOMPARE(bare.display, DisplayStatus::SurveyOverdue);

    const QDate            extensionExpiry = QDate(2026, 12, 1);
    const CertificateState extended =
        computeCertificateState(certificate, {}, thresholds, today, extensionExpiry);

    QCOMPARE(extended.survey, SurveySeverity::NotRequired);
    QCOMPARE(extended.display, DisplayStatus::Valid);
    QVERIFY(extended.isValid);
    QCOMPARE(extended.daysLeft, today.daysTo(extensionExpiry));
    QCOMPARE(extended.activeExtensionExpiry, std::optional<QDate>(extensionExpiry));

    // No window is reported, so the list's two survey cells stay blank.
    QVERIFY(!extended.windowOpens.isValid());
    QVERIFY(!extended.windowCloses.isValid());
    QVERIFY2(extended.reason.contains(QStringLiteral("Short-term extension")),
             qPrintable(extended.reason));

    // And the extension's own date still drives severity through the usual
    // thresholds: inside 30 days it is Critical, past it, Expired.
    const CertificateState critical = computeCertificateState(
        certificate, {}, thresholds, extensionExpiry.addDays(-10), extensionExpiry);
    QCOMPARE(critical.display, DisplayStatus::Critical);

    const CertificateState expired = computeCertificateState(
        certificate, {}, thresholds, extensionExpiry.addDays(1), extensionExpiry);
    QCOMPARE(expired.display, DisplayStatus::Expired);
    QVERIFY(!expired.isValid);
}

// §8 case 6.
void TestCertificateExtension::renewIsDisabledWhileAnExtensionIsActive()
{
    QSqlDatabase db    = database();
    const QDate  today = QDate::currentDate();
    seedCertificateWithSchedule(db, QStringLiteral("c1"), m_vesselId, QStringLiteral("Load Line"),
                                QStringLiteral("1"), today.addYears(-1), today.addYears(4), false);

    const InstallationContext      context = officeContext();
    CertificateRepository          certificates(db, context);
    EndorsementRepository          endorsements(db, context);
    CertificateExtensionRepository extensions(db, context);
    AppSettingRepository           appSettings(db);

    AttachmentFixture attachments(db, context);
    CertificateListWidget widget(certificates, endorsements, extensions, attachments.service,
                                 appSettings, context);
    widget.setVesselId(m_vesselId);
    tableOf(widget)->selectRow(0);

    QVERIFY2(renewButtonOf(widget)->isEnabled(), "an ordinary certificate can be renewed");

    fillExtensionDialog(today.addMonths(2), QString(), /*acceptIt=*/true);
    extensionButtonOf(widget)->click();

    QVERIFY2(!renewButtonOf(widget)->isEnabled(),
             "renewal starts a fresh cycle, which does not fit while an extension is in force");

    // Resolving puts Renew back.
    extensionButtonOf(widget)->click();
    QVERIFY2(renewButtonOf(widget)->isEnabled(), "re-enabled once the extension is resolved");
}

// §8 case 7.
void TestCertificateExtension::aResolvedExtensionSurvivesAsSoftDeleted()
{
    QSqlDatabase db    = database();
    const QDate  today = QDate::currentDate();
    seedCertificateWithSchedule(db, QStringLiteral("c1"), m_vesselId, QStringLiteral("Load Line"),
                                QStringLiteral("1"), today.addYears(-1), today.addYears(4), false);

    const InstallationContext      context = officeContext();
    CertificateExtensionRepository extensions(db, context);

    QVERIFY(startExtension(extensions, QStringLiteral("c1"), today.addMonths(2),
                           QStringLiteral("Issued by flag")));
    QVERIFY(extensions.resolve(QStringLiteral("c1")));

    // Gone as far as the application is concerned...
    std::optional<CertificateExtension> active;
    QVERIFY(extensions.findActiveFor(QStringLiteral("c1"), &active));
    QVERIFY2(!active.has_value(), "a resolved extension is no longer the active one");

    // ...but the row, and the note on it, are still there (§2, §9).
    QSqlQuery query(db);
    QVERIFY(query.exec(QStringLiteral(
        "SELECT is_deleted, revision, note FROM certificate_extension WHERE certificate_id = 'c1'")));
    QVERIFY2(query.next(), "the row must survive a resolve, not be deleted");
    QCOMPARE(query.value(0).toInt(), 1);
    QCOMPARE(query.value(1).toInt(), 2); // revision bumped
    QCOMPARE(query.value(2).toString(), QStringLiteral("Issued by flag"));
    QVERIFY2(!query.next(), "exactly one row");
}

// §8 case 8.
void TestCertificateExtension::vesselScopeRefusesAnotherVesselsCertificate()
{
    QSqlDatabase db    = database();
    const QDate  today = QDate::currentDate();
    // The certificate belongs to v2; this installation is v1.
    seedCertificateWithSchedule(db, QStringLiteral("c2"), QStringLiteral("v2"),
                                QStringLiteral("Someone else's"), QStringLiteral("1"),
                                today.addYears(-1), today.addYears(4), false);

    const InstallationContext      context = vesselContext(m_vesselId);
    CertificateExtensionRepository extensions(db, context);

    QVERIFY2(!startExtension(extensions, QStringLiteral("c2"), today.addMonths(2)),
             "starting an extension on another vessel's certificate must be refused");
    QVERIFY2(extensions.errorString().contains(QStringLiteral("own vessel")),
             qPrintable(extensions.errorString()));

    QVERIFY2(!extensions.resolve(QStringLiteral("c2")),
             "resolving another vessel's extension must be refused too");

    QCOMPARE(rowCountOf(db, QStringLiteral("certificate_extension")), 0);

    // Reading is the "not found is a successful answer" case, not an error.
    std::optional<CertificateExtension> found;
    QVERIFY(extensions.findActiveFor(QStringLiteral("c2"), &found));
    QVERIFY(!found.has_value());
}

// §8 case 9: the second call site named in §3.
void TestCertificateExtension::alertProviderIgnoresAPausedSurveyWhileExtended()
{
    QSqlDatabase db    = database();
    const QDate  today = QDate::currentDate();
    seedVessel(m_vesselId, QStringLiteral("MV Aurora"));
    seedOverdueCertificate(QStringLiteral("c1"), m_vesselId, today);

    const InstallationContext      context = officeContext();
    VesselRepository               vessels(db, context);
    CertificateRepository          certificates(db, context);
    EndorsementRepository          endorsements(db, context);
    CertificateExtensionRepository extensions(db, context);
    AppSettingRepository           appSettings(db);

    CertificateAlertProvider provider(vessels, certificates, endorsements, extensions, appSettings,
                                      context);

    const QList<VesselAttentionCount> before = provider.attentionByVessel();
    QCOMPARE(before.size(), 1);
    QCOMPARE(before.first().count, 1);

    QVERIFY(startExtension(extensions, QStringLiteral("c1"), today.addMonths(6)));

    // Had the provider been left on the old call signature, this would still
    // report 1 and the badge would contradict the list.
    const QList<VesselAttentionCount> during = provider.attentionByVessel();
    QVERIFY2(during.isEmpty(),
             "a paused survey schedule must not keep feeding the badge or the banner");

    QVERIFY(extensions.resolve(QStringLiteral("c1")));
    const QList<VesselAttentionCount> after = provider.attentionByVessel();
    QCOMPARE(after.size(), 1);
    QCOMPARE(after.first().count, 1);
}

void TestCertificateExtension::findActiveForReturnsNothingWhenThereIsNone()
{
    QSqlDatabase db    = database();
    const QDate  today = QDate::currentDate();
    seedCertificateWithSchedule(db, QStringLiteral("c1"), m_vesselId, QStringLiteral("Load Line"),
                                QStringLiteral("1"), today.addYears(-1), today.addYears(4), false);

    const InstallationContext      context = officeContext();
    CertificateExtensionRepository extensions(db, context);

    std::optional<CertificateExtension> found;
    QVERIFY2(extensions.findActiveFor(QStringLiteral("c1"), &found),
             "nothing recorded is a successful answer, not a failure");
    QVERIFY(!found.has_value());

    // An empty id is asked constantly by a list with no selection.
    QVERIFY(extensions.findActiveFor(QString(), &found));
    QVERIFY(!found.has_value());
}

void TestCertificateExtension::startRefusesAnExtensionWithNoExpiryDate()
{
    QSqlDatabase db    = database();
    const QDate  today = QDate::currentDate();
    seedCertificateWithSchedule(db, QStringLiteral("c1"), m_vesselId, QStringLiteral("Load Line"),
                                QStringLiteral("1"), today.addYears(-1), today.addYears(4), false);

    const InstallationContext      context = officeContext();
    CertificateExtensionRepository extensions(db, context);

    QVERIFY2(!startExtension(extensions, QStringLiteral("c1"), QDate()),
             "expiry_date is NOT NULL and a short-term certificate without one is meaningless");
    QVERIFY2(extensions.errorString().contains(QStringLiteral("needs its own expiry date")),
             qPrintable(extensions.errorString()));
    QCOMPARE(rowCountOf(db, QStringLiteral("certificate_extension")), 0);

    // And a certificate that is not there at all.
    QVERIFY(!startExtension(extensions, QStringLiteral("nope"), today.addMonths(2)));
    QVERIFY2(extensions.errorString().contains(QStringLiteral("no longer exists")),
             qPrintable(extensions.errorString()));
}

void TestCertificateExtension::resolveWithNothingActiveSucceeds()
{
    QSqlDatabase db    = database();
    const QDate  today = QDate::currentDate();
    seedCertificateWithSchedule(db, QStringLiteral("c1"), m_vesselId, QStringLiteral("Load Line"),
                                QStringLiteral("1"), today.addYears(-1), today.addYears(4), false);

    const InstallationContext      context = officeContext();
    CertificateExtensionRepository extensions(db, context);

    QVERIFY2(extensions.resolve(QStringLiteral("c1")),
             "affecting zero rows is success: there was simply nothing to resolve");
}

// §6's closing requirement: omitting the new parameter must change nothing.
void TestCertificateExtension::omittingTheParameterLeavesTheCalculationUnchanged()
{
    Certificate certificate;
    certificate.id                   = QStringLiteral("c1");
    certificate.name                 = QStringLiteral("Safety Equipment");
    certificate.category             = CertificateCategory::Statutory;
    certificate.issueDate            = QDate(2022, 6, 1);
    certificate.expiryDate           = QDate(2027, 6, 1);
    certificate.requiresAnnualSurvey = true;

    const QDate           today = QDate(2026, 6, 1);
    const AlertThresholds thresholds;

    const CertificateState omitted = computeCertificateState(certificate, {}, thresholds, today);
    const CertificateState explicitNullopt =
        computeCertificateState(certificate, {}, thresholds, today, std::nullopt);

    QCOMPARE(omitted.display, explicitNullopt.display);
    QCOMPARE(omitted.survey, explicitNullopt.survey);
    QCOMPARE(omitted.daysLeft, explicitNullopt.daysLeft);
    QCOMPARE(omitted.reason, explicitNullopt.reason);
    QVERIFY(!omitted.activeExtensionExpiry.has_value());
}

QTEST_MAIN(TestCertificateExtension)
#include "tst_CertificateExtension.moc"
