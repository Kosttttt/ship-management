#include "CertificateTestSupport.h"

#include "modules/certificates/data/CertificateRepository.h"
#include "modules/certificates/data/EndorsementRepository.h"
#include "modules/certificates/domain/CertificateState.h"
#include "modules/certificates/ui/CertificateEditDialog.h"
#include "modules/certificates/ui/EndorsementEditForm.h"
#include "modules/certificates/ui/EndorsementSection.h"

#include <QComboBox>
#include <QDateEdit>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QtTest>

#include <optional>

using namespace CertificateTestSupport;

// certificate-attachment-addendum-2-spec §6, and §9 cases 8 and 9: an
// endorsement can be corrected in place and it can be removed.
class TestEndorsementEditDelete : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    // §9 case 8 — the repository.
    void updatingChangesTheRowInPlaceWithNoSecondRow();
    void updatingRefusesADateBeforeTheCertificateWasIssued();
    void updatingCannotMoveAnEndorsementToAnotherCertificate();
    void updatingRefusesAnEndorsementThatIsNoLongerThere();

    // §9 case 9 — the repository.
    void deletingHidesOneRowAndLeavesTheOthersAlone();
    void deletingChangesWhatTheStateCalculationSees();
    void deletingIsASoftDeleteSoTheRowSurvives();
    void vesselScopeRefusesAnotherVesselsEndorsement();

    // §6 — the form and the section.
    void theFormCanBePreFilledAndHandsBackTheSameRow();
    void aStoredTypeTheCertificateNoLongerRequiresIsStillOffered();
    void theSectionOffersEditAndDeleteOnlyWithASelection();
    void theSectionFollowsADeletedEndorsement();

private:
    QSqlDatabase database() { return QSqlDatabase::database(m_connectionName); }

    static int rowsFor(QSqlDatabase& db, const QString& certificateId, bool deleted)
    {
        QSqlQuery query(db);
        query.prepare(QStringLiteral("SELECT COUNT(*) FROM endorsement"
                                     " WHERE certificate_id = ? AND is_deleted = ?"));
        query.addBindValue(certificateId);
        query.addBindValue(deleted ? 1 : 0);
        if (!query.exec() || !query.next()) {
            return -1;
        }
        return query.value(0).toInt();
    }

    // A certificate requiring both kinds of survey, so the section's combo has
    // something to choose between.
    Certificate seedOne(const QString& id       = QStringLiteral("c1"),
                        const QString& vesselId = QStringLiteral("v1"))
    {
        QSqlDatabase db = database();
        seedCertificateWithSchedule(db, id, vesselId, QStringLiteral("Load Line"),
                                    QStringLiteral("15D"), QDate(2026, 2, 4), QDate(2031, 2, 4),
                                    /*requiresAnnual=*/true, /*requiresIntermediate=*/true);

        CertificateRepository      certificates(db, officeContext());
        std::optional<Certificate> loaded;
        certificates.findById(id, &loaded);
        return loaded.value_or(Certificate{});
    }

    QString m_connectionName;
};

void TestEndorsementEditDelete::init()
{
    m_connectionName = QStringLiteral("endedit_%1").arg(QTest::currentTestFunction());
    QVERIFY(openDatabase(m_connectionName));
}

void TestEndorsementEditDelete::cleanup()
{
    closeDatabase(m_connectionName);
}

