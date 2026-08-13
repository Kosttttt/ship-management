#include "app/VesselEditForm.h"

#include <QLineEdit>
#include <QSignalSpy>
#include <QSpinBox>
#include <QtTest>

// vessel-crud-spec §8.9: gross tonnage must be rejected by the form before it
// ever reaches the repository, along with the name and IMO rules the Save
// button depends on.
//
// A widget test, so it links ShipApp and runs with a real QApplication.
class TestVesselEditForm : public QObject
{
    Q_OBJECT

private slots:
    void emptyFormIsNotValid();
    void becomesValidOnceNameAndImoAreGood();
    void invalidCheckDigitKeepsFormInvalid();
    void grossTonnageCannotGoNegative();
    void grossTonnageRejectsNonIntegerTyping();
    void grossTonnageZeroMeansNotEntered();
    void roundTripsAVesselThroughTheFields();
    void keepsTheIdItWasGiven();
    void stripsImoPrefixAndTrimsText();
    void announcesValidityChanges();

private:
    QLineEdit* nameEdit(VesselEditForm& form) const;
    QLineEdit* imoEdit(VesselEditForm& form) const;
    QSpinBox*  tonnageSpin(VesselEditForm& form) const;
};

// The form keeps its widgets private, so the test finds them the way a user
// would — by walking the widget tree — rather than by widening the interface
// just for testing.
QLineEdit* TestVesselEditForm::nameEdit(VesselEditForm& form) const
{
    return form.findChildren<QLineEdit*>().value(0);
}

QLineEdit* TestVesselEditForm::imoEdit(VesselEditForm& form) const
{
    return form.findChildren<QLineEdit*>().value(1);
}

QSpinBox* TestVesselEditForm::tonnageSpin(VesselEditForm& form) const
{
    return form.findChild<QSpinBox*>();
}

void TestVesselEditForm::emptyFormIsNotValid()
{
    VesselEditForm form;
    QVERIFY(!form.isValid());
}

void TestVesselEditForm::becomesValidOnceNameAndImoAreGood()
{
    VesselEditForm form;

    nameEdit(form)->setText(QStringLiteral("MV Example"));
    QVERIFY2(!form.isValid(), "a name alone is not enough");

    imoEdit(form)->setText(QStringLiteral("9074729"));
    QVERIFY(form.isValid());
}

void TestVesselEditForm::invalidCheckDigitKeepsFormInvalid()
{
    VesselEditForm form;
    nameEdit(form)->setText(QStringLiteral("MV Example"));
    imoEdit(form)->setText(QStringLiteral("9074728")); // wrong check digit

    QVERIFY(!form.isValid());
}

void TestVesselEditForm::grossTonnageCannotGoNegative()
{
    VesselEditForm form;
    QSpinBox*      spin = tonnageSpin(form);
    QVERIFY(spin != nullptr);

    // A QSpinBox clamps to its range, so a negative value cannot be held at
    // all — the rule is enforced by the choice of widget.
    spin->setValue(-1);
    QCOMPARE(spin->value(), 0);
    QCOMPARE(form.vessel().grossTonnage, 0);

    spin->stepDown();
    QVERIFY(spin->value() >= 0);
}

void TestVesselEditForm::grossTonnageRejectsNonIntegerTyping()
{
    VesselEditForm form;
    QSpinBox*      spin = tonnageSpin(form);

    // Typing a decimal point: the spin box's validator refuses the character,
    // so the digits arrive as a whole number.
    spin->clear();
    QTest::keyClicks(spin, QStringLiteral("12.5"));

    QCOMPARE(form.vessel().grossTonnage, 125);
    QVERIFY2(form.vessel().grossTonnage >= 0, "gross tonnage is always a whole number");
}

void TestVesselEditForm::grossTonnageZeroMeansNotEntered()
{
    VesselEditForm form;
    QSpinBox*      spin = tonnageSpin(form);

    spin->setValue(0);
    QCOMPARE(form.vessel().grossTonnage, 0);
    // Shown as words rather than a misleading "0" tonne ship.
    QCOMPARE(spin->text(), spin->specialValueText());
}

void TestVesselEditForm::roundTripsAVesselThroughTheFields()
{
    Vessel original;
    original.id             = QStringLiteral("vessel-1");
    original.name           = QStringLiteral("MV Example");
    original.imoNumber      = QStringLiteral("9074729");
    original.callSign       = QStringLiteral("ABCD");
    original.grossTonnage   = 51000;
    original.portOfRegistry = QStringLiteral("Limassol");
    original.flagState      = QStringLiteral("Cyprus");

    VesselEditForm form;
    form.setVessel(original);

    const Vessel returned = form.vessel();
    QCOMPARE(returned.id, original.id);
    QCOMPARE(returned.name, original.name);
    QCOMPARE(returned.imoNumber, original.imoNumber);
    QCOMPARE(returned.callSign, original.callSign);
    QCOMPARE(returned.grossTonnage, original.grossTonnage);
    QCOMPARE(returned.portOfRegistry, original.portOfRegistry);
    QCOMPARE(returned.flagState, original.flagState);
    QVERIFY(form.isValid());
}

void TestVesselEditForm::keepsTheIdItWasGiven()
{
    Vessel original;
    original.id        = QStringLiteral("vessel-42");
    original.name      = QStringLiteral("MV Example");
    original.imoNumber = QStringLiteral("9074729");

    VesselEditForm form;
    form.setVessel(original);

    // The id is never shown, but an edit has to save back to the right row.
    nameEdit(form)->setText(QStringLiteral("MV Renamed"));
    QCOMPARE(form.vessel().id, QStringLiteral("vessel-42"));
}

void TestVesselEditForm::stripsImoPrefixAndTrimsText()
{
    VesselEditForm form;
    nameEdit(form)->setText(QStringLiteral("  MV Example  "));
    imoEdit(form)->setText(QStringLiteral("IMO 9074729"));

    QVERIFY(form.isValid());
    QCOMPARE(form.vessel().name, QStringLiteral("MV Example"));
    QCOMPARE(form.vessel().imoNumber, QStringLiteral("9074729"));
}

void TestVesselEditForm::announcesValidityChanges()
{
    VesselEditForm form;
    QSignalSpy     spy(&form, &VesselEditForm::validityChanged);

    nameEdit(form)->setText(QStringLiteral("MV Example"));
    imoEdit(form)->setText(QStringLiteral("9074729"));

    QVERIFY2(!spy.isEmpty(), "a Save button needs to hear about validity changes");
    QCOMPARE(spy.last().at(0).toBool(), true);

    imoEdit(form)->setText(QStringLiteral("907"));
    QCOMPARE(spy.last().at(0).toBool(), false);
}

QTEST_MAIN(TestVesselEditForm)
#include "tst_VesselEditForm.moc"
