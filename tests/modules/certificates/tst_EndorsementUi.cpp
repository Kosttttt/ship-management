#include "CertificateTestSupport.h"

#include "modules/certificates/data/CertificateRepository.h"
#include "modules/certificates/data/EndorsementRepository.h"
#include "modules/certificates/ui/CertificateEditDialog.h"
#include "modules/certificates/ui/EndorsementEditForm.h"

#include <QComboBox>
#include <QGroupBox>
#include <QLabel>
#include <QtTest>

using namespace CertificateTestSupport;

// certificate-endorsement-spec §8 items 18 and 19.
class TestEndorsementUi : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    // Item 18
    void sectionIsHiddenOnABrandNewCertificate();
    void sectionIsHiddenWhenNoSurveyIsRequired();
    void sectionIsShownWhenEditingACertificateThatRequiresASurvey();

    // Item 19
    void onlyAnnualRequiredMeansNoComboAndAnAnnualEndorsement();
    void onlyIntermediateRequiredMeansNoComboAndAnIntermediateEndorsement();
    void bothRequiredOffersExactlyThoseTwoAndNeverInitialOrRenewal();

private:
    QSqlDatabase database() { return QSqlDatabase::database(m_connectionName); }

    // The certificate as it would be loaded for editing.
    Certificate savedCertificate(bool annual, bool intermediate) const
    {
        Certificate certificate = sampleCertificate(QStringLiteral("v1"),
                                                    QStringLiteral("Load Line"));
        certificate.id                         = QStringLiteral("c1");
        certificate.requiresAnnualSurvey       = annual;
        certificate.requiresIntermediateSurvey = intermediate;
        return certificate;
    }

    static bool hasEndorsementsSection(CertificateEditDialog& dialog)
    {
        return dialog.findChild<QGroupBox*>(QStringLiteral("endorsementsSection")) != nullptr;
    }

    QString m_connectionName;
};

void TestEndorsementUi::init()
{
    m_connectionName = QStringLiteral("endui_%1").arg(QTest::currentTestFunction());
    QVERIFY(openDatabase(m_connectionName));

    QSqlDatabase db = database();
    seedCertificate(db, QStringLiteral("c1"), QStringLiteral("v1"), QStringLiteral("Load Line"));
}

void TestEndorsementUi::cleanup()
{
    closeDatabase(m_connectionName);
}

void TestEndorsementUi::sectionIsHiddenOnABrandNewCertificate()
{
    // A certificate being added has no id yet, so there is nothing for an
    // endorsement to point at.
    QSqlDatabase              db      = database();
    const InstallationContext context = officeContext();
    CertificateRepository     certificates(db, context);
    EndorsementRepository     endorsements(db, context);

    CertificateEditDialog dialog(certificates, endorsements, QStringLiteral("v1"), std::nullopt);

    QVERIFY(!hasEndorsementsSection(dialog));
}

void TestEndorsementUi::sectionIsHiddenWhenNoSurveyIsRequired()
{
    // Nothing meaningful to record against a certificate with no survey
    // schedule at all.
    QSqlDatabase              db      = database();
    const InstallationContext context = officeContext();
    CertificateRepository     certificates(db, context);
    EndorsementRepository     endorsements(db, context);

    CertificateEditDialog dialog(certificates, endorsements, QStringLiteral("v1"),
                                 savedCertificate(/*annual=*/false, /*intermediate=*/false));

    QVERIFY(!hasEndorsementsSection(dialog));
}

void TestEndorsementUi::sectionIsShownWhenEditingACertificateThatRequiresASurvey()
{
    QSqlDatabase              db      = database();
    const InstallationContext context = officeContext();
    CertificateRepository     certificates(db, context);
    EndorsementRepository     endorsements(db, context);

    CertificateEditDialog annualOnly(certificates, endorsements, QStringLiteral("v1"),
                                     savedCertificate(true, false));
    QVERIFY(hasEndorsementsSection(annualOnly));

    CertificateEditDialog intermediateOnly(certificates, endorsements, QStringLiteral("v1"),
                                           savedCertificate(false, true));
    QVERIFY(hasEndorsementsSection(intermediateOnly));
}

void TestEndorsementUi::onlyAnnualRequiredMeansNoComboAndAnAnnualEndorsement()
{
    // §6: one option is not a choice, so no combo is built at all.
    EndorsementEditForm form(QStringLiteral("c1"), {SurveyType::Annual});

    QVERIFY2(form.findChild<QComboBox*>(QStringLiteral("surveyTypeCombo")) == nullptr,
             "a single allowed type should not be presented as a choice");
    QVERIFY(form.findChild<QLabel*>(QStringLiteral("fixedSurveyTypeLabel")) != nullptr);

    QCOMPARE(form.endorsement().surveyType, SurveyType::Annual);
    QVERIFY(form.isValid());
}

void TestEndorsementUi::onlyIntermediateRequiredMeansNoComboAndAnIntermediateEndorsement()
{
    EndorsementEditForm form(QStringLiteral("c1"), {SurveyType::Intermediate});

    QVERIFY(form.findChild<QComboBox*>(QStringLiteral("surveyTypeCombo")) == nullptr);
    QCOMPARE(form.endorsement().surveyType, SurveyType::Intermediate);
    QVERIFY(form.isValid());
}

void TestEndorsementUi::bothRequiredOffersExactlyThoseTwoAndNeverInitialOrRenewal()
{
    EndorsementEditForm form(QStringLiteral("c1"),
                             {SurveyType::Annual, SurveyType::Intermediate});

    auto* combo = form.findChild<QComboBox*>(QStringLiteral("surveyTypeCombo"));
    QVERIFY(combo != nullptr);
    QCOMPARE(combo->count(), 2);

    QList<SurveyType> offered;
    for (int index = 0; index < combo->count(); ++index) {
        offered.append(static_cast<SurveyType>(combo->itemData(index).toInt()));
    }
    QVERIFY(offered.contains(SurveyType::Annual));
    QVERIFY(offered.contains(SurveyType::Intermediate));
    QVERIFY2(!offered.contains(SurveyType::Initial), "INITIAL is never offered here");
    QVERIFY2(!offered.contains(SurveyType::Renewal), "RENEWAL is never offered here");

    // Nothing preselected, so the form starts invalid and the Add button off.
    QCOMPARE(combo->currentIndex(), -1);
    QVERIFY(!form.isValid());

    combo->setCurrentIndex(combo->findData(static_cast<int>(SurveyType::Intermediate)));
    QVERIFY(form.isValid());
    QCOMPARE(form.endorsement().surveyType, SurveyType::Intermediate);
}

QTEST_MAIN(TestEndorsementUi)
#include "tst_EndorsementUi.moc"