// §9 case 8: an UPDATE, not an INSERT. An endorsement records something that
// happened once; a second row would claim a second survey.
void TestEndorsementEditDelete::updatingChangesTheRowInPlaceWithNoSecondRow()
{
    QSqlDatabase db = database();
    seedOne();

    EndorsementRepository endorsements(db, officeContext());

    Endorsement original;
    original.certificateId   = QStringLiteral("c1");
    original.surveyType      = SurveyType::Annual;
    original.endorsementDate = QDate(2027, 2, 4);
    original.place           = QStringLiteral("Limasol");
    original.surveyor        = QStringLiteral("Lloyds Register");

    QString id;
    QVERIFY2(endorsements.create(original, &id), qPrintable(endorsements.errorString()));
    QCOMPARE(rowsFor(db, QStringLiteral("c1"), false), 1);

    std::optional<Endorsement> loaded;
    QVERIFY(endorsements.findById(id, &loaded));
    QVERIFY(loaded.has_value());
    QCOMPARE(loaded->place, QStringLiteral("Limasol"));

    // Remembered so the update below can be shown not to have touched it.
    QSqlQuery createdQuery(db);
    QVERIFY(createdQuery.exec(QStringLiteral("SELECT created_at FROM endorsement")));
    QVERIFY(createdQuery.next());
    const QString createdAt = createdQuery.value(0).toString();

    // The correction a typo actually needs: the place, and the date too.
    Endorsement corrected      = *loaded;
    corrected.place            = QStringLiteral("Limassol");
    corrected.endorsementDate  = QDate(2027, 2, 11);
    corrected.result           = QStringLiteral("Satisfactory");
    QVERIFY2(endorsements.update(corrected), qPrintable(endorsements.errorString()));

    // One row still, and it is the same row.
    QCOMPARE(rowsFor(db, QStringLiteral("c1"), false), 1);
    QCOMPARE(rowCountOf(db, QStringLiteral("endorsement")), 1);

    QVERIFY(endorsements.findById(id, &loaded));
    QVERIFY(loaded.has_value());
    QCOMPARE(loaded->id, id);
    QCOMPARE(loaded->place, QStringLiteral("Limassol"));
    QCOMPARE(loaded->endorsementDate, QDate(2027, 2, 11));
    QCOMPARE(loaded->result, QStringLiteral("Satisfactory"));

    // And the revision moved on, while created_at and created_by were left
    // alone (CLAUDE.md §6.5). Not asserted by comparing created_at to
    // updated_at: both are ISO-8601 to the second, and a test writes and
    // corrects a row inside the same second.
    QSqlQuery query(db);
    QVERIFY(query.exec(QStringLiteral("SELECT revision, created_at, created_by, updated_by"
                                      " FROM endorsement")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 2);
    QCOMPARE(query.value(1).toString(), createdAt);
    QCOMPARE(query.value(2).toString(), QStringLiteral("SYSTEM"));
    QCOMPARE(query.value(3).toString(), QStringLiteral("SYSTEM"));
}

// A correction can be wrong in all the same ways an entry can, so it goes
// through the same validation (certificate-control-spec §4.8 item 5).
void TestEndorsementEditDelete::updatingRefusesADateBeforeTheCertificateWasIssued()
{
    QSqlDatabase db = database();
    seedOne(); // issued 2026-02-04

    EndorsementRepository endorsements(db, officeContext());

    Endorsement endorsement;
    endorsement.certificateId   = QStringLiteral("c1");
    endorsement.surveyType      = SurveyType::Annual;
    endorsement.endorsementDate = QDate(2027, 2, 4);

    QString id;
    QVERIFY(endorsements.create(endorsement, &id));

    Endorsement impossible      = endorsement;
    impossible.id               = id;
    impossible.endorsementDate  = QDate(2025, 1, 1);
    QVERIFY2(!endorsements.update(impossible),
             "a survey cannot have happened before the certificate existed");
    QVERIFY(!endorsements.errorString().isEmpty());

    // Nothing was changed, so it can be corrected and tried again.
    std::optional<Endorsement> loaded;
    QVERIFY(endorsements.findById(id, &loaded));
    QCOMPARE(loaded->endorsementDate, QDate(2027, 2, 4));

    // An unset type is refused for the same reason.
    Endorsement typeless = *loaded;
    typeless.surveyType  = SurveyType::Unset;
    QVERIFY(!endorsements.update(typeless));
}

// The row's own parent is what scope and the issue-date check are measured
// against, so a struct arriving with somebody else's certificate id cannot
// move the endorsement there.
void TestEndorsementEditDelete::updatingCannotMoveAnEndorsementToAnotherCertificate()
{
    QSqlDatabase db = database();
    seedOne(QStringLiteral("c1"));
    seedOne(QStringLiteral("c2"));

    EndorsementRepository endorsements(db, officeContext());

    Endorsement endorsement;
    endorsement.certificateId   = QStringLiteral("c1");
    endorsement.surveyType      = SurveyType::Annual;
    endorsement.endorsementDate = QDate(2027, 2, 4);

    QString id;
    QVERIFY(endorsements.create(endorsement, &id));

    Endorsement moved      = endorsement;
    moved.id               = id;
    moved.certificateId    = QStringLiteral("c2");
    moved.place            = QStringLiteral("Somewhere");
    QVERIFY2(endorsements.update(moved), qPrintable(endorsements.errorString()));

    // The field it was allowed to change changed; the parent did not.
    QList<Endorsement> onFirst;
    QList<Endorsement> onSecond;
    QVERIFY(endorsements.list(QStringLiteral("c1"), &onFirst));
    QVERIFY(endorsements.list(QStringLiteral("c2"), &onSecond));
    QCOMPARE(onFirst.size(), 1);
    QCOMPARE(onFirst.first().place, QStringLiteral("Somewhere"));
    QCOMPARE(onSecond.size(), 0);
}

void TestEndorsementEditDelete::updatingRefusesAnEndorsementThatIsNoLongerThere()
{
    QSqlDatabase db = database();
    seedOne();

    EndorsementRepository endorsements(db, officeContext());

    Endorsement ghost;
    ghost.id               = QStringLiteral("no-such-endorsement");
    ghost.certificateId    = QStringLiteral("c1");
    ghost.surveyType       = SurveyType::Annual;
    ghost.endorsementDate  = QDate(2027, 2, 4);

    QVERIFY(!endorsements.update(ghost));
    QVERIFY(!endorsements.errorString().isEmpty());

    // No id at all is refused with its own message rather than becoming an
    // UPDATE with an empty WHERE.
    ghost.id = QString();
    QVERIFY(!endorsements.update(ghost));
    QCOMPARE(rowCountOf(db, QStringLiteral("endorsement")), 0);
}

// §9 case 9: one endorsement goes, the others on the same certificate do not.
void TestEndorsementEditDelete::deletingHidesOneRowAndLeavesTheOthersAlone()
{
    QSqlDatabase db = database();
    seedOne();

    EndorsementRepository endorsements(db, officeContext());

    seedEndorsement(db, QStringLiteral("e1"), QStringLiteral("c1"), QStringLiteral("ANNUAL"),
                    QDate(2027, 2, 4));
    seedEndorsement(db, QStringLiteral("e2"), QStringLiteral("c1"), QStringLiteral("ANNUAL"),
                    QDate(2028, 2, 4));
    seedEndorsement(db, QStringLiteral("e3"), QStringLiteral("c1"),
                    QStringLiteral("INTERMEDIATE"), QDate(2028, 6, 4));

    QVERIFY2(endorsements.softDelete(QStringLiteral("e2")),
             qPrintable(endorsements.errorString()));

    QList<Endorsement> remaining;
    QVERIFY(endorsements.list(QStringLiteral("c1"), &remaining));
    QCOMPARE(remaining.size(), 2);

    QStringList ids;
    for (const Endorsement& endorsement : remaining) {
        ids.append(endorsement.id);
    }
    QVERIFY(ids.contains(QStringLiteral("e1")));
    QVERIFY(ids.contains(QStringLiteral("e3")));
    QVERIFY2(!ids.contains(QStringLiteral("e2")), "only the one asked for may disappear");
}

// §6: and it disappears from the anniversary matching too, which is the part
// that changes what the user sees on the list.
void TestEndorsementEditDelete::deletingChangesWhatTheStateCalculationSees()
{
    QSqlDatabase db = database();
    const Certificate certificate = seedOne();

    EndorsementRepository endorsements(db, officeContext());

    // The first annual, signed off on its anniversary. The window for it runs
    // from three months before to three months after.
    seedEndorsement(db, QStringLiteral("e1"), QStringLiteral("c1"), QStringLiteral("ANNUAL"),
                    QDate(2027, 2, 4));

    const AlertThresholds thresholds;
    const QDate           today = QDate(2027, 3, 1); // inside that window

    QList<Endorsement> before;
    QVERIFY(endorsements.list(QStringLiteral("c1"), &before));
    QCOMPARE(before.size(), 1);

    const CertificateState satisfied =
        computeCertificateState(certificate, before, thresholds, today);
    QCOMPARE(satisfied.nextAnniversary, QDate(2028, 2, 4));

    QVERIFY(endorsements.softDelete(QStringLiteral("e1")));

    QList<Endorsement> after;
    QVERIFY(endorsements.list(QStringLiteral("c1"), &after));
    QVERIFY(after.isEmpty());

    // The anniversary it satisfied is outstanding again, which is exactly the
    // behaviour every other soft delete in the app has.
    const CertificateState outstanding =
        computeCertificateState(certificate, after, thresholds, today);
    QCOMPARE(outstanding.nextAnniversary, QDate(2027, 2, 4));
    QCOMPARE(outstanding.survey, SurveySeverity::InWindow);
}

void TestEndorsementEditDelete::deletingIsASoftDeleteSoTheRowSurvives()
{
    QSqlDatabase db = database();
    seedOne();

    EndorsementRepository endorsements(db, officeContext());
    seedEndorsement(db, QStringLiteral("e1"), QStringLiteral("c1"), QStringLiteral("ANNUAL"),
                    QDate(2027, 2, 4));

    QVERIFY(endorsements.softDelete(QStringLiteral("e1")));

    // CLAUDE.md §6.4: never a hard DELETE. The deletion has to replicate.
    QCOMPARE(rowCountOf(db, QStringLiteral("endorsement")), 1);
    QCOMPARE(rowsFor(db, QStringLiteral("c1"), true), 1);

    QSqlQuery query(db);
    QVERIFY(query.exec(QStringLiteral("SELECT revision FROM endorsement WHERE id = 'e1'")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 2);

    // Asked twice, as a second click on a stale table would: refused with a
    // message rather than silently affecting nothing.
    QVERIFY(!endorsements.softDelete(QStringLiteral("e1")));
    QVERIFY(!endorsements.errorString().isEmpty());
}

void TestEndorsementEditDelete::vesselScopeRefusesAnotherVesselsEndorsement()
{
    QSqlDatabase db = database();
    seedOne(QStringLiteral("c1"), QStringLiteral("v2")); // somebody else's vessel
    seedEndorsement(db, QStringLiteral("e1"), QStringLiteral("c1"), QStringLiteral("ANNUAL"),
                    QDate(2027, 2, 4));

    // An endorsement has no vessel of its own, so the filter has to go through
    // the certificate (CLAUDE.md §3).
    EndorsementRepository endorsements(db, vesselContext(QStringLiteral("v1")));

    std::optional<Endorsement> loaded;
    QVERIFY(endorsements.findById(QStringLiteral("e1"), &loaded));
    QVERIFY2(!loaded.has_value(), "another vessel's endorsement must not be readable");

    QVERIFY2(!endorsements.softDelete(QStringLiteral("e1")),
             "nor deletable");
    QVERIFY(endorsements.errorString().contains(QStringLiteral("its own vessel")));

    Endorsement correction;
    correction.id               = QStringLiteral("e1");
    correction.certificateId    = QStringLiteral("c1");
    correction.surveyType       = SurveyType::Annual;
    correction.endorsementDate  = QDate(2027, 3, 4);
    QVERIFY2(!endorsements.update(correction), "nor editable");

    // Still live, and still as it was.
    QCOMPARE(rowsFor(db, QStringLiteral("c1"), false), 1);
}

// §6: the same dialog pre-filled. The form is driven directly because the
// dialog's own OK button would need a real exec() to press.
void TestEndorsementEditDelete::theFormCanBePreFilledAndHandsBackTheSameRow()
{
    Endorsement existing;
    existing.id               = QStringLiteral("e1");
    existing.certificateId    = QStringLiteral("c1");
    existing.surveyType       = SurveyType::Intermediate;
    existing.endorsementDate  = QDate(2028, 6, 4);
    existing.place            = QStringLiteral("Limassol");
    existing.surveyor         = QStringLiteral("Lloyds Register");
    existing.result           = QStringLiteral("Satisfactory");
    existing.remarks          = QStringLiteral("Hull inspected afloat.");

    EndorsementEditForm form(QStringLiteral("c1"),
                             {SurveyType::Annual, SurveyType::Intermediate});
    form.setEndorsement(existing);

    // Every field comes back, including the three the table never shows — an
    // edit that quietly blanked the remarks would be worse than no edit.
    const Endorsement readBack = form.endorsement();
    QCOMPARE(readBack.id, existing.id);
    QCOMPARE(readBack.certificateId, existing.certificateId);
    QCOMPARE(readBack.surveyType, existing.surveyType);
    QCOMPARE(readBack.endorsementDate, existing.endorsementDate);
    QCOMPARE(readBack.place, existing.place);
    QCOMPARE(readBack.surveyor, existing.surveyor);
    QCOMPARE(readBack.result, existing.result);
    QCOMPARE(readBack.remarks, existing.remarks);
    QVERIFY(form.isValid());

    // An added endorsement still carries no id, so the dialog knows to insert.
    EndorsementEditForm adding(QStringLiteral("c1"), {SurveyType::Annual});
    QVERIFY(adding.endorsement().id.isEmpty());
}

// The requirement was unticked after the survey was recorded. The stored type
// is offered rather than dropped: correcting the place must not change which
// kind of survey happened.
void TestEndorsementEditDelete::aStoredTypeTheCertificateNoLongerRequiresIsStillOffered()
{
    Endorsement existing;
    existing.id               = QStringLiteral("e1");
    existing.certificateId    = QStringLiteral("c1");
    existing.surveyType       = SurveyType::Intermediate;
    existing.endorsementDate  = QDate(2028, 6, 4);

    // Only Annual is required now.
    EndorsementEditForm form(QStringLiteral("c1"),
                             {SurveyType::Annual, SurveyType::Renewal});
    form.setEndorsement(existing);

    auto* combo = form.findChild<QComboBox*>(QStringLiteral("surveyTypeCombo"));
    QVERIFY(combo != nullptr);
    QVERIFY2(combo->findData(static_cast<int>(SurveyType::Intermediate)) >= 0,
             "the stored type has to be selectable or the edit would change it");
    QCOMPARE(form.endorsement().surveyType, SurveyType::Intermediate);

    // The same situation with only one type allowed, where there is no combo
    // at all and the type is shown as a label. The stored type still wins, and
    // the label is corrected rather than left saying something else happened.
    EndorsementEditForm single(QStringLiteral("c1"), {SurveyType::Annual});
    single.setEndorsement(existing);

    QVERIFY(single.findChild<QComboBox*>(QStringLiteral("surveyTypeCombo")) == nullptr);
    QCOMPARE(single.endorsement().surveyType, SurveyType::Intermediate);

    auto* label = single.findChild<QLabel*>(QStringLiteral("fixedSurveyTypeLabel"));
    QVERIFY(label != nullptr);
    QVERIFY2(label->text().contains(QStringLiteral("Intermediate")), qPrintable(label->text()));
}

void TestEndorsementEditDelete::theSectionOffersEditAndDeleteOnlyWithASelection()
{
    QSqlDatabase db = database();
    seedOne();
    seedEndorsement(db, QStringLiteral("e1"), QStringLiteral("c1"), QStringLiteral("ANNUAL"),
                    QDate(2027, 2, 4));

    EndorsementRepository endorsements(db, officeContext());
    EndorsementSection    section(endorsements, QStringLiteral("c1"),
                                  {SurveyType::Annual, SurveyType::Intermediate});

    auto* table = section.findChild<QTableWidget*>(QStringLiteral("endorsementTable"));
    auto* edit  = section.findChild<QPushButton*>(QStringLiteral("editEndorsementButton"));
    auto* remove =
        section.findChild<QPushButton*>(QStringLiteral("deleteEndorsementButton"));
    QVERIFY(table != nullptr);
    QVERIFY(edit != nullptr);
    QVERIFY(remove != nullptr);

    QCOMPARE(table->rowCount(), 1);
    QVERIFY2(!edit->isEnabled(), "nothing selected, nothing to correct");
    QVERIFY(!remove->isEnabled());

    table->selectRow(0);
    QVERIFY(edit->isEnabled());
    QVERIFY(remove->isEnabled());

    // Add is never selection-driven: it needs only the certificate.
    auto* add = section.findChild<QPushButton*>(QStringLiteral("addEndorsementButton"));
    QVERIFY(add != nullptr);
    QVERIFY(add->isEnabled());
}

void TestEndorsementEditDelete::theSectionFollowsADeletedEndorsement()
{
    QSqlDatabase db = database();
    seedOne();
    seedEndorsement(db, QStringLiteral("e1"), QStringLiteral("c1"), QStringLiteral("ANNUAL"),
                    QDate(2027, 2, 4));
    seedEndorsement(db, QStringLiteral("e2"), QStringLiteral("c1"), QStringLiteral("ANNUAL"),
                    QDate(2028, 2, 4));

    EndorsementRepository endorsements(db, officeContext());
    EndorsementSection    section(endorsements, QStringLiteral("c1"),
                                  {SurveyType::Annual, SurveyType::Intermediate});

    auto* table = section.findChild<QTableWidget*>(QStringLiteral("endorsementTable"));
    QVERIFY(table != nullptr);
    QCOMPARE(table->rowCount(), 2);
    table->selectRow(0);

    // The confirmation dialog cannot be answered from a test, so the delete is
    // driven through the repository and the section asked to follow it — which
    // is what the button does once the question is answered.
    QVERIFY(endorsements.softDelete(QStringLiteral("e1")));
    section.reload();

    // The deleted one is gone and the survivor is the one that should be,
    // read from the Date column.
    QCOMPARE(table->rowCount(), 1);
    QCOMPARE(table->item(0, 1)->text(), QDate(2028, 2, 4).toString(QStringLiteral("dd MMM yyyy")));

    // The last one too: the table is now empty, so the two selection-driven
    // buttons must have disabled themselves rather than staying live over
    // nothing.
    QVERIFY(endorsements.softDelete(QStringLiteral("e2")));
    section.reload();

    QCOMPARE(table->rowCount(), 0);

    auto* edit = section.findChild<QPushButton*>(QStringLiteral("editEndorsementButton"));
    auto* remove =
        section.findChild<QPushButton*>(QStringLiteral("deleteEndorsementButton"));
    QVERIFY(edit != nullptr);
    QVERIFY(remove != nullptr);
    QVERIFY2(!edit->isEnabled(), "an empty table must leave nothing to correct");
    QVERIFY(!remove->isEnabled());
}

QTEST_MAIN(TestEndorsementEditDelete)
#include "tst_EndorsementEditDelete.moc"
