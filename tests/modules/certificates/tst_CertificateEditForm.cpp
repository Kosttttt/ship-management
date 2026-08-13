#include "modules/certificates/ui/CertificateEditForm.h"

#include <QCheckBox>
#include <QComboBox>
#include <QtTest>

// certificate-crud-spec §7 items 7 and 8: the no-expiry rule and the
// intermediate-mode picker, enforced by the form before anything reaches the
// repository.
class TestCertificateEditForm : public QObject
{
    Q_OBJECT

private slots:
    void aNewFormDoesNotDefaultToNeverExpiring();
    void setVesselIdLeavesTheFieldsAlone();
    void everyControlIsReachable();
    void interimIsNotTheIntermediateSurveyCheckbox();
    void noExpiryDisablesAndClearsTheSurveyControls();
    void uncheckingNoExpiryReEnablesTheSurveyControls();
    void noExpiryResetsTheIntermediateModeToAdditional();
    void intermediateModeIsDisabledUntilIntermediateSurveyIsChecked();
    void intermediateModeStaysDisabledUnderNoExpiry();
    void noExpiryProducesANullExpiryDate();
    void formIsInvalidWithoutNameOrCategory();
    void roundTripsACertificate();

private:
    QCheckBox* noExpiry(CertificateEditForm& form) const;
    QCheckBox* annual(CertificateEditForm& form) const;
    QCheckBox* intermediate(CertificateEditForm& form) const;
    QComboBox* intermediateMode(CertificateEditForm& form) const;
    QComboBox* category(CertificateEditForm& form) const;
};

// The form keeps its widgets private, so the test finds them by object name.
// Matching on visible label text was tried first and was a trap: "Requires an
// intermediate survey" and "Interim certificate" both contain "inter", so the
// fuzzy match silently returned the wrong checkbox.
QCheckBox* TestCertificateEditForm::noExpiry(CertificateEditForm& form) const
{
    return form.findChild<QCheckBox*>(QStringLiteral("noExpiryCheck"));
}

QCheckBox* TestCertificateEditForm::annual(CertificateEditForm& form) const
{
    return form.findChild<QCheckBox*>(QStringLiteral("annualSurveyCheck"));
}

QCheckBox* TestCertificateEditForm::intermediate(CertificateEditForm& form) const
{
    return form.findChild<QCheckBox*>(QStringLiteral("intermediateSurveyCheck"));
}

QComboBox* TestCertificateEditForm::intermediateMode(CertificateEditForm& form) const
{
    return form.findChild<QComboBox*>(QStringLiteral("intermediateModeCombo"));
}

QComboBox* TestCertificateEditForm::category(CertificateEditForm& form) const
{
    return form.findChild<QComboBox*>(QStringLiteral("categoryCombo"));
}

void TestCertificateEditForm::aNewFormDoesNotDefaultToNeverExpiring()
{
    // Most certificates expire. Handing the form a default-constructed
    // Certificate would tick "does not expire", because a blank expiry date
    // means exactly that — right about the data, wrong as a starting point.
    CertificateEditForm form;

    QVERIFY2(!noExpiry(form)->isChecked(), "a new certificate form must not start as no-expiry");
    QVERIFY(form.certificate().expiryDate.isValid());
    QVERIFY2(annual(form)->isEnabled(), "the survey controls must start usable");
}

void TestCertificateEditForm::setVesselIdLeavesTheFieldsAlone()
{
    CertificateEditForm form;
    const QDate defaultExpiry = form.certificate().expiryDate;

    form.setVesselId(QStringLiteral("vessel-7"));

    QCOMPARE(form.certificate().vesselId, QStringLiteral("vessel-7"));
    QCOMPARE(form.certificate().expiryDate, defaultExpiry);
    QVERIFY(!noExpiry(form)->isChecked());
    QVERIFY(form.certificate().id.isEmpty()); // still a new certificate
}

void TestCertificateEditForm::everyControlIsReachable()
{
    // If a control is ever renamed, this fails loudly here rather than making
    // some other test quietly assert nothing.
    CertificateEditForm form;
    QVERIFY(noExpiry(form) != nullptr);
    QVERIFY(annual(form) != nullptr);
    QVERIFY(intermediate(form) != nullptr);
    QVERIFY(intermediateMode(form) != nullptr);
    QVERIFY(category(form) != nullptr);
}

void TestCertificateEditForm::interimIsNotTheIntermediateSurveyCheckbox()
{
    // "Interim certificate" and "intermediate survey" are different fields
    // with confusingly similar names. Ticking one must not touch the other.
    CertificateEditForm form;
    auto* interim = form.findChild<QCheckBox*>(QStringLiteral("interimCheck"));
    QVERIFY(interim != nullptr);
    QVERIFY(interim != intermediate(form));

    interim->setChecked(true);
    QVERIFY(form.certificate().isInterim);
    QVERIFY2(!form.certificate().requiresIntermediateSurvey,
             "an interim certificate is not the same as requiring an intermediate survey");
    QVERIFY2(!intermediateMode(form)->isEnabled(),
             "the interim checkbox must not enable the intermediate-mode picker");
}

void TestCertificateEditForm::noExpiryDisablesAndClearsTheSurveyControls()
{
    CertificateEditForm form;

    annual(form)->setChecked(true);
    intermediate(form)->setChecked(true);
    QVERIFY(annual(form)->isChecked());
    QVERIFY(intermediate(form)->isChecked());

    noExpiry(form)->setChecked(true);

    // Cleared, not merely greyed out — the invalid combination must not
    // survive hidden behind a disabled control.
    QVERIFY2(!annual(form)->isChecked(), "the annual survey box must be cleared");
    QVERIFY2(!intermediate(form)->isChecked(), "the intermediate survey box must be cleared");
    QVERIFY2(!annual(form)->isEnabled(), "the annual survey box must be disabled");
    QVERIFY2(!intermediate(form)->isEnabled(), "the intermediate survey box must be disabled");
    QVERIFY2(!intermediateMode(form)->isEnabled(), "the mode picker must be disabled");
}

void TestCertificateEditForm::uncheckingNoExpiryReEnablesTheSurveyControls()
{
    CertificateEditForm form;

    noExpiry(form)->setChecked(true);
    QVERIFY(!annual(form)->isEnabled());

    noExpiry(form)->setChecked(false);

    QVERIFY(annual(form)->isEnabled());
    QVERIFY(intermediate(form)->isEnabled());
    // The mode picker follows its own checkbox, which is still unchecked.
    QVERIFY(!intermediateMode(form)->isEnabled());
}

void TestCertificateEditForm::noExpiryResetsTheIntermediateModeToAdditional()
{
    CertificateEditForm form;

    intermediate(form)->setChecked(true);
    intermediateMode(form)->setCurrentIndex(
        intermediateMode(form)->findData(static_cast<int>(IntermediateMode::ReplacesAnnual)));
    QCOMPARE(form.certificate().intermediateMode, IntermediateMode::ReplacesAnnual);

    noExpiry(form)->setChecked(true);

    QCOMPARE(form.certificate().intermediateMode, IntermediateMode::Additional);
}

void TestCertificateEditForm::intermediateModeIsDisabledUntilIntermediateSurveyIsChecked()
{
    CertificateEditForm form;

    QVERIFY2(!intermediateMode(form)->isEnabled(),
             "the mode means nothing until an intermediate survey is required");

    intermediate(form)->setChecked(true);
    QVERIFY(intermediateMode(form)->isEnabled());

    intermediate(form)->setChecked(false);
    QVERIFY(!intermediateMode(form)->isEnabled());
}

void TestCertificateEditForm::intermediateModeStaysDisabledUnderNoExpiry()
{
    CertificateEditForm form;

    intermediate(form)->setChecked(true);
    QVERIFY(intermediateMode(form)->isEnabled());

    noExpiry(form)->setChecked(true);
    QVERIFY(!intermediateMode(form)->isEnabled());
}

void TestCertificateEditForm::noExpiryProducesANullExpiryDate()
{
    CertificateEditForm form;
    QVERIFY(form.certificate().expiryDate.isValid());

    noExpiry(form)->setChecked(true);

    const Certificate certificate = form.certificate();
    QVERIFY2(certificate.neverExpires(), "a null expiry date is how 'does not expire' travels");
    QVERIFY(!certificate.requiresAnnualSurvey);
    QVERIFY(!certificate.requiresIntermediateSurvey);
}

void TestCertificateEditForm::formIsInvalidWithoutNameOrCategory()
{
    CertificateEditForm form;
    QVERIFY2(!form.isValid(), "an empty form has neither a name nor a category");

    Certificate named;
    named.name     = QStringLiteral("Load Line");
    named.category = CertificateCategory::Unset;
    form.setCertificate(named);
    QVERIFY2(!form.isValid(), "a name without a category is not enough");

    named.category = CertificateCategory::Statutory;
    form.setCertificate(named);
    QVERIFY(form.isValid());
}

void TestCertificateEditForm::roundTripsACertificate()
{
    Certificate original;
    original.id                         = QStringLiteral("cert-1");
    original.vesselId                   = QStringLiteral("vessel-1");
    original.name                       = QStringLiteral("Safety Construction");
    original.category                   = CertificateCategory::Statutory;
    original.certificateNumber          = QStringLiteral("SC-12345");
    original.appliesTo                  = QStringLiteral("Hull");
    original.issueDate                  = QDate(2026, 2, 4);
    original.expiryDate                 = QDate(2031, 2, 4);
    original.issuedBy                   = QStringLiteral("Lloyd's Register");
    original.placeOfIssue               = QStringLiteral("Limassol");
    original.isInterim                  = true;
    original.requiresAnnualSurvey       = true;
    original.requiresIntermediateSurvey = true;
    original.intermediateMode           = IntermediateMode::ReplacesAnnual;
    original.notes                      = QStringLiteral("Endorsed in Rotterdam.");

    CertificateEditForm form;
    form.setCertificate(original);

    const Certificate returned = form.certificate();
    QCOMPARE(returned.id, original.id);
    QCOMPARE(returned.vesselId, original.vesselId);
    QCOMPARE(returned.name, original.name);
    QCOMPARE(returned.category, original.category);
    QCOMPARE(returned.certificateNumber, original.certificateNumber);
    QCOMPARE(returned.appliesTo, original.appliesTo);
    QCOMPARE(returned.issueDate, original.issueDate);
    QCOMPARE(returned.expiryDate, original.expiryDate);
    QCOMPARE(returned.issuedBy, original.issuedBy);
    QCOMPARE(returned.placeOfIssue, original.placeOfIssue);
    QCOMPARE(returned.isInterim, original.isInterim);
    QCOMPARE(returned.requiresAnnualSurvey, original.requiresAnnualSurvey);
    QCOMPARE(returned.requiresIntermediateSurvey, original.requiresIntermediateSurvey);
    QCOMPARE(returned.intermediateMode, original.intermediateMode);
    QCOMPARE(returned.notes, original.notes);
    QVERIFY(form.isValid());
}

QTEST_MAIN(TestCertificateEditForm)
#include "tst_CertificateEditForm.moc"
